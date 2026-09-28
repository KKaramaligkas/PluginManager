/*
    Plugin Manager for ARK-5
    tlsdiag.c: explains a failed certificate check in words.
*/

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tlsdiag.h"
#include "util.h"

static const char *month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

void tlsdiag_set_host(tlsdiag *d, const char *url)
{
    d->host[0] = 0;
    if (!url) return;
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t auth = strcspn(p, "/?#");
    for (size_t i = auth; i > 0; i--) {
        if (p[i - 1] == '@') {      /* user info */
            p += i;
            break;
        }
    }
    size_t n = (*p == '[') ? strcspn(p, "]") + 1 : strcspn(p, ":/?#");
    if (n >= sizeof(d->host)) n = sizeof(d->host) - 1;
    memcpy(d->host, p, n);
    d->host[n] = 0;
}

static int copy_value(const char *dn, const char *key, char *out, int size)
{
    size_t kl = strlen(key);
    for (const char *p = dn; p && *p; ) {
        if (!strncmp(p, key, kl)) {
            p += kl;
            size_t n = strcspn(p, ",");
            if (n >= (size_t)size) n = size - 1;
            memcpy(out, p, n);
            out[n] = 0;
            return 1;
        }
        p = strstr(p, ", ");
        if (p) p += 2;
    }
    return 0;
}

void tlsdiag_common_name(const char *dn, char *out, int size)
{
    if (size <= 0) return;
    if (copy_value(dn, "CN=", out, size) || copy_value(dn, "O=", out, size)) return;
    pm_strlcpy(out, dn, size);
}

void tlsdiag_format_time(time_t t, int with_time, char *out, int size)
{
    struct tm tm;
    if (t == (time_t)-1 || !gmtime_r(&t, &tm)) {
        snprintf(out, size, "unreadable");
        return;
    }
    if (with_time)
        snprintf(out, size, "%d %s %d, %02d:%02d", tm.tm_mday, month_names[tm.tm_mon], tm.tm_year + 1900,
                 tm.tm_hour, tm.tm_min);
    else
        snprintf(out, size, "%d %s %d", tm.tm_mday, month_names[tm.tm_mon], tm.tm_year + 1900);
}

static void cert_date(const tlsdiag_time *t, char *out, int size)
{
    if (t->month >= 1 && t->month <= 12)
        snprintf(out, size, "%d %s %d", t->day, month_names[t->month - 1], t->year);
    else
        snprintf(out, size, "?");
}

void tlsdiag_message(const tlsdiag *d, const char *clock, char *out, int size)
{
    const char *host = d->host[0] ? d->host : "the server";
    const tlsdiag_cert *future = NULL, *expired = NULL;
    char date[32], name[96];

    for (int i = 0; i < d->count; i++) {
        if (!future && (d->certs[i].flags & TLSDIAG_FUTURE)) future = &d->certs[i];
        if (!expired && (d->certs[i].flags & TLSDIAG_EXPIRED)) expired = &d->certs[i];
    }

    if (future) {
        cert_date(&future->valid_from, date, sizeof(date));
        snprintf(out, size, "The certificate of %s is only valid from %s, but your PSP's clock says %s. "
                 "Set the date and time in Settings > Date & Time Settings.", host, date, clock);
    }
    else if (expired) {
        cert_date(&expired->valid_to, date, sizeof(date));
        snprintf(out, size, "The certificate of %s expired on %s, and your PSP's clock says %s. "
                 "If that isn't today, set the date in Settings > Date & Time Settings.", host, date, clock);
    }
    else if ((d->flags & TLSDIAG_NAME) && d->count > 0) {
        tlsdiag_common_name(d->certs[0].subject, name, sizeof(name));
        snprintf(out, size, "%s answered with a certificate for \"%s\". The network may be redirecting "
                 "secure connections (a hotspot's login page or a filter). PSP clock: %s.", host, name, clock);
    }
    else if ((d->flags & TLSDIAG_NOT_TRUSTED) && d->count > 0) {
        tlsdiag_common_name(d->certs[d->count - 1].issuer, name, sizeof(name));
        snprintf(out, size, "The certificate of %s, issued by \"%s\", isn't from a trusted authority. "
                 "The network may be intercepting secure connections (a hotspot's login page or a filter). "
                 "PSP clock: %s.", host, name, clock);
    }
    else {
        snprintf(out, size, "The certificate of %s was rejected (problems %08X, details in data/tls_error.txt). "
                 "PSP clock: %s.", host, (unsigned)d->flags, clock);
    }
}

static int flag_names(uint32_t flags, char *out, int size)
{
    static const struct { uint32_t bit; const char *name; } names[] = {
        { TLSDIAG_EXPIRED, "expired" }, { TLSDIAG_FUTURE, "not valid yet" },
        { TLSDIAG_NAME, "for another server" }, { TLSDIAG_NOT_TRUSTED, "not trusted" },
        { TLSDIAG_REVOKED, "revoked" },
    };
    int len = snprintf(out, size, "%08X", (unsigned)flags);
    int named = 0;
    for (size_t i = 0; i < NELEMS(names) && len < size; i++) {
        if (!(flags & names[i].bit)) continue;
        len += snprintf(out + len, size - len, "%s%s", named++ ? ", " : " (", names[i].name);
    }
    if (named && len < size) len += snprintf(out + len, size - len, ")");
    return len < size ? len : size - 1;
}

static void cert_time(const tlsdiag_time *t, char *out, int size)
{
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d", t->year, t->month, t->day, t->hour, t->minute, t->second);
}

int tlsdiag_report(const tlsdiag *d, char *out, int size)
{
    char flags[96], from[24], to[24];
    int len = 0;
    if (size <= 0) return 0;
    out[0] = 0;

    flag_names(d->flags, flags, sizeof(flags));
    len += snprintf(out + len, size - len, "Server: %s\nProblems: %s\n", d->host[0] ? d->host : "?", flags);
    for (int i = 0; i < d->count && len < size; i++) {
        const tlsdiag_cert *c = &d->certs[i];
        flag_names(c->flags, flags, sizeof(flags));
        cert_time(&c->valid_from, from, sizeof(from));
        cert_time(&c->valid_to, to, sizeof(to));
        len += snprintf(out + len, size - len,
                        "\nCertificate %d%s: problems %s\n  Subject: %s\n  Issuer:  %s\n  Valid:   %s to %s UTC\n",
                        i, i == 0 ? " (the server's)" : "", flags, c->subject, c->issuer, from, to);
    }
    return len < size ? len : size - 1;
}
