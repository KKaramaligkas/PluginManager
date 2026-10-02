#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/transfer.h"
#include "../src/http_policy.h"
static struct timespec started;
static long cancel_after;
static int cancel(void *ud)
{
    (void)ud;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    long elapsed=(now.tv_sec-started.tv_sec)*1000+(now.tv_nsec-started.tv_nsec)/1000000;
    return cancel_after>=0 && elapsed>=cancel_after;
}
static size_t discard(char *data,size_t size,size_t count,void *ud) { return size*count; }
int main(int argc,char **argv)
{
    if(argc!=4) return 2;
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *easy=curl_easy_init();
    if(!easy || pm_http_policy(easy,argv[1],"PM-cancel-test",1)!=CURLE_OK) return 2;
    curl_easy_setopt(easy,CURLOPT_URL,argv[1]);
    curl_easy_setopt(easy,CURLOPT_PROXY,""); /* local fixtures only */
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,discard);
    curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,strtol(argv[3],NULL,10));
    /* Deliberately omit curl's progress callback: the runner must cancel independently. */
    cancel_after=strtol(argv[2],NULL,10);
    clock_gettime(CLOCK_MONOTONIC,&started);
    CURLcode result=pm_transfer_run(easy,cancel,NULL);
    printf("%d\n",(int)result);
    curl_easy_cleanup(easy); curl_global_cleanup();
    return 0;
}
