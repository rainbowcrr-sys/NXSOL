/* SPDX-License-Identifier: GPL-2.0-only
 * NX System On Linux -- NXIcons
 * Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
 *
 * Icons are compiled into this library as path data, not loaded from a font
 * file. See glyphs.h for the reason; the short version is that a window
 * frame must not have a "the icon did not load" state.
 */

#ifndef NXICONS_H
#define NXICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private Use Area base -- cannot collide with real text. */
#define NXICONS_BASE 0xE100u

/* Traffic light: window buttons, top right. */
#define NXICON_WINDOW_MINIMISE (NXICONS_BASE + 0)
#define NXICON_WINDOW_MAXIMISE (NXICONS_BASE + 1)
#define NXICON_WINDOW_CLOSE    (NXICONS_BASE + 2)

/* Window modes. */
#define NXICON_MODE_NORMAL        (NXICONS_BASE + 3)
#define NXICON_MODE_BORDERLESS    (NXICONS_BASE + 4)
#define NXICON_MODE_FULLSCREEN    (NXICONS_BASE + 5)
#define NXICON_MODE_FS_BORDERLESS (NXICONS_BASE + 6)
#define NXICON_MODE_LOCKED        (NXICONS_BASE + 7)

/* Terminal state. */
#define NXICON_TERM_OK   (NXICONS_BASE + 8)
#define NXICON_TERM_ERR  (NXICONS_BASE + 9)
#define NXICON_TERM_WARN (NXICONS_BASE + 10)

/* System. */
#define NXICON_VOLUME      (NXICONS_BASE + 11)
#define NXICON_VOLUME_MUTE (NXICONS_BASE + 12)
#define NXICON_SETTINGS    (NXICONS_BASE + 13)

/* Which module carries a glyph. The set can be split at build time so a
 * compositor that only draws window controls does not also carry the system
 * icons -- see NXICONS_MODULE in nxicons.c. */
#define NXICONS_MOD_WINDOW 0
#define NXICONS_MOD_SYSTEM 1

typedef struct {
    const char *name;
    uint32_t    codepoint;
    int         n_paths;
    const void *paths;      /* const nxicon_path_t * -- see glyphs.h */
} nxicon_info_t;

/* Look a glyph up. by_name() takes "window-close"; by_codepoint() takes a
 * NXICON_* value. Both return NULL if this module does not carry it. */
const nxicon_info_t *nxicons_by_name(const char *name);
const nxicon_info_t *nxicons_by_codepoint(uint32_t cp);

/* Number of glyphs this module carries, and a pointer to the table. */
int  nxicons_count(void);
const nxicon_info_t *nxicons_table(void);

/* Path data for a glyph. Returns NULL if not carried here. */
const void *nxicons_paths(const nxicon_info_t *info, int *out_n_paths);

#ifdef __cplusplus
}
#endif

#endif /* NXICONS_H */
