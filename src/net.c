/*
    Plugin Manager for ARK-5
    net.c: Wi-Fi connection (system dialog) and HTTPS downloads.

    The PSP's own SSL library only speaks obsolete protocol versions, so all
    transfers go through libcurl + mbedTLS (TLS 1.2). The CA bundle is parsed
    once and handed to every connection through CURLOPT_SSL_CTX_FUNCTION.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psputility.h>
#include <pspwlan.h>

#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "fs.h"
#include "gfx.h"
#include "net.h"
#include "util.h"
#include "version.h"

static int inited;
static char ca_path[PM_PATH_MAX];
static int tls_verify = 1;
static mbedtls_x509_crt ca_chain;
static int ca_state;            /* 0 not loaded, 1 loaded, -1 failed */
static SceUID ca_lock = -1;

int net_init(void)
{
    if (inited) return 0;
    sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

    int r = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
    if (r < 0) return r;
    sceNetInetInit();
    sceNetApctlInit(0x8000, 48);
    sceNetResolverInit();

    curl_global_init(CURL_GLOBAL_ALL);
    ca_lock = sceKernelCreateSema("pm_ca", 0, 1, 1, NULL);
    inited = 1;
    return 0;
}

void net_term(void)
{
    if (!inited) return;
    curl_global_cleanup();
    if (ca_state == 1) mbedtls_x509_crt_free(&ca_chain);
    ca_state = 0;
    sceNetApctlDisconnect();
    sceNetResolverTerm();
    sceNetApctlTerm();
    sceNetInetTerm();
    sceNetTerm();
    sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    inited = 0;
}

int net_wlan_switch_on(void)
{
    return sceWlanGetSwitchState() != 0;
}

int net_is_connected(void)
{
    int state = 0;
    if (!inited) return 0;
    if (sceNetApctlGetState(&state) < 0) return 0;
    return state == PSP_NET_APCTL_STATE_GOT_IP;
}

int net_connect_dialog(void (*draw)(void *ud), void *ud)
{
    if (net_init() < 0) return -1;
    if (net_is_connected()) return 0;

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

    if (sceUtilityNetconfInitStart(&data) < 0) return -1;

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
    return net_is_connected() ? 0 : -1;
}

void net_set_tls(const char *ca_file, int verify)
{
    pm_strlcpy(ca_path, ca_file ? ca_file : "", sizeof(ca_path));
    tls_verify = verify;
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
        if (ca_state < 0) mbedtls_x509_crt_free(&ca_chain);
    }
    sceKernelSignalSema(ca_lock, 1);
    return ca_state;
}

static CURLcode sslctx_cb(CURL *curl, void *sslctx, void *parm)
{
    (void)curl;
    (void)parm;
    mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config *)sslctx, &ca_chain, NULL);
    return CURLE_OK;
}

typedef struct {
    fs_file f;
    char *mem;
    int len, cap, max;
    int too_big, write_error;
    net_progress_fn cb;
    void *ud;
} xfer;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    xfer *x = userdata;
    size_t n = size * nmemb;
    if (x->f >= 0) {
        if (fs_write(x->f, ptr, (int)n) != (int)n) {
            x->write_error = 1;
            return 0;
        }
        return n;
    }
    if (x->len + (int)n > x->max) {
        x->too_big = 1;
        return 0;
    }
    if (x->len + (int)n + 1 > x->cap) {
        int cap = x->cap ? x->cap : 64 * 1024;
        while (cap < x->len + (int)n + 1) cap *= 2;
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
    return x->cb ? x->cb(x->ud, (int64_t)dlnow, dltotal > 0 ? (int64_t)dltotal : -1) : 0;
}

static int perform(const char *url, xfer *x, char *err, int errlen)
{
    if (!inited && net_init() < 0) {
        snprintf(err, errlen, "Network initialization failed");
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
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "PluginManager/" PM_VERSION " (PSP; ARK-5)");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 16L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
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
            snprintf(err, errlen, "Can't load the certificate bundle (cacert.pem)");
            return -1;
        }
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(c, CURLOPT_CAINFO, NULL);
        curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
        if (curl_easy_setopt(c, CURLOPT_SSL_CTX_FUNCTION, sslctx_cb) != CURLE_OK) {
            char native[PM_PATH_MAX];
            curl_easy_setopt(c, CURLOPT_CAINFO, fs_native_path(ca_path, native, sizeof(native)));
        }
    }
    else {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);

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
    case CURLE_PEER_FAILED_VERIFICATION:   /* == CURLE_SSL_CACERT since curl 7.62 */
        snprintf(err, errlen, "Secure connection failed: the certificate couldn't be verified. Check the date and time of your PSP.");
        break;
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
    xfer x;
    memset(&x, 0, sizeof(x));
    x.cb = cb;
    x.ud = ud;
    x.f = fs_open(dest_path, FS_WRITE);
    if (x.f < 0) {
        snprintf(err, errlen, "Can't create %s", dest_path);
        return -1;
    }
    int r = perform(url, &x, err, errlen);
    fs_close(x.f);
    if (r < 0) fs_remove(dest_path);
    return r;
}

char *net_get(const char *url, int max_size, int *out_len, net_progress_fn cb, void *ud, char *err, int errlen)
{
    xfer x;
    memset(&x, 0, sizeof(x));
    x.f = -1;
    x.max = max_size;
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
