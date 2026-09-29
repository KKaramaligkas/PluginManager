/*
    Plugin Manager for ARK-5
    net.h: Wi-Fi connection (system dialog) and HTTPS downloads (libcurl + mbedTLS).
*/

#ifndef PM_NET_H
#define PM_NET_H

#include <stdint.h>

/* Return non-zero to cancel the transfer. */
typedef int (*net_progress_fn)(void *ud, int64_t done, int64_t total);

/* net_connect_dialog() and net_connect_auto() results; errors are negative
   (see net_last_error()) */
#define NET_CONNECTED   0
#define NET_CANCELLED   1
#define NET_NO_PROFILE  2       /* no connection to pick without asking */

/* Loads and starts the system network libraries. Returns < 0 on failure. */
int net_init(void);
void net_term(void);

/* What the last failed net_init()/net_connect_dialog() couldn't do, with
   the system error code. */
const char *net_last_error(void);

int net_wlan_switch_on(void);
int net_is_connected(void);

/* Shows the system "connect to an access point" dialog. `draw` renders the
   app background each frame (the dialog is drawn on top of it).
   Returns NET_CONNECTED, NET_CANCELLED when the user backed out (the dialog
   shows its own connection errors), or < 0 when the network couldn't be
   started or the dialog couldn't be shown. */
int net_connect_dialog(void (*draw)(void *ud), void *ud);

/* Connects without the dialog to the network connection used last: the one
   named `last_name` (as saved by the app), else the one the PSP used last,
   else the only one set up. `frame` draws a frame while it connects and
   returns non-zero to stop (to pick another network in the dialog).
   Returns NET_CONNECTED, NET_CANCELLED when stopped, NET_NO_PROFILE, or < 0
   when it couldn't connect (net_last_error() says why). */
int net_connect_auto(const char *last_name, int (*frame)(void *ud, const char *name), void *ud);

/* Name of the network connection in use, "" when offline. */
void net_profile_name(char *out, int size);

/* Drops the Wi-Fi connection. */
void net_disconnect(void);

/* TLS settings: CA bundle (PEM) and whether certificates are verified. */
void net_set_tls(const char *ca_file, int verify);
/* Where the details of a failed certificate check are written. */
void net_set_report_file(const char *path);

int net_download(const char *url, const char *dest_path, net_progress_fn cb, void *ud, char *err, int errlen);
/* Downloads into memory (NUL terminated). */
char *net_get(const char *url, int max_size, int *out_len, net_progress_fn cb, void *ud, char *err, int errlen);

#endif
