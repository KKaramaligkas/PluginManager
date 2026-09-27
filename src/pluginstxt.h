/*
    Plugin Manager for ARK-5
    pluginstxt.h: reading and editing ARK's PLUGINS.TXT
    ("runlevel, path, on|off" lines; everything else is preserved verbatim).
*/

#ifndef PM_PLUGINSTXT_H
#define PM_PLUGINSTXT_H

typedef struct {
    char *raw;          /* original text of the line (without newline) */
    char *runlevel;     /* NULL for comments/unknown lines */
    char *path;
    int enabled;
    int dirty;          /* line must be regenerated when saving */
} ptxt_line;

typedef struct {
    ptxt_line *lines;
    int count;
    int modified;
} ptxt_t;

int ptxt_load(ptxt_t *pt, const char *file);        /* missing file = empty list */
int ptxt_save(ptxt_t *pt, const char *file);
void ptxt_free(ptxt_t *pt);

/* Paths are compared case-insensitively and "ms0:PATH" == "ms0:/PATH". */
int ptxt_same_path(const char *a, const char *b);

/* Index of the first plugin line with that path (and runlevel when not NULL). */
int ptxt_find(const ptxt_t *pt, const char *path, const char *runlevel);

/* Adds "runlevel, path, on/off". If an identical path+runlevel line already
   exists it is kept as is (the user's enabled/disabled choice survives
   updates). `first` inserts the line before every other plugin line. */
int ptxt_add(ptxt_t *pt, const char *runlevel, const char *path, int enabled, int first);

/* Changes the state of every line using that path. Returns lines changed. */
int ptxt_set_enabled(ptxt_t *pt, const char *path, int enabled);

/* Removes every line using that path (and runlevel when not NULL). */
int ptxt_remove(ptxt_t *pt, const char *path, const char *runlevel);

/* Returns 1 when any line with that path is enabled. */
int ptxt_is_enabled(const ptxt_t *pt, const char *path);

#endif
