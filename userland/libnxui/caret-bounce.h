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

/*
 * Caret bounce: the character you just typed lands slightly oversized and
 * slightly high, then springs back into place.
 *
 * This is the shared implementation every NX control calls -- NXTerm, the
 * text field in NXExplorer's rename box, the search box in nxvault, and any
 * application built on libnxui. Third-party software does not get it unless
 * it links libnxui and opts in.
 *
 * The strength comes from NX Vault:
 *
 *     /user/interface/typing/caret-bounce
 *     /user/interface/typing/caret-bounce-duration
 *
 * strength 0 means the effect is off and draw_char() is called directly --
 * there is no animation cost at all at the default setting.
 */

#ifndef NXUI_CARET_BOUNCE_H
#define NXUI_CARET_BOUNCE_H

#include <stdint.h>

/* Defaults, used when Vault has no value or cannot be reached. */
#define NXUI_BOUNCE_DEFAULT_STRENGTH 0.0f
#define NXUI_BOUNCE_DEFAULT_DURATION 150   /* ms */

/* Hard ceiling. Vault refuses anything above this, because past it the text
 * is genuinely unreadable -- that range exists to be played with, not to be
 * used. */
#define NXUI_BOUNCE_MAX_STRENGTH 1.0f

/* One animating character. A control keeps a small ring of these, one per
 * character typed within the last `duration` ms. */
struct nxui_bounce {
    uint32_t codepoint;     /* what to draw                        */
    int      col;           /* cell column                         */
    int      row;           /* cell row                            */
    uint64_t start_ms;      /* when it was typed                   */
    int      active;        /* 0 = slot free                       */
};

#define NXUI_BOUNCE_SLOTS 32

struct nxui_bounce_state {
    struct nxui_bounce slots[NXUI_BOUNCE_SLOTS];
    float    strength;      /* 0.0 = off                           */
    uint32_t duration_ms;   /* animation length                    */
    uint64_t now_ms;        /* refreshed once per frame            */
};

void nxui_bounce_init(struct nxui_bounce_state *s);

/* Pull strength/duration out of NX Vault. Safe to call at any time; if the
 * daemon is unreachable it keeps the compiled-in defaults. */
void nxui_bounce_load_config(struct nxui_bounce_state *s);

/* Register a freshly typed character so the next frame animates it. */
void nxui_bounce_push(struct nxui_bounce_state *s, uint32_t codepoint,
                      int col, int row, uint64_t now_ms);

/* Advance the clock. Call once per frame before drawing. */
void nxui_bounce_tick(struct nxui_bounce_state *s, uint64_t now_ms);

/* Per-character transform for the current frame.
 * Returns 1 if the character is still animating (caller must scale/offset
 * the glyph), 0 if it has settled (caller draws it normally). */
int nxui_bounce_sample(const struct nxui_bounce_state *s,
                       int col, int row,
                       float *out_scale, float *out_dy);

/* The spring curve, exposed so the compositor and NXTerm agree exactly. */
float nxui_bounce_curve(float t);

#endif /* NXUI_CARET_BOUNCE_H */
