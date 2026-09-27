/*
    Plugin Manager for ARK-5
    util.h: small string/path helpers shared by the PSP app and the host tests.
*/

#ifndef PM_UTIL_H
#define PM_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define PM_PATH_MAX 256

#define NELEMS(a) (sizeof(a) / sizeof((a)[0]))

char *pm_strdup(const char *s);
char *pm_strndup(const char *s, size_t n);
char *pm_sprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* strlcpy/strlcat semantics: always NUL terminates, returns strlen(src). */
size_t pm_strlcpy(char *dst, const char *src, size_t size);
size_t pm_strlcat(char *dst, const char *src, size_t size);

int pm_strcasecmp(const char *a, const char *b);
int pm_strncasecmp(const char *a, const char *b, size_t n);
int pm_starts_with(const char *s, const char *prefix);          /* case-insensitive */
int pm_ends_with(const char *s, const char *suffix);            /* case-insensitive */
char *pm_trim(char *s);                                          /* in place */

/* Case-insensitive glob with '*' and '?'. */
int pm_glob_match(const char *pattern, const char *text);

/* Compares version strings like "1.0h3", "v2.8.1", "2026-01-06".
   Returns <0, 0, >0 like strcmp. */
int pm_version_compare(const char *a, const char *b);

/* Path helpers (PSP style paths such as "ms0:/SEPLUGINS/x.prx"). */
const char *pm_basename(const char *path);
void pm_dirname(const char *path, char *out, size_t size);      /* keeps trailing '/' */
int pm_path_join(char *out, size_t size, const char *dir, const char *name);

/* Normalizes a relative path taken from an archive or a store entry:
   converts '\\' to '/', removes "./" and duplicate slashes and a leading '/'.
   Returns 0 on success, -1 when the path is unsafe (".." components, a
   device prefix, control characters) or empty. */
int pm_sanitize_relpath(const char *in, char *out, size_t size);

/* Returns 1 when `id` is a valid package id ([A-Za-z0-9._-], 1..48 chars). */
int pm_valid_id(const char *id);

/* Formats a byte count as "123 KB" / "4.5 MB". */
void pm_format_size(int64_t bytes, char *out, size_t size);

/* Lower-case hex SHA-256 of a file / buffer. Returns 0 on success. */
int pm_sha256_file(const char *path, char out_hex[65]);
void pm_sha256_buf(const void *data, size_t len, char out_hex[65]);

#endif
