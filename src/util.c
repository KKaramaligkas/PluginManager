/*
    Plugin Manager for ARK-5
    util.c: small string/path helpers shared by the PSP app and the host tests.
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/sha256.h>

#include "fs.h"
#include "util.h"

char *pm_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

char *pm_strndup(const char *s, size_t n)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len > n) len = n;
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

char *pm_sprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return NULL;
    char *buf = malloc(n + 1);
    if (!buf) return NULL;
    va_start(ap, fmt);
    vsnprintf(buf, n + 1, fmt, ap);
    va_end(ap);
    return buf;
}

size_t pm_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t n = (len >= size) ? size - 1 : len;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

size_t pm_strlcat(char *dst, const char *src, size_t size)
{
    size_t dlen = strnlen(dst, size);
    if (dlen == size) return size + strlen(src);
    return dlen + pm_strlcpy(dst + dlen, src, size - dlen);
}

int pm_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

int pm_strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

int pm_starts_with(const char *s, const char *prefix)
{
    return pm_strncasecmp(s, prefix, strlen(prefix)) == 0;
}

int pm_ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && pm_strcasecmp(s + ls - lx, suffix) == 0;
}

char *pm_trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int pm_glob_match(const char *p, const char *t)
{
    const char *star_p = NULL, *star_t = NULL;
    while (*t) {
        if (*p == '*') {
            star_p = ++p;
            star_t = t;
        }
        else if (*p == '?' || (*p && tolower((unsigned char)*p) == tolower((unsigned char)*t))) {
            p++;
            t++;
        }
        else if (star_p) {
            p = star_p;
            t = ++star_t;
        }
        else {
            return 0;
        }
    }
    while (*p == '*') p++;
    return *p == 0;
}

int pm_version_compare(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;

    while (*a || *b) {
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;
        if (!*a || !*b) break;

        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *sa = a, *sb = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;
            size_t la = a - sa, lb = b - sb;
            if (la != lb) return (la < lb) ? -1 : 1;
            int c = strncmp(sa, sb, la);
            if (c) return c;
        }
        else if (isdigit((unsigned char)*a)) {
            return 1;   /* "1.0.1" > "1.0beta" */
        }
        else if (isdigit((unsigned char)*b)) {
            return -1;
        }
        else {
            const char *sa = a, *sb = b;
            while (isalpha((unsigned char)*a)) a++;
            while (isalpha((unsigned char)*b)) b++;
            size_t la = a - sa, lb = b - sb;
            int c = pm_strncasecmp(sa, sb, la < lb ? la : lb);
            if (c) return c;
            if (la != lb) return (la < lb) ? -1 : 1;
        }
    }

    while (*a && !isalnum((unsigned char)*a)) a++;
    while (*b && !isalnum((unsigned char)*b)) b++;
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

const char *pm_basename(const char *path)
{
    const char *s = strrchr(path, '/');
    if (!s) s = strrchr(path, ':');
    return s ? s + 1 : path;
}

void pm_dirname(const char *path, char *out, size_t size)
{
    const char *s = strrchr(path, '/');
    if (!s) s = strrchr(path, ':');
    size_t n = s ? (size_t)(s - path) + 1 : 0;
    if (n >= size) n = size - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

int pm_path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int need_slash = dl && dir[dl - 1] != '/' && dir[dl - 1] != ':';
    int n = snprintf(out, size, "%s%s%s", dir, need_slash ? "/" : "", name);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int pm_sanitize_relpath(const char *in, char *out, size_t size)
{
    size_t o = 0;
    const char *p = in;

    if (!in || !*in) return -1;

    while (*p) {
        /* skip separators */
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;

        /* find the end of this component */
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return -1;
            p++;
        }
        size_t len = p - start;
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') return -1;

        if (o && o + 1 < size) out[o++] = '/';
        if (o + len >= size) return -1;
        memcpy(out + o, start, len);
        o += len;
        out[o] = 0;
    }

    if (o == 0) return -1;
    /* keep a trailing slash if the input designated a directory */
    size_t il = strlen(in);
    if (in[il - 1] == '/' || in[il - 1] == '\\') {
        if (o + 1 >= size) return -1;
        out[o++] = '/';
        out[o] = 0;
    }
    return 0;
}

int pm_valid_id(const char *id)
{
    if (!id || !*id) return 0;
    size_t n = 0;
    for (; *id; id++, n++) {
        char c = *id;
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) return 0;
    }
    return n <= 48;
}

void pm_format_size(int64_t bytes, char *out, size_t size)
{
    if (bytes < 0) {
        pm_strlcpy(out, "?", size);
    }
    else if (bytes < 1024) {
        snprintf(out, size, "%d B", (int)bytes);
    }
    else if (bytes < 1024 * 1024) {
        snprintf(out, size, "%d KB", (int)((bytes + 1023) / 1024));
    }
    else {
        int tenths = (int)((bytes * 10 + 512 * 1024) / (1024 * 1024));
        snprintf(out, size, "%d.%d MB", tenths / 10, tenths % 10);
    }
}

static void hex32(const unsigned char *d, char out_hex[65])
{
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out_hex[i * 2] = hx[d[i] >> 4];
        out_hex[i * 2 + 1] = hx[d[i] & 15];
    }
    out_hex[64] = 0;
}

void pm_sha256_buf(const void *data, size_t len, char out_hex[65])
{
    unsigned char digest[32];
    mbedtls_sha256_ret(data, len, digest, 0);
    hex32(digest, out_hex);
}

int pm_sha256_file(const char *path, char out_hex[65])
{
    fs_file f = fs_open(path, FS_READ);
    if (f < 0) return -1;

    unsigned char *buf = malloc(32 * 1024);
    if (!buf) {
        fs_close(f);
        return -1;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    int n, ret = 0;
    while ((n = fs_read(f, buf, 32 * 1024)) > 0)
        mbedtls_sha256_update_ret(&ctx, buf, n);
    if (n < 0) ret = -1;

    unsigned char digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    free(buf);
    fs_close(f);

    hex32(digest, out_hex);
    return ret;
}
