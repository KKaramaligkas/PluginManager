/*
    Plugin Manager for ARK-5
    app.c: application state: paths, settings, item list, plugin toggles and
    the files shared with XMBControl.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <kubridge.h>

#include "app.h"
#include "fs.h"
#include "pluginstxt.h"
#include "version.h"

app_t app;

const char *app_model_name(int model)
{
    switch (model) {
    case MODEL_1000: return "PSP-1000";
    case MODEL_2000: return "PSP-2000";
    case MODEL_3000: return "PSP-3000";
    case MODEL_GO: return "PSP Go";
    case MODEL_STREET: return "PSP Street";
    case MODEL_VITA: return "PS Vita";
    default: return "PSP";
    }
}

static int detect_model(void)
{
    int m = kuKernelGetModel();
    switch (m) {
    case 0: return MODEL_1000;
    case 1: return MODEL_2000;
    case 4: return MODEL_GO;
    case 10: return MODEL_STREET;
    default: return m < 0 ? MODEL_UNKNOWN : MODEL_3000;
    }
}

static void set_path(char *out, size_t size, const char *dir, const char *name)
{
    snprintf(out, size, "%s%s", dir, name);
}

void app_init_paths(const char *argv0)
{
    memset(&app, 0, sizeof(app));

    if (argv0 && (pm_starts_with(argv0, "ms0:/") || pm_starts_with(argv0, "ef0:/")) && strrchr(argv0, '/'))
        pm_dirname(argv0, app.app_dir, sizeof(app.app_dir));
    else
        pm_strlcpy(app.app_dir, "ms0:/PSP/APPS/PluginManager/", sizeof(app.app_dir));

    set_path(app.data_dir, sizeof(app.data_dir), app.app_dir, "data/");
    set_path(app.store_cache, sizeof(app.store_cache), app.data_dir, "store.json");
    set_path(app.db_file, sizeof(app.db_file), app.data_dir, "installed.json");
    set_path(app.settings_file, sizeof(app.settings_file), app.data_dir, "settings.json");
    set_path(app.icons_dir, sizeof(app.icons_dir), app.data_dir, "icons/");
    set_path(app.temp_dir, sizeof(app.temp_dir), app.data_dir, "tmp/");
    set_path(app.xmb_index, sizeof(app.xmb_index), app.data_dir, "xmbnames.txt");
    set_path(app.xmb_on_flag, sizeof(app.xmb_on_flag), app.data_dir, "xmbcat");
    set_path(app.xmb_off_flag, sizeof(app.xmb_off_flag), app.data_dir, "noxmbcat");
    set_path(app.launch_file, sizeof(app.launch_file), app.data_dir, "launch.txt");
    set_path(app.ca_file, sizeof(app.ca_file), app.app_dir, "cacert.pem");
    set_path(app.bundled_store, sizeof(app.bundled_store), app.app_dir, "store.json");

    fs_mkdirs(app.data_dir, NULL, NULL);
    fs_mkdirs(app.icons_dir, NULL, NULL);

    /* the XMB "Plugins" category is off until turned on in the settings,
       also for ARK 5.1.2's XMBControl (see app_set_xmb_category) */
    if (!app_xmb_category_enabled() && !fs_exists(app.xmb_off_flag))
        fs_write_all(app.xmb_off_flag, "1", 1);

    app.has_ms = fs_is_dir("ms0:/");
    app.has_ef = fs_is_dir("ef0:/");
    app.model = detect_model();

    if (fs_is_dir("ef0:/PSP/SAVEDATA/ARK_01234/") && pm_starts_with(app.app_dir, "ef0:"))
        pm_strlcpy(app.ark_path, "ef0:/PSP/SAVEDATA/ARK_01234/", sizeof(app.ark_path));
    else
        pm_strlcpy(app.ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/", sizeof(app.ark_path));

    /* leftovers of an interrupted install */
    fs_mkdirs(app.temp_dir, NULL, NULL);
}

void app_settings_load(void)
{
    settings_t *c = &app.cfg;
    pm_strlcpy(c->store_url, PM_DEFAULT_STORE, sizeof(c->store_url));
    snprintf(c->root, sizeof(c->root), "%.4s/", app.app_dir);
    c->verify_tls = 1;
    c->auto_refresh = 1;

    char *text = fs_read_all(app.settings_file, NULL, 64 * 1024);
    if (!text) return;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) return;

    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, "store");
    if (cJSON_IsString(it) && pm_starts_with(it->valuestring, "https://"))
        pm_strlcpy(c->store_url, it->valuestring, sizeof(c->store_url));
    it = cJSON_GetObjectItemCaseSensitive(root, "root");
    if (cJSON_IsString(it) && (!strcmp(it->valuestring, "ms0:/") || !strcmp(it->valuestring, "ef0:/")))
        pm_strlcpy(c->root, it->valuestring, sizeof(c->root));
    it = cJSON_GetObjectItemCaseSensitive(root, "verifyTls");
    if (cJSON_IsBool(it)) c->verify_tls = cJSON_IsTrue(it);
    it = cJSON_GetObjectItemCaseSensitive(root, "autoRefresh");
    if (cJSON_IsBool(it)) c->auto_refresh = cJSON_IsTrue(it);
    cJSON_Delete(root);

    /* the install device must exist */
    if (!fs_is_dir(c->root)) snprintf(c->root, sizeof(c->root), "%.4s/", app.app_dir);
}

void app_settings_save(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "store", app.cfg.store_url);
    cJSON_AddStringToObject(root, "root", app.cfg.root);
    cJSON_AddBoolToObject(root, "verifyTls", app.cfg.verify_tls);
    cJSON_AddBoolToObject(root, "autoRefresh", app.cfg.auto_refresh);
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text) {
        fs_write_all(app.settings_file, text, (int)strlen(text));
        free(text);
    }
}

static int try_store_file(const char *file, const char *origin)
{
    char err[128];
    char *json = fs_read_all(file, NULL, STORE_MAX_SIZE);
    if (!json) return -1;
    store_t st;
    int r = store_parse(&st, json, app.cfg.store_url, err, sizeof(err));
    free(json);
    if (r < 0) return -1;
    app_set_store(&st, origin);
    return 0;
}

void app_load_offline_store(void)
{
    if (try_store_file(app.store_cache, "cached copy") == 0) return;
    if (try_store_file(app.bundled_store, "built-in copy") == 0) return;
    app_rebuild_items();
}

void app_set_store(store_t *st, const char *origin)
{
    if (app.have_store) store_free(&app.store);
    app.store = *st;
    memset(st, 0, sizeof(*st));
    app.have_store = 1;
    pm_strlcpy(app.store_origin, origin, sizeof(app.store_origin));
    app_rebuild_items();
}

void app_db_load(void)
{
    if (db_load(&app.db, app.db_file) < 0) {
        /* corrupted database: keep a copy for recovery and start over */
        char bak[256];
        snprintf(bak, sizeof(bak), "%s.bad", app.db_file);
        fs_remove(bak);
        fs_rename(app.db_file, bak);
        db_free(&app.db);
    }
}

void app_db_changed(void)
{
    db_save(&app.db, app.db_file);
}

/* ------------------------------------------------------------------------ */

static void free_items(void)
{
    for (int i = 0; i < app.n_items; i++) tex_free(app.items[i].icon);
    free(app.items);
    app.items = NULL;
    app.n_items = 0;
}

static item_t *new_item(void)
{
    item_t *n = realloc(app.items, sizeof(item_t) * (app.n_items + 1));
    if (!n) return NULL;
    app.items = n;
    item_t *it = &app.items[app.n_items++];
    memset(it, 0, sizeof(*it));
    return it;
}

static void add_runlevel(item_t *it, const char *rl)
{
    if (strstr(it->runlevels, rl)) return;
    if (it->runlevels[0]) pm_strlcat(it->runlevels, ", ", sizeof(it->runlevels));
    pm_strlcat(it->runlevels, rl, sizeof(it->runlevels));
}

/* PLUGINS.TXT files ARK-5 reads */
static const char *ptxt_files[3];
static ptxt_t ptxts[3];

static void load_ptxts(void)
{
    static char f0[64], f1[64], f2[200];
    installer_plugins_txt("ms0:/", f0, sizeof(f0));
    installer_plugins_txt("ef0:/", f1, sizeof(f1));
    snprintf(f2, sizeof(f2), "%sPLUGINS.TXT", app.ark_path);
    ptxt_files[0] = f0;
    ptxt_files[1] = f1;
    ptxt_files[2] = f2;
    for (int i = 0; i < 3; i++) ptxt_load(&ptxts[i], ptxt_files[i]);
}

static void free_ptxts(void)
{
    for (int i = 0; i < 3; i++) ptxt_free(&ptxts[i]);
}

static int owned_by_db(const char *path)
{
    for (int i = 0; i < app.db.count; i++) {
        const db_package *p = &app.db.pkgs[i];
        for (int j = 0; j < p->n_plugins; j++)
            if (ptxt_same_path(p->plugins[j].path, path)) return 1;
    }
    return 0;
}

static void fill_package_state(item_t *it, const db_package *p)
{
    pm_strlcpy(it->installed_version, p->version ? p->version : "?", sizeof(it->installed_version));
    for (int j = 0; j < p->n_plugins; j++) {
        for (int k = 0; k < 3; k++) {
            int idx = ptxt_find(&ptxts[k], p->plugins[j].path, p->plugins[j].runlevel);
            if (idx < 0) continue;
            it->n_lines++;
            if (ptxts[k].lines[idx].enabled) it->enabled = 1;
            add_runlevel(it, p->plugins[j].runlevel);
            if (!it->plugin_path[0]) pm_strlcpy(it->plugin_path, p->plugins[j].path, sizeof(it->plugin_path));
            break;
        }
    }
}

/* keeps the list of the XMB "Plugins" category (XMBControl) in sync */
static void write_xmb_index(void)
{
    const char **local = malloc(sizeof(char *) * (app.n_items + 1));
    int n = 0;
    for (int i = 0; local && i < app.n_items; i++)
        if (app.items[i].status == ST_LOCAL) local[n++] = app.items[i].plugin_path;
    db_write_xmb_index(&app.db, local, n, app.xmb_index);
    free(local);
}

void app_rebuild_items(void)
{
    free_items();
    load_ptxts();

    /* store entries */
    for (int i = 0; app.have_store && i < app.store.count; i++) {
        const store_entry *e = &app.store.entries[i];
        item_t *it = new_item();
        if (!it) break;
        it->entry = e;
        pm_strlcpy(it->id, e->id, sizeof(it->id));
        pm_strlcpy(it->title, e->title, sizeof(it->title));
        it->category = e->category;
        it->status = ST_AVAILABLE;

        const db_package *p = db_find(&app.db, e->id);
        if (e->version_file) {
            /* installed by other means (ARK itself): the file tells the version */
            install_ctx ctx;
            app_fill_install_ctx(&ctx);
            if (installer_read_version_file(&ctx, e->version_file, it->installed_version,
                                            sizeof(it->installed_version)) == 0)
                it->status = ST_INSTALLED;
        }
        else if (p) {
            fill_package_state(it, p);
            it->status = ST_INSTALLED;
        }
        else if (!strcmp(e->id, "pluginmanager")) {
            pm_strlcpy(it->installed_version, PM_VERSION, sizeof(it->installed_version));
            it->status = ST_INSTALLED;
        }
        if (it->status == ST_INSTALLED && pm_version_compare(e->version, it->installed_version) > 0)
            it->status = ST_UPDATE;
    }

    /* installed packages that are not in the store (anymore) */
    for (int i = 0; i < app.db.count; i++) {
        const db_package *p = &app.db.pkgs[i];
        if (app.have_store && store_find(&app.store, p->id)) continue;
        item_t *it = new_item();
        if (!it) break;
        pm_strlcpy(it->id, p->id, sizeof(it->id));
        pm_strlcpy(it->title, p->title ? p->title : p->id, sizeof(it->title));
        it->category = store_category_from_name(p->category);
        it->status = ST_INSTALLED;
        fill_package_state(it, p);
    }

    /* plugins added by hand (or by other tools) */
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < ptxts[k].count; j++) {
            const ptxt_line *l = &ptxts[k].lines[j];
            if (!l->runlevel || owned_by_db(l->path)) continue;
            item_t *it = app_find_local(l->path);
            if (!it) {
                it = new_item();
                if (!it) break;
                it->status = ST_LOCAL;
                it->category = CAT_PLUGIN;
                pm_strlcpy(it->plugin_path, l->path, sizeof(it->plugin_path));
                pm_strlcpy(it->plugin_file, ptxt_files[k], sizeof(it->plugin_file));
                const char *base = pm_basename(l->path);
                pm_strlcpy(it->title, base, sizeof(it->title));
                char *dot = strrchr(it->title, '.');
                if (dot && dot != it->title) *dot = 0;
            }
            it->n_lines++;
            if (l->enabled) it->enabled = 1;
            add_runlevel(it, l->runlevel);
        }
    }

    free_ptxts();
    write_xmb_index();
}

item_t *app_find_item(const char *id)
{
    for (int i = 0; id && i < app.n_items; i++)
        if (app.items[i].id[0] && !strcmp(app.items[i].id, id)) return &app.items[i];
    return NULL;
}

item_t *app_find_local(const char *plugin_path)
{
    for (int i = 0; i < app.n_items; i++) {
        item_t *it = &app.items[i];
        if (it->status == ST_LOCAL && ptxt_same_path(it->plugin_path, plugin_path)) return it;
    }
    return NULL;
}

static int toggle_in_file(const char *file, const char *path, int enabled)
{
    ptxt_t pt;
    ptxt_load(&pt, file);
    int changed = ptxt_set_enabled(&pt, path, enabled);
    int r = changed ? ptxt_save(&pt, file) : 0;
    ptxt_free(&pt);
    return r < 0 ? -1 : changed;
}

int app_set_plugin_enabled(item_t *it, int enabled)
{
    int changed = 0;
    if (it->status == ST_LOCAL) {
        changed = toggle_in_file(it->plugin_file, it->plugin_path, enabled);
    }
    else {
        const db_package *p = db_find(&app.db, it->id);
        if (!p) return -1;
        char file[64];
        installer_plugins_txt(p->root ? p->root : app.cfg.root, file, sizeof(file));
        for (int j = 0; j < p->n_plugins; j++) {
            int r = toggle_in_file(file, p->plugins[j].path, enabled);
            if (r < 0) return -1;
            changed += r;
        }
    }
    if (changed >= 0) it->enabled = enabled;
    return changed;
}

int app_remove_local_plugin(item_t *it)
{
    if (it->status != ST_LOCAL) return -1;
    ptxt_t pt;
    ptxt_load(&pt, it->plugin_file);
    ptxt_remove(&pt, it->plugin_path, NULL);
    int r = ptxt_save(&pt, it->plugin_file);
    ptxt_free(&pt);
    return r;
}

void app_fill_install_ctx(install_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    pm_strlcpy(ctx->root, app.cfg.root, sizeof(ctx->root));
    pm_strlcpy(ctx->ark_path, app.ark_path, sizeof(ctx->ark_path));
    pm_strlcpy(ctx->temp_dir, app.temp_dir, sizeof(ctx->temp_dir));
    pm_strlcpy(ctx->protect_dir, app.data_dir, sizeof(ctx->protect_dir));
    ctx->model = app.model;
    ctx->store_url = app.cfg.store_url;
}

int app_xmb_category_enabled(void)
{
    return fs_exists(app.xmb_on_flag);
}

/* XMBControl shows the "Plugins" category only when data/xmbcat exists. The
   one in ARK 5.1.2 showed it unless data/noxmbcat existed, so both files are
   kept in step. */
void app_set_xmb_category(int enabled)
{
    if (enabled) {
        fs_write_all(app.xmb_on_flag, "1", 1);
        fs_remove(app.xmb_off_flag);
    }
    else {
        fs_remove(app.xmb_on_flag);
        fs_write_all(app.xmb_off_flag, "1", 1);
    }
}

int app_read_launch_request(char *kind, int kind_len, char *value, int value_len)
{
    char *text = fs_read_all(app.launch_file, NULL, 1024);
    if (!text) return 0;
    fs_remove(app.launch_file);

    char *line = pm_trim(text);
    char *sep = strpbrk(line, "\t ");
    int ok = 0;
    if (sep) {
        *sep = 0;
        char *v = pm_trim(sep + 1);
        char *eol = strpbrk(v, "\r\n");
        if (eol) *eol = 0;
        pm_strlcpy(kind, line, kind_len);
        pm_strlcpy(value, v, value_len);
        ok = kind[0] && value[0];
    }
    free(text);
    return ok;
}
