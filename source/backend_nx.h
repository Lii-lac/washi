// The real backend: libnx. See backend.h for what each call means.
#ifndef BACKEND_NX_H
#define BACKEND_NX_H

#include "backend.h"

// Initialises account/ns/psm and returns the backend, or NULL (with a reason in *why) if a service is unavailable.
Backend *backend_nx_init(const char **why);
void backend_nx_exit(Backend *be);

#endif
