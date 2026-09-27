/*
    Plugin Manager for ARK-5
    store.c: store (catalog) parsing and validation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "util.h"

static const char *category_names[CAT_COUNT] = {
    "plugin", "homebrew", "emulator", "game", "utility", "theme", "other",
};

const char *store_category_name(store_category c)
{
    return (c >= 0 && c < CAT_COUNT) ? category_names[c] : "other";
}

store_category store_category_from_name(const char *name)
{
    if (!name) return CAT_OTHER;
    for (int i = 0; i < CAT_COUNT; i++) {
        if (pm_strcasecmp(name, category_names[i]) == 0) return (store_category)i;
    }
    if (pm_strcasecmp(name, "plugins") == 0) return CAT_PLUGIN;
    if (pm_strcasecmp(name, "app") == 0 || pm_strcasecmp(name, "apps") == 0) return CAT_HOMEBREW;
    if (pm_strcasecmp(name, "emulators") == 0) return CAT_EMULATOR;
    if (pm_strcasecmp(name, "games") == 0) return CAT_GAME;
    if (pm_strcasecmp(name, "utilities") == 0 || pm_strcasecmp(name, "tool") == 0) return CAT_UTILITY;
    if (pm_strcasecmp(name, "themes") == 0) return CAT_THEME;
    return CAT_OTHER;
}

static const char *get_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0]) return it->valuestring;
    return NULL;
}

static int is_http_url(const char *s)
{
    return s && (pm_starts_with(s, "https://") || pm_starts_with(s, "http://"));
}

static char *own(store_t *st, char *s)
{
    if (!s) return NULL;
    char **n = realloc(st->owned, sizeof(char *) * (st->n_owned + 1));
    if (!n) {
        free(s);
        return NULL;
    }
    st->owned = n;
    st->owned[st->n_owned++] = s;
    return s;
}

/* Resolves an icon reference to an absolute URL. */
static const char *resolve_icon(store_t *st, const char *icon, const char *source_url)
{
    if (!icon) return NULL;
    if (is_http_url(icon)) return icon;

    char rel[PM_PATH_MAX];
    if (pm_sanitize_relpath(icon, rel, sizeof(rel)) < 0) return NULL;

    if (st->icon_base) {
        size_t bl = strlen(st->icon_base);
        const char *sep = (bl && st->icon_base[bl - 1] == '/') ? "" : "/";
        return own(st, pm_sprintf("%s%s%s", st->icon_base, sep, rel));
    }
    if (is_http_url(source_url)) {
        const char *q = strchr(source_url, '?');
        size_t len = q ? (size_t)(q - source_url) : strlen(source_url);
        char *base = pm_strndup(source_url, len);
        if (!base) return NULL;
        char *slash = strrchr(base, '/');
        if (slash) slash[1] = 0;
        char *url = pm_sprintf("%s%s", base, rel);
        free(base);
        return own(st, url);
    }
    return NULL;
}

static int validate_steps(const cJSON *install, char *why, int whylen)
{
    static const char *types[] = {
        "download", "extract", "copy", "mkdir", "delete", "plugin", "message",
    };

    if (!cJSON_IsArray(install) || cJSON_GetArraySize(install) == 0) {
        snprintf(why, whylen, "missing install steps");
        return -1;
    }
    const cJSON *step;
    cJSON_ArrayForEach(step, install) {
        const char *type = cJSON_IsObject(step) ? get_str(step, "type") : NULL;
        int known = 0;
        for (size_t i = 0; type && i < NELEMS(types); i++)
            if (strcmp(type, types[i]) == 0) known = 1;
        if (!known) {
            snprintf(why, whylen, "unknown step type '%s'", type ? type : "?");
            return -1;
        }
    }
    return 0;
}

int store_parse(store_t *st, const char *json, const char *source_url, char *err, int errlen)
{
    memset(st, 0, sizeof(*st));

    st->root = cJSON_Parse(json);
    if (!st->root || !cJSON_IsObject(st->root)) {
        snprintf(err, errlen, "The store is not valid JSON");
        store_free(st);
        return -1;
    }

    const cJSON *info = cJSON_GetObjectItemCaseSensitive(st->root, "storeInfo");
    if (cJSON_IsObject(info)) {
        const cJSON *ver = cJSON_GetObjectItemCaseSensitive(info, "version");
        if (cJSON_IsNumber(ver) && ver->valueint > STORE_FORMAT_VERSION) {
            snprintf(err, errlen, "This store needs a newer Plugin Manager (format %d)", ver->valueint);
            store_free(st);
            return -1;
        }
        const cJSON *rev = cJSON_GetObjectItemCaseSensitive(info, "revision");
        st->title = get_str(info, "title");
        st->author = get_str(info, "author");
        st->description = get_str(info, "description");
        st->url = get_str(info, "url");
        st->icon_base = get_str(info, "iconBase");
        st->revision = cJSON_IsNumber(rev) ? rev->valueint : 0;
        if (st->url && !is_http_url(st->url)) st->url = NULL;
        if (st->icon_base && !is_http_url(st->icon_base)) st->icon_base = NULL;
    }
    if (!st->title) st->title = "Untitled store";

    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(st->root, "entries");
    if (!cJSON_IsArray(entries)) {
        snprintf(err, errlen, "The store has no \"entries\" list");
        store_free(st);
        return -1;
    }

    int n = cJSON_GetArraySize(entries);
    if (n > STORE_MAX_ENTRIES) n = STORE_MAX_ENTRIES;
    st->entries = calloc(n ? n : 1, sizeof(store_entry));
    if (!st->entries) {
        snprintf(err, errlen, "Out of memory");
        store_free(st);
        return -1;
    }

    const cJSON *e;
    int i = 0;
    cJSON_ArrayForEach(e, entries) {
        if (i++ >= n) break;
        if (!cJSON_IsObject(e)) continue;

        store_entry *se = &st->entries[st->count];
        char why[96];

        se->id = get_str(e, "id");
        se->title = get_str(e, "title");
        se->install = cJSON_GetObjectItemCaseSensitive(e, "install");
        if (!pm_valid_id(se->id) || !se->title || store_find(st, se->id) ||
                validate_steps(se->install, why, sizeof(why)) < 0) {
            memset(se, 0, sizeof(*se));
            continue;   /* skip invalid entries instead of rejecting the store */
        }

        se->author = get_str(e, "author");
        se->version = get_str(e, "version");
        se->description = get_str(e, "description");
        se->license = get_str(e, "license");
        se->website = get_str(e, "website");
        se->updated = get_str(e, "updated");
        se->runlevel = get_str(e, "runlevel");
        se->notes = get_str(e, "notes");
        se->category = store_category_from_name(get_str(e, "category"));
        se->icon = resolve_icon(st, get_str(e, "icon"), source_url);

        const cJSON *size = cJSON_GetObjectItemCaseSensitive(e, "size");
        se->size = cJSON_IsNumber(size) ? (int64_t)size->valuedouble : -1;

        if (!se->version) se->version = "?";
        if (!se->author) se->author = "Unknown";
        if (!se->description) se->description = "";

        st->count++;
    }

    if (st->count == 0) {
        snprintf(err, errlen, "The store doesn't contain any valid entry");
        store_free(st);
        return -1;
    }
    return 0;
}

void store_free(store_t *st)
{
    if (!st) return;
    for (int i = 0; i < st->n_owned; i++) free(st->owned[i]);
    free(st->owned);
    free(st->entries);
    if (st->root) cJSON_Delete(st->root);
    memset(st, 0, sizeof(*st));
}

const store_entry *store_find(const store_t *st, const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < st->count; i++) {
        if (st->entries[i].id && strcmp(st->entries[i].id, id) == 0) return &st->entries[i];
    }
    return NULL;
}
