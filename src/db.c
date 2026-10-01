/*
    Plugin Manager for ARK-5
    db.c: database of packages installed through the Plugin Manager
    (stored as JSON next to the app).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "db.h"
#include "fs.h"
#include "pluginstxt.h"
#include "util.h"

static char *dup_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? pm_strdup(it->valuestring) : NULL;
}

static void load_list(const cJSON *obj, const char *key, char ***list, int *count)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsString(it) && it->valuestring) db_list_add(list, count, it->valuestring);
    }
}

int db_load(db_t *db, const char *file)
{
    memset(db, 0, sizeof(*db));
    char *text = fs_read_all(file, NULL, 4 * 1024 * 1024);
    if (!text) return 0;

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) return -1;

    const cJSON *pkgs = cJSON_GetObjectItemCaseSensitive(root, "packages");
    const cJSON *p;
    cJSON_ArrayForEach(p, pkgs) {
        db_package pkg;
        memset(&pkg, 0, sizeof(pkg));
        pkg.id = dup_str(p, "id");
        if (!pkg.id || !pm_valid_id(pkg.id)) {
            free(pkg.id);
            continue;
        }
        pkg.title = dup_str(p, "title");
        pkg.version = dup_str(p, "version");
        pkg.category = dup_str(p, "category");
        pkg.root = dup_str(p, "root");
        pkg.store = dup_str(p, "store");
        load_list(p, "files", &pkg.files, &pkg.n_files);
        load_list(p, "dirs", &pkg.dirs, &pkg.n_dirs);

        const cJSON *plugins = cJSON_GetObjectItemCaseSensitive(p, "plugins");
        const cJSON *pl;
        cJSON_ArrayForEach(pl, plugins) {
            char *rl = dup_str(pl, "runlevel");
            char *path = dup_str(pl, "path");
            db_plugin *n = rl && path ? realloc(pkg.plugins, sizeof(db_plugin) * (pkg.n_plugins + 1)) : NULL;
            if (!n) {
                free(rl);
                free(path);
                continue;
            }
            pkg.plugins = n;
            pkg.plugins[pkg.n_plugins].runlevel = rl;
            pkg.plugins[pkg.n_plugins].path = path;
            pkg.n_plugins++;
        }

        db_package *heap = malloc(sizeof(db_package));
        if (!heap) {
            db_package_free(&pkg);
            continue;
        }
        *heap = pkg;
        db_put(db, heap);
    }

    cJSON_Delete(root);
    return 0;
}

static void add_list(cJSON *obj, const char *key, char **list, int count)
{
    cJSON *arr = cJSON_AddArrayToObject(obj, key);
    for (int i = 0; arr && i < count; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(list[i]));
}

static void add_str(cJSON *obj, const char *key, const char *value)
{
    if (value) cJSON_AddStringToObject(obj, key, value);
}

int db_save(const db_t *db, const char *file)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON *pkgs = cJSON_AddArrayToObject(root, "packages");

    for (int i = 0; pkgs && i < db->count; i++) {
        const db_package *p = &db->pkgs[i];
        cJSON *o = cJSON_CreateObject();
        if (!o) continue;
        add_str(o, "id", p->id);
        add_str(o, "title", p->title);
        add_str(o, "version", p->version);
        add_str(o, "category", p->category);
        add_str(o, "root", p->root);
        add_str(o, "store", p->store);
        add_list(o, "files", p->files, p->n_files);
        add_list(o, "dirs", p->dirs, p->n_dirs);
        cJSON *pl = cJSON_AddArrayToObject(o, "plugins");
        for (int j = 0; pl && j < p->n_plugins; j++) {
            cJSON *x = cJSON_CreateObject();
            add_str(x, "runlevel", p->plugins[j].runlevel);
            add_str(x, "path", p->plugins[j].path);
            cJSON_AddItemToArray(pl, x);
        }
        cJSON_AddItemToArray(pkgs, o);
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return -1;

    char dir[PM_PATH_MAX];
    pm_dirname(file, dir, sizeof(dir));
    if (dir[0]) fs_mkdirs(dir, NULL, NULL);

    int ret = fs_write_all(file, text, (int)strlen(text));
    free(text);
    return ret;
}

void db_package_free(db_package *p)
{
    if (!p) return;
    free(p->id);
    free(p->title);
    free(p->version);
    free(p->category);
    free(p->root);
    free(p->store);
    for (int i = 0; i < p->n_files; i++) free(p->files[i]);
    free(p->files);
    for (int i = 0; i < p->n_dirs; i++) free(p->dirs[i]);
    free(p->dirs);
    for (int i = 0; i < p->n_plugins; i++) {
        free(p->plugins[i].runlevel);
        free(p->plugins[i].path);
    }
    free(p->plugins);
    memset(p, 0, sizeof(*p));
}

void db_free(db_t *db)
{
    for (int i = 0; i < db->count; i++) db_package_free(&db->pkgs[i]);
    free(db->pkgs);
    memset(db, 0, sizeof(*db));
}

db_package *db_find(db_t *db, const char *id)
{
    for (int i = 0; id && i < db->count; i++) {
        if (strcmp(db->pkgs[i].id, id) == 0) return &db->pkgs[i];
    }
    return NULL;
}

int db_put(db_t *db, db_package *pkg)
{
    db_package *old = db_find(db, pkg->id);
    if (old) {
        db_package_free(old);
        *old = *pkg;
        free(pkg);
        return 0;
    }
    db_package *n = realloc(db->pkgs, sizeof(db_package) * (db->count + 1));
    if (!n) {
        db_package_free(pkg);
        free(pkg);
        return -1;
    }
    db->pkgs = n;
    db->pkgs[db->count++] = *pkg;
    free(pkg);
    return 0;
}

void db_remove(db_t *db, const char *id)
{
    db_package *p = db_find(db, id);
    if (!p) return;
    int index = (int)(p - db->pkgs);
    db_package_free(p);
    memmove(&db->pkgs[index], &db->pkgs[index + 1], sizeof(db_package) * (db->count - index - 1));
    db->count--;
}

int db_list_has(char **list, int count, const char *s)
{
    for (int i = 0; i < count; i++) {
        if (pm_strcasecmp(list[i], s) == 0) return 1;
    }
    return 0;
}

int db_list_add(char ***list, int *count, const char *s)
{
    if (db_list_has(*list, *count, s)) return 0;
    char **n = realloc(*list, sizeof(char *) * (*count + 1));
    if (!n) return -1;
    *list = n;
    n[*count] = pm_strdup(s);
    if (!n[*count]) return -1;
    (*count)++;
    return 1;
}

static void index_line(char *out, size_t cap, size_t *len, const char *path, const char *title, int title_len)
{
    *len += snprintf(out + *len, cap - *len, "%s\t", path);
    for (int i = 0; i < title_len && title[i] && *len + 2 < cap; i++) {
        char c = title[i];
        out[(*len)++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    out[(*len)++] = '\n';
    out[*len] = 0;
}

int db_write_xmb_index(const db_t *db, const char *const *local_paths, int n_local, const char *file)
{
    size_t cap = 64, len = 0;
    for (int i = 0; i < db->count; i++) {
        const db_package *p = &db->pkgs[i];
        if (p->n_plugins)
            cap += strlen(p->plugins[0].path) + strlen(p->title ? p->title : p->id) + 4;
    }
    for (int i = 0; i < n_local; i++) cap += 2 * strlen(local_paths[i]) + 4;
    char *out = malloc(cap);
    if (!out) return -1;
    out[0] = 0;

    /* one line per package, named after it; the app maps the path back to the package */
    for (int i = 0; i < db->count; i++) {
        const db_package *p = &db->pkgs[i];
        const char *title = p->title ? p->title : p->id;
        if (p->n_plugins) index_line(out, cap, &len, p->plugins[0].path, title, (int)strlen(title));
    }
    /* plugins that were added by hand are named after their file */
    for (int i = 0; i < n_local; i++) {
        const char *base = pm_basename(local_paths[i]);
        const char *dot = strrchr(base, '.');
        int n = (dot && dot != base) ? (int)(dot - base) : (int)strlen(base);
        index_line(out, cap, &len, local_paths[i], base, n);
    }

    /* the memory stick is slow: only write when something changed */
    int old_len = 0;
    char *old = fs_read_all(file, &old_len, 64 * 1024);
    int same = old && old_len == (int)len && !memcmp(old, out, len);
    free(old);
    int ret = 0;
    if (!same) {
        char dir[PM_PATH_MAX];
        pm_dirname(file, dir, sizeof(dir));
        if (dir[0]) fs_mkdirs(dir, NULL, NULL);
        ret = fs_write_all(file, out, (int)len);
    }
    free(out);
    return ret;
}
