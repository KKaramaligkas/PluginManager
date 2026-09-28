/*
    Plugin Manager for ARK-5
    worker.h: background thread for network and install jobs, so the UI keeps
    running (progress bars, cancel button) while they execute.
*/

#ifndef PM_WORKER_H
#define PM_WORKER_H

#include <stdint.h>

#include "store.h"

typedef enum {
    JOB_NONE,
    JOB_REFRESH,        /* download + parse the store */
    JOB_ICONS,          /* download missing icons */
    JOB_INSTALL,
    JOB_UNINSTALL,
} job_type;

typedef struct {
    job_type type;
    volatile int running;
    volatile int finished;      /* set by the worker, cleared by worker_collect() */
    volatile int result;        /* 0 ok, -1 error */
    volatile int cancel;

    char id[64];
    char title[96];
    char stage[128];
    volatile int64_t cur, total;
    unsigned int started;       /* sceKernelGetSystemTimeLow at start */

    char error[256];
    char messages[512];
    char run_path[256];         /* JOB_INSTALL: program to offer to start ("run" step) */
    char run_title[64];

    store_t *new_store;         /* JOB_REFRESH result */
    volatile int icons_done;    /* JOB_ICONS progress, bumps on every icon */
} job_t;

extern job_t job;

int worker_start(void);
void worker_stop(void);

int worker_busy(void);
/* Starts a job; fails when another one is running. */
int worker_submit(job_type type, const char *id, const char *title);
/* Returns 1 (and clears the flag) when a job just finished. */
int worker_collect(void);
void worker_cancel(void);

#endif
