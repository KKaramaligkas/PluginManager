#include <ctype.h>
#include <string.h>
#include "http_policy.h"
static int secure(const char *url)
{
    const char *scheme="https://";
    while (*scheme) if (tolower((unsigned char)*url++) != *scheme++) return 0;
    return 1;
}
CURLcode pm_http_policy(CURL *curl, const char *url, const char *agent, int tls12)
{
    CURLcode result;
#define SET(option,value) do { result=curl_easy_setopt(curl,option,value); if(result!=CURLE_OK)return result; } while(0)
#if LIBCURL_VERSION_NUM >= 0x075500
    SET(CURLOPT_PROTOCOLS_STR, "http,https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR, secure(url) ? "https" : "http,https");
#else
    SET(CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    SET(CURLOPT_REDIR_PROTOCOLS, (long)(secure(url) ? CURLPROTO_HTTPS : CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    SET(CURLOPT_USERAGENT, agent);
    if(tls12) SET(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_FOLLOWLOCATION, 1L);
    SET(CURLOPT_MAXREDIRS, 8L);
#undef SET
    return CURLE_OK;
}
