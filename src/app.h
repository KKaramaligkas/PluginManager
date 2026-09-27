/*
    Plugin Manager for ARK-5
    app.h: application state shared by the UI and the worker thread.
*/

#ifndef PM_APP_H
#define PM_APP_H

#include "db.h"
#include "gfx.h"
#include "installer.h"
#include "store.h"
#include "util.h"

typedef enum {
    ST_AVAILABLE,       /* in the store, not installed */
    ST_INSTALLED,
    ST_UPDATE,          /* installed, the store has a newer version */
    ST_LOCAL,           /* a PLUGINS.TXT plugin not installed by us */
} item_status;

typedef struct {
    const store_entry *entry;   /* NULL for local plugins and orphaned packages */
    char id[64];                /* store/package id, empty for local plugins */
    char title[96];
    char installed_version[32];
    item_status status;
    store_category category;

    /* plugin state (PLUGINS.TXT) */
    int n_lines;                /* PLUGINS.TXT lines for this item */
    int enabled;                /* at least one line is enabled */
    char plugin_path[PM_PATH_MAX];
    char plugin_file[PM_PATH_MAX];  /* PLUGINS.TXT holding the lines (local plugins) */
    char runlevels[96];

    /* icon cache */
    int icon_state;             /* 0 unknown, 1 loaded, -1 none */
    texture *icon;
    unsigned int icon_used;     /* frame counter, for the LRU */
} item_t;

typedef struct {
    char store_url[256];
    char root[16];              /* install device: "ms0:/" or "ef0:/" */
    int verify_tls;
    int auto_refresh;
} settings_t;

typedef struct {
    char app_dir[160];          /* folder of our EBOOT.PBP, with trailing '/' */
    char data_dir[176];
    char store_cache[200];
    char db_file[200];
    char settings_file[200];
    char icons_dir[200];
    char temp_dir[200];
    char ca_file[200];
    char bundled_store[200];
    char xmb_index[200];
    char xmb_on_flag[200];
    char xmb_off_flag[200];
    char launch_file[200];
    char ark_path[128];
    int model;
    int has_ms, has_ef;

    settings_t cfg;

    store_t store;
    int have_store;
    char store_origin[64];      /* "online", "cached copy", "built-in copy" */

    db_t db;

    item_t *items;
    int n_items;
} app_t;

extern app_t app;

void app_init_paths(const char *argv0);
void app_settings_load(void);
void app_settings_save(void);

/* Loads the cached (or bundled) store without network access. */
void app_load_offline_store(void);
/* Replaces the store (takes ownership of `st`). */
void app_set_store(store_t *st, const char *origin);

void app_db_load(void);
void app_db_changed(void);          /* save db + XMB index */

/* Rebuilds app.items from the store, the db and the PLUGINS.TXT files. */
void app_rebuild_items(void);
item_t *app_find_item(const char *id);
item_t *app_find_local(const char *plugin_path);

/* Enables/disables every PLUGINS.TXT line belonging to the item. */
int app_set_plugin_enabled(item_t *it, int enabled);
/* Removes a local plugin's PLUGINS.TXT lines. */
int app_remove_local_plugin(item_t *it);

void app_fill_install_ctx(install_ctx *ctx);

int app_xmb_category_enabled(void);
void app_set_xmb_category(int enabled);

/* Request left by the XMB "Plugins" category: "plugin <path>" / "entry <id>". */
int app_read_launch_request(char *kind, int kind_len, char *value, int value_len);

const char *app_model_name(int model);

#endif
