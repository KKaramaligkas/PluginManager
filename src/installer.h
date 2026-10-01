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
    char db_file[256];          /* included in the transaction; empty for in-memory callers */
    char firmware[16];          /* emulated PSP system software, e.g. 6.61 */
    int model;                  /* MODEL_* */
    const char *store_url;

    /* Downloads `url` into `dest`. Returns 0 on success. */
    int (*download)(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen);
    /* Progress of the current step (stage text + bytes). */
    void (*progress)(install_ctx *ctx, const char *stage, int64_t done, int64_t total);
    /* Returns non-zero when the user asked to cancel. */
    int (*cancelled)(install_ctx *ctx);
    /* Review newline-separated replacements before applying; return 1 to proceed. */
    int (*review)(install_ctx *ctx, const char *text, int count);
    void *ud;

    /* Messages collected from "message" steps, shown after installing. */
    char messages[512];
    /* Program set by a "run" step, which the app offers to start after installing,
       and its name (the step's "title"; empty: the entry's title). */
    char run_path[256];
    char run_title[64];
};

/* Return 0 when constraints permit installation, otherwise describe the blocker. */
int installer_compatible(const install_ctx *ctx, const store_entry *e, const db_t *db, char *err, int errlen);

/* Installs (or updates) `e`. Commits files and ctx->db_file together, then
   updates the in-memory `db`. Returns 0 on success. */
int installer_install(install_ctx *ctx, const store_entry *e, db_t *db, char *err, int errlen);

/* Removes every file/folder/PLUGINS.TXT line recorded for package `id`. */
int installer_uninstall(install_ctx *ctx, db_t *db, const char *id, char *err, int errlen);

/* Location of the PLUGINS.TXT the installer edits for a device root. */
void installer_plugins_txt(const char *root, char *out, int size);

/* Expands %VARS% and checks that the result is a path we're allowed to
   write to. Returns 0 on success. Exposed for the unit tests. */
int installer_resolve_path(const install_ctx *ctx, const char *in, char *out, int size, int must_be_writable);

/* Checks a path that is already expanded, as read from an archive or from the
   database: a '%' is an ordinary character there. Returns 0 when it may be written. */
int installer_check_path(const install_ctx *ctx, const char *path, char *out, int size);

/* Returns 1 when a step's "if" condition matches this console. */
int installer_condition_ok(const install_ctx *ctx, const void *cjson_step);

/* Reads the version written in an entry's "versionFile" (its first line).
   Returns 0 on success, -1 when the file is missing or not a version. */
int installer_read_version_file(const install_ctx *ctx, const char *spec, char *out, int size);

#endif
