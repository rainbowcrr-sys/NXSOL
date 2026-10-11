/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX Vault wire protocol
 * Copyright (C) 2026 rainbowcrr-sys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * NOT IMPLEMENTED -- superseded, kept only as a design note.
 *
 * The protocol nxvaultd actually speaks is the one documented in nxvault.h:
 * a single line of text per request,
 *
 *     GET <path>\n
 *     SET <path> <value>\n
 *
 * answered with
 *
 *     OK <payload>\n
 *     ERR <code>\n
 *
 * This header describes a fixed-size binary scheme (struct nxvp_req) that
 * was never wired up: grep confirms nothing includes it. It is retained
 * because the field-by-field thinking is still the reference for a future
 * binary framing, but nothing here is live code and it must not be mistaken
 * for the wire format.
 *
 * The original description follows.
 *
 * ---------------------------------------------------------------------------
 *
 * The protocol between clients and nxvaultd.
 *
 * A request is fixed-size: an opcode, a path, and (for writes) a value.
 * A reply is an error code followed by a payload. Nothing else crosses the
 * socket, which is what makes it practical to audit what a caller is
 * allowed to ask for.
 *
 * Paths are NUL-terminated inside the fixed arrays, and any request whose
 * path is not terminated is rejected rather than read past -- the daemon
 * treats the buffers as untrusted input.
 */

#ifndef NX_VAULT_PROTOCOL_H
#define NX_VAULT_PROTOCOL_H

#include <stdint.h>

#define NXVP_MAGIC 0x4E585650u   /* "NXVP" */

enum nxvp_op {
    NXVP_GET = 1,
    NXVP_SET,
    NXVP_LIST,
    NXVP_TIER,
    NXVP_TYPE,
    NXVP_MAINTENANCE,
    NXVP_REVERT,
};

struct nxvp_req {
    uint32_t magic;
    uint32_t op;
    char     path[256];
    char     value[4096];
};

struct nxvp_resp {
    uint32_t magic;
    int32_t  err;            /* NX_OK or a negative NX_ERR_*   */
    uint32_t payload_len;
    char     payload[8192];
};

/* The daemon checks these before touching any field. */
#define NXVP_REQ_SIZE  ((uint32_t)sizeof(struct nxvp_req))
#define NXVP_RESP_SIZE ((uint32_t)sizeof(struct nxvp_resp))

#endif /* NX_VAULT_PROTOCOL_H */
