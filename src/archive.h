/*
    Plugin Manager for ARK-5
    archive.h: extraction of zip, rar, 7z, tar and tar.gz archives.
*/

#ifndef PM_ARCHIVE_H
#define PM_ARCHIVE_H

#include <stdint.h>

typedef struct {
    /* Called for every file entry with its sanitized relative path. Return:
         <0  abort with an error
          0  skip the entry
          1  extract to `dest` (filled by the callback) */
    int (*select)(void *ud, const char *relpath, int64_t size, char *dest, int destlen);
    /* Called after a file has been written completely. */
    void (*written)(void *ud, const char *dest);
    /* Byte progress over the whole archive; return non-zero to cancel. */
    int (*progress)(void *ud, int64_t done, int64_t total);
    void *ud;
    const char *temp_dir;       /* scratch space for .tar.gz decompression */
} archive_opts;

/* Returns the number of extracted files, or -1 on error (see `err`). */
int archive_extract(const char *path, const archive_opts *opts, char *err, int errlen);

/* Detects the archive type from its magic bytes: "zip", "rar", "7z", "gz",
   "tar" or NULL. */
const char *archive_detect(const char *path);

#endif
