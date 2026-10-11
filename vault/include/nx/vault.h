/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX Vault, public API
 * Copyright (C) 2026 rainbowcrr-sys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * NX Vault -- the system's configuration store.
 *
 * It is deliberately not a registry:
 *
 *   - Values live in signed binary files, not in a hive that any tool can
 *     open. nxvaultd is the only process that reads or writes them.
 *   - Every key has a type and a range. A write that does not match is
 *     refused, and the refusal comes from the daemon rather than from each
 *     front end deciding for itself.
 *   - Every key carries a tier. Four subtrees -- boot, kernel, security and
 *     the vault's own keys -- cannot be written by root at all; changing
 *     them means rebooting into maintenance mode, on purpose.
 *
 * Programs talk to it over a Unix socket. There is no file format to parse,
 * no text file to hand-edit, and no way to reach the values except through
 * the daemon.
 */

#ifndef NX_VAULT_H
#define NX_VAULT_H

#include <stddef.h>
#include <stdint.h>

#define NX_VAULT_SOCKET "/run/nxvault.sock"

#define NX_VAULT_PATH_MAX 256
#define NX_VAULT_VAL_MAX  4096

/* ------------------------------------------------------------------ tiers */

/*
 * Who may write a key.
 *
 * The point of NX_VAULT_IMMUTABLE is not to make root's life harder for its
 * own sake: boot and kernel parameters are the ones whose bad values stop a
 * machine from coming back up, so editing them should require a deliberate
 * trip through maintenance mode rather than one mistyped command.
 */
enum nx_vault_tier {
    NX_VAULT_USER = 0,      /* the owning account                        */
    NX_VAULT_ADMIN,         /* root / sudo                               */
    NX_VAULT_IMMUTABLE,     /* nobody; maintenance mode only             */
};

/* ------------------------------------------------------------------ types */

enum nx_vault_type {
    NX_VAULT_BOOL = 0,
    NX_VAULT_INT,           /* int64                                     */
    NX_VAULT_UINT,          /* uint64                                    */
    NX_VAULT_FLOAT,         /* double                                    */
    NX_VAULT_STRING,
    NX_VAULT_ENUM,          /* one of a fixed set of strings             */
};

/* ---------------------------------------------------------------- errors */

#define NX_OK           0
#define NX_ERR_PERM    -1      /* caller may not write this key           */
#define NX_ERR_NOENT   -2      /* no such key                             */
#define NX_ERR_INVAL   -3      /* malformed request                       */
#define NX_ERR_TYPE    -4      /* value does not match the key's type     */
#define NX_ERR_RANGE   -5      /* value outside the key's allowed range   */
#define NX_ERR_NOMEM   -6
#define NX_ERR_IO      -7      /* backing file could not be read/written  */
#define NX_ERR_BUSY    -8
#define NX_ERR_EXIST   -9
#define NX_ERR_SIG     -10     /* a .nxv failed its signature check       */

const char *nx_vault_strerror(int err);

/* ------------------------------------------------------------ client API */

/*
 * These are the only way a program reaches a value. They talk to nxvaultd;
 * if it is not running they fail rather than falling back to reading a file,
 * because a fallback is exactly how a store stops being authoritative.
 */

int nx_vault_get(const char *path, void *out, size_t out_len);
int nx_vault_get_str(const char *path, char *out, size_t out_len);
int nx_vault_get_int(const char *path, int64_t *out);
int nx_vault_get_uint(const char *path, uint64_t *out);
int nx_vault_get_double(const char *path, double *out);
int nx_vault_get_bool(const char *path, int *out);

/* The value is always passed as text; the daemon parses it according to the
 * key's type and range. That keeps type checking in one place. */
int nx_vault_set(const char *path, const char *value_text);

int nx_vault_list(const char *prefix, char *out, size_t out_len);

/* Metadata, so a front end can grey out what the caller cannot change. */
int nx_vault_tier_of(const char *path, enum nx_vault_tier *out);
int nx_vault_type_of(const char *path, enum nx_vault_type *out);

/* True while the machine was booted into maintenance mode. */
int nx_vault_maintenance_active(void);

/* --------------------------------------------------------------- daemon */

/* nxvaultd's own entry point; not used by clients. */
int nxvaultd_main(int argc, char **argv);

#endif /* NX_VAULT_H */
