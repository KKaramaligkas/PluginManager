/*
    Plugin Manager for ARK-5
    installer.c: runs store install scripts and removes installed packages.

    Every destination goes through installer_resolve_path(), which expands the
    %VARIABLES% and only accepts paths below a small set of folders on the
    memory stick / internal storage, so a store can never touch flash0, ARK's
    own files or save data.
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "archive.h"
#include "fs.h"
#include "installer.h"
#include "pluginstxt.h"
#include "util.h"

/* Folders (relative to ms0:/ or ef0:/) packages may write into. */
static const char *writable_prefixes[] = {
    "SEPLUGINS/",
    "PSP/GAME/",
    "PSP/GAME150/",
    "PSP/APPS/",
    "PSP/PLUGINS/",
    "PSP/THEME/",
    "PSP/VSH/",
    "ISO/",
    "kd/",
    "MUSIC/",
    "PICTURE/",
    "VIDEO/",
};

static const char *model_names[] = { "1000", "2000", "3000", "go", "street", "vita" };

typedef struct {
    install_ctx *ctx;
    db_package *rec;            /* record being built */
    const db_package *old;      /* previous record when updating */
    char **temp_files;
    int n_temp;
    ptxt_t ptxt;
    char ptxt_file[PM_PATH_MAX];
    char *err;
    int errlen;

    /* current extract step */
    const cJSON *step;
    char output[PM_PATH_MAX];
    char input[PM_PATH_MAX];
    char stage[96];
    char last_download[PM_PATH_MAX];
} run_state;

void installer_plugins_txt(const char *root, char *out, int size)
{
    snprintf(out, size, "%sSEPLUGINS/PLUGINS.TXT", root);
}

static const char *get_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

static int get_bool(const cJSON *obj, const char *key, int def)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    return def;
}

/* Splits "dev:/rest" and validates both halves. */
static int split_device(const char *path, char *dev, int devlen, char *rest, int restlen)
{
    const char *colon = strchr(path, ':');
    if (!colon || colon == path || colon - path >= devlen) return -1;
    memcpy(dev, path, colon - path);
    dev[colon - path] = 0;
    const char *r = colon + 1;
    if (*r != '/') return -1;
    while (*r == '/') r++;
    if (!*r) {
        rest[0] = 0;
        return 0;
    }
    return pm_sanitize_relpath(r, rest, restlen);
}

static int is_writable(const install_ctx *ctx, const char *dev, const char *rest)
{
    if (pm_strcasecmp(dev, "ms0") != 0 && pm_strcasecmp(dev, "ef0") != 0) return 0;

    if (ctx->protect_dir[0]) {
        char full[PM_PATH_MAX + 32];
        snprintf(full, sizeof(full), "%s:/%s", dev, rest);
        size_t pl = strlen(ctx->protect_dir);
        if (pm_strncasecmp(full, ctx->protect_dir, pl) == 0) return 0;
        /* the folder itself, without its trailing slash */
        if (pl > 1 && pm_strncasecmp(full, ctx->protect_dir, pl - 1) == 0 && full[pl - 1] == 0) return 0;
    }

    for (size_t i = 0; i < NELEMS(writable_prefixes); i++) {
        const char *p = writable_prefixes[i];
        size_t pl = strlen(p);
        /* the prefix folder itself is fine too ("PSP/GAME/") */
        if (pm_strncasecmp(rest, p, pl) == 0 && rest[pl] != 0) return 1;
        if (pm_strncasecmp(rest, p, pl - 1) == 0 && (rest[pl - 1] == 0 || (rest[pl - 1] == '/' && rest[pl] == 0)))
            return 1;
    }

    /* ARK's Custom Launcher theme */
    char full[PM_PATH_MAX + 32];
    snprintf(full, sizeof(full), "%s:/%s", dev, rest);
    if (ctx->ark_path[0]) {
        char theme[PM_PATH_MAX];
        snprintf(theme, sizeof(theme), "%sTHEME.ARK", ctx->ark_path);
        if (pm_strcasecmp(full, theme) == 0) return 1;
    }
    return 0;
}

int installer_resolve_path(const install_ctx *ctx, const char *in, char *out, int size, int must_be_writable)
{
    char exp[PM_PATH_MAX * 2];
    size_t o = 0;

    if (!in || !*in) return -1;

    for (const char *p = in; *p;) {
        const char *val = NULL;
        char tmp[PM_PATH_MAX];
        if (*p == '%') {
            const char *end = strchr(p + 1, '%');
            if (!end) return -1;
            size_t n = end - p - 1;
            char var[32];
            if (n == 0 || n >= sizeof(var)) return -1;
            memcpy(var, p + 1, n);
            var[n] = 0;

            if (!strcmp(var, "ROOT")) val = ctx->root;
            else if (!strcmp(var, "ARK")) val = ctx->ark_path;
            else if (!strcmp(var, "TEMP")) val = ctx->temp_dir;
            else {
                static const struct { const char *name, *sub; } vars[] = {
                    { "SEPLUGINS", "SEPLUGINS/" },
                    { "GAME", "PSP/GAME/" },
                    { "APPS", "PSP/APPS/" },
                    { "PSPPLUGINS", "PSP/PLUGINS/" },
                    { "ISO", "ISO/" },
                    { "THEME", "PSP/THEME/" },
                    { "VSH", "PSP/VSH/" },
                };
                for (size_t i = 0; i < NELEMS(vars); i++) {
                    if (!strcmp(var, vars[i].name)) {
                        snprintf(tmp, sizeof(tmp), "%s%s", ctx->root, vars[i].sub);
                        val = tmp;
                    }
                }
            }
            if (!val || !*val) return -1;
            p = end + 1;
        }
        else {
            if (o + 1 >= sizeof(exp)) return -1;
            exp[o++] = *p++;
            exp[o] = 0;
            continue;
        }
        size_t vl = strlen(val);
        if (o + vl >= sizeof(exp)) return -1;
        memcpy(exp + o, val, vl);
        o += vl;
        exp[o] = 0;
    }
    exp[o] = 0;

    char dev[16], rest[PM_PATH_MAX];
    if (split_device(exp, dev, sizeof(dev), rest, sizeof(rest)) < 0) return -1;
    if (must_be_writable && !is_writable(ctx, dev, rest)) return -1;

    int n = snprintf(out, size, "%s:/%s", dev, rest);
    if (n < 0 || n >= size) return -1;
    return 0;
}

int installer_check_path(const install_ctx *ctx, const char *path, char *out, int size)
{
    char dev[16], rest[PM_PATH_MAX];
    if (!path || split_device(path, dev, sizeof(dev), rest, sizeof(rest)) < 0) return -1;
    if (!is_writable(ctx, dev, rest)) return -1;
    int n = snprintf(out, size, "%s:/%s", dev, rest);
    return (n < 0 || n >= size) ? -1 : 0;
}

static int model_matches(const install_ctx *ctx, const cJSON *v)
{
    const cJSON *it;
    if (cJSON_IsString(v)) {
        const char *s = v->valuestring;
        if (pm_starts_with(s, "psp-")) s += 4;
        return ctx->model >= 0 && ctx->model < (int)NELEMS(model_names) &&
               pm_strcasecmp(s, model_names[ctx->model]) == 0;
    }
    cJSON_ArrayForEach(it, v) {
        if (model_matches(ctx, it)) return 1;
    }
    return 0;
}

int installer_condition_ok(const install_ctx *ctx, const void *step_ptr)
{
    const cJSON *step = step_ptr;
    const cJSON *cond = cJSON_GetObjectItemCaseSensitive(step, "if");
    if (!cond) return 1;
    if (!cJSON_IsObject(cond)) return 0;

    const cJSON *c;
    cJSON_ArrayForEach(c, cond) {
        if (!strcmp(c->string, "model")) {
            if (!model_matches(ctx, c)) return 0;
        }
        else if (!strcmp(c->string, "notModel")) {
            if (model_matches(ctx, c)) return 0;
        }
        else if (!strcmp(c->string, "platform")) {
            int vita = ctx->model == MODEL_VITA;
            if (!cJSON_IsString(c)) return 0;
            if (!pm_strcasecmp(c->valuestring, "vita") && !vita) return 0;
            if (!pm_strcasecmp(c->valuestring, "psp") && vita) return 0;
        }
        else if (!strcmp(c->string, "device")) {
            if (!cJSON_IsString(c) || !pm_starts_with(ctx->root, c->valuestring)) return 0;
        }
        else {
            return 0;   /* unknown condition: don't run steps we don't understand */
        }
    }
    return 1;
}

static void track_dir(void *ud, const char *dir)
{
    run_state *rs = ud;
    db_list_add(&rs->rec->dirs, &rs->rec->n_dirs, dir);
}

static int fail(run_state *rs, const char *fmt, const char *arg)
{
    snprintf(rs->err, rs->errlen, fmt, arg ? arg : "");
    return -1;
}

static int temp_name(run_state *rs, const cJSON *step, const char *url, char *out, int size)
{
    const char *file = get_str(step, "file");
    char name[96];
    if (!file && !url && rs->last_download[0]) {
        /* extract/copy without "file": use the most recent download */
        return pm_strlcpy(out, rs->last_download, size) < (size_t)size ? 0 : -1;
    }
    if (!file && url) {
        /* last path component of the URL, without query string */
        const char *q = strpbrk(url, "?#");
        size_t len = q ? (size_t)(q - url) : strlen(url);
        const char *s = url + len;
        while (s > url && s[-1] != '/') s--;
        size_t n = (url + len) - s;
        if (n == 0 || n >= sizeof(name)) return -1;
        memcpy(name, s, n);
        name[n] = 0;
        file = name;
    }
    if (!file || strchr(file, '/') || strchr(file, '\\') || strchr(file, ':') || !strcmp(file, "..") || !strcmp(file, "."))
        return -1;
    return pm_path_join(out, size, rs->ctx->temp_dir, file);
}

static int step_download(run_state *rs, const cJSON *step)
{
    install_ctx *ctx = rs->ctx;
    const char *url = get_str(step, "url");
    const char *sha = get_str(step, "sha256");
    if (!url || !(pm_starts_with(url, "https://") || pm_starts_with(url, "http://")))
        return fail(rs, "Invalid download URL", NULL);
    /* the store comes over https: its checksum is what vouches for a plain http download */
    if (pm_starts_with(url, "http://") && !sha)
        return fail(rs, "Unsafe download (http without a sha256 checksum): %s", url);

    char dest[PM_PATH_MAX];
    if (temp_name(rs, step, url, dest, sizeof(dest)) < 0) return fail(rs, "Invalid download file name", NULL);

    fs_mkdirs(ctx->temp_dir, NULL, NULL);
    db_list_add(&rs->temp_files, &rs->n_temp, dest);
    pm_strlcpy(rs->last_download, dest, sizeof(rs->last_download));

    if (ctx->progress) {
        snprintf(rs->stage, sizeof(rs->stage), "Downloading %s", pm_basename(dest));
        ctx->progress(ctx, rs->stage, 0, -1);
    }
    if (!ctx->download || ctx->download(ctx, url, dest, rs->err, rs->errlen) < 0) {
        if (!rs->err[0]) snprintf(rs->err, rs->errlen, "Download failed");
        return -1;
    }

    if (sha) {
        char hex[65];
        if (ctx->progress) ctx->progress(ctx, "Verifying download", 0, -1);
        if (pm_sha256_file(dest, hex) < 0) return fail(rs, "Can't read the downloaded file", NULL);
        if (pm_strcasecmp(hex, sha) != 0) return fail(rs, "Checksum mismatch for %s", pm_basename(dest));
    }
    return 0;
}

static int list_matches(const cJSON *list, const char *rel)
{
    const cJSON *it;
    if (cJSON_IsString(list)) {
        const char *pat = list->valuestring;
        return pm_glob_match(pat, strchr(pat, '/') ? rel : pm_basename(rel));
    }
    cJSON_ArrayForEach(it, list) {
        if (cJSON_IsString(it) && list_matches(it, rel)) return 1;
    }
    return 0;
}

static int extract_select(void *ud, const char *relpath, int64_t size, char *dest, int destlen)
{
    run_state *rs = ud;
    const cJSON *step = rs->step;
    const char *rel = relpath;
    (void)size;

    if (rs->input[0]) {
        size_t il = strlen(rs->input);
        if (pm_strncasecmp(rel, rs->input, il) != 0) return 0;
        rel += il;
        if (!*rel) return 0;
    }

    const cJSON *include = cJSON_GetObjectItemCaseSensitive(step, "include");
    const cJSON *exclude = cJSON_GetObjectItemCaseSensitive(step, "exclude");
    if (include && !list_matches(include, rel)) return 0;
    if (exclude && list_matches(exclude, rel)) return 0;

    const char *target = get_bool(step, "flatten", 0) ? pm_basename(rel) : rel;
    if (snprintf(dest, destlen, "%s%s", rs->output, target) >= destlen) return fail(rs, "Path too long: %s", rel);

    /* re-validate the final path (rel is already sanitized; file names may contain '%') */
    char checked[PM_PATH_MAX];
    if (installer_check_path(rs->ctx, dest, checked, sizeof(checked)) < 0)
        return fail(rs, "Refused to write %s", dest);

    const cJSON *keep = cJSON_GetObjectItemCaseSensitive(step, "keep");
    if (keep && list_matches(keep, rel) && fs_exists(dest)) {
        /* preserve user configuration, but keep owning the file */
        if (rs->old && db_list_has(rs->old->files, rs->old->n_files, dest))
            db_list_add(&rs->rec->files, &rs->rec->n_files, dest);
        return 0;
    }

    char dir[PM_PATH_MAX];
    pm_dirname(dest, dir, sizeof(dir));
    if (fs_mkdirs(dir, track_dir, rs) < 0) return fail(rs, "Can't create folder %s", dir);
    return 1;
}

static void extract_written(void *ud, const char *dest)
{
    run_state *rs = ud;
    db_list_add(&rs->rec->files, &rs->rec->n_files, dest);
}

static int extract_progress(void *ud, int64_t done, int64_t total)
{
    run_state *rs = ud;
    if (rs->ctx->progress) rs->ctx->progress(rs->ctx, rs->stage, done, total);
    return rs->ctx->cancelled ? rs->ctx->cancelled(rs->ctx) : 0;
}

static int step_extract(run_state *rs, const cJSON *step)
{
    install_ctx *ctx = rs->ctx;
    char archive[PM_PATH_MAX];
    if (temp_name(rs, step, NULL, archive, sizeof(archive)) < 0) return fail(rs, "Invalid archive name", NULL);
    if (!fs_exists(archive)) return fail(rs, "%s was not downloaded", pm_basename(archive));

    /* The output folder may be the device root (%ROOT% plus "include"
       patterns such as kd/...): every extracted file is checked against the
       writable folders in extract_select(). */
    const char *output = get_str(step, "output");
    if (!output || installer_resolve_path(ctx, output, rs->output, sizeof(rs->output), 0) < 0)
        return fail(rs, "Invalid extract destination %s", output);
    if (pm_strncasecmp(rs->output, "ms0:/", 5) != 0 && pm_strncasecmp(rs->output, "ef0:/", 5) != 0)
        return fail(rs, "Invalid extract destination %s", output);
    size_t ol = strlen(rs->output);
    if (ol && rs->output[ol - 1] != '/') pm_strlcat(rs->output, "/", sizeof(rs->output));

    rs->input[0] = 0;
    const char *input = get_str(step, "input");
    if (input && *input) {
        if (pm_sanitize_relpath(input, rs->input, sizeof(rs->input)) < 0) return fail(rs, "Invalid input folder %s", input);
        size_t il = strlen(rs->input);
        if (rs->input[il - 1] != '/') pm_strlcat(rs->input, "/", sizeof(rs->input));
    }

    rs->step = step;
    snprintf(rs->stage, sizeof(rs->stage), "Extracting %s", pm_basename(archive));
    if (ctx->progress) ctx->progress(ctx, rs->stage, 0, -1);

    archive_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.select = extract_select;
    opts.written = extract_written;
    opts.progress = extract_progress;
    opts.ud = rs;
    opts.temp_dir = ctx->temp_dir;

    rs->err[0] = 0;
    int n = archive_extract(archive, &opts, rs->err, rs->errlen);
    if (n < 0) return -1;
    if (n == 0 && !cJSON_GetObjectItemCaseSensitive(step, "keep"))
        return fail(rs, "Nothing to extract from %s (wrong \"input\"?)", pm_basename(archive));
    return 0;
}

static int step_copy(run_state *rs, const cJSON *step)
{
    char src[PM_PATH_MAX], dest[PM_PATH_MAX];
    if (temp_name(rs, step, NULL, src, sizeof(src)) < 0) return fail(rs, "Invalid file name", NULL);
    if (!fs_exists(src)) return fail(rs, "%s was not downloaded", pm_basename(src));

    const char *to = get_str(step, "to");
    if (!to || installer_resolve_path(rs->ctx, to, dest, sizeof(dest), 1) < 0)
        return fail(rs, "Invalid copy destination %s", to);
    size_t tl = strlen(to);
    if (to[tl - 1] == '/') pm_strlcat(dest, pm_basename(src), sizeof(dest));

    char dir[PM_PATH_MAX];
    pm_dirname(dest, dir, sizeof(dir));
    if (fs_mkdirs(dir, track_dir, rs) < 0) return fail(rs, "Can't create folder %s", dir);
    if (rs->ctx->progress) rs->ctx->progress(rs->ctx, "Copying files", 0, -1);
    if (fs_copy(src, dest) < 0) return fail(rs, "Can't write %s", dest);
    db_list_add(&rs->rec->files, &rs->rec->n_files, dest);
    return 0;
}

static int step_mkdir(run_state *rs, const cJSON *step)
{
    char dir[PM_PATH_MAX];
    const char *path = get_str(step, "path");
    if (!path || installer_resolve_path(rs->ctx, path, dir, sizeof(dir), 1) < 0)
        return fail(rs, "Invalid folder %s", path);
    if (fs_mkdirs(dir, track_dir, rs) < 0) return fail(rs, "Can't create folder %s", dir);
    return 0;
}

static int step_delete(run_state *rs, const cJSON *step)
{
    char file[PM_PATH_MAX];
    const char *path = get_str(step, "path");
    if (!path || installer_resolve_path(rs->ctx, path, file, sizeof(file), 1) < 0)
        return fail(rs, "Invalid path %s", path);
    if (fs_exists(file) && !fs_is_dir(file)) fs_remove(file);
    return 0;
}

static int valid_runlevel(const char *rl)
{
    if (!rl || !*rl || strlen(rl) > 200) return 0;
    for (const char *p = rl; *p; p++) {
        unsigned char c = *p;
        if (!(isalnum(c) || c == ' ' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int add_plugin_line(run_state *rs, const char *runlevel, const char *path, int enabled, int first)
{
    if (!valid_runlevel(runlevel)) return fail(rs, "Invalid runlevel %s", runlevel);
    if (ptxt_add(&rs->ptxt, runlevel, path, enabled, first) < 0) return fail(rs, "Out of memory", NULL);

    db_plugin *n = realloc(rs->rec->plugins, sizeof(db_plugin) * (rs->rec->n_plugins + 1));
    if (!n) return fail(rs, "Out of memory", NULL);
    rs->rec->plugins = n;
    n[rs->rec->n_plugins].runlevel = pm_strdup(runlevel);
    n[rs->rec->n_plugins].path = pm_strdup(path);
    rs->rec->n_plugins++;
    return 0;
}

static int step_plugin(run_state *rs, const cJSON *step)
{
    char path[PM_PATH_MAX];
    const char *p = get_str(step, "path");
    if (!p || installer_resolve_path(rs->ctx, p, path, sizeof(path), 1) < 0)
        return fail(rs, "Invalid plugin path %s", p);
    if (!pm_ends_with(path, ".prx")) return fail(rs, "%s is not a .prx plugin", path);

    int enabled = get_bool(step, "enabled", 1);
    const char *pos = get_str(step, "position");
    int first = pos && !pm_strcasecmp(pos, "first");

    const cJSON *rl = cJSON_GetObjectItemCaseSensitive(step, "runlevel");
    if (cJSON_IsString(rl)) return add_plugin_line(rs, rl->valuestring, path, enabled, first);
    if (cJSON_IsArray(rl) && cJSON_GetArraySize(rl) > 0) {
        const cJSON *it;
        cJSON_ArrayForEach(it, rl) {
            if (!cJSON_IsString(it)) return fail(rs, "Invalid runlevel", NULL);
            if (add_plugin_line(rs, it->valuestring, path, enabled, first) < 0) return -1;
        }
        return 0;
    }
    return fail(rs, "Missing plugin runlevel", NULL);
}

static void step_message(run_state *rs, const cJSON *step)
{
    const char *text = get_str(step, "text");
    if (!text) return;
    install_ctx *ctx = rs->ctx;
    if (ctx->messages[0]) pm_strlcat(ctx->messages, "\n", sizeof(ctx->messages));
    pm_strlcat(ctx->messages, text, sizeof(ctx->messages));
}

/* "run": once the install is done, the app offers to start this program
   (an EBOOT.PBP in PSP/GAME or PSP/APPS). Nothing is started here. */
static int step_run(run_state *rs, const cJSON *step)
{
    const char *p = get_str(step, "path");
    char path[PM_PATH_MAX], dev[16], rest[PM_PATH_MAX];
    if (!p || installer_resolve_path(rs->ctx, p, path, sizeof(path), 1) < 0 ||
            split_device(path, dev, sizeof(dev), rest, sizeof(rest)) < 0)
        return fail(rs, "Invalid program path '%s'", p);
    if ((pm_strncasecmp(rest, "PSP/GAME/", 9) != 0 && pm_strncasecmp(rest, "PSP/APPS/", 9) != 0) ||
            pm_strcasecmp(pm_basename(rest), "EBOOT.PBP") != 0)
        return fail(rs, "Only an EBOOT.PBP in PSP/GAME or PSP/APPS can be started: '%s'", p);
    if (!fs_exists(path)) return fail(rs, "The program to start is missing: %s", path);
    pm_strlcpy(rs->ctx->run_path, path, sizeof(rs->ctx->run_path));
    const char *title = get_str(step, "title");
    pm_strlcpy(rs->ctx->run_title, title ? title : "", sizeof(rs->ctx->run_title));
    return 0;
}

int installer_read_version_file(const install_ctx *ctx, const char *spec, char *out, int size)
{
    char path[PM_PATH_MAX], dev[16], rest[PM_PATH_MAX];
    if (size > 0) out[0] = 0;
    if (installer_resolve_path(ctx, spec, path, sizeof(path), 0) < 0 ||
            split_device(path, dev, sizeof(dev), rest, sizeof(rest)) < 0 ||
            (pm_strcasecmp(dev, "ms0") != 0 && pm_strcasecmp(dev, "ef0") != 0))
        return -1;

    char *text = fs_read_all(path, NULL, 256);
    if (!text) return -1;
    char *line = pm_trim(text);
    line[strcspn(line, "\r\n")] = 0;
    pm_trim(line);

    /* "5.1.6", "1.0h3" or "2026-09-12": what pm_version_compare understands */
    int ok = line[0] != 0 && strlen(line) < (size_t)size;
    for (const char *c = line; ok && *c; c++)
        ok = isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == '_';
    if (ok) pm_strlcpy(out, line, size);
    free(text);
    return ok ? 0 : -1;
}

static int cmp_len_desc(const void *a, const void *b)
{
    size_t la = strlen(*(char *const *)a), lb = strlen(*(char *const *)b);
    return (la < lb) - (la > lb);
}

/* Removes the given directories when empty, deepest first. */
static void remove_empty_dirs(char **dirs, int n)
{
    if (n <= 0) return;
    char **copy = malloc(sizeof(char *) * n);
    if (!copy) return;
    memcpy(copy, dirs, sizeof(char *) * n);
    qsort(copy, n, sizeof(char *), cmp_len_desc);
    for (int i = 0; i < n; i++) {
        if (fs_is_dir(copy[i]) && fs_dir_empty(copy[i])) fs_rmdir(copy[i]);
    }
    free(copy);
}

/* A file (or PLUGINS.TXT line) belongs to the last package that installed
   it, so uninstalling another package never removes it. */
static void take_ownership(db_t *db, const db_package *rec)
{
    for (int i = 0; i < db->count; i++) {
        db_package *p = &db->pkgs[i];
        if (strcmp(p->id, rec->id) == 0) continue;

        for (int j = 0; j < p->n_files;) {
            if (db_list_has(rec->files, rec->n_files, p->files[j])) {
                free(p->files[j]);
                p->files[j] = p->files[--p->n_files];
                continue;
            }
            j++;
        }
        for (int j = 0; j < p->n_plugins;) {
            int dup = 0;
            for (int k = 0; k < rec->n_plugins; k++) {
                if (ptxt_same_path(rec->plugins[k].path, p->plugins[j].path) &&
                        !pm_strcasecmp(rec->plugins[k].runlevel, p->plugins[j].runlevel))
                    dup = 1;
            }
            if (dup) {
                free(p->plugins[j].runlevel);
                free(p->plugins[j].path);
                p->plugins[j] = p->plugins[--p->n_plugins];
                continue;
            }
            j++;
        }
    }
}

static int remove_plugin_lines(const char *root, const db_plugin *plugins, int n, const db_package *keep)
{
    if (n == 0) return 0;
    char file[PM_PATH_MAX];
    installer_plugins_txt(root, file, sizeof(file));
    ptxt_t pt;
    ptxt_load(&pt, file);
    for (int i = 0; i < n; i++) {
        int still_used = 0;
        for (int j = 0; keep && j < keep->n_plugins; j++) {
            if (ptxt_same_path(keep->plugins[j].path, plugins[i].path) &&
                    !pm_strcasecmp(keep->plugins[j].runlevel, plugins[i].runlevel))
                still_used = 1;
        }
        if (!still_used) ptxt_remove(&pt, plugins[i].path, plugins[i].runlevel);
    }
    int ret = pt.modified ? ptxt_save(&pt, file) : 0;
    ptxt_free(&pt);
    return ret;
}

int installer_install(install_ctx *ctx, const store_entry *e, db_t *db, char *err, int errlen)
{
    run_state rs;
    memset(&rs, 0, sizeof(rs));
    rs.ctx = ctx;
    rs.err = err;
    rs.errlen = errlen;
    err[0] = 0;
    ctx->messages[0] = 0;
    ctx->run_path[0] = 0;
    ctx->run_title[0] = 0;

    db_package *rec = calloc(1, sizeof(db_package));
    if (!rec) {
        snprintf(err, errlen, "Out of memory");
        return -1;
    }
    rs.rec = rec;
    rec->id = pm_strdup(e->id);
    rec->title = pm_strdup(e->title);
    rec->version = pm_strdup(e->version);
    rec->category = pm_strdup(store_category_name(e->category));
    rec->root = pm_strdup(ctx->root);
    rec->store = ctx->store_url ? pm_strdup(ctx->store_url) : NULL;

    const db_package *old = db_find(db, e->id);
    rs.old = old;

    installer_plugins_txt(ctx->root, rs.ptxt_file, sizeof(rs.ptxt_file));
    ptxt_load(&rs.ptxt, rs.ptxt_file);

    int ret = 0;
    const cJSON *step;
    cJSON_ArrayForEach(step, e->install) {
        if (ctx->cancelled && ctx->cancelled(ctx)) {
            snprintf(err, errlen, "Cancelled");
            ret = -1;
            break;
        }
        if (!installer_condition_ok(ctx, step)) continue;

        const char *type = get_str(step, "type");
        if (!type) continue;
        if (!strcmp(type, "download")) ret = step_download(&rs, step);
        else if (!strcmp(type, "extract")) ret = step_extract(&rs, step);
        else if (!strcmp(type, "copy")) ret = step_copy(&rs, step);
        else if (!strcmp(type, "mkdir")) ret = step_mkdir(&rs, step);
        else if (!strcmp(type, "delete")) ret = step_delete(&rs, step);
        else if (!strcmp(type, "plugin")) ret = step_plugin(&rs, step);
        else if (!strcmp(type, "message")) step_message(&rs, step);
        else if (!strcmp(type, "run")) ret = step_run(&rs, step);
        if (ret < 0) break;
    }

    if (ret == 0 && rs.ptxt.modified && ptxt_save(&rs.ptxt, rs.ptxt_file) < 0) {
        snprintf(err, errlen, "Can't update %s", rs.ptxt_file);
        ret = -1;
    }
    ptxt_free(&rs.ptxt);

    /* downloads are not needed anymore */
    for (int i = 0; i < rs.n_temp; i++) {
        fs_remove(rs.temp_files[i]);
        free(rs.temp_files[i]);
    }
    free(rs.temp_files);

    if (ret == 0) {
        if (old) {
            /* update: drop what the previous version installed and this one doesn't */
            for (int i = 0; i < old->n_files; i++) {
                if (!db_list_has(rec->files, rec->n_files, old->files[i])) {
                    char checked[PM_PATH_MAX];
                    if (installer_check_path(ctx, old->files[i], checked, sizeof(checked)) == 0)
                        fs_remove(checked);
                }
            }
            remove_plugin_lines(old->root ? old->root : ctx->root, old->plugins, old->n_plugins, rec);
            for (int i = 0; i < old->n_dirs; i++) db_list_add(&rec->dirs, &rec->n_dirs, old->dirs[i]);
            remove_empty_dirs(rec->dirs, rec->n_dirs);
        }
        take_ownership(db, rec);
        db_put(db, rec);
        return 0;
    }

    /* failure */
    ctx->run_path[0] = 0;
    if (old) {
        /* keep owning everything so a later uninstall cleans up */
        db_package *merged = rec;
        for (int i = 0; i < old->n_files; i++) db_list_add(&merged->files, &merged->n_files, old->files[i]);
        for (int i = 0; i < old->n_dirs; i++) db_list_add(&merged->dirs, &merged->n_dirs, old->dirs[i]);
        for (int i = 0; i < merged->n_plugins; i++) {
            free(merged->plugins[i].runlevel);
            free(merged->plugins[i].path);
        }
        free(merged->plugins);
        merged->plugins = NULL;
        merged->n_plugins = 0;
        for (int i = 0; i < old->n_plugins; i++) {
            db_plugin *n = realloc(merged->plugins, sizeof(db_plugin) * (merged->n_plugins + 1));
            if (!n) break;
            merged->plugins = n;
            n[merged->n_plugins].runlevel = pm_strdup(old->plugins[i].runlevel);
            n[merged->n_plugins].path = pm_strdup(old->plugins[i].path);
            merged->n_plugins++;
        }
        free(merged->version);
        merged->version = pm_strdup(old->version);
        db_put(db, merged);
    }
    else {
        for (int i = 0; i < rec->n_files; i++) fs_remove(rec->files[i]);
        remove_empty_dirs(rec->dirs, rec->n_dirs);
        db_package_free(rec);
        free(rec);
    }
    return -1;
}

int installer_uninstall(install_ctx *ctx, db_t *db, const char *id, char *err, int errlen)
{
    db_package *p = db_find(db, id);
    err[0] = 0;
    if (!p) {
        snprintf(err, errlen, "%s is not installed", id);
        return -1;
    }

    if (ctx->progress) ctx->progress(ctx, "Removing files", 0, -1);

    remove_plugin_lines(p->root ? p->root : ctx->root, p->plugins, p->n_plugins, NULL);

    for (int i = 0; i < p->n_files; i++) {
        char checked[PM_PATH_MAX];
        /* never trust the db blindly: only delete inside the allowed folders */
        if (installer_check_path(ctx, p->files[i], checked, sizeof(checked)) == 0)
            fs_remove(checked);
        if (ctx->progress) ctx->progress(ctx, "Removing files", i + 1, p->n_files);
    }

    char **dirs = NULL;
    int nd = 0;
    for (int i = 0; i < p->n_dirs; i++) {
        char checked[PM_PATH_MAX];
        if (installer_check_path(ctx, p->dirs[i], checked, sizeof(checked)) == 0)
            db_list_add(&dirs, &nd, checked);
    }
    remove_empty_dirs(dirs, nd);
    for (int i = 0; i < nd; i++) free(dirs[i]);
    free(dirs);

    db_remove(db, id);
    return 0;
}
