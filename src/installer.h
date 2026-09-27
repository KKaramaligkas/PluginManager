/*
    Plugin Manager for ARK-5
    installer.h: runs store install scripts and removes installed packages.
*/

#ifndef PM_INSTALLER_H
#define PM_INSTALLER_H

#include <stdint.h>

#include "db.h"
#include "store.h"

enum {
    MODEL_1000,
    MODEL_2000,
    MODEL_3000,
    MODEL_GO,
    MODEL_STREET,
    MODEL_VITA,
    MODEL_UNKNOWN,
};

typedef struct install_ctx install_ctx;

struct install_ctx {
    char root[16];              /* "ms0:/" or "ef0:/" */
    char ark_path[128];         /* e.g. "ms0:/PSP/SAVEDATA/ARK_01234/" */
    char temp_dir[160];         /* scratch folder for downloads */
    char protect_dir[160];      /* packages may never write here (our own data) */
    int model;                  /* MODEL_* */
    const char *store_url;

    /* Downloads `url` into `dest`. Returns 0 on success. */
    int (*download)(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen);
    /* Progress of the current step (stage text + bytes). */
    void (*progress)(install_ctx *ctx, const char *stage, int64_t done, int64_t total);
    /* Returns non-zero when the user asked to cancel. */
    int (*cancelled)(install_ctx *ctx);
    void *ud;

    /* Messages collected from "message" steps, shown after installing. */
    char messages[512];
};

/* Installs (or updates) `e`. The package record is stored into `db`
   (the caller saves the db). Returns 0 on success. */
int installer_install(install_ctx *ctx, const store_entry *e, db_t *db, char *err, int errlen);

/* Removes every file/folder/PLUGINS.TXT line recorded for package `id`. */
int installer_uninstall(install_ctx *ctx, db_t *db, const char *id, char *err, int errlen);

/* Location of the PLUGINS.TXT the installer edits for a device root. */
void installer_plugins_txt(const char *root, char *out, int size);

/* Expands %VARS% and checks that the result is a path we're allowed to
   write to. Returns 0 on success. Exposed for the unit tests. */
int installer_resolve_path(const install_ctx *ctx, const char *in, char *out, int size, int must_be_writable);

/* Returns 1 when a step's "if" condition matches this console. */
int installer_condition_ok(const install_ctx *ctx, const void *cjson_step);

#endif
