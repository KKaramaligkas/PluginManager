#define _POSIX_C_SOURCE 200809L
#include "transfer.h"
#ifdef __PSP__
#include <pspkernel.h>
#else
#include <time.h>
#endif

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
    CURLM *multi = curl_multi_init();
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
    curl_multi_cleanup(multi);
    return result;
}
