/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX Vault, client protocol
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 *
 * NX Vault is not a registry and this is not a registry editor. The .nxv
 * files are signed binaries; nxvaultd is the only process that holds them
 * open, and every read and write from anywhere else goes through it.
 *
 * On the NX kernel the kernel itself enforced that: .nxv inodes were marked
 * so open() failed for everyone but nxvaultd, root included. On Linux there
 * is no such mark, so the guarantee is weaker and stated honestly:
 *
 *   - nxvaultd holds the files open and they are 0600
 *   - every file carries a checksum and a backup
 *   - a write is rejected by tier before it is applied, root included
 *   - a file whose checksum does not match is not loaded; the previous
 *     signed version is loaded instead
 *
 * Root with a hex editor can still corrupt the file, but the system then
 * refuses to trust it and falls back -- which is the property that matters.
 */

#ifndef NXSOL_VAULT_H
#define NXSOL_VAULT_H

#include <stdint.h>
#include <stddef.h>

/* Abstract Unix socket: no filesystem needed, so the daemon can be reached
 * from the initramfs before anything is mounted. */
#define NXVAULT_SOCK "@nxvault"

/* Tier of a key. Drives who may write it. */
#define NXV_TIER_USER      0   /* the owning account            */
#define NXV_TIER_ADMIN     1   /* root, or after elevation      */
#define NXV_TIER_IMMUTABLE 2   /* maintenance mode only         */

/* Value types. */
#define NXV_T_BOOL  1
#define NXV_T_U32   2
#define NXV_T_FLOAT 3
#define NXV_T_STR   4
#define NXV_T_ENUM  5

/* Errors. */
#define NXV_OK        0
#define NXV_ERR_PERM  1
#define NXV_ERR_NOENT 2
#define NXV_ERR_TYPE  3
#define NXV_ERR_RANGE 4
#define NXV_ERR_IO    5
#define NXV_ERR_PROTO 6

/* Wire protocol, one line per request:
 *   GET <path>
 *   SET <path> <value>
 *   LIST <prefix>
 * replies:
 *   OK <payload>
 *   ERR <code>
 */
#define NXV_LINE_MAX 4096

/* ------------------------------------------------------------ file format */

#define NXV_MAGIC   "NXV1"
#define NXV_MAX_BACKUPS 20

struct nxv_file_header {
    char     magic[4];        /* "NXV1"                        */
    uint32_t version;
    uint32_t flags;           /* 1 = system tier               */
    uint32_t entry_count;
    uint32_t body_size;       /* bytes of entries that follow  */
    uint32_t checksum;        /* CRC32 over the entries        */
    uint32_t reserved;
};

/* One entry, serialised as: path\0 type tier min max value\0 */
struct nxv_entry {
    char  *path;
    int    type;
    int    tier;
    double min, max;      /* numeric range; unused for str/bool */
    char  *value;         /* always the textual form            */
};

#endif /* NXSOL_VAULT_H */
