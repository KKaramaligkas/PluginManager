/*
    Plugin Manager for ARK-5
    net.c: Wi-Fi connection (automatic, or the system dialog) and HTTPS downloads.

    The PSP's own SSL library only speaks obsolete protocol versions, so all
    transfers go through libcurl + mbedTLS (TLS 1.2). The CA bundle is parsed
    once and handed to every connection through CURLOPT_SSL_CTX_FUNCTION.

    The system network libraries are loaded into user memory next to the app
    (main.c keeps room for them). Every setup step is checked and the first
    failure is kept for net_last_error(), so a failed connection tells the
    user why instead of just leaving the app "Offline".

    A certificate that fails the check is explained in the error (tlsdiag.c),
    and the details go to a report file, with the PSP's clock as read by the
    check and by the kernel. The check's clock comes from the RTC (clock.c).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psprtc.h>
#include <psputility.h>
#include <psputility_netparam.h>
#include <psputils.h>
#include <pspwlan.h>
#include <time.h>

#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "fs.h"
#include "gfx.h"
#include "net.h"
#include "http_policy.h"
#include "transfer.h"
#include "resume.h"
#include "tlsdiag.h"
#include "util.h"
#include "version.h"

_Static_assert(TLSDIAG_EXPIRED == MBEDTLS_X509_BADCERT_EXPIRED && TLSDIAG_REVOKED == MBEDTLS_X509_BADCERT_REVOKED &&
               TLSDIAG_NAME == MBEDTLS_X509_BADCERT_CN_MISMATCH && TLSDIAG_NOT_TRUSTED == MBEDTLS_X509_BADCERT_NOT_TRUSTED &&
               TLSDIAG_FUTURE == MBEDTLS_X509_BADCERT_FUTURE, "tlsdiag flags must match mbedTLS");

/* sceUtilityLoadModule() error for a module that is already loaded */
#define UTILITY_MODULE_ALREADY_LOADED   0x80111102
/* kernel errors for a memory allocation that failed */
#define KERNEL_NO_MEMORY                0x80020190
#define KERNEL_MEMBLOCK_ALLOC_FAILED    0x800200D9

/* stubs.S: the network connection the PSP used last (1 to n) */
int sceUtilityGetNetParamLatestID(int *id);

/* network connections are numbered from 1; the PSP keeps a handful */
#define MAX_PROFILES    32
/* how long an automatic connection may take */
#define AUTO_TIMEOUT_US     (30 * 1000 * 1000)
#define AUTO_START_US       (5 * 1000 * 1000)

/* memory pool of the TCP/IP stack (socket buffers) */
#define NET_POOL_SIZE   (256 * 1024)

/* setup steps done so far: a failed setup resumes where it stopped, and
   net_term() undoes exactly these */
enum {
    STEP_MODULE_COMMON  = 1 << 0,
    STEP_MODULE_INET    = 1 << 1,
    STEP_NET            = 1 << 2,
    STEP_INET           = 1 << 3,
    STEP_RESOLVER       = 1 << 4,
    STEP_APCTL          = 1 << 5,
};

static int steps;
static int inited;
/* Cookies: off unless the app sets a file (the Plugin Manager keeps none).
   One store shared by every request; requests come from one thread at a
   time, so the share needs no locks. */
static CURLSH *cookie_share;
static char cookie_path[PM_PATH_MAX];
static int cookies_loaded;

static char last_error[200];
static char ca_path[PM_PATH_MAX];
static char report_path[PM_PATH_MAX];
static int tls_verify = 1;
static int minimum_tls12;
static char user_agent[96] = "PluginManager/" PM_VERSION " (PSP; ARK-5)";
void net_set_client(const char *agent, int tls12)
{
    if (agent) pm_strlcpy(user_agent, agent, sizeof(user_agent));
    minimum_tls12 = tls12;
}
static mbedtls_x509_crt ca_chain;
static int ca_state;            /* 0 not loaded, 1 loaded, -1 failed */
static int ca_parse_result, ca_count;   /* for the report file */
static SceUID ca_lock = -1;

static int fail(const char *what, int code)
{
    if (code == (int)KERNEL_NO_MEMORY || code == (int)KERNEL_MEMBLOCK_ALLOC_FAILED)
        snprintf(last_error, sizeof(last_error), "%s: not enough free memory (error %08X).", what, (unsigned)code);
    else
        snprintf(last_error, sizeof(last_error), "%s (error %08X).", what, (unsigned)code);
    return code < 0 ? code : -1;
}

const char *net_last_error(void)
{
    return last_error[0] ? last_error : "Unknown network error.";
}

static int load_module(int module)
{
    int r = sceUtilityLoadModule(module);
    return r == (int)UTILITY_MODULE_ALREADY_LOADED ? 0 : r;
}

int net_init(void)
{
    int r;
    if (inited) return 0;

    if (!(steps & STEP_MODULE_COMMON)) {
        if ((r = load_module(PSP_MODULE_NET_COMMON)) < 0) return fail("Couldn't load the system network library", r);
        steps |= STEP_MODULE_COMMON;
    }
    if (!(steps & STEP_MODULE_INET)) {
        if ((r = load_module(PSP_MODULE_NET_INET)) < 0) return fail("Couldn't load the system Internet library", r);
        steps |= STEP_MODULE_INET;
    }
    if (!(steps & STEP_NET)) {
        if ((r = sceNetInit(NET_POOL_SIZE, 42, 4 * 1024, 42, 4 * 1024)) < 0) return fail("Couldn't start the network", r);
        steps |= STEP_NET;
    }
    if (!(steps & STEP_INET)) {
        if ((r = sceNetInetInit()) < 0) return fail("Couldn't start the Internet protocols", r);
        steps |= STEP_INET;
    }
    if (!(steps & STEP_RESOLVER)) {
        if ((r = sceNetResolverInit()) < 0) return fail("Couldn't start the name resolver", r);
        steps |= STEP_RESOLVER;
    }
    if (!(steps & STEP_APCTL)) {
        if ((r = sceNetApctlInit(0x8000, 48)) < 0) return fail("Couldn't start the Wi-Fi connection manager", r);
        steps |= STEP_APCTL;
    }

    if (ca_lock < 0) ca_lock = sceKernelCreateSema("pm_ca", 0, 1, 1, NULL);
    curl_global_init(CURL_GLOBAL_ALL);
    inited = 1;
    return 0;
}

void net_term(void)
{
    if (inited) {
        if (cookie_share) curl_share_cleanup(cookie_share);
        cookie_share = NULL;
        curl_global_cleanup();
        if (ca_state == 1) mbedtls_x509_crt_free(&ca_chain);
        ca_state = 0;
        inited = 0;
    }
    if (steps & STEP_APCTL) {
        sceNetApctlDisconnect();
        sceNetApctlTerm();
    }
    if (steps & STEP_RESOLVER) sceNetResolverTerm();
    if (steps & STEP_INET) sceNetInetTerm();
    if (steps & STEP_NET) sceNetTerm();
    if (steps & STEP_MODULE_INET) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
    if (steps & STEP_MODULE_COMMON) sceUtilityUnloadModule(PSP_MODULE_NET_COMMON);
    steps = 0;
}

int net_wlan_switch_on(void)
{
    return sceWlanGetSwitchState() != 0;
}

static int apctl_state(void)
{
    int state = PSP_NET_APCTL_STATE_DISCONNECTED;
    if (!inited || sceNetApctlGetState(&state) < 0) return PSP_NET_APCTL_STATE_DISCONNECTED;
    return state;
}

int net_is_connected(void)
{
    return apctl_state() == PSP_NET_APCTL_STATE_GOT_IP;
}

static void draw_frame(void (*draw)(void *ud), void *ud)
{
    gfx_begin();
    if (draw) draw(ud);
    gfx_end();
    gfx_swap();
}

int net_connect_dialog(void (*draw)(void *ud), void *ud)
{
    int r = net_init();
    if (r < 0) return r;
    if (net_is_connected()) return NET_CONNECTED;

    pspUtilityNetconfData data;
    struct pspUtilityNetconfAdhoc adhoc;
    memset(&data, 0, sizeof(data));
    memset(&adhoc, 0, sizeof(adhoc));
    data.base.size = sizeof(data);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &data.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &data.base.buttonSwap);
    data.base.graphicsThread = 17;
    data.base.accessThread = 19;
    data.base.fontThread = 18;
    data.base.soundThread = 16;
    data.action = PSP_NETCONF_ACTION_CONNECTAP;
    data.adhocparam = &adhoc;
    data.hotspot = 1;

    if ((r = sceUtilityNetconfInitStart(&data)) < 0) return fail("Couldn't open the network connection dialog", r);

    for (int done = 0; !done;) {
        gfx_begin();
        if (draw) draw(ud);
        gfx_end();
        switch (sceUtilityNetconfGetStatus()) {
        case PSP_UTILITY_DIALOG_NONE:
            done = 1;
            break;
        case PSP_UTILITY_DIALOG_VISIBLE:
            sceUtilityNetconfUpdate(1);
            break;
        case PSP_UTILITY_DIALOG_QUIT:
            sceUtilityNetconfShutdownStart();
            break;
        default:
            break;
        }
        gfx_swap();
    }

    /* the dialog closes once the PSP has its address; give a connection that
       is still being set up a few more seconds */
    int state = apctl_state();
    for (int i = 0; i < 5 * 60 && data.base.result == 0 && state != PSP_NET_APCTL_STATE_GOT_IP &&
                    state != PSP_NET_APCTL_STATE_DISCONNECTED; i++) {
        draw_frame(draw, ud);
        state = apctl_state();
    }
    if (state == PSP_NET_APCTL_STATE_GOT_IP) return NET_CONNECTED;

    if (data.base.result < 0) return fail("The network connection dialog failed", data.base.result);
    if (data.base.result == 0 && state != PSP_NET_APCTL_STATE_DISCONNECTED) {
        snprintf(last_error, sizeof(last_error), "The connection didn't finish (state %d). Try again.", state);
        return -1;
    }
    /* cancelled, or the dialog already showed why it couldn't connect */
    return NET_CANCELLED;
}

/* Name of network connection `id`; -1 when there's none with that number. */
static int profile_name(int id, char *out, int size)
{
    netData d;
    memset(&d, 0, sizeof(d));
    if (sceUtilityCheckNetParam(id) != 0 || sceUtilityGetNetParam(id, PSP_NETPARAM_NAME, &d) != 0) return -1;
    d.asString[sizeof(d.asString) - 1] = 0;
    pm_strlcpy(out, d.asString, size);
    return 0;
}

/* The connection to use without asking (see net_connect_auto()). Returns its
   number and name, or 0. */
static int pick_profile(const char *last_name, char *name, int size)
{
    char n[128];
    int count = 0, only = 0;
    for (int id = 1; id <= MAX_PROFILES; id++) {
        if (profile_name(id, n, sizeof(n)) < 0) continue;
        if (last_name && last_name[0] && !strcmp(n, last_name)) {
            pm_strlcpy(name, n, size);
            return id;
        }
        count++;
        only = id;
    }
    int latest = 0;
    if (sceUtilityGetNetParamLatestID(&latest) >= 0 && latest >= 1 && latest <= MAX_PROFILES &&
            profile_name(latest, name, size) == 0)
        return latest;
    if (count == 1 && profile_name(only, name, size) == 0) return only;
    return 0;
}

static volatile int apctl_error;

static void apctl_handler(int old_state, int new_state, int event, int error, void *arg)
{
    (void)old_state;
    (void)new_state;
    (void)arg;
    if (event == PSP_NET_APCTL_EVENT_ERROR) apctl_error = error;
}

int net_connect_auto(const char *last_name, int (*frame)(void *ud, const char *name), void *ud)
{
    int r = net_init();
    if (r < 0) return r;
    if (net_is_connected()) return NET_CONNECTED;

    char name[128];
    int id = pick_profile(last_name, name, sizeof(name));
    if (id <= 0) return NET_NO_PROFILE;

    apctl_error = 0;
    int handler = sceNetApctlAddHandler(apctl_handler, NULL);
    if ((r = sceNetApctlConnect(id)) < 0) {
        if (handler >= 0) sceNetApctlDelHandler(handler);
        snprintf(last_error, sizeof(last_error), "Couldn't connect to %s (error %08X).", name, (unsigned)r);
        return r;
    }

    /* the state goes from disconnected through scanning and joining to
       "got IP"; back to disconnected means the attempt failed */
    unsigned int start = sceKernelGetSystemTimeLow();
    int started = 0, result = -1;
    for (;;) {
        if (frame && frame(ud, name)) {
            result = NET_CANCELLED;
            break;
        }
        int state = apctl_state();
        unsigned int elapsed = sceKernelGetSystemTimeLow() - start;
        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            result = NET_CONNECTED;
            break;
        }
        if (state != PSP_NET_APCTL_STATE_DISCONNECTED) started = 1;
        else if (started || apctl_error || elapsed > AUTO_START_US) break;
        if (elapsed > AUTO_TIMEOUT_US) break;
    }
    if (handler >= 0) sceNetApctlDelHandler(handler);

    if (result != NET_CONNECTED) net_disconnect();
    if (result < 0) {
        if (apctl_error)
            snprintf(last_error, sizeof(last_error), "Couldn't connect to %s (error %08X).", name, (unsigned)apctl_error);
        else
            snprintf(last_error, sizeof(last_error), "Couldn't connect to %s.", name);
    }
    return result;
}

void net_profile_name(char *out, int size)
{
    union SceNetApctlInfo info;
    memset(&info, 0, sizeof(info));
    if (size <= 0) return;
    out[0] = 0;
    if (net_is_connected() && sceNetApctlGetInfo(PSP_NET_APCTL_INFO_PROFILE_NAME, &info) == 0) {
        info.name[sizeof(info.name) - 1] = 0;
        pm_strlcpy(out, info.name, size);
    }
}

void net_disconnect(void)
{
    if (!inited || apctl_state() == PSP_NET_APCTL_STATE_DISCONNECTED) return;
    sceNetApctlDisconnect();
    /* the state changes a moment later; until then the PSP still looks
       connected, and a new connection can't start */
    for (int i = 0; i < 300 && apctl_state() != PSP_NET_APCTL_STATE_DISCONNECTED; i++)
        sceKernelDelayThread(10 * 1000);
}

void net_set_tls(const char *ca_file, int verify)
{
    pm_strlcpy(ca_path, ca_file ? ca_file : "", sizeof(ca_path));
    tls_verify = verify;
}

void net_set_report_file(const char *path)
{
    pm_strlcpy(report_path, path ? path : "", sizeof(report_path));
}

static int load_ca(void)
{
    if (ca_state) return ca_state;
    sceKernelWaitSema(ca_lock, 1, NULL);
    if (!ca_state) {
        mbedtls_x509_crt_init(&ca_chain);
        char native[PM_PATH_MAX];
        int r = ca_path[0] ? mbedtls_x509_crt_parse_file(&ca_chain, fs_native_path(ca_path, native, sizeof(native))) : -1;
        /* r > 0 means some certificates were skipped, which is fine */
        ca_state = (r >= 0 && ca_chain.version != 0) ? 1 : -1;
        ca_parse_result = r;
        ca_count = 0;
        for (const mbedtls_x509_crt *c = &ca_chain; ca_state > 0 && c && c->version; c = c->next) ca_count++;
        if (ca_state < 0) mbedtls_x509_crt_free(&ca_chain);
    }
    sceKernelSignalSema(ca_lock, 1);
    return ca_state;
}

static void copy_time(tlsdiag_time *out, const mbedtls_x509_time *t)
{
    out->year = t->year;
    out->month = t->mon;
    out->day = t->day;
    out->hour = t->hour;
    out->minute = t->min;
    out->second = t->sec;
}

/* Called by mbedTLS for every certificate of the chain it checked, from the
   top down to the server's own (depth 0). Records them without changing the
   verdict. */
static int verify_cb(void *p, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    tlsdiag *d = p;
    if (depth >= 0 && depth < TLSDIAG_MAX_CERTS) {
        tlsdiag_cert *c = &d->certs[depth];
        c->flags = *flags;
        if (mbedtls_x509_dn_gets(c->subject, sizeof(c->subject), &crt->subject) < 0) c->subject[0] = 0;
        if (mbedtls_x509_dn_gets(c->issuer, sizeof(c->issuer), &crt->issuer) < 0) c->issuer[0] = 0;
        copy_time(&c->valid_from, &crt->valid_from);
        copy_time(&c->valid_to, &crt->valid_to);
        if (d->count < depth + 1) d->count = depth + 1;
    }
    d->flags |= *flags;
    return 0;
}

static CURLcode sslctx_cb(CURL *curl, void *sslctx, void *parm)
{
    (void)curl;
    mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config *)sslctx, &ca_chain, NULL);
    if (parm) {
        /* each connection (a redirect opens a new one) starts a new record */
        tlsdiag *d = parm;
        memset(d, 0, sizeof(*d));
        mbedtls_ssl_conf_verify((mbedtls_ssl_config *)sslctx, verify_cb, d);
    }
    return CURLE_OK;
}

/* The PSP's clock as the certificate check read it (time()), in local time. */
static void clock_text(time_t now, char *out, int size)
{
    int tz = 0, dst = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tz);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst);
    if (now != (time_t)-1) now += (time_t)tz * 60 + (dst ? 3600 : 0);
    tlsdiag_format_time(now, 1, out, size);
}

static void rtc_text(const ScePspDateTime *t, char *out, int size)
{
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d", t->year, t->month, t->day, t->hour, t->minute, t->second);
}

/* Writes the details of a failed certificate check to the report file. */
static void write_report(const char *url, const tlsdiag *d)
{
    if (!report_path[0]) return;
    char *buf = malloc(4096);
    if (!buf) return;

    time_t now = time(NULL);
    struct tm tm;
    char utc[32] = "unreadable", kernel[32] = "?", rtc_utc[40] = "?", rtc_local[40] = "?";
    if (now != (time_t)-1 && gmtime_r(&now, &tm))
        strftime(utc, sizeof(utc), "%Y-%m-%d %H:%M:%S", &tm);
    /* what the SDK's time() would have said */
    SceKernelTimeval ktv;
    memset(&ktv, 0, sizeof(ktv));
    int kr = sceKernelLibcGettimeofday(&ktv, NULL);
    time_t kt = (time_t)ktv.tv_sec;
    if (kr >= 0 && gmtime_r(&kt, &tm))
        strftime(kernel, sizeof(kernel), "%Y-%m-%d %H:%M:%S", &tm);
    ScePspDateTime dt;
    int r1 = sceRtcGetCurrentClock(&dt, 0);
    if (r1 >= 0) rtc_text(&dt, rtc_utc, sizeof(rtc_utc));
    int r2 = sceRtcGetCurrentClockLocalTime(&dt);
    if (r2 >= 0) rtc_text(&dt, rtc_local, sizeof(rtc_local));
    int tz = 0, dst = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tz);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst);

    int len = snprintf(buf, 4096,
                       "Plugin Manager %s: a certificate check failed\n"
                       "URL: %s\n"
                       "Clock used by the check: %lld = %s UTC\n"
                       "Kernel libc clock (not used): %u = %s (%d)\n"
                       "RTC: %s UTC (%d), %s local (%d)\n"
                       "Time zone: %+d minutes, daylight saving: %d\n"
                       "Trusted authorities: %d loaded from %s (parse result %d)\n\n",
                       PM_VERSION, url ? url : "?", (long long)now, utc, (unsigned)ktv.tv_sec, kernel, kr,
                       rtc_utc, r1, rtc_local, r2, tz, dst, ca_count, ca_path, ca_parse_result);
    if (len > 0 && len < 4096) len += tlsdiag_report(d, buf + len, 4096 - len);
    if (len > 0) fs_write_all(report_path, buf, len < 4096 ? len : 4095);
    free(buf);
}

typedef struct {
    fs_file f;
    char *mem;
    int len, cap, max;
    int too_big, write_error;
    net_progress_fn cb;
    void *ud;
    tlsdiag tls;            /* the certificate check of the last connection */
    net_response *response;
    resume_state *download;
    int64_t done, total;
    const char *post;       /* form data to POST, or NULL for GET */
    long post_len;
} xfer;

void net_set_cookies(const char *path)
{
    pm_strlcpy(cookie_path, path ? path : "", sizeof(cookie_path));
}

static int cookies_ready(void)
{
    if (!cookie_path[0]) return 0;
    if (!cookie_share) {
        cookie_share = curl_share_init();
        if (!cookie_share) return 0;
        curl_share_setopt(cookie_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    }
    return 1;
}

void net_save_cookies(void)
{
    if (!inited || !cookie_share || !cookie_path[0]) return;
    CURL *c = curl_easy_init();
    if (!c) return;
    char native[PM_PATH_MAX];
    curl_easy_setopt(c, CURLOPT_SHARE, cookie_share);
    curl_easy_setopt(c, CURLOPT_COOKIEJAR, fs_native_path(cookie_path, native, sizeof(native)));
    curl_easy_cleanup(c);   /* writes the jar */
}

void net_clear_cookies(void)
{
    if (inited && cookie_share) {
        CURL *c = curl_easy_init();
        if (c) {
            curl_easy_setopt(c, CURLOPT_SHARE, cookie_share);
            curl_easy_setopt(c, CURLOPT_COOKIELIST, "ALL");
            curl_easy_cleanup(c);
        }
    }
    if (cookie_path[0]) fs_remove(cookie_path);
    cookies_loaded = 1;     /* nothing left to load */
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    xfer *x = userdata;
    if (nmemb && size > SIZE_MAX / nmemb) return 0;
    size_t n = size * nmemb;
    if (x->download) return resume_write(x->download, ptr, n);
    if (x->f >= 0) {
        if (n > (size_t)(x->max - x->len)) { x->too_big = 1; return 0; }
        if (fs_write(x->f, ptr, (int)n) != (int)n) {
            x->write_error = 1;
            return 0;
        }
        x->len += (int)n;
        return n;
    }
    if (n > (size_t)(x->max - x->len)) {
        x->too_big = 1;
        return 0;
    }
    if (x->len + (int)n + 1 > x->cap) {
        int cap = x->cap ? x->cap : 64 * 1024;
        while (cap < x->len + (int)n + 1) cap *= 2;
        if (cap > x->max + 1) cap = x->max + 1;
        char *m = realloc(x->mem, cap);
        if (!m) {
            x->too_big = 1;
            return 0;
        }
        x->mem = m;
        x->cap = cap;
    }
    memcpy(x->mem + x->len, ptr, n);
    x->len += (int)n;
    x->mem[x->len] = 0;
    return n;
}

static int xferinfo_cb(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    xfer *x = p;
    int64_t offset = x->download ? x->download->offset : 0;
    int64_t done = dlnow > INT64_MAX - offset ? INT64_MAX : (int64_t)dlnow + offset;
    int64_t total = dltotal > 0 && dltotal <= INT64_MAX - offset ? (int64_t)dltotal + offset : -1;
    x->done = done; x->total = total;
    return x->cb ? x->cb(x->ud, done, total) : 0;
}

static int poll_cancel(void *ud)
{
    xfer *x = ud;
    return x->cb ? x->cb(x->ud, x->done, x->total) : 0;
}

static int perform(const char *url, xfer *x, char *err, int errlen)
{
    if (!inited && net_init() < 0) {
        snprintf(err, errlen, "%s", net_last_error());
        return -1;
    }
    if (!net_is_connected()) {
        snprintf(err, errlen, "Not connected to a network");
        return -1;
    }

    CURL *c = curl_easy_init();
    if (!c) {
        snprintf(err, errlen, "Out of memory");
        return -1;
    }

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    struct curl_slist *headers = NULL;
    /* GitHub asset IDs identify a particular uploaded file; the API needs
       this header to return its bytes instead of JSON metadata. */
    if (pm_starts_with(url, "https://api.github.com/repos/") && strstr(url, "/releases/assets/")) {
        headers = curl_slist_append(NULL, "Accept: application/octet-stream");
        if (!headers) { curl_easy_cleanup(c); snprintf(err, errlen, "Out of memory"); return -1; }
    }
    if (x->download) {
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, resume_header);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, x->download);
        if (x->download->offset) {
            char range_validator[192];
            snprintf(range_validator, sizeof(range_validator), "If-Range: %s", x->download->validator);
            struct curl_slist *next = curl_slist_append(headers, range_validator);
            if (!next) {
                curl_slist_free_all(headers); curl_easy_cleanup(c);
                snprintf(err, errlen, "Out of memory"); return -1;
            }
            headers = next;
            curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)x->download->offset);
        }
    }
    if (x->post) {
        struct curl_slist *next = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        if (!next) { curl_slist_free_all(headers); curl_easy_cleanup(c); snprintf(err, errlen, "Out of memory"); return -1; }
        headers = next;
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, x->post_len);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, x->post);
    }
    if (cookies_ready()) {
        char native[PM_PATH_MAX];
        curl_easy_setopt(c, CURLOPT_SHARE, cookie_share);
        /* The first request loads the saved cookies; "" just turns cookies on. */
        curl_easy_setopt(c, CURLOPT_COOKIEFILE, cookies_loaded ? "" : fs_native_path(cookie_path, native, sizeof(native)));
        cookies_loaded = 1;
    }
    if (headers) curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    if (pm_http_policy(c, url, user_agent, minimum_tls12) != CURLE_OK) {
        curl_slist_free_all(headers); curl_easy_cleanup(c);
        snprintf(err, errlen, "Could not configure secure HTTP requests"); return -1;
    }
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 16L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, x->download ? "identity" : "");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, x);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, x);

    if (tls_verify) {
        if (load_ca() < 0) {
            curl_easy_cleanup(c);
            curl_slist_free_all(headers);
            snprintf(err, errlen, "Can't load the certificate bundle (cacert.pem)");
            return -1;
        }
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(c, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
        curl_easy_setopt(c, CURLOPT_SSL_CTX_DATA, &x->tls);
        if (curl_easy_setopt(c, CURLOPT_SSL_CTX_FUNCTION, sslctx_cb) != CURLE_OK) {
            char native[PM_PATH_MAX];
            curl_easy_setopt(c, CURLOPT_CAINFO, fs_native_path(ca_path, native, sizeof(native)));
        }
    }
    else {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = pm_transfer_run(c, poll_cancel, x);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (x->response) {
        char *final_url = NULL, *type = NULL;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &final_url);
        curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &type);
        x->response->url_too_long = pm_strlcpy(x->response->url, final_url ? final_url : url, sizeof(x->response->url)) >= sizeof(x->response->url);
        pm_strlcpy(x->response->content_type, type ? type : "", sizeof(x->response->content_type));
        x->response->status = code;
    }
    char *effective = NULL;
    char failed_url[512] = "";
    if (res == CURLE_PEER_FAILED_VERIFICATION) {
        /* the URL of the connection that failed, which may be a redirect */
        if (curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &effective) != CURLE_OK || !effective) effective = (char *)url;
        pm_strlcpy(failed_url, effective, sizeof(failed_url));
        failed_url[strcspn(failed_url, "?#")] = 0;     /* download links carry long signed queries */
        tlsdiag_set_host(&x->tls, failed_url);
    }
    curl_easy_cleanup(c);
    curl_slist_free_all(headers);

    if (x->download && x->download->space_error) {
        snprintf(err, errlen, "Not enough free space for this download");
        return -1;
    }

    if (res == CURLE_OK && x->response && x->response->url_too_long) {
        snprintf(err, errlen, "The redirected address is too long"); return -1;
    }
    if (res == CURLE_OK) return 0;

    switch (res) {
    case CURLE_ABORTED_BY_CALLBACK:
        snprintf(err, errlen, "Cancelled");
        break;
    case CURLE_COULDNT_RESOLVE_HOST:
        snprintf(err, errlen, "Couldn't find the server. Check your internet connection.");
        break;
    case CURLE_COULDNT_CONNECT:
        snprintf(err, errlen, "Couldn't connect to the server");
        break;
    case CURLE_OPERATION_TIMEDOUT:
        snprintf(err, errlen, "The connection timed out");
        break;
    case CURLE_HTTP_RETURNED_ERROR:
        snprintf(err, errlen, "The server answered with error %ld", code);
        break;
    case CURLE_PEER_FAILED_VERIFICATION: {   /* == CURLE_SSL_CACERT since curl 7.62 */
        char clock[48];
        clock_text(time(NULL), clock, sizeof(clock));
        tlsdiag_message(&x->tls, clock, err, errlen);
        write_report(failed_url, &x->tls);
        break;
    }
    case CURLE_SSL_CONNECT_ERROR:
        snprintf(err, errlen, "Secure connection failed (%s)", errbuf[0] ? errbuf : "TLS error");
        break;
    case CURLE_WRITE_ERROR:
        if (x->too_big) snprintf(err, errlen, "The file is too large");
        else snprintf(err, errlen, "Couldn't write the file (storage full?)");
        break;
    default:
        snprintf(err, errlen, "Download failed: %s", errbuf[0] ? errbuf : curl_easy_strerror(res));
        break;
    }
    return -1;
}

int net_download(const char *url, const char *dest_path, net_progress_fn cb, void *ud, char *err, int errlen)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (cb && cb(ud, 0, -1)) { snprintf(err, errlen, "Cancelled"); return -1; }
        resume_state state;
        if (resume_open(&state, url, dest_path) < 0) {
            snprintf(err, errlen, "Can't create %s", dest_path);
            return -1;
        }
        xfer x;
        memset(&x, 0, sizeof(x));
        x.f = -1; x.cb = cb; x.ud = ud; x.download = &state;
        x.done = state.offset; x.total = -1;
        int result = perform(url, &x, err, errlen);
        int closed = resume_close(&state, result == 0);
        if (result == 0 && closed == 0) return 0;
        if (state.retry_fresh && attempt == 0) {
            resume_discard(&state); /* Server changed the file or refused Range. */
            continue;
        }
        if (result == 0) snprintf(err, errlen, "Could not finish the download; retry to resume");
        return -1;
    }
    return -1;
}

char *net_get_info(const char *url, int max_size, int *out_len, net_response *response,
                   net_progress_fn cb, void *ud, char *err, int errlen)
{
    if (response) memset(response, 0, sizeof(*response));
    if (out_len) *out_len = 0;
    if (max_size < 1 || max_size > 4 * 1024 * 1024) {
        snprintf(err, errlen, "Invalid response size limit"); return NULL;
    }
    xfer x;
    memset(&x, 0, sizeof(x));
    x.f = -1;
    x.max = max_size;
    x.response = response;
    x.total = -1;
    x.cb = cb;
    x.ud = ud;
    if (perform(url, &x, err, errlen) < 0) {
        free(x.mem);
        return NULL;
    }
    if (!x.mem) x.mem = calloc(1, 1);
    if (out_len) *out_len = x.len;
    return x.mem;
}

char *net_get(const char *url, int max_size, int *out_len, net_progress_fn cb, void *ud, char *err, int errlen)
{
    return net_get_info(url, max_size, out_len, NULL, cb, ud, err, errlen);
}

/* Ephemeral, decoded page spool; never resumes or keeps failed content. */
static int request_file(const char *url, const char *post, const char *path, int maximum, net_response *response,
                        net_progress_fn cb, void *ud, char *err, int errlen)
{
    if (maximum < 1 || maximum > 8 * 1024 * 1024) {
        snprintf(err, errlen, "Invalid page limit"); return -1;
    }
    if (cb && cb(ud, 0, -1)) { snprintf(err, errlen, "Cancelled"); return -1; }
    if (response) memset(response, 0, sizeof(*response));
    xfer x; memset(&x, 0, sizeof(x));
    x.f = fs_open(path, FS_WRITE); x.max = maximum; x.response = response;
    x.cb = cb; x.ud = ud; x.total = -1;
    x.post = post; x.post_len = post ? (long)strlen(post) : 0;
    if (x.f < 0) { snprintf(err, errlen, "Could not create the page cache"); return -1; }
    int r = perform(url, &x, err, errlen); fs_close(x.f);
    if (r < 0) fs_remove(path);
    return r < 0 ? -1 : x.len;
}

int net_get_file(const char *url, const char *path, int maximum, net_response *response,
                 net_progress_fn cb, void *ud, char *err, int errlen)
{
    return request_file(url, NULL, path, maximum, response, cb, ud, err, errlen);
}

int net_post_file(const char *url, const char *form, const char *path, int maximum, net_response *response,
                  net_progress_fn cb, void *ud, char *err, int errlen)
{
    return request_file(url, form, path, maximum, response, cb, ud, err, errlen);
}
