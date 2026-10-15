// The Switch implementation of platform.h (libnx): a 1280x720 framebuffer, the controllers, the console's clock and theme,
// the system keyboard, and the console's own font as a fallback for characters the built-in font lacks.
#ifndef PLATFORM_NX_H
#define PLATFORM_NX_H

#include "platform.h"

// Returns NULL if the display can't be set up.
Platform *platform_nx_init(void);
void platform_nx_exit(Platform *p);
// updates (net_nx.c): fills in online, http_get, install_update, restart and the going-back ones; self_path = where this app's NRO is (argv[0]); the network
// comes up when first wanted
void net_nx_attach(Platform *p, const char *self_path);
void net_nx_exit(void);
// first thing in main: 1 = a new version on trial never got going twice, so the kept one is back in place and opens next: return from main at once
int net_nx_boot_check(const char *self_path);

#endif
