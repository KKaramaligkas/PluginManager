#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "resume.h"
#include "util.h"

static int usable(const char *validator)
{
    return validator && *validator && !pm_starts_with(validator, "W/") &&
           !strchr(validator, '\r') && !strchr(validator, '\n') && strlen(validator) < 160;
}

void resume_discard(resume_state *state)
{
    fs_remove(state->partial);
    fs_remove(state->metadata);
    state->offset = 0;
    state->validator[0] = 0;
}

int resume_open(resume_state *s, const char *url, const char *destination)
{
    memset(s, 0, sizeof(*s));
    s->file = -1;
    s->url = url;
    s->total = s->range_start = s->range_end = -1;
    if (pm_strlcpy(s->destination, destination, sizeof(s->destination)) >= sizeof(s->destination) ||
        snprintf(s->partial, sizeof(s->partial), "%s.part", destination) >= (int)sizeof(s->partial) ||
        snprintf(s->metadata, sizeof(s->metadata), "%s.part.json", destination) >= (int)sizeof(s->metadata)) return -1;
    char *text = fs_read_all(s->metadata, NULL, 4096);
    cJSON *meta = text ? cJSON_Parse(text) : NULL;
    free(text);
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(meta, "url");
    const cJSON *validator = cJSON_GetObjectItemCaseSensitive(meta, "validator");
    int64_t bytes = fs_size(s->partial);
    if (cJSON_IsString(source) && !strcmp(source->valuestring, url) &&
        cJSON_IsString(validator) && usable(validator->valuestring) && bytes > 0) {
        s->offset = bytes;
        pm_strlcpy(s->validator, validator->valuestring, sizeof(s->validator));
    } else resume_discard(s);
    cJSON_Delete(meta);
    s->file = fs_open(s->partial, s->offset ? FS_APPEND : FS_WRITE);
    return s->file >= 0 ? 0 : -1;
}

static int save_validator(resume_state *s, const char *value)
{
    if (!usable(value)) { fs_remove(s->metadata); s->validator[0] = 0; return 0; }
    cJSON *meta = cJSON_CreateObject();
    if (!meta) return -1;
    if (!cJSON_AddStringToObject(meta, "url", s->url) ||
        !cJSON_AddStringToObject(meta, "validator", value)) { cJSON_Delete(meta); return -1; }
    char *text = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    if (!text) return -1;
    int ret = fs_write_all(s->metadata, text, (int)strlen(text));
    free(text);
    if (ret == 0) pm_strlcpy(s->validator, value, sizeof(s->validator));
    return ret;
}

static int number(const char **text, int64_t *out)
{
    if (**text < '0' || **text > '9') return -1;
    errno = 0;
    char *end;
    long long value = strtoll(*text, &end, 10);
    if (errno || value < 0 || end == *text) return -1;
    *out = value; *text = end;
    return 0;
}

size_t resume_header(char *data, size_t size, size_t count, void *ud)
{
    resume_state *s = ud;
    if (count && size > SIZE_MAX / count) return 0;
    size_t n = size * count;
    char line[1024];
    if (n >= sizeof(line)) return n; /* large irrelevant headers are ignored */
    memcpy(line, data, n); line[n] = 0;
    char *value = pm_trim(line);
    if (pm_starts_with(value, "HTTP/")) {
        const char *space = strchr(value, ' ');
        s->status = space && strlen(space + 1) >= 3 && space[1] >= '0' && space[1] <= '9' &&
            space[2] >= '0' && space[2] <= '9' && space[3] >= '0' && space[3] <= '9' ?
            (space[1] - '0') * 100 + (space[2] - '0') * 10 + space[3] - '0' : 0;
        s->ready = s->encoded = 0;
        s->range_start = s->range_end = s->total = -1;
        s->etag[0] = s->modified[0] = 0;
    } else if (!*value) {
        if (s->status == 416 && s->offset) { s->retry_fresh = 1; return 0; }
        if (s->status >= 300 || s->status < 200) return n;
        if (s->encoded || (s->offset && (s->status != 206 || s->range_start != s->offset ||
            s->range_end < s->range_start || s->total <= s->range_end)) ||
            (!s->offset && s->status != 200)) {
            s->retry_fresh = s->offset != 0;
            return 0;
        }
        const char *validator = usable(s->etag) ? s->etag : s->modified;
        if (s->offset && usable(validator) && strcmp(validator, s->validator)) {
            s->retry_fresh = 1; return 0;
        }
        int64_t free_bytes = fs_free_bytes(s->partial);
        if (s->total >= s->offset && free_bytes >= 0 &&
            (s->total - s->offset > INT64_MAX - 64 * 1024 ||
             free_bytes < s->total - s->offset + 64 * 1024)) { s->space_error = 1; return 0; }
        if (save_validator(s, validator) < 0) return 0;
        s->ready = 1;
    } else if (!pm_strncasecmp(value, "Content-Range:", 14)) {
        const char *range = pm_trim(value + 14);
        if (pm_starts_with(range, "bytes ")) {
            range += 6;
            int64_t start, end, total;
            if (number(&range, &start) == 0 && *range++ == '-' &&
                number(&range, &end) == 0 && *range++ == '/' &&
                number(&range, &total) == 0 && !*range) {
                s->range_start = start; s->range_end = end; s->total = total;
            }
        }
    } else if (!pm_strncasecmp(value, "Content-Length:", 15)) {
        const char *number_text = pm_trim(value + 15);
        int64_t length;
        if (number(&number_text, &length) == 0 && !*number_text && s->status == 200) s->total = length;
    } else if (!pm_strncasecmp(value, "ETag:", 5)) {
        char *etag = pm_trim(value + 5);
        if (strlen(etag) < sizeof(s->etag)) pm_strlcpy(s->etag, etag, sizeof(s->etag));
    } else if (!pm_strncasecmp(value, "Last-Modified:", 14)) {
        char *modified = pm_trim(value + 14);
        if (strlen(modified) < sizeof(s->modified)) pm_strlcpy(s->modified, modified, sizeof(s->modified));
    } else if (!pm_strncasecmp(value, "Content-Encoding:", 17)) {
        s->encoded = pm_strcasecmp(pm_trim(value + 17), "identity") != 0;
    }
    return n;
}

size_t resume_write(resume_state *s, const void *data, size_t size)
{
    if (s->status >= 300 || s->status < 200) return size; /* no redirect/error bodies in the file */
    if (!s->ready || size > INT_MAX) return 0;
    int64_t free_bytes = fs_free_bytes(s->partial);
    if (free_bytes >= 0 && (uint64_t)free_bytes < size + 64 * 1024) { s->space_error = 1; return 0; }
    return fs_write(s->file, data, (int)size) == (int)size ? size : 0;
}

int resume_close(resume_state *s, int success)
{
    fs_close(s->file); s->file = -1;
    if (success) {
        if (!s->ready || (s->total >= 0 && fs_size(s->partial) != s->total) || fs_sync(s->partial) < 0) return -1;
        fs_remove(s->destination);
        if (fs_rename(s->partial, s->destination) < 0) {
            if (fs_copy(s->partial, s->destination) < 0) return -1;
            fs_remove(s->partial);
        }
        fs_remove(s->metadata);
        return 0;
    }
    if (!usable(s->validator) || !fs_exists(s->metadata) || fs_size(s->partial) <= 0)
        resume_discard(s);
    return -1;
}
