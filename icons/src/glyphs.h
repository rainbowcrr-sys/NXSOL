/* SPDX-License-Identifier: GPL-2.0-only
 * GENERATED FILE -- do not edit by hand.
 * Regenerate with:  python3 icons/gen_so.py
 */
#ifndef NXICONS_GLYPHS_H
#define NXICONS_GLYPHS_H
#include <stdint.h>

/* The grid every glyph is drawn on, and the stroke width used. */
#define NXICON_GLYPH_MAX 32
#define NXICON_GRID   1024.0f
#define NXICON_STROKE 88.0f

typedef struct { const float *pts; int n; int closed; } nxicon_path_t;
typedef struct {
    uint32_t codepoint;
    const char *name;
    int n_paths;
    const nxicon_path_t *paths;
} nxicon_glyph_t;

extern const nxicon_glyph_t nxicon_glyphs[];
extern const int nxicon_glyph_count;
#endif
