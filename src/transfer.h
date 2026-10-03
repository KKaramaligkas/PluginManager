#ifndef PM_TRANSFER_H
#define PM_TRANSFER_H
#include <curl/curl.h>

/* Poll cancellation even when the peer sends nothing. The caller keeps
   ownership of the easy handle and any partial download. The connection
   stays open afterwards for the next transfer to the same server. */
CURLcode pm_transfer_run(CURL *easy, int (*cancelled)(void *), void *ud);
/* Closes the connections kept open, unless a transfer is using them. Call it
   before the network goes down. */
void pm_transfer_close(void);
#endif
