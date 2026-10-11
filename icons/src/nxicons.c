/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NXIcons, compiled-in glyph geometry
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 *
 * Nothing here opens a file. The glyph outlines are static arrays in
 * glyphs.c, generated from the SVG sources by icons/gen_so.py.
 *
 * Build variants:
 *
 *   -DNXICONS_MODULE=0   window controls and window modes (default)
 *   -DNXICONS_MODULE=1   terminal state and system icons
 *   -DNXICONS_MODULE_ALL carry everything
 *
 * The split exists so a binary that only draws a title bar does not also
 * carry the settings gear. It is a link-time decision, not a runtime one.
 */

#include "nxicons.h"
#include "glyphs.h"
#include <string.h>

#ifndef NXICONS_MODULE
#define NXICONS_MODULE NXICONS_MOD_WINDOW
#endif

/* Which module a glyph belongs to, by codepoint range. The ranges follow the
 * ORDER list in gen_so.py; if that list is reordered this must follow. */
/* static inline rather than plain static: under NXICONS_MODULE_ALL nothing
 * calls this, and GCC does not warn about an unused static inline -- which
 * is exactly the case the attribute above was trying (and failing) to
 * express. */
static inline int module_of(uint32_t cp)
{
    if (cp >= NXICON_WINDOW_MINIMISE && cp <= NXICON_MODE_LOCKED)
        return NXICONS_MOD_WINDOW;
    return NXICONS_MOD_SYSTEM;
}

static int carries(uint32_t cp)
{
#ifdef NXICONS_MODULE_ALL
    (void)cp;
    return 1;
#else
    return module_of(cp) == NXICONS_MODULE;
#endif
}

/*
 * The public table. It is built from the generated one rather than cast to
 * it: glyphs.h and nxicons.h describe the same shape but are separate types,
 * and a cast between them would be UB-adjacent and would also drag the
 * generated header into every consumer's include path.
 */
static nxicon_info_t g_info[NXICON_GLYPH_MAX];
static int g_info_built;

static void build(void)
{
    int i;
    for (i = 0; i < nxicon_glyph_count && i < NXICON_GLYPH_MAX; i++) {
        g_info[i].name       = nxicon_glyphs[i].name;
        g_info[i].codepoint  = nxicon_glyphs[i].codepoint;
        g_info[i].n_paths    = nxicon_glyphs[i].n_paths;
        g_info[i].paths      = nxicon_glyphs[i].paths;
    }
    g_info_built = 1;
}

static const nxicon_info_t *table(void)
{
    if (!g_info_built) build();
    return g_info;
}

const nxicon_info_t *nxicons_by_codepoint(uint32_t cp)
{
    int i;
    const nxicon_info_t *t = table();
    for (i = 0; i < nxicon_glyph_count && i < NXICON_GLYPH_MAX; i++)
        if (t[i].codepoint == cp)
            return carries(cp) ? &t[i] : NULL;
    return NULL;
}

const nxicon_info_t *nxicons_by_name(const char *name)
{
    int i;
    const nxicon_info_t *t;
    if (!name) return NULL;
    t = table();
    for (i = 0; i < nxicon_glyph_count && i < NXICON_GLYPH_MAX; i++)
        if (t[i].name && strcmp(t[i].name, name) == 0)
            return carries(t[i].codepoint) ? &t[i] : NULL;
    return NULL;
}

int nxicons_count(void)
{
    int i, n = 0;
    const nxicon_info_t *t = table();
    for (i = 0; i < nxicon_glyph_count && i < NXICON_GLYPH_MAX; i++)
        if (carries(t[i].codepoint)) n++;
    return n;
}

const nxicon_info_t *nxicons_table(void)
{
    return table();
}

const void *nxicons_paths(const nxicon_info_t *info, int *out_n_paths)
{
    if (!info) return NULL;
    if (out_n_paths) *out_n_paths = info->n_paths;
    return info->paths;
}
