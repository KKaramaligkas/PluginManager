#ifndef PM_TRANSFER_H
#define PM_TRANSFER_H
#include <curl/curl.h>

/* Poll cancellation even when the peer sends nothing. The caller keeps
   ownership of the easy handle and any partial download. */
CURLcode pm_transfer_run(CURL *easy, int (*cancelled)(void *), void *ud);
#endif
