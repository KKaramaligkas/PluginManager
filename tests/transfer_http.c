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
/* One request with a new easy handle each time, as net.c makes them. */
static int request(const char *url,long timeout)
{
    CURL *easy=curl_easy_init();
    if(!easy || pm_http_policy(easy,url,"PM-cancel-test",1)!=CURLE_OK) return -1;
    curl_easy_setopt(easy,CURLOPT_URL,url);
    curl_easy_setopt(easy,CURLOPT_PROXY,""); /* local fixtures only */
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,discard);
    curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,timeout);
    /* Deliberately omit curl's progress callback: the runner must cancel independently. */
    clock_gettime(CLOCK_MONOTONIC,&started);
    CURLcode result=pm_transfer_run(easy,cancel,NULL);
    curl_easy_cleanup(easy);
    return (int)result;
}
/* transfer_http URL CANCEL_MS TIMEOUT_MS [REQUESTS]: with REQUESTS, makes
   that many requests, closes the kept connections, then makes one more. */
int main(int argc,char **argv)
{
    if(argc!=4&&argc!=5) return 2;
    curl_global_init(CURL_GLOBAL_ALL);
    cancel_after=strtol(argv[2],NULL,10);
    long timeout=strtol(argv[3],NULL,10);
    int requests=argc==5?(int)strtol(argv[4],NULL,10):1;
    for(int i=0;i<requests;i++)printf("%d\n",request(argv[1],timeout));
    if(argc==5){pm_transfer_close();printf("%d\n",request(argv[1],timeout));}
    pm_transfer_close();
    curl_global_cleanup();
    return 0;
}
