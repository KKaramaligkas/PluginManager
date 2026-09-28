/*
    Plugin Manager for ARK-5
    tlsdiag.h: explains a failed certificate check in words: which certificate
    was rejected, why, and what the PSP's clock said at the time.
*/

#ifndef PM_TLSDIAG_H
#define PM_TLSDIAG_H

#include <stdint.h>
#include <time.h>

/* Problems of a certificate, with the values of mbedTLS' MBEDTLS_X509_BADCERT_* */
#define TLSDIAG_EXPIRED         0x0001
#define TLSDIAG_REVOKED         0x0002
#define TLSDIAG_NAME            0x0004      /* issued for another server name */
#define TLSDIAG_NOT_TRUSTED     0x0008      /* no trusted authority signed it */
#define TLSDIAG_FUTURE          0x0200      /* not valid yet */

#define TLSDIAG_MAX_CERTS       5

typedef struct {
    int year, month, day, hour, minute, second;
} tlsdiag_time;

typedef struct {
    uint32_t flags;
    char subject[160];          /* distinguished names, "C=US, O=..., CN=..." */
    char issuer[160];
    tlsdiag_time valid_from, valid_to;
} tlsdiag_cert;

typedef struct {
    char host[128];
    uint32_t flags;             /* problems of all the certificates */
    int count;                  /* certificates recorded; [0] is the server's own */
    tlsdiag_cert certs[TLSDIAG_MAX_CERTS];
} tlsdiag;

/* Records the server name of `url` ("https://host:port/path"). */
void tlsdiag_set_host(tlsdiag *d, const char *url);

/* The common name of a distinguished name (or its organization, or all of it). */
void tlsdiag_common_name(const char *dn, char *out, int size);

/* "28 Sep 2026, 17:59" (with the time) or "28 Sep 2026"; "unknown" before 1980. */
void tlsdiag_format_time(time_t t, int with_time, char *out, int size);

/* A short explanation for the user. `clock` is the PSP's clock as text. */
void tlsdiag_message(const tlsdiag *d, const char *clock, char *out, int size);

/* The details, one certificate per paragraph, for the report file. Returns the length. */
int tlsdiag_report(const tlsdiag *d, char *out, int size);

#endif
