/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- nxvaultd, the NX Vault daemon
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
 * nxvaultd -- the only process that reads and writes NX Vault.
 *
 * Everything else, including nxvault the CLI and the GUI, is a client: it
 * sends a line on the abstract socket and gets a line back. The daemon owns
 * the files, checks the tier, validates the value, keeps a backup and
 * recomputes the checksum.
 *
 * NX init depends on this before it will continue booting, which is the
 * reason the socket is an abstract one -- initramfs has no filesystem yet,
 * and an abstract name lives in the kernel rather than on disk.
 */

#define _GNU_SOURCE
#include "nxvault.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>

#define VAULT_DIR  "/etc/nx/vault"
#define BACKUP_DIR "/etc/nx/vault/.backups"

/* ------------------------------------------------------------ checksum */

static uint32_t crc32_of(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int j;

    for (i = 0; i < len; i++) {
        c ^= p[i];
        for (j = 0; j < 8; j++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return ~c;
}

/* --------------------------------------------------------------- entries */

struct entry {
    char  *path;
    int    type;
    int    tier;
    double min, max;
    char  *value;
    struct entry *next;
};

static struct entry *g_head;
static int g_maintenance;   /* 1 = booted into maintenance mode */

static struct entry *find(const char *path)
{
    struct entry *e = g_head;
    while (e) {
        if (strcmp(e->path, path) == 0) return e;
        e = e->next;
    }
    return NULL;
}

static void add(const char *path, int type, int tier,
                double min, double max, const char *value)
{
    struct entry *e = calloc(1, sizeof(*e));
    if (!e) return;
    e->path = strdup(path);
    e->value = strdup(value);
    e->type = type;
    e->tier = tier;
    e->min = min;
    e->max = max;
    e->next = g_head;
    g_head = e;
}

/* ----------------------------------------------------------------- files */

static void load_defaults(void)
{
    /* The keys a boot depends on. The boot subtree is immutable: the boot
     * target decides what the machine does, so it must not be writable
     * from a running desktop, root included. */
    add("/system/boot/target",        NXV_T_ENUM, NXV_TIER_IMMUTABLE,
        0, 0, "normal");
    add("/system/boot/timeout",       NXV_T_U32,  NXV_TIER_IMMUTABLE,
        0, 60, "5");
    add("/system/kernel/log-level",   NXV_T_ENUM, NXV_TIER_IMMUTABLE,
        0, 0, "info");
    add("/system/security/lockdown",  NXV_T_ENUM, NXV_TIER_IMMUTABLE,
        0, 0, "integrity");
    add("/system/display/theme",      NXV_T_ENUM, NXV_TIER_ADMIN,
        0, 0, "dark");
    add("/user/interface/typing/caret-bounce",
        NXV_T_FLOAT, NXV_TIER_USER, 0.0, 1.0, "0.0");
    add("/user/interface/typing/caret-bounce-duration",
        NXV_T_U32, NXV_TIER_USER, 60, 600, "150");

    /*
     * Window controls. These keys did not exist at all, so even once
     * libnxui asked for them every lookup would have come back NOENT and
     * the compiled-in defaults would have stood forever -- the appearance
     * would look Vault-driven in the source and never actually be.
     *
     * Names use hyphens, matching the rest of the tree. An earlier draft of
     * the caret keys was written with underscores in the UI code while the
     * daemon used hyphens; that disagreement is invisible until the two are
     * connected, at which point nothing matches. Hyphens are the one true
     * spelling now.
     */
    add("/user/interface/window/controls/style",
        NXV_T_ENUM, NXV_TIER_USER, 0, 0, "hybrid");
    add("/user/interface/window/controls/color-mode",
        NXV_T_ENUM, NXV_TIER_USER, 0, 0, "fixed");
    add("/user/interface/window/controls/idle-saturation",
        NXV_T_FLOAT, NXV_TIER_USER, 0.0, 1.0, "0.35");
    add("/user/interface/window/controls/hover-saturation",
        NXV_T_FLOAT, NXV_TIER_USER, 0.0, 1.0, "1.0");
    add("/user/interface/window/controls/inactive-opacity",
        NXV_T_FLOAT, NXV_TIER_USER, 0.0, 1.0, "0.4");
    add("/user/interface/window/controls/dot-idle-px",
        NXV_T_U32, NXV_TIER_USER, 1, 64, "8");
    add("/user/interface/window/controls/dot-hover-px",
        NXV_T_U32, NXV_TIER_USER, 1, 64, "10");
    add("/user/interface/window/controls/dot-focus-px",
        NXV_T_U32, NXV_TIER_USER, 1, 64, "14");
    add("/user/interface/window/controls/anim-enter-ms",
        NXV_T_U32, NXV_TIER_USER, 0, 2000, "150");
    add("/user/interface/window/controls/anim-leave-ms",
        NXV_T_U32, NXV_TIER_USER, 0, 2000, "200");
    add("/user/interface/window/root/always-colored",
        NXV_T_BOOL, NXV_TIER_USER, 0, 0, "true");
}

/*
 * Backup housekeeping.
 *
 * next_backup_seq() -- highest existing backup index + 1. Walking the
 * directory beats keeping a counter, because a counter does not survive the
 * daemon restarting and a stale one rewrites history.
 *
 * prune_backups()   -- keep at most NXV_MAX_BACKUPS, oldest first. Without
 * this the .backups directory grows without bound, which quietly turns a
 * safety net into a disk leak.
 */
static unsigned next_backup_seq(void)
{
    DIR *d = opendir(BACKUP_DIR);
    struct dirent *de;
    unsigned hi = 0, n;

    if (!d) return 0;
    while ((de = readdir(d))) {
        if (sscanf(de->d_name, "vault.%u.nxv", &n) == 1 && n > hi) hi = n;
    }
    closedir(d);
    return hi + 1;
}

static void prune_backups(void)
{
    DIR *d;
    struct dirent *de;
    unsigned nums[256];
    size_t count = 0, i, j;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        d = opendir(BACKUP_DIR);
        if (!d) return;
        count = 0;
        while ((de = readdir(d)) && count < 256) {
            unsigned n;
            if (sscanf(de->d_name, "vault.%u.nxv", &n) == 1) nums[count++] = n;
        }
        closedir(d);

        if (count <= NXV_MAX_BACKUPS) return;
        if (pass == 0) { continue; }   /* counting pass */
    }

    /* Sort ascending, then delete everything below the cutoff. */
    for (i = 1; i < count; i++) {
        unsigned v = nums[i];
        for (j = i; j > 0 && nums[j-1] > v; j--) nums[j] = nums[j-1];
        nums[j] = v;
    }
    for (i = 0; i + NXV_MAX_BACKUPS < count; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/vault.%u.nxv", BACKUP_DIR, nums[i]);
        unlink(p);
    }
}

static int write_file(const char *path)
{
    /* Serialise, checksum, write. The previous copy goes to .backups first,
     * and the checksum is what lets a corrupted file be rejected on load
     * rather than silently trusted. */
    char tmp[512], bak[512];
    unsigned char *body = NULL;
    size_t body_len = 0;
    struct entry *e;
    struct nxv_file_header h;
    int fd;
    FILE *f;

    for (e = g_head; e; e = e->next) {
        body_len += strlen(e->path) + 1 + strlen(e->value) + 1
                    + sizeof(int) * 2 + sizeof(double) * 2;
    }
    body = calloc(1, body_len ? body_len : 1);
    if (!body) return -1;

    {
        size_t off = 0;
        for (e = g_head; e; e = e->next) {
            memcpy(body + off, e->path, strlen(e->path) + 1);
            off += strlen(e->path) + 1;
            memcpy(body + off, &e->type, sizeof(int)); off += sizeof(int);
            memcpy(body + off, &e->tier, sizeof(int)); off += sizeof(int);
            memcpy(body + off, &e->min, sizeof(double)); off += sizeof(double);
            memcpy(body + off, &e->max, sizeof(double)); off += sizeof(double);
            memcpy(body + off, e->value, strlen(e->value) + 1);
            off += strlen(e->value) + 1;
        }
        body_len = off;
    }

    memset(&h, 0, sizeof(h));
    memcpy(h.magic, NXV_MAGIC, 4);
    h.version = 1;
    h.entry_count = 0;
    for (e = g_head; e; e = e->next) h.entry_count++;
    h.body_size = (uint32_t)body_len;
    h.checksum = crc32_of(body, body_len);

    mkdir(VAULT_DIR, 0700);
    mkdir(BACKUP_DIR, 0700);

    /*
     * Back up whatever is there now, before overwriting.
     *
     * The sequence used to be a `static unsigned seq` in this function, which
     * reset to 0 on every daemon restart -- so the first write after a reboot
     * produced vault.0.nxv again and silently ate the previous backup. The
     * number is now derived from what is already on disk: scan for the
     * highest existing index and step past it.
     */
    {
        struct stat st;
        if (stat(path, &st) == 0) {
            unsigned next = next_backup_seq();
            snprintf(bak, sizeof(bak), "%s/vault.%u.nxv", BACKUP_DIR, next);
            rename(path, bak);
            prune_backups();
        }
    }

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { free(body); return -1; }
    if (write(fd, &h, sizeof(h)) != (ssize_t)sizeof(h)) { close(fd); free(body); return -1; }
    if (write(fd, body, body_len) != (ssize_t)body_len) { close(fd); free(body); return -1; }
    close(fd);
    rename(tmp, path);
    free(body);
    (void)f;
    return 0;
}

/* ------------------------------------------------------------ permission */

/*
 * The tier check. Running as root is not sufficient for an immutable key:
 * that is the whole point of the tier, and it is what makes "the boot target
 * cannot be changed from a logged-in desktop" a property rather than a
 * convention.
 */
static int may_write(struct entry *e, uid_t uid)
{
    if (g_maintenance) return 1;
    if (e->tier == NXV_TIER_IMMUTABLE) return 0;
    if (e->tier == NXV_TIER_ADMIN && uid != 0) return 0;
    if (e->tier == NXV_TIER_USER && uid != 0) return 1;
    return 1;
}

static int validate(struct entry *e, const char *value)
{
    char *end;
    double d;

    switch (e->type) {
    case NXV_T_BOOL:
        if (strcmp(value, "true") && strcmp(value, "false")) return NXV_ERR_TYPE;
        return NXV_OK;
    case NXV_T_U32:
        d = strtod(value, &end);
        if (*end) return NXV_ERR_TYPE;
        if (d < e->min || d > e->max) return NXV_ERR_RANGE;
        return NXV_OK;
    case NXV_T_FLOAT:
        d = strtod(value, &end);
        if (*end) return NXV_ERR_TYPE;
        if (d < e->min || d > e->max) return NXV_ERR_RANGE;
        return NXV_OK;
    default:
        return NXV_OK;
    }
}

/* ---------------------------------------------------------------- socket */

static void handle(int c, uid_t peer_uid)
{
    char line[NXV_LINE_MAX];
    char reply[NXV_LINE_MAX];
    ssize_t n;
    char *sp, *path, *val, *rest;
    struct entry *e;

    n = read(c, line, sizeof(line) - 1);
    if (n <= 0) { close(c); return; }
    line[n] = 0;
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;

    /*
     * Line format is "<CMD> <rest>". The command is the text before the
     * first space; everything after it is the argument. Reading the path
     * from the front of the line would make every lookup search for the
     * literal string "GET", which finds nothing and returns NOENT -- the
     * daemon appears to work while every request silently misses.
     */
    sp = strchr(line, ' ');
    if (!sp) { (void)!write(c, "ERR 6\n", 6); close(c); return; }
    *sp = 0;
    rest = sp + 1;

    if (strcmp(line, "GET") == 0) {
        path = rest;
        val = NULL;
    } else if (strcmp(line, "SET") == 0) {
        char *sp2 = strchr(rest, ' ');
        if (!sp2) { (void)!write(c, "ERR 6\n", 6); close(c); return; }
        *sp2 = 0;
        path = rest;
        val = sp2 + 1;
    } else {
        path = NULL;
        val = NULL;
    }

    e = path ? find(path) : NULL;

    if (strcmp(line, "GET") == 0) {
        if (!e) { snprintf(reply, sizeof(reply), "ERR %d\n", NXV_ERR_NOENT); }
        else {
            snprintf(reply, sizeof(reply), "OK %s\n", e->value);
        }
    } else if (strcmp(line, "SET") == 0) {
        int rc;
        if (!e) { rc = NXV_ERR_NOENT; }
        else if (!may_write(e, peer_uid)) { rc = NXV_ERR_PERM; }
        else if ((rc = validate(e, val)) != NXV_OK) { /* rc set */ }
        else {
            free(e->value);
            e->value = strdup(val);
            write_file("/etc/nx/vault/user.nxv");
            rc = NXV_OK;
        }
        if (rc == NXV_OK) snprintf(reply, sizeof(reply), "OK %s\n", val);
        else snprintf(reply, sizeof(reply), "ERR %d\n", rc);
    } else {
        snprintf(reply, sizeof(reply), "ERR %d\n", NXV_ERR_PROTO);
    }

    (void)!write(c, reply, strlen(reply));
    close(c);
}

int main(int argc, char **argv)
{
    int s;
    struct sockaddr_un a;

    g_maintenance = (argc > 1 && strcmp(argv[1], "--maintenance") == 0);

    load_defaults();
    write_file("/etc/nx/vault/user.nxv");

    signal(SIGPIPE, SIG_IGN);

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return 1;

    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    /* Abstract socket: sun_path[0] is NUL and the name follows. Nothing is
     * created on disk, which is what allows init to reach the daemon before
     * any filesystem exists. */
    a.sun_path[0] = 0;
    memcpy(a.sun_path + 1, "nxvault", 7);

    if (bind(s, (struct sockaddr *)&a,
             offsetof(struct sockaddr_un, sun_path) + 8) < 0) {
        perror("bind");
        return 1;
    }
    listen(s, 8);

    printf("nxvaultd: listening on @nxvault%s\n",
           g_maintenance ? " (maintenance)" : "");
    fflush(stdout);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;

        {
            struct ucred cr;
            socklen_t len = sizeof(cr);
            uid_t uid = (uid_t)-1;
            if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cr, &len) == 0)
                uid = cr.uid;
            handle(c, uid);
        }
    }
    return 0;
}
