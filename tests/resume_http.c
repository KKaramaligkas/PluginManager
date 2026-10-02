/* Exercise portable download callbacks through a real libcurl transfer. */
#include <stdio.h>
#include <curl/curl.h>
#include "../src/resume.h"
#include "../src/transfer.h"
static size_t body(char *p, size_t a, size_t b, void *ud) { return resume_write(ud, p, a*b); }
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    curl_global_init(CURL_GLOBAL_ALL);
    for (int attempt = 0; attempt < 2; attempt++) {
        resume_state s;
        if (resume_open(&s, argv[1], argv[2]) < 0) return 2;
        CURL *c = curl_easy_init();
        struct curl_slist *headers = NULL;
        curl_easy_setopt(c, CURLOPT_URL, argv[1]);
        curl_easy_setopt(c, CURLOPT_PROXY, "");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "identity");
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, resume_header);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &s);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
        if (s.offset) {
            char line[192];
            snprintf(line, sizeof(line), "If-Range: %s", s.validator);
            headers = curl_slist_append(NULL, line);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)s.offset);
        }
        CURLcode result = pm_transfer_run(c, NULL, NULL);
        curl_easy_cleanup(c);
        curl_slist_free_all(headers);
        int closed = resume_close(&s, result == CURLE_OK);
        if (result == CURLE_OK && closed == 0) { curl_global_cleanup(); return 0; }
        if (!s.retry_fresh || attempt) { curl_global_cleanup(); return 1; }
        resume_discard(&s);
    }
    return 1;
}
