/*
    Plugin Manager for ARK-5
    store.h: store (catalog) parsing. See PluginManager/README.md for the format.
*/

#ifndef PM_STORE_H
#define PM_STORE_H

#include <stdint.h>

#include <cjson/cJSON.h>

#define STORE_FORMAT_VERSION 1
#define STORE_MAX_ENTRIES    512
#define STORE_MAX_SIZE       (2 * 1024 * 1024)

typedef enum {
    CAT_PLUGIN,
    CAT_HOMEBREW,
    CAT_EMULATOR,
    CAT_GAME,
    CAT_UTILITY,
    CAT_THEME,
    CAT_OTHER,
    CAT_COUNT
} store_category;

typedef struct {
    const char *id;
    const char *title;
    const char *author;
    const char *version;
    const char *description;
    const char *license;
    const char *website;
    const char *icon;           /* absolute URL (resolved against iconBase) or NULL */
    const char *updated;
    const char *runlevel;       /* informational, for plugins */
    const char *notes;          /* shown before installing */
    const char *version_file;   /* installed version is read from this file (ARK itself), or NULL */
    const cJSON *compatibility; /* models and firmware allowlists */
    const cJSON *requires;      /* installed package IDs */
    const cJSON *conflicts;     /* mutually exclusive package IDs */
    int runs;                   /* has a "run" step: installing ends by starting a program */
    store_category category;
    int64_t size;
    const cJSON *install;       /* array of steps */
} store_entry;

typedef struct {
    const char *title;
    const char *author;
    const char *description;
    const char *url;
    const char *icon_base;
    int revision;

    store_entry *entries;
    int count;

    cJSON *root;                /* owns most strings above */
    char **owned;               /* strings allocated while resolving (icon URLs) */
    int n_owned;
} store_t;

/* Parses and validates a store document. `source_url` (may be NULL) is used
   to resolve relative icon paths when storeInfo.iconBase is missing.
   Returns 0 on success; on failure `err` describes the problem. */
int store_parse(store_t *st, const char *json, const char *source_url, char *err, int errlen);
void store_free(store_t *st);

const store_entry *store_find(const store_t *st, const char *id);

const char *store_category_name(store_category c);
store_category store_category_from_name(const char *name);

#endif

