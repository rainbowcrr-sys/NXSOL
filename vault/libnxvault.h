/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NXSOL_LIBNXVAULT_H
#define NXSOL_LIBNXVAULT_H
#include <stddef.h>
#include "nxvault.h"
int  nxvault_get(const char *path, char *out, size_t outsz);
int  nxvault_set(const char *path, const char *value);
const char *nxvault_strerror(int err);
#endif
