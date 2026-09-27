/*
    Plugin Manager for ARK-5
    net.h: Wi-Fi connection (system dialog) and HTTPS downloads (libcurl + mbedTLS).
*/

#ifndef PM_NET_H
#define PM_NET_H

#include <stdint.h>

/* Return non-zero to cancel the transfer. */
typedef int (*net_progress_fn)(void *ud, int64_t done, int64_t total);

int net_init(void);
void net_term(void);

int net_wlan_switch_on(void);
int net_is_connected(void);

/* Shows the system "connect to an access point" dialog. `draw` renders the
   app background each frame (the dialog is drawn on top of it).
   Returns 0 when connected. */
int net_connect_dialog(void (*draw)(void *ud), void *ud);

/* TLS settings: CA bundle (PEM) and whether certificates are verified. */
void net_set_tls(const char *ca_file, int verify);

int net_download(const char *url, const char *dest_path, net_progress_fn cb, void *ud, char *err, int errlen);
/* Downloads into memory (NUL terminated). */
char *net_get(const char *url, int max_size, int *out_len, net_progress_fn cb, void *ud, char *err, int errlen);

#endif
