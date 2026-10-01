/* Shared libcurl protocol policy; also exercised by native HTTP/TLS tests. */
#ifndef PM_HTTP_POLICY_H
#define PM_HTTP_POLICY_H
#include <curl/curl.h>
CURLcode pm_http_policy(CURL *curl, const char *url, const char *agent, int tls12);
#endif
