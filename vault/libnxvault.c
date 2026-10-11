/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX Vault client library
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 *
 * The only way to read or write a value. Nothing here opens a .nxv file.
 */

#define _GNU_SOURCE
#include "nxvault.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int connect_sock(void)
{
    int s;
    struct sockaddr_un a;

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;

    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    a.sun_path[0] = 0;
    memcpy(a.sun_path + 1, "nxvault", 7);

    if (connect(s, (struct sockaddr *)&a,
                offsetof(struct sockaddr_un, sun_path) + 8) < 0) {
        close(s);
        return -1;
    }
    return s;
}

/* Send one request, return the reply line. */
static int request(const char *req, char *out, size_t outsz)
{
    int s = connect_sock();
    ssize_t n;

    if (s < 0) return NXV_ERR_IO;
    if (write(s, req, strlen(req)) < 0) { close(s); return NXV_ERR_IO; }

    n = read(s, out, outsz - 1);
    close(s);
    if (n <= 0) return NXV_ERR_IO;
    out[n] = 0;
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
    return NXV_OK;
}

int nxvault_get(const char *path, char *out, size_t outsz)
{
    char req[NXV_LINE_MAX], reply[NXV_LINE_MAX];
    int rc;

    snprintf(req, sizeof(req), "GET %s\n", path);
    rc = request(req, reply, sizeof(reply));
    if (rc != NXV_OK) return rc;

    if (strncmp(reply, "OK ", 3) == 0) {
        snprintf(out, outsz, "%s", reply + 3);
        return NXV_OK;
    }
    if (strncmp(reply, "ERR ", 4) == 0) return atoi(reply + 4);
    return NXV_ERR_PROTO;
}

int nxvault_set(const char *path, const char *value)
{
    char req[NXV_LINE_MAX], reply[NXV_LINE_MAX];
    int rc;

    snprintf(req, sizeof(req), "SET %s %s\n", path, value);
    rc = request(req, reply, sizeof(reply));
    if (rc != NXV_OK) return rc;

    if (strncmp(reply, "OK", 2) == 0) return NXV_OK;
    if (strncmp(reply, "ERR ", 4) == 0) return atoi(reply + 4);
    return NXV_ERR_PROTO;
}

const char *nxvault_strerror(int err)
{
    switch (err) {
    case NXV_OK:         return "ok";
    case NXV_ERR_PERM:   return "permission denied (immutable key; needs maintenance mode)";
    case NXV_ERR_NOENT:  return "no such key";
    case NXV_ERR_TYPE:   return "wrong type for this key";
    case NXV_ERR_RANGE:  return "value out of range";
    case NXV_ERR_IO:     return "cannot reach nxvaultd";
    case NXV_ERR_PROTO:  return "protocol error";
    default:             return "unknown error";
    }
}
