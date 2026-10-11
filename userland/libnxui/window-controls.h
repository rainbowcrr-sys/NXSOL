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

/*
 * The buttons sit top-RIGHT, in the order minimise, maximise, close -- that
 * matches Windows, because a migration cost of zero is worth more than
 * being different for its own sake.
 *
 * The *display logic* is a hybrid of macOS and plain minimalism:
 *
 *   macOS    active window  -> full-colour dots always, icons on hover
 *   minimal  active window  -> grey dots, colour only while hovering
 *   NX       active window  -> DESATURATED colour dots, full colour once the
 *                              pointer enters the title bar, icon on the
 *                              button itself
 *
 * So colour is always faintly present (you can tell there are buttons and
 * which is which) but never competes with the window's content.
 *
 * Everything here is driven from NX Vault under /user/interface/window/
 * so a user who wants the plain macOS look, or something quieter, gets it.
 */

#ifndef NXUI_WINDOW_CONTROLS_H
#define NXUI_WINDOW_CONTROLS_H

#include <stdint.h>

/* ------------------------------------------------------------------ modes */

/*
 * Window mode. The user's note asked for these as a matrix, and a matrix is
 * what they are: three independent axes collapsed into one enum because the
 * combinations that do not make sense (fullscreen + resizable border, say)
 * are not worth representing.
 *
 *   normal                 windowed, with a frame
 *   borderless             windowed, no frame
 *   fullscreen             forced fullscreen, frame drawn
 *   fs_borderless          forced fullscreen, no frame
 *   fs_borderless_locked   forced fullscreen, no frame, cannot be dragged
 */
enum nxui_window_mode {
    NXUI_WINDOW_NORMAL = 0,
    NXUI_WINDOW_BORDERLESS,
    NXUI_WINDOW_FULLSCREEN,
    NXUI_WINDOW_FS_BORDERLESS,
    NXUI_WINDOW_FS_BORDERLESS_LOCKED,
    NXUI_WINDOW_MODE_COUNT
};

/* Which of the three axes a mode sets. Kept separate so the composer can
 * reason about them without a lookup table. */
struct nxui_window_traits {
    int fullscreen;      /* covers the whole output            */
    int decorated;       /* draws a title bar / frame          */
    int draggable;       /* the user can move it               */
    int resizable;       /* the user can resize it             */
};

struct nxui_window_traits nxui_window_traits_of(enum nxui_window_mode m);

/* ---------------------------------------------------------------- visuals */

/* Button identity, left to right. */
#define NXUI_BTN_MINIMISE 0
#define NXUI_BTN_MAXIMISE 1
#define NXUI_BTN_CLOSE    2
#define NXUI_BTN_COUNT    3

/* How visible a button currently is. The renderer interpolates between
 * these rather than switching, so the transition is what the user sees. */
enum nxui_btn_phase {
    NXUI_BTN_HIDDEN = 0,   /* not drawn at all                        */
    NXUI_BTN_IDLE,         /* desaturated dot, small                  */
    NXUI_BTN_BAR_HOVER,    /* full colour, larger  (pointer in bar)   */
    NXUI_BTN_FOCUS,        /* full colour, largest, icon shown        */
};

/*
 * How the idle dots get their colour. This is a Vault key
 * (/user/interface/window/controls/color-mode), not a compile-time choice,
 * because the two answers suit different people:
 *
 *   fixed       use the hand-picked pale values (E8DCA0 / A0D8A0 / E8A0A0).
 *               Fast, predictable, and the designer's intent is preserved
 *               exactly.
 *   saturation  compute from the full-strength colours at idle_sat, so the
 *               saturation slider becomes a live control.
 *
 * fixed is the default, which keeps the appearance identical to before this
 * key existed -- anyone who wants the slider to bite opts in.
 */
enum nxui_color_mode {
    NXUI_COLOR_FIXED = 0,
    NXUI_COLOR_SATURATION,
};

/* Sizes in pixels, from Vault. */
#define NXUI_DOT_IDLE_DEFAULT   8
#define NXUI_DOT_HOVER_DEFAULT  10
#define NXUI_DOT_FOCUS_DEFAULT  14

/* Saturation applied to the idle dots. 0.0 is the plain-minimal look,
 * 1.0 is plain macOS. */
#define NXUI_IDLE_SAT_DEFAULT    0.35f
#define NXUI_HOVER_SAT_DEFAULT   1.0f
#define NXUI_INACTIVE_OPACITY_DEFAULT 0.4f

/* Animation lengths, milliseconds. */
#define NXUI_ANIM_ENTER_DEFAULT  150
#define NXUI_ANIM_LEAVE_DEFAULT  200
#define NXUI_ANIM_CLICK_DEFAULT  80

/* ------------------------------------------------------------------ state */

struct nxui_window_controls {
    enum nxui_window_mode mode;
    int      active;            /* 1 = focused window                   */
    int      root;              /* 1 = privileged process               */
    int      pointer_in_bar;    /* pointer anywhere in the title bar    */
    int      hovered_button;    /* -1 or NXUI_BTN_*                     */
    int      pressed_button;    /* -1 or NXUI_BTN_*                     */

    /* From Vault -- see nxui_window_load_config(). */
    int      style_hybrid;      /* 1 = hybrid, 0 = plain macOS          */
    enum nxui_color_mode color_mode;  /* fixed | saturation            */
    float    idle_sat;
    float    hover_sat;
    float    inactive_opacity;
    int      dot_idle;
    int      dot_hover;
    int      dot_focus;
    int      anim_enter;
    int      anim_leave;
    int      root_always_colored;
};

/* Per-button resolved appearance, handed to the renderer. */
struct nxui_btn_view {
    uint32_t colour;        /* 0xAARRGGBB                            */
    int      diameter;      /* px                                    */
    int      show_icon;     /* 1 = draw the glyph inside the dot     */
    float    opacity;       /* 0.0 - 1.0                             */
};

void nxui_window_init(struct nxui_window_controls *c);

/* Pull the appearance values out of NX Vault. Safe to call before nxvaultd
 * is up; the compiled-in defaults stand until it answers. */
void nxui_window_load_config(struct nxui_window_controls *c);

/* Switch mode. Returns 0 on success, negative if the mode is out of range
 * or the window is locked against it. */
int nxui_window_set_mode(struct nxui_window_controls *c,
                         enum nxui_window_mode m);

/* Pointer and focus updates. */
void nxui_window_set_active(struct nxui_window_controls *c, int active);
void nxui_window_set_pointer_in_bar(struct nxui_window_controls *c, int in);
void nxui_window_set_hover(struct nxui_window_controls *c, int button);
void nxui_window_set_pressed(struct nxui_window_controls *c, int button);

/* Resolve what button i should look like right now. */
void nxui_window_button_view(const struct nxui_window_controls *c,
                             int button, struct nxui_btn_view *out);

/* Whether the cluster is drawn at all. Locked fullscreen never draws it --
 * that is the point of the mode, and exiting it needs a key chord. */
int nxui_window_controls_visible(const struct nxui_window_controls *c);

/* Icon glyph for a button, as a codepoint in the NX icon font. */
uint32_t nxui_window_button_icon(int button);

#endif /* NXUI_WINDOW_CONTROLS_H */
