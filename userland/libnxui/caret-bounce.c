/* SPDX-License-Identifier: GPL-2.0-only
 *
 * NX System -- caret bounce
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

#include "caret-bounce.h"
#include "../../vault/libnxvault.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

/*
 * The curve: a damped oscillation that starts at 1, overshoots below 1,
 * and settles back to 1. At t=0 it is 1 (full displacement) and at t=1 it
 * is ~1 (settled). Sampling it at the character's age gives the amount of
 * "not yet in place" left at this instant.
 */
float nxui_bounce_curve(float t)
{
    if (t <= 0.0f) return 1.0f;
    if (t >= 1.0f) return 0.0f;

    /* exp decay * cos = one overshoot, then quiet. */
    return (float)(exp((double)(-t * 5.0)) * cos((double)(t * 11.0)));
}

void nxui_bounce_init(struct nxui_bounce_state *s)
{
    memset(s, 0, sizeof(*s));
    s->strength    = NXUI_BOUNCE_DEFAULT_STRENGTH;
    s->duration_ms = NXUI_BOUNCE_DEFAULT_DURATION;
}

void nxui_bounce_load_config(struct nxui_bounce_state *s)
{
    /*
     * Read the two keys from NX Vault over IPC:
     *
     *     /user/interface/typing/caret-bounce          float 0.0 .. 1.0
     *     /user/interface/typing/caret-bounce-duration uint32 60 .. 600
     *
     * nxvaultd clamps on write, so anything arriving from here is already
     * in range; the clamps below only matter for a hand-built tree.
     */
    float    strength  = NXUI_BOUNCE_DEFAULT_STRENGTH;
    uint32_t duration  = NXUI_BOUNCE_DEFAULT_DURATION;
    char raw[64];

    /*
     * Actually ask the daemon.
     *
     * This used to be a TODO: the defaults stood and nothing was ever read,
     * so setting /user/interface/typing/caret-bounce to 0.5 in the Vault had
     * no effect at all -- the effect stayed off no matter what the user
     * asked for. The keys only became real once something read them.
     *
     * A failure here is not an error: before nxvaultd is up (early boot) or
     * if it is unreachable, the compiled-in defaults stand. That is why the
     * read is best-effort rather than fatal.
     */
    if (nxvault_get("/user/interface/typing/caret-bounce",
                    raw, sizeof(raw)) == NXV_OK)
        strength = (float)atof(raw);

    if (nxvault_get("/user/interface/typing/caret-bounce-duration",
                    raw, sizeof(raw)) == NXV_OK)
        duration = (uint32_t)strtoul(raw, NULL, 10);

    if (strength < 0.0f)                strength = 0.0f;
    if (strength > NXUI_BOUNCE_MAX_STRENGTH)
                                        strength = NXUI_BOUNCE_MAX_STRENGTH;
    if (duration < 60)                  duration = 60;
    if (duration > 600)                 duration = 600;

    s->strength    = strength;
    s->duration_ms = duration;
}

void nxui_bounce_tick(struct nxui_bounce_state *s, uint64_t now_ms)
{
    int i;

    s->now_ms = now_ms;

    if (s->strength <= 0.0f) {
        /* Off: make sure no stale animation survives a config change. */
        for (i = 0; i < NXUI_BOUNCE_SLOTS; i++) s->slots[i].active = 0;
        return;
    }

    for (i = 0; i < NXUI_BOUNCE_SLOTS; i++) {
        struct nxui_bounce *b = &s->slots[i];
        if (!b->active) continue;
        if (now_ms - b->start_ms >= s->duration_ms) b->active = 0;
    }
}

void nxui_bounce_push(struct nxui_bounce_state *s, uint32_t codepoint,
                      int col, int row, uint64_t now_ms)
{
    int i, slot = -1;

    if (s->strength <= 0.0f) return;    /* off: nothing is recorded */

    for (i = 0; i < NXUI_BOUNCE_SLOTS; i++) {
        if (!s->slots[i].active)          { slot = i; break; }
        if (slot < 0)                       slot = i;
    }

    /* With a full ring we recycle the oldest rather than dropping the
     * newest -- the newest is the one the user is looking at. */
    if (slot < 0 || s->slots[slot].active) {
        uint64_t oldest = now_ms;
        slot = 0;
        for (i = 0; i < NXUI_BOUNCE_SLOTS; i++) {
            if (s->slots[i].start_ms < oldest) {
                oldest = s->slots[i].start_ms;
                slot = i;
            }
        }
    }

    s->slots[slot].codepoint = codepoint;
    s->slots[slot].col       = col;
    s->slots[slot].row       = row;
    s->slots[slot].start_ms  = now_ms;
    s->slots[slot].active    = 1;
}

int nxui_bounce_sample(const struct nxui_bounce_state *s,
                       int col, int row,
                       float *out_scale, float *out_dy)
{
    int i;

    if (s->strength <= 0.0f) return 0;

    for (i = 0; i < NXUI_BOUNCE_SLOTS; i++) {
        const struct nxui_bounce *b = &s->slots[i];
        float t, k;

        if (!b->active) continue;
        if (b->col != col || b->row != row) continue;

        t = (float)(s->now_ms - b->start_ms) / (float)s->duration_ms;
        if (t >= 1.0f) return 0;

        k = nxui_bounce_curve(t);

        /* At strength 1 the glyph starts 34% large and 5px high. Scaled by
         * the user's setting, so 0.3 is a barely-there "alive" feeling. */
        if (out_scale) *out_scale = 1.0f + s->strength * 0.34f * k;
        if (out_dy)    *out_dy    = -s->strength * 5.0f * k;
        return 1;
    }

    return 0;
}
