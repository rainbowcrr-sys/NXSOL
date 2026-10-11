/* SPDX-License-Identifier: GPL-2.0-only
 *
 * NX System -- window controls ("traffic light") and window modes
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

#include "window-controls.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "../../vault/libnxvault.h"

/* Button colours. The full-saturation row is macOS's; the idle row is that
 * same hue with the saturation dialled back, which is what makes an active
 * window read as "there are buttons" without shouting. */
#define NX_CLOSE_FULL  0xFFFF5F57u
#define NX_MIN_FULL    0xFFFFBD2Fu
#define NX_MAX_FULL    0xFF28C840u

#define NX_CLOSE_IDLE  0xFFE8A0A0u
#define NX_MIN_IDLE    0xFFE8DCA0u
#define NX_MAX_IDLE    0xFFA0D8A0u

/* Privileged windows: close stays red, the other two go orange, and the
 * cluster is never allowed to fade out. */
#define NX_ROOT_CLOSE  0xFFE0453Fu
#define NX_ROOT_OTHER  0xFFE8933Cu

#define NX_GREY_DOT    0xFF888888u

/* Icon font codepoints (NX icon font, private use area). */
#define NX_ICON_MINIMISE 0xE010u
#define NX_ICON_MAXIMISE 0xE011u
#define NX_ICON_CLOSE    0xE012u

uint32_t nxui_window_button_icon(int button)
{
    switch (button) {
    case NXUI_BTN_MINIMISE: return NX_ICON_MINIMISE;
    case NXUI_BTN_MAXIMISE: return NX_ICON_MAXIMISE;
    case NXUI_BTN_CLOSE:    return NX_ICON_CLOSE;
    default:                 return 0;
    }
}

struct nxui_window_traits nxui_window_traits_of(enum nxui_window_mode m)
{
    struct nxui_window_traits t;

    memset(&t, 0, sizeof(t));

    switch (m) {
    case NXUI_WINDOW_NORMAL:
        t.draggable = 1; t.resizable = 1; t.decorated = 1;
        break;
    case NXUI_WINDOW_BORDERLESS:
        t.draggable = 1; t.resizable = 1;
        break;
    case NXUI_WINDOW_FULLSCREEN:
        t.fullscreen = 1; t.decorated = 1;
        break;
    case NXUI_WINDOW_FS_BORDERLESS:
        t.fullscreen = 1;
        break;
    case NXUI_WINDOW_FS_BORDERLESS_LOCKED:
        t.fullscreen = 1;
        break;
    default:
        break;
    }
    return t;
}

void nxui_window_init(struct nxui_window_controls *c)
{
    memset(c, 0, sizeof(*c));

    c->mode              = NXUI_WINDOW_NORMAL;
    c->active            = 1;
    c->hovered_button    = -1;
    c->pressed_button    = -1;

    /* Defaults, standing in until nxvaultd answers. */
    c->style_hybrid        = 1;
    c->idle_sat            = NXUI_IDLE_SAT_DEFAULT;
    c->hover_sat           = NXUI_HOVER_SAT_DEFAULT;
    c->inactive_opacity    = NXUI_INACTIVE_OPACITY_DEFAULT;
    c->dot_idle            = NXUI_DOT_IDLE_DEFAULT;
    c->dot_hover           = NXUI_DOT_HOVER_DEFAULT;
    c->dot_focus           = NXUI_DOT_FOCUS_DEFAULT;
    c->anim_enter          = NXUI_ANIM_ENTER_DEFAULT;
    c->anim_leave          = NXUI_ANIM_LEAVE_DEFAULT;
    c->root_always_colored = 1;
    c->color_mode          = NXUI_COLOR_FIXED;
}

void nxui_window_load_config(struct nxui_window_controls *c)
{
    /*
     * Reads, all under /user/interface/window/:
     *
     *   controls/style              enum   macos | minimal | hybrid
     *   controls/idle-saturation    float  0.0 .. 1.0
     *   controls/hover-saturation   float  0.0 .. 1.0
     *   controls/inactive-opacity   float  0.0 .. 1.0
     *   controls/dot-idle-px        u32
     *   controls/dot-hover-px       u32
     *   controls/dot-focus-px       u32
     *   controls/anim-enter-ms      u32
     *   controls/anim-leave-ms      u32
     *   root/always-colored         bool
     *
     * nxvaultd clamps on write, so what arrives is already in range; the
     * clamps below only matter for a hand-built tree.
     */
    /*
     * Actually ask the daemon, key by key.
     *
     * This was a TODO, which meant every value below stayed at its
     * compiled-in default forever and the appearance was never driven by
     * the Vault at all -- despite the header saying it was. Reading them
     * is what makes the keys mean anything.
     *
     * Best-effort: if nxvaultd is not up yet (early boot) or unreachable,
     * each key keeps its default. Nothing here can fail the caller.
     */
    {
        char raw[64];

        if (nxvault_get("/user/interface/window/controls/style",
                        raw, sizeof(raw)) == NXV_OK) {
            if (strcmp(raw, "macos") == 0)   { c->style_hybrid = 0; }
            else if (strcmp(raw, "hybrid") == 0)  { c->style_hybrid = 1; }
            else if (strcmp(raw, "minimal") == 0) { c->style_hybrid = 1; }
        }

        if (nxvault_get("/user/interface/window/controls/color-mode",
                        raw, sizeof(raw)) == NXV_OK) {
            if (strcmp(raw, "saturation") == 0) c->color_mode = NXUI_COLOR_SATURATION;
            else                                 c->color_mode = NXUI_COLOR_FIXED;
        }

#define GETF(key, field)                                              \
        if (nxvault_get(key, raw, sizeof(raw)) == NXV_OK)             \
            c->field = (float)atof(raw);
#define GETU(key, field)                                              \
        if (nxvault_get(key, raw, sizeof(raw)) == NXV_OK)             \
            c->field = (int)strtol(raw, NULL, 10);

        GETF("/user/interface/window/controls/idle-saturation",    idle_sat)
        GETF("/user/interface/window/controls/hover-saturation",   hover_sat)
        GETF("/user/interface/window/controls/inactive-opacity",   inactive_opacity)
        GETU("/user/interface/window/controls/dot-idle-px",        dot_idle)
        GETU("/user/interface/window/controls/dot-hover-px",       dot_hover)
        GETU("/user/interface/window/controls/dot-focus-px",       dot_focus)
        GETU("/user/interface/window/controls/anim-enter-ms",      anim_enter)
        GETU("/user/interface/window/controls/anim-leave-ms",      anim_leave)
#undef GETF
#undef GETU

        if (nxvault_get("/user/interface/window/root/always-colored",
                        raw, sizeof(raw)) == NXV_OK)
            c->root_always_colored = (strcmp(raw, "true") == 0) ? 1 : 0;
    }

    if (c->idle_sat < 0.0f)  c->idle_sat = 0.0f;
    if (c->idle_sat > 1.0f)  c->idle_sat = 1.0f;
    if (c->hover_sat < 0.0f) c->hover_sat = 0.0f;
    if (c->hover_sat > 1.0f) c->hover_sat = 1.0f;
    if (c->inactive_opacity < 0.0f) c->inactive_opacity = 0.0f;
    if (c->inactive_opacity > 1.0f) c->inactive_opacity = 1.0f;

    if (c->dot_idle <= 0)  c->dot_idle = NXUI_DOT_IDLE_DEFAULT;
    if (c->dot_hover <= 0) c->dot_hover = NXUI_DOT_HOVER_DEFAULT;
    if (c->dot_focus <= 0) c->dot_focus = NXUI_DOT_FOCUS_DEFAULT;
}

int nxui_window_set_mode(struct nxui_window_controls *c,
                         enum nxui_window_mode m)
{
    if (!c) return -1;
    if ((int)m < 0 || (int)m >= NXUI_WINDOW_MODE_COUNT) return -1;

    /* Nothing may drag a locked window out of its mode except the
     * privileged exit path -- that is what "locked" means. */
    if (c->mode == NXUI_WINDOW_FS_BORDERLESS_LOCKED &&
        m != NXUI_WINDOW_FS_BORDERLESS_LOCKED)
        return -2;

    c->mode = m;
    return 0;
}

void nxui_window_set_active(struct nxui_window_controls *c, int active)
{
    if (c) c->active = active ? 1 : 0;
}

void nxui_window_set_pointer_in_bar(struct nxui_window_controls *c, int in)
{
    if (!c) return;
    c->pointer_in_bar = in ? 1 : 0;
    if (!in) c->hovered_button = -1;
}

void nxui_window_set_hover(struct nxui_window_controls *c, int button)
{
    if (!c) return;
    if (button < 0 || button >= NXUI_BTN_COUNT) { c->hovered_button = -1; return; }
    c->hovered_button = button;
}

void nxui_window_set_pressed(struct nxui_window_controls *c, int button)
{
    if (!c) return;
    if (button < 0 || button >= NXUI_BTN_COUNT) { c->pressed_button = -1; return; }
    c->pressed_button = button;
}

/* Blend a colour towards grey. saturation 0 gives pure grey, 1 gives the
 * original hue; the NX hybrid look lives in between. */
static uint32_t desaturate(uint32_t rgb, float saturation)
{
    float r = (float)((rgb >> 16) & 0xFF);
    float g = (float)((rgb >> 8) & 0xFF);
    float b = (float)(rgb & 0xFF);
    float grey = 0.299f * r + 0.587f * g + 0.114f * b;

    r = grey + (r - grey) * saturation;
    g = grey + (g - grey) * saturation;
    b = grey + (b - grey) * saturation;

    if (r < 0.0f)   r = 0.0f;
    if (r > 255.0f) r = 255.0f;
    if (g < 0.0f)   g = 0.0f;
    if (g > 255.0f) g = 255.0f;
    if (b < 0.0f)   b = 0.0f;
    if (b > 255.0f) b = 255.0f;

    return 0xFF000000u |
           ((uint32_t)(r + 0.5f) << 16) |
           ((uint32_t)(g + 0.5f) << 8)  |
           ((uint32_t)(b + 0.5f));
}

int nxui_window_controls_visible(const struct nxui_window_controls *c)
{
    if (!c) return 0;

    /* Locked fullscreen has no visible controls by definition; leaving it
     * is a key chord, not a click. */
    if (c->mode == NXUI_WINDOW_FS_BORDERLESS_LOCKED) return 0;

    /* Borderless and fullscreen hide the cluster until the pointer reaches
     * the top edge. */
    if (c->mode == NXUI_WINDOW_BORDERLESS ||
        c->mode == NXUI_WINDOW_FULLSCREEN ||
        c->mode == NXUI_WINDOW_FS_BORDERLESS) {
        return c->pointer_in_bar ? 1 : 0;
    }

    return 1;
}

void nxui_window_button_view(const struct nxui_window_controls *c,
                             int button, struct nxui_btn_view *out)
{
    uint32_t full;
    float sat;
    int diameter;

    if (!c || !out || button < 0 || button >= NXUI_BTN_COUNT) return;

    memset(out, 0, sizeof(*out));
    out->opacity = 1.0f;
    out->show_icon = 0;

    if (c->root) {
        /* Privileged windows: always fully coloured, never faded. */
        full = (button == NXUI_BTN_CLOSE) ? NX_ROOT_CLOSE : NX_ROOT_OTHER;
        sat  = 1.0f;
        diameter = (button == c->hovered_button) ? c->dot_focus : c->dot_hover;
        if (c->root_always_colored) { /* nothing further to do */ }
    } else {
        switch (button) {
        case NXUI_BTN_CLOSE:    full = NX_CLOSE_FULL; break;
        case NXUI_BTN_MINIMISE: full = NX_MIN_FULL;   break;
        default:                full = NX_MAX_FULL;   break;
        }
        sat = c->pointer_in_bar ? c->hover_sat : c->idle_sat;
    }

    if (!c->active) {
        /* Unfocused windows back off entirely -- grey, small, faded. */
        out->colour   = NX_GREY_DOT;
        out->diameter = (button == c->hovered_button && c->pointer_in_bar)
                        ? c->dot_focus : c->dot_idle;
        out->opacity  = c->inactive_opacity;
        out->show_icon = (button == c->hovered_button && c->pointer_in_bar);
        return;
    }

    if (button == c->hovered_button && c->pointer_in_bar) {
        diameter = c->dot_focus;
        out->show_icon = 1;
        if (c->style_hybrid) sat = c->hover_sat;
    } else if (c->pointer_in_bar) {
        diameter = c->dot_hover;
    } else {
        diameter = c->dot_idle;
    }

    /* A privileged window's buttons must not shrink to the idle size: the
     * whole point is that you keep noticing them. */
    if (c->root && diameter < c->dot_hover) diameter = c->dot_hover;

    /* Pressing squashes briefly; the spring back is the compositor's job. */
    if (button == c->pressed_button) {
        diameter = (int)((float)diameter * 0.8f + 0.5f);
        if (diameter < 1) diameter = 1;
    }

    out->colour   = desaturate(full, sat);
    out->diameter = diameter;

    /* In hybrid mode the idle dots start from the pre-mixed pale colours and
     * are only further dialled towards grey, so the default reads as "a red
     * dot, a yellow dot, a green dot" rather than three identical greys. */
    /*
     * Idle colour, when the pointer is not in the title bar.
     *
     * Two answers, chosen by /user/interface/window/controls/color-mode:
     *
     *   fixed        the hand-picked pale values, exactly as designed.
     *
     *   saturation   computed from the full-strength colour at idle_sat, so
     *                the saturation key becomes a live slider.
     *
     * The old code divided by idle_sat to normalise, which cancelled itself
     * out: with sat == idle_sat it always evaluated to desaturate(pale, 1.0)
     * == pale, so moving the saturation slider changed nothing at all. The
     * two modes are now genuinely different code paths.
     */
    if (!c->root && !c->pointer_in_bar) {
        if (c->color_mode == NXUI_COLOR_SATURATION) {
            out->colour = desaturate(full, c->idle_sat);
        } else {
            uint32_t pale = (button == NXUI_BTN_CLOSE)    ? NX_CLOSE_IDLE
                          : (button == NXUI_BTN_MINIMISE) ? NX_MIN_IDLE
                          : NX_MAX_IDLE;
            out->colour = pale;
        }
    }
}
