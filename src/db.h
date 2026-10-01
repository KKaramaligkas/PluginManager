/*
    Plugin Manager for ARK-5
    db.h: database of packages installed through the Plugin Manager.
*/

#ifndef PM_DB_H
#define PM_DB_H

typedef struct {
    char *runlevel;
    char *path;
} db_plugin;

typedef struct {
    char *id;
    char *title;
    char *version;
    char *category;
    char *root;             /* install device root, "ms0:/" or "ef0:/" */
    char *store;            /* store URL the package came from */
    char **files;           /* every file written by the installer */
    int n_files;
    char **dirs;            /* directories created by the installer */
    int n_dirs;
    db_plugin *plugins;     /* PLUGINS.TXT lines added by the installer */
    int n_plugins;
} db_package;

typedef struct {
    db_package *pkgs;
    int count;
} db_t;

int db_load(db_t *db, const char *file);        /* missing file = empty db */
int db_save(const db_t *db, const char *file);
void db_free(db_t *db);

db_package *db_find(db_t *db, const char *id);
/* Replaces (or adds) a package record; takes ownership of `pkg`'s memory. */
int db_put(db_t *db, db_package *pkg); /* returns -1 on allocation failure */
void db_remove(db_t *db, const char *id);

void db_package_free(db_package *p);

/* Helpers used while installing */
int db_list_add(char ***list, int *count, const char *s);   /* ignores duplicates */
int db_list_has(char **list, int count, const char *s);

/* Writes the "path<TAB>title" index XMBControl reads to fill the XMB
   "Plugins" category: one line per installed package that has plugins (its
   first plugin), then the plugins in local_paths (added by hand). */
int db_write_xmb_index(const db_t *db, const char *const *local_paths, int n_local, const char *file);

#endif
