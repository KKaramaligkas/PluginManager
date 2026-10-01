/*
    Plugin Manager for ARK-5
    fs.h: file system layer. On the PSP it maps to sceIo*, on the host (unit
    tests) PSP style paths like "ms0:/SEPLUGINS/a.prx" are mapped below the
    directory given by the PM_FS_ROOT environment variable.
*/

#ifndef PM_FS_H
#define PM_FS_H

#include <stdint.h>

typedef int fs_file;

enum {
    FS_READ,
    FS_WRITE,       /* create + truncate */
    FS_APPEND,      /* create + append */
};

fs_file fs_open(const char *path, int mode);
int fs_read(fs_file f, void *buf, int size);
int fs_write(fs_file f, const void *buf, int size);
int64_t fs_seek(fs_file f, int64_t offset, int whence);   /* whence: 0 set, 1 cur, 2 end */
void fs_close(fs_file f);

int fs_exists(const char *path);        /* file or directory */
int fs_is_dir(const char *path);
int64_t fs_size(const char *path);      /* -1 when missing */
int64_t fs_free_bytes(const char *path); /* -1 when the device cannot report it */
int fs_mkdir(const char *path);         /* single level, 0 or already exists */
int fs_remove(const char *path);
int fs_rmdir(const char *path);
int fs_rename(const char *from, const char *to);
int fs_sync(const char *path); /* flush file/directory changes before journalling */
#ifdef PM_FS_TESTING
/* One-shot failure after n successful writes; host tests only. */
void fs_test_fail_after_writes(int n);
void fs_test_free_bytes(int64_t ms0, int64_t ef0); /* -2 restores real values */
#endif

/* Creates every missing directory of `path` (a directory path, trailing '/'
   optional). When `created` is not NULL, every directory that did not exist
   before is reported through it (outermost first). */
int fs_mkdirs(const char *path, void (*created)(void *ud, const char *dir), void *ud);

/* Calls cb for every entry (excluding "." and ".."). Stops when cb returns
   non-zero. Returns 0, or -1 when the directory can't be opened. */
int fs_list(const char *path, int (*cb)(void *ud, const char *name, int is_dir), void *ud);

/* Returns 1 when the directory is empty (or missing). */
int fs_dir_empty(const char *path);

/* Reads a whole file into a malloc'ed, NUL terminated buffer. */
char *fs_read_all(const char *path, int *size_out, int max_size);

/* Writes a whole file through a temporary file + rename. */
int fs_write_all(const char *path, const void *data, int size);

/* Copies a file. Returns 0 on success. */
int fs_copy(const char *from, const char *to);

/* Path usable by libc's fopen() (identity on the PSP). */
const char *fs_native_path(const char *path, char *buf, int size);

#endif
