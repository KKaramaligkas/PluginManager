/*
    Plugin Manager for ARK-5
    pluginstxt.c: reading and editing ARK's PLUGINS.TXT. The tokenizer follows
    ARK-5's own parser (SystemControl/XMBControl configparser.c).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "pluginstxt.h"
#include "util.h"

static int is_enabled_word(const char *s)
{
    return pm_strcasecmp(s, "on") == 0 || pm_strcasecmp(s, "1") == 0 ||
           pm_strcasecmp(s, "enabled") == 0 || pm_strcasecmp(s, "true") == 0;
}

/* Splits a line into its three tokens. Returns 0 when it is a plugin line. */
static int parse_line(const char *raw, char **runlevel, char **path, int *enabled)
{
    char *line = pm_strdup(raw);
    if (!line) return -1;
    char *s = pm_trim(line);

    if (!*s || strncmp(s, "//", 2) == 0 || s[0] == ';' || s[0] == '#') {
        free(line);
        return -1;
    }

    char *tok_path = NULL, *tok_enabled = NULL;
    for (char *p = s; *p; p++) {
        if (tok_enabled && (strncmp(p, "//", 2) == 0 || *p == ';' || *p == '#')) {
            *p = 0;
            break;
        }
        if (*p == ',') {
            *p = 0;
            if (!tok_path) tok_path = p + 1;
            else if (!tok_enabled) tok_enabled = p + 1;
            else break;
        }
    }
    if (!tok_enabled) {
        free(line);
        return -1;
    }

    *runlevel = pm_strdup(pm_trim(s));
    *path = pm_strdup(pm_trim(tok_path));
    *enabled = is_enabled_word(pm_trim(tok_enabled));
    free(line);
    if (!*runlevel || !*path) {
        free(*runlevel);
        free(*path);
        return -1;
    }
    return 0;
}

static int push_line(ptxt_t *pt, int index, ptxt_line *ln)
{
    ptxt_line *n = realloc(pt->lines, sizeof(ptxt_line) * (pt->count + 1));
    if (!n) return -1;
    pt->lines = n;
    if (index < 0 || index > pt->count) index = pt->count;
    memmove(&pt->lines[index + 1], &pt->lines[index], sizeof(ptxt_line) * (pt->count - index));
    pt->lines[index] = *ln;
    pt->count++;
    return 0;
}

int ptxt_load(ptxt_t *pt, const char *file)
{
    memset(pt, 0, sizeof(*pt));
    int size = 0;
    char *buf = fs_read_all(file, &size, 1024 * 1024);
    if (!buf) return 0;

    char *p = buf;
    /* skip an UTF-8 BOM */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    while (*p) {
        char *eol = strpbrk(p, "\r\n");
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        ptxt_line ln;
        memset(&ln, 0, sizeof(ln));
        ln.raw = pm_strndup(p, len);
        if (ln.raw && parse_line(ln.raw, &ln.runlevel, &ln.path, &ln.enabled) < 0) {
            ln.runlevel = ln.path = NULL;
        }
        if (!ln.raw || push_line(pt, -1, &ln) < 0) {
            free(ln.raw);
            break;
        }

        if (!eol) break;
        p = eol;
        if (*p == '\r' && p[1] == '\n') p += 2;
        else p++;
    }

    /* drop trailing empty lines, they are re-added on save */
    while (pt->count && !pt->lines[pt->count - 1].runlevel && !*pm_trim(pt->lines[pt->count - 1].raw)) {
        pt->count--;
        free(pt->lines[pt->count].raw);
    }

    free(buf);
    return 0;
}

int ptxt_save(ptxt_t *pt, const char *file)
{
    size_t cap = 256, len = 0;
    for (int i = 0; i < pt->count; i++) {
        ptxt_line *l = &pt->lines[i];
        cap += (l->raw ? strlen(l->raw) : 0) + (l->runlevel ? strlen(l->runlevel) : 0) +
               (l->path ? strlen(l->path) : 0) + 16;
    }
    char *out = malloc(cap);
    if (!out) return -1;

    for (int i = 0; i < pt->count; i++) {
        ptxt_line *l = &pt->lines[i];
        int n;
        if (l->dirty && l->runlevel)
            n = snprintf(out + len, cap - len, "%s, %s, %s\n", l->runlevel, l->path, l->enabled ? "on" : "off");
        else
            n = snprintf(out + len, cap - len, "%s\n", l->raw ? l->raw : "");
        len += n;
    }

    char dir[PM_PATH_MAX];
    pm_dirname(file, dir, sizeof(dir));
    if (dir[0]) fs_mkdirs(dir, NULL, NULL);

    int ret = fs_write_all(file, out, (int)len);
    free(out);
    if (ret == 0) pt->modified = 0;
    return ret;
}

void ptxt_free(ptxt_t *pt)
{
    for (int i = 0; i < pt->count; i++) {
        free(pt->lines[i].raw);
        free(pt->lines[i].runlevel);
        free(pt->lines[i].path);
    }
    free(pt->lines);
    memset(pt, 0, sizeof(*pt));
}

/* "ms0:SEPLUGINS/a.prx" and "MS0:/seplugins/a.prx" are the same file */
static const char *skip_device(const char *p, char *dev, size_t devlen)
{
    const char *colon = strchr(p, ':');
    dev[0] = 0;
    if (colon && colon - p < 8) {
        size_t n = colon - p;
        if (n >= devlen) n = devlen - 1;
        memcpy(dev, p, n);
        dev[n] = 0;
        p = colon + 1;
    }
    while (*p == '/') p++;
    return p;
}

int ptxt_same_path(const char *a, const char *b)
{
    char da[8], db[8];
    const char *ra = skip_device(a, da, sizeof(da));
    const char *rb = skip_device(b, db, sizeof(db));
    return pm_strcasecmp(da, db) == 0 && pm_strcasecmp(ra, rb) == 0;
}

int ptxt_find(const ptxt_t *pt, const char *path, const char *runlevel)
{
    for (int i = 0; i < pt->count; i++) {
        const ptxt_line *l = &pt->lines[i];
        if (!l->runlevel) continue;
        if (!ptxt_same_path(l->path, path)) continue;
        if (runlevel && pm_strcasecmp(l->runlevel, runlevel) != 0) continue;
        return i;
    }
    return -1;
}

int ptxt_add(ptxt_t *pt, const char *runlevel, const char *path, int enabled, int first)
{
    if (ptxt_find(pt, path, runlevel) >= 0) return 0;

    ptxt_line ln;
    memset(&ln, 0, sizeof(ln));
    ln.runlevel = pm_strdup(runlevel);
    ln.path = pm_strdup(path);
    ln.enabled = enabled ? 1 : 0;
    ln.dirty = 1;
    ln.raw = pm_sprintf("%s, %s, %s", runlevel, path, enabled ? "on" : "off");
    if (!ln.runlevel || !ln.path || !ln.raw) {
        free(ln.runlevel);
        free(ln.path);
        free(ln.raw);
        return -1;
    }

    int index = -1;
    if (first) {
        for (int i = 0; i < pt->count; i++) {
            if (pt->lines[i].runlevel) {
                index = i;
                break;
            }
        }
    }
    if (push_line(pt, index, &ln) < 0) {
        free(ln.runlevel);
        free(ln.path);
        free(ln.raw);
        return -1;
    }
    pt->modified = 1;
    return 1;
}

int ptxt_set_enabled(ptxt_t *pt, const char *path, int enabled)
{
    int changed = 0;
    for (int i = 0; i < pt->count; i++) {
        ptxt_line *l = &pt->lines[i];
        if (!l->runlevel || !ptxt_same_path(l->path, path)) continue;
        if (l->enabled != (enabled ? 1 : 0)) {
            l->enabled = enabled ? 1 : 0;
            l->dirty = 1;
            changed++;
        }
    }
    if (changed) pt->modified = 1;
    return changed;
}

int ptxt_remove(ptxt_t *pt, const char *path, const char *runlevel)
{
    int removed = 0;
    for (int i = 0; i < pt->count;) {
        ptxt_line *l = &pt->lines[i];
        if (l->runlevel && ptxt_same_path(l->path, path) &&
                (!runlevel || pm_strcasecmp(l->runlevel, runlevel) == 0)) {
            free(l->raw);
            free(l->runlevel);
            free(l->path);
            memmove(&pt->lines[i], &pt->lines[i + 1], sizeof(ptxt_line) * (pt->count - i - 1));
            pt->count--;
            removed++;
            continue;
        }
        i++;
    }
    if (removed) pt->modified = 1;
    return removed;
}

int ptxt_is_enabled(const ptxt_t *pt, const char *path)
{
    for (int i = 0; i < pt->count; i++) {
        const ptxt_line *l = &pt->lines[i];
        if (l->runlevel && l->enabled && ptxt_same_path(l->path, path)) return 1;
    }
    return 0;
}
