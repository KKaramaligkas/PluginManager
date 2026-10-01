/* Portable HTTP download state. Validated ranges never mix file versions. */
#ifndef PM_RESUME_H
#define PM_RESUME_H
#include <stddef.h>
#include <stdint.h>
#include "fs.h"

typedef struct {
    fs_file file;
    const char *url;
    char destination[256], partial[256], metadata[256];
    char validator[160], etag[160], modified[160];
    int64_t offset, total, range_start, range_end;
    int status, ready, retry_fresh, encoded, space_error;
} resume_state;

int resume_open(resume_state *state, const char *url, const char *destination);
size_t resume_header(char *data, size_t size, size_t count, void *state);
size_t resume_write(resume_state *state, const void *data, size_t size);
/* On failure, preserve only partial files with a usable server validator. */
int resume_close(resume_state *state, int success);
void resume_discard(resume_state *state);
#endif
