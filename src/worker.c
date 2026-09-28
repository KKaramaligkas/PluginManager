/*
    Plugin Manager for ARK-5
    worker.c: background thread for network and install jobs.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>

#include "app.h"
#include "fs.h"
#include "image.h"
#include "installer.h"
#include "net.h"
#include "worker.h"

job_t job;

static SceUID thread = -1;
static SceUID sema = -1;
static volatile int quit;

static int dl_progress(void *ud, int64_t done, int64_t total)
{
    (void)ud;
    job.cur = done;
    job.total = total;
    return job.cancel;
}

static int ctx_download(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen)
{
    (void)ctx;
    job.cur = 0;
    job.total = -1;
    return net_download(url, dest, dl_progress, NULL, err, errlen);
}

static void ctx_progress(install_ctx *ctx, const char *stage, int64_t done, int64_t total)
{
    (void)ctx;
    if (stage && strcmp(stage, job.stage) != 0) pm_strlcpy(job.stage, stage, sizeof(job.stage));
    if (total >= 0 || done == 0) {
        job.cur = done;
        job.total = total;
    }
}

static int ctx_cancelled(install_ctx *ctx)
{
    (void)ctx;
    return job.cancel;
}

static void do_refresh(void)
{
    int len = 0;
    pm_strlcpy(job.stage, "Downloading the store", sizeof(job.stage));
    char *json = net_get(app.cfg.store_url, STORE_MAX_SIZE, &len, dl_progress, NULL, job.error, sizeof(job.error));
    if (!json) {
        job.result = -1;
        return;
    }

    store_t *st = calloc(1, sizeof(store_t));
    if (!st || store_parse(st, json, app.cfg.store_url, job.error, sizeof(job.error)) < 0) {
        free(st);
        free(json);
        job.result = -1;
        return;
    }
    fs_write_all(app.store_cache, json, len);
    free(json);
    job.new_store = st;
    job.result = 0;
}

/* icons.txt remembers which URL every cached icon came from */
static char *icon_index;

static int icon_is_current(const char *id, const char *url)
{
    if (!icon_index) return 0;
    char key[400];
    snprintf(key, sizeof(key), "%s\t%s\n", id, url);
    return strstr(icon_index, key) != NULL;
}

static void do_icons(void)
{
    char index_file[256];
    snprintf(index_file, sizeof(index_file), "%sicons.txt", app.icons_dir);
    icon_index = fs_read_all(index_file, NULL, 256 * 1024);

    /* snapshot, the store may not be touched while we run */
    int n = app.store.count;
    size_t cap = 1;
    for (int i = 0; i < n; i++)
        if (app.store.entries[i].icon) cap += strlen(app.store.entries[i].id) + strlen(app.store.entries[i].icon) + 2;
    char *new_index = malloc(cap);
    if (!new_index) {
        free(icon_index);
        icon_index = NULL;
        return;
    }
    new_index[0] = 0;
    size_t len = 0;

    for (int i = 0; i < n && !job.cancel && !quit; i++) {
        const store_entry *e = &app.store.entries[i];
        if (!e->icon) continue;

        char path[256], tmp[272];
        snprintf(path, sizeof(path), "%s%s.png", app.icons_dir, e->id);
        snprintf(tmp, sizeof(tmp), "%s.part", path);

        int have = fs_exists(path) && icon_is_current(e->id, e->icon);
        if (!have) {
            snprintf(job.stage, sizeof(job.stage), "Downloading icons (%d/%d)", i + 1, n);
            char err[128];
            if (net_download(e->icon, tmp, NULL, NULL, err, sizeof(err)) == 0 && image_png_valid(tmp)) {
                fs_remove(path);
                if (fs_rename(tmp, path) == 0) have = 1;
            }
            fs_remove(tmp);
            job.icons_done++;
        }
        if (have) len += snprintf(new_index + len, cap - len, "%s\t%s\n", e->id, e->icon);
    }

    /* keep entries of the old index for icons we didn't look at */
    if (!job.cancel) fs_write_all(index_file, new_index, (int)len);
    free(new_index);
    free(icon_index);
    icon_index = NULL;
    job.result = 0;
}

static void do_install(void)
{
    const store_entry *e = store_find(&app.store, job.id);
    if (!e) {
        snprintf(job.error, sizeof(job.error), "%s is not in the store anymore", job.id);
        job.result = -1;
        return;
    }

    install_ctx ctx;
    app_fill_install_ctx(&ctx);
    ctx.download = ctx_download;
    ctx.progress = ctx_progress;
    ctx.cancelled = ctx_cancelled;

    job.result = installer_install(&ctx, e, &app.db, job.error, sizeof(job.error));
    pm_strlcpy(job.messages, ctx.messages, sizeof(job.messages));
    pm_strlcpy(job.run_path, ctx.run_path, sizeof(job.run_path));
    pm_strlcpy(job.run_title, ctx.run_title, sizeof(job.run_title));
    app_db_changed();
}

static void do_uninstall(void)
{
    install_ctx ctx;
    app_fill_install_ctx(&ctx);
    ctx.progress = ctx_progress;
    job.result = installer_uninstall(&ctx, &app.db, job.id, job.error, sizeof(job.error));
    app_db_changed();
}

static int worker_main(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    while (!quit) {
        if (sceKernelWaitSema(sema, 1, NULL) < 0 || quit) break;

        switch (job.type) {
        case JOB_REFRESH: do_refresh(); break;
        case JOB_ICONS: do_icons(); break;
        case JOB_INSTALL: do_install(); break;
        case JOB_UNINSTALL: do_uninstall(); break;
        default: job.result = 0; break;
        }

        job.running = 0;
        job.finished = 1;
    }
    return 0;
}

int worker_start(void)
{
    sema = sceKernelCreateSema("pm_jobs", 0, 0, 1, NULL);
    if (sema < 0) return -1;
    thread = sceKernelCreateThread("pm_worker", worker_main, 0x30, 256 * 1024,
                                   PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    if (thread < 0) return -1;
    return sceKernelStartThread(thread, 0, NULL);
}

void worker_stop(void)
{
    if (thread < 0) return;
    quit = 1;
    job.cancel = 1;
    sceKernelSignalSema(sema, 1);
    SceUInt timeout = 3 * 1000 * 1000;
    if (sceKernelWaitThreadEnd(thread, &timeout) < 0) sceKernelTerminateThread(thread);
    sceKernelDeleteThread(thread);
    sceKernelDeleteSema(sema);
    thread = -1;
}

int worker_busy(void)
{
    return job.running || job.finished;
}

int worker_submit(job_type type, const char *id, const char *title)
{
    if (worker_busy()) return -1;
    job.type = type;
    job.cancel = 0;
    job.result = 0;
    job.cur = 0;
    job.total = -1;
    job.error[0] = 0;
    job.messages[0] = 0;
    job.run_path[0] = 0;
    job.run_title[0] = 0;
    job.new_store = NULL;
    job.stage[0] = 0;
    pm_strlcpy(job.id, id ? id : "", sizeof(job.id));
    pm_strlcpy(job.title, title ? title : "", sizeof(job.title));
    job.started = sceKernelGetSystemTimeLow();
    job.running = 1;
    sceKernelSignalSema(sema, 1);
    return 0;
}

int worker_collect(void)
{
    if (!job.finished) return 0;
    job.finished = 0;
    return 1;
}

void worker_cancel(void)
{
    job.cancel = 1;
}
