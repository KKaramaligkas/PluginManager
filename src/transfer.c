#define _POSIX_C_SOURCE 200809L
#include "transfer.h"
#ifdef __PSP__
#include <pspkernel.h>
#else
#include <pthread.h>
#include <time.h>
#endif

/* Connections stay open between transfers in one multi handle kept for the
   next transfer: a page's stylesheets and scripts, and the next page from the
   same site, skip the TCP and TLS handshakes, which take a PSP's CPU most of
   a second each. One transfer uses it at a time; a concurrent one gets a
   multi handle of its own, as every transfer used to. */
static CURLM *kept;
#ifdef __PSP__
static SceUID kept_lock = -1;
static int take(void)
{
    if (kept_lock < 0) kept_lock = sceKernelCreateSema("pm_connections", 0, 1, 1, NULL);
    return kept_lock >= 0 && sceKernelPollSema(kept_lock, 1) >= 0;
}
static void give(void) { sceKernelSignalSema(kept_lock, 1); }
#else
static pthread_mutex_t kept_lock = PTHREAD_MUTEX_INITIALIZER;
static int take(void) { return pthread_mutex_trylock(&kept_lock) == 0; }
static void give(void) { pthread_mutex_unlock(&kept_lock); }
#endif

void pm_transfer_close(void)
{
    if (!take()) return;
    if (kept) curl_multi_cleanup(kept);
    kept = NULL;
    give();
}

static void idle(void)
{
#ifdef __PSP__
    sceKernelDelayThread(100 * 1000);
#else
    const struct timespec delay = {0, 100 * 1000 * 1000};
    nanosleep(&delay, NULL);
#endif
}

CURLcode pm_transfer_run(CURL *easy, int (*cancelled)(void *), void *ud)
{
    if (cancelled && cancelled(ud)) return CURLE_ABORTED_BY_CALLBACK;
    CURLM *multi = NULL;
    int keep = take();
    if (keep) {
        /* A few servers: the page's, and the ones its stylesheets and scripts
           come from. An idle TLS connection keeps mbedTLS's two 16 KB record
           buffers, about 35 KB. */
        if (!kept && (kept = curl_multi_init()) != NULL) curl_multi_setopt(kept, CURLMOPT_MAXCONNECTS, 4L);
        multi = kept;
        if (!multi) { give(); keep = 0; }
    }
    if (!multi) multi = curl_multi_init();
    if (!multi) return CURLE_OUT_OF_MEMORY;
    CURLcode result = CURLE_FAILED_INIT;
    int added = curl_multi_add_handle(multi, easy) == CURLM_OK;
    if (!added) goto done;
    for (;;) {
        if (cancelled && cancelled(ud)) { result = CURLE_ABORTED_BY_CALLBACK; break; }
        int running = 0;
        CURLMcode code;
        do { code = curl_multi_perform(multi, &running); } while (code == CURLM_CALL_MULTI_PERFORM);
        if (code != CURLM_OK) break;
        int pending;
        CURLMsg *message;
        while ((message = curl_multi_info_read(multi, &pending))) {
            if (message->msg == CURLMSG_DONE && message->easy_handle == easy) {
                result = message->data.result;
                goto done;
            }
        }
        if (!running) break;
        if (cancelled && cancelled(ud)) { result = CURLE_ABORTED_BY_CALLBACK; break; }
        int ready = 0;
        if (curl_multi_wait(multi, NULL, 0, 100, &ready) != CURLM_OK) break;
        /* curl_multi_wait returns immediately when it has no descriptors. */
        if (!ready) idle();
    }
done:
    if (added) curl_multi_remove_handle(multi, easy);
    if (keep) give();
    else curl_multi_cleanup(multi);
    return result;
}
