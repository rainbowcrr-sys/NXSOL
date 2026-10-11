/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NX init, milestone 2
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
 * NX init -- milestone 2.
 *
 * PID 1. From here on the boot depends on NX Vault: the machine does not
 * decide what to start until it has read /system/boot/target out of the
 * vault. That is deliberate rather than incidental -- the boot target is a
 * vault key, so "what happens at boot" is governed by the same signed,
 * permission-checked, backed-up store as everything else, and cannot be
 * changed from a logged-in desktop.
 *
 * Order:
 *   1. mount /proc, /sys, /dev        -- the minimum to do anything
 *   2. start nxvaultd                 -- the vault owns the boot decision
 *   3. read /system/boot/target       -- must succeed, or the boot stops
 *   4. announce, then wait
 *
 * If the vault cannot be reached, init stops with a message rather than
 * guessing. A system that boots without its configuration is worse than one
 * that says it could not.
 */

#define _GNU_SOURCE

#include "../vault/libnxvault.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void out(const char *s)
{
    ssize_t r = write(1, s, strlen(s));
    (void)r;
}

static void outln(const char *s)
{
    char b[512];
    snprintf(b, sizeof(b), "%s\n", s);
    out(b);
}

static void mount_minimal(void)
{
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mkdir("/tmp", 0755);

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        outln("nxinit: warning: /proc not mounted");
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) != 0)
        outln("nxinit: warning: /sys not mounted");
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0)
        outln("nxinit: warning: /dev not mounted");
}

/* Start nxvaultd and wait for its socket to answer. */
static int start_vault(void)
{
    pid_t p;
    int i;

    p = fork();
    if (p == 0) {
        /* Child: become the daemon. exec rather than returning, so the
         * process image is nxvaultd's and PID 1 stays the parent. */
        execl("/nxvaultd", "nxvaultd", (char *)NULL);
        _exit(127);
    }
    if (p < 0) return -1;

    /* Poll: the daemon needs a moment to bind. */
    for (i = 0; i < 100; i++) {
        char v[256];
        if (nxvault_get("/system/boot/target", v, sizeof(v)) == NXV_OK)
            return 0;
        usleep(50000);
    }
    return -1;
}

int main(void)
{
    struct utsname u;
    char target[128];
    char line[NXV_LINE_MAX];
    int rc;

    outln("Hello NXSOL!");

    if (uname(&u) == 0) {
        snprintf(line, sizeof(line), "Linux %s", u.release);
        outln(line);
    } else {
        outln("Linux (version unavailable)");
    }

    mount_minimal();

    out("nxinit: starting NX Vault ... ");
    if (start_vault() != 0) {
        outln("failed");
        outln("nxinit: NX Vault is required to boot; halting.");
        for (;;) pause();
    }
    outln("ok");

    rc = nxvault_get("/system/boot/target", target, sizeof(target));
    if (rc != NXV_OK) {
        snprintf(line, sizeof(line),
                 "nxinit: cannot read boot target: %s",
                 nxvault_strerror(rc));
        outln(line);
        outln("nxinit: halting.");
        for (;;) pause();
    }

    snprintf(line, sizeof(line), "NX Vault: target = %s", target);
    outln(line);

    /* What running as root cannot change: the boot target. */
    rc = nxvault_set("/system/boot/target", "rescue");
    if (rc == NXV_OK) {
        outln("nxinit: boot target was writable -- this is wrong");
    } else {
        outln("nxinit: boot target is immutable (even for root)");
    }

    outln("nxinit: milestone 2 reached, staying resident");

    for (;;)
        pause();

    return 0;
}
