/*
    Plugin Manager for ARK-5
    jar.c: the cookies a web page's scripts read and set (document.cookie),
    kept in libcurl's cookie store with the ones servers set.
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "jar.h"

#define COOKIE_MAX 4096

/* The parts of an http(s) address a cookie depends on. */
static int split_url(const char *url, int *secure, char *host, size_t host_size, char *path, size_t path_size)
{
    const char *p;
    if (!strncasecmp(url, "https://", 8)) { *secure = 1; p = url + 8; }
    else if (!strncasecmp(url, "http://", 7)) { *secure = 0; p = url + 7; }
    else return -1;
    size_t n = strcspn(p, "/?#"), h = 0;
    const char *at = memchr(p, '@', n);
    if (at) { n -= (size_t)(at + 1 - p); p = at + 1; }
    while (h < n && p[h] != ':') h++;
    if (!h || h >= host_size) return -1;
    for (size_t i = 0; i < h; i++) host[i] = (char)tolower((unsigned char)p[i]);
    host[h] = 0;
    p += n;
    size_t m = *p == '/' ? strcspn(p, "?#") : 0;
    if (!m) { m = 1; p = "/"; }
    if (m >= path_size) m = path_size - 1;
    memcpy(path, p, m);
    path[m] = 0;
    return 0;
}

/* RFC 6265 5.1.3: the host is the domain or a name under it. */
static int domain_match(const char *host, const char *domain)
{
    size_t h = strlen(host), d = strlen(domain);
    if (h == d) return !strcmp(host, domain);
    return h > d && host[h - d - 1] == '.' && !strcmp(host + h - d, domain);
}

/* RFC 6265 5.1.4 */
static int path_match(const char *path, const char *cookie_path)
{
    size_t n = strlen(cookie_path);
    if (strncmp(path, cookie_path, n)) return 0;
    return path[n] == 0 || path[n] == '/' || (n && cookie_path[n - 1] == '/');
}

int jar_cookie_string(CURLSH *share, const char *url, time_t now, char *out, size_t size)
{
    char host[256], path[1024];
    int secure;
    if (!size) return -1;
    out[0] = 0;
    if (!share || split_url(url, &secure, host, sizeof(host), path, sizeof(path)) < 0) return -1;
    CURL *c = curl_easy_init();
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_SHARE, share);
    struct curl_slist *list = NULL;
    curl_easy_getinfo(c, CURLINFO_COOKIELIST, &list);
    size_t used = 0;
    for (struct curl_slist *l = list; l; l = l->next) {
        /* domain, subdomains too, path, secure, expiry, name, value */
        char *field[7], line[COOKIE_MAX + 512];
        if (strlen(l->data) >= sizeof(line)) continue;
        strcpy(line, l->data);
        if (!strncmp(line, "#HttpOnly_", 10)) continue;     /* not for scripts */
        int n = 0;
        for (char *p = line; n < 7; n++) {
            field[n] = p;
            char *tab = strchr(p, '\t');
            if (!tab) { n++; break; }
            *tab = 0;
            p = tab + 1;
        }
        if (n < 7) continue;
        const char *domain = field[0][0] == '.' ? field[0] + 1 : field[0];
        int subdomains = !strcmp(field[1], "TRUE");
        long long expires = atoll(field[4]);
        if ((subdomains ? !domain_match(host, domain) : strcasecmp(host, domain)) || !path_match(path, field[2]) ||
            (!strcmp(field[3], "TRUE") && !secure) || (expires && expires <= (long long)now)) continue;
        int k = snprintf(out + used, size - used, "%s%s=%s", used ? "; " : "", field[5], field[6]);
        if (k < 0 || (size_t)k >= size - used) { out[used] = 0; break; }
        used += (size_t)k;
    }
    curl_slist_free_all(list);
    curl_easy_cleanup(c);
    return 0;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    return s;
}

int jar_cookie_set(CURLSH *share, const char *url, const char *cookie, time_t now)
{
    char host[256], path[1024], copy[COOKIE_MAX + 1];
    int secure;
    if (!share || split_url(url, &secure, host, sizeof(host), path, sizeof(path)) < 0) return -1;
    if (strlen(cookie) > COOKIE_MAX || strpbrk(cookie, "\t\r\n")) return -1;
    strcpy(copy, cookie);
    char *rest = strchr(copy, ';');
    if (rest) *rest++ = 0;
    char *eq = strchr(copy, '=');
    if (!eq) return -1;
    *eq = 0;
    char *name = trim(copy), *value = trim(eq + 1);
    if (!*name) return -1;
    /* the default path: the address's directory */
    char cookie_path[1024], domain[256] = "";
    snprintf(cookie_path, sizeof(cookie_path), "%s", path);
    char *slash = strrchr(cookie_path, '/');
    if (slash && slash != cookie_path) *slash = 0;
    else strcpy(cookie_path, "/");
    long long expires = 0;
    int secure_only = 0;
    for (char *attribute = rest; attribute && *attribute;) {
        char *next = strchr(attribute, ';');
        if (next) *next++ = 0;
        char *v = strchr(attribute, '=');
        if (v) *v++ = 0;
        char *key = trim(attribute);
        v = v ? trim(v) : (char *)"";
        if (!strcasecmp(key, "domain") && *v) {
            if (*v == '.') v++;
            snprintf(domain, sizeof(domain), "%s", v);
            for (char *p = domain; *p; p++) *p = (char)tolower((unsigned char)*p);
        } else if (!strcasecmp(key, "path") && *v == '/') snprintf(cookie_path, sizeof(cookie_path), "%s", v);
        else if (!strcasecmp(key, "max-age") && *v) {
            long long age = atoll(v);
            expires = age > 0 ? (long long)now + age : 1;
        } else if (!strcasecmp(key, "expires") && *v && !expires) {
            time_t when = curl_getdate(v, NULL);
            if (when != (time_t)-1) expires = when > now ? (long long)when : 1;
        } else if (!strcasecmp(key, "secure")) secure_only = 1;
        else if (!strcasecmp(key, "httponly")) return -1;      /* only servers may set those */
        attribute = next;
    }
    /* RFC 6265 5.3: a page may set cookies for its own domain or one above it */
    if (*domain && !domain_match(host, domain)) return -1;
    if (secure_only && !secure) return -1;
    char line[COOKIE_MAX + 1024];
    int n = snprintf(line, sizeof(line), "%s%s\t%s\t%s\t%s\t%lld\t%s\t%s", *domain ? "." : "", *domain ? domain : host,
                     *domain ? "TRUE" : "FALSE", cookie_path, secure_only ? "TRUE" : "FALSE", expires, name, value);
    if (n < 0 || (size_t)n >= sizeof(line)) return -1;
    CURL *c = curl_easy_init();
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_SHARE, share);
    CURLcode r = curl_easy_setopt(c, CURLOPT_COOKIELIST, line);
    curl_easy_cleanup(c);
    return r == CURLE_OK ? 0 : -1;
}
