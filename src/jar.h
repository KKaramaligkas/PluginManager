/*
    Plugin Manager for ARK-5
    jar.h: the cookies a web page's scripts read and set (document.cookie).
*/

#ifndef PM_JAR_H
#define PM_JAR_H

#include <stddef.h>
#include <time.h>
#include <curl/curl.h>

/* document.cookie for a page at `url`: "name=value; name2=value2", the
   cookies of `share` sent to it, but not HttpOnly ones. 0 on success. */
int jar_cookie_string(CURLSH *share, const char *url, time_t now, char *out, size_t size);
/* A page at `url` assigning `cookie` to document.cookie ("name=value;
   path=/; max-age=3600"). Like a browser, it ignores cookies for other
   sites, HttpOnly ones and Secure ones from http pages. 0 when stored. */
int jar_cookie_set(CURLSH *share, const char *url, const char *cookie, time_t now);

#endif
