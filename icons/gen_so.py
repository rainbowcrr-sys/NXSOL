#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate libnxicons sources with the glyph geometry compiled in.

Why the geometry is compiled in rather than loaded from nxicons.ttf:

  the icons are part of the window frame. They must not have a
  "failed to load" state -- a window cannot be drawn with a blank gap where
  the close button belongs. A font is a file: it can be missing, corrupt,
  or slower than the first frame. Compiled-in data cannot.

  The .ttf is still generated (gen_font.py) because it is useful for
  documents and for applications that want a font, but nothing in the
  compositor depends on it.

Output: src/glyphs.c  -- the path data, as static arrays
        src/glyphs.h  -- the lookup table
"""

import os
import re
import math

HERE = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(HERE, "svg")
OUT_C = os.path.join(HERE, "src", "glyphs.c")
OUT_H = os.path.join(HERE, "src", "glyphs.h")

GRID = 1024.0
STROKE = 88.0          # the stroke-width every glyph was drawn with

ORDER = [
    "window-minimise", "window-maximise", "window-close",
    "window-mode-normal", "window-mode-borderless",
    "window-mode-fullscreen", "window-mode-fs-borderless",
    "window-mode-locked",
    "term-ok", "term-err", "term-warn",
    "volume", "volume-mute", "settings",
]

# --------------------------------------------------------------- svg arc

def arc_points(x0, y0, rx, ry, phi, large, sweep, x1, y1, steps=16):
    """Sample an SVG elliptical arc into a polyline (endpoint -> endpoint)."""
    if rx == 0 or ry == 0:
        return [(x1, y1)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)

    dx2, dy2 = (x0 - x1) / 2.0, (y0 - y1) / 2.0
    x1p = cp * dx2 + sp * dy2
    y1p = -sp * dx2 + cp * dy2

    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx

    cx = cp * cxp - sp * cyp + (x0 + x1) / 2.0
    cy = sp * cxp + cp * cyp + (y0 + y1) / 2.0

    def ang(ux, uy, vx, vy):
        d = math.sqrt(ux * ux + uy * uy) * math.sqrt(vx * vx + vy * vy)
        c = max(-1.0, min(1.0, (ux * vx + uy * vy) / d)) if d else 1.0
        a = math.acos(c)
        return -a if (ux * vy - uy * vx) < 0 else a

    ux, uy = 1.0, 0.0
    vx, vy = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx2, vy2 = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    th1 = ang(ux, uy, vx, vy)
    dth = ang(vx, vy, vx2, vy2)
    if sweep == 0 and dth > 0:
        dth -= 2 * math.pi
    elif sweep == 1 and dth < 0:
        dth += 2 * math.pi

    pts = []
    for i in range(1, steps + 1):
        th = th1 + dth * i / steps
        ct, st = math.cos(th), math.sin(th)
        px = rx * ct
        py = ry * st
        pts.append((cp * px - sp * py + cx, sp * px + cp * py + cy))
    return pts

# ------------------------------------------------------------ svg parsing

def parse_path(d, cx, cy):
    """Return a list of polylines. Commands seen in these files: M L V Z a."""
    toks = re.findall(r"([MLVZmlvzaA])([^MLVZmlvzaA]*)", d)
    subs = []
    cur = []
    closed = False
    cmd_x, cmd_y = cx, cy
    for cmd, args in toks:
        n = [float(x) for x in re.findall(r"-?[0-9.]+", args or "")]
        up = cmd.upper()
        rel = cmd.islower()
        if up == "M":
            if len(cur) > 1:
                subs.append((cur, closed))
            cur = [(cx + n[0], cy + n[1])] if rel else [(n[0], n[1])]
            cmd_x, cmd_y = cur[-1]
            closed = False
        elif up == "L":
            i = 0
            while i + 1 < len(n):
                p = (cmd_x + n[i], cmd_y + n[i + 1]) if rel else (n[i], n[i + 1])
                cur.append(p)
                cmd_x, cmd_y = p
                i += 2
        elif up == "V":
            for v in n:
                cmd_y = cmd_y + v if rel else v
                cur.append((cmd_x, cmd_y))
        elif up == "Z":
            if len(cur) > 1:
                cur.append(cur[0])
                closed = True
                subs.append((cur, closed))
            cur = []
            closed = False
        elif up == "A":
            i = 0
            while i + 6 < len(n):
                rx, ry, phi, laf, sf = n[i], n[i+1], n[i+2], int(n[i+3]), int(n[i+4])
                ex, ey = n[i+5], n[i+6]
                tx, ty = (cmd_x + ex, cmd_y + ey) if rel else (ex, ey)
                cur.extend(arc_points(cmd_x, cmd_y, rx, ry, phi, laf, sf, tx, ty))
                cmd_x, cmd_y = tx, ty
                i += 7
    if len(cur) > 1:
        subs.append((cur, closed))
    return subs

def load(name):
    s = open(os.path.join(SVG, name + ".svg")).read()
    subs = []
    for m in re.finditer(r"<line\b([^/>]*)/?>", s):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        subs.append(([(float(a["x1"]), float(a["y1"])),
                      (float(a["x2"]), float(a["y2"]))], False))
    for m in re.finditer(r"<rect\b([^/>]*)/?>", s):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        x, y = float(a["x"]), float(a["y"])
        w, h = float(a["width"]), float(a["height"])
        if w <= 0 or h <= 0:
            continue
        rx = float(a.get("rx", 0) or 0)
        ry = float(a.get("ry", rx) or rx)
        if rx <= 0 or ry <= 0:
            subs.append(([(x, y), (x + w, y), (x + w, y + h),
                          (x, y + h), (x, y)], True))
        else:
            # Rounded corners: sample each corner as a quarter arc. Drawing
            # these as sharp corners lost the rounding the glyphs were
            # designed with, which showed up as a measurable difference
            # against the SVG -- and on the larger window-mode glyphs it was
            # the single biggest one.
            rx = min(rx, w / 2.0)
            ry = min(ry, h / 2.0)
            pts = []
            corners = [
                (x + w - rx, y + ry, -90, 0),
                (x + w - rx, y + h - ry, 0, 90),
                (x + rx, y + h - ry, 90, 180),
                (x + rx, y + ry, 180, 270),
            ]
            for ccx, ccy, a0, a1 in corners:
                for i in range(0, 7):
                    t = math.radians(a0 + (a1 - a0) * i / 6.0)
                    pts.append((ccx + rx * math.cos(t), ccy + ry * math.sin(t)))
            pts.append(pts[0])
            subs.append((pts, True))
    for m in re.finditer(r"<circle\b([^/>]*)/?>", s):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        cx, cy, r = float(a["cx"]), float(a["cy"]), float(a["r"])
        pts = [(cx + r * math.cos(2 * math.pi * i / 32),
                cy + r * math.sin(2 * math.pi * i / 32)) for i in range(33)]
        subs.append((pts, True))
    for m in re.finditer(r"<path\b([^/>]*)/?>", s):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        subs.extend(parse_path(a.get("d", ""), 0.0, 0.0))
    return [(p, c) for p, c in subs if len(p) > 1]

# ------------------------------------------------------------------ emit

def fmt(v):
    return ("%.1f" % v).rstrip("0").rstrip(".")

def main():
    glyphs = [(n, load(n)) for n in ORDER]
    L = []
    L.append("/* SPDX-License-Identifier: GPL-2.0-only")
    L.append(" * GENERATED FILE -- do not edit by hand.")
    L.append(" * Regenerate with:  python3 icons/gen_so.py")
    L.append(" *")
    L.append(" * Glyph geometry compiled in. See glyphs.h for why.")
    L.append(" */")
    L.append("")
    L.append('#include "glyphs.h"')
    L.append("")

    for name, subs in glyphs:
        for i, (pts, closed) in enumerate(subs):
            vals = ", ".join(fmt(v) for p in pts for v in p)
            L.append("static const float g_%s_%d[] = {%s};"
                     % (name.replace("-", "_"), i, vals))
        L.append("")
        L.append("static const nxicon_path_t p_%s[] = {"
                 % name.replace("-", "_"))
        for i, (pts, closed) in enumerate(subs):
            L.append("    { g_%s_%d, %d, %d },"
                     % (name.replace("-", "_"), i, len(pts), 1 if closed else 0))
        L.append("};")
        L.append("")

    L.append("const nxicon_glyph_t nxicon_glyphs[] = {")
    for idx, (name, subs) in enumerate(glyphs):
        L.append('    { 0x%04Xu, "%s", %d, p_%s },'
                 % (0xE100 + idx, name, len(subs), name.replace("-", "_")))
    L.append("};")
    L.append("")
    L.append("const int nxicon_glyph_count = %d;" % len(glyphs))
    L.append("")

    os.makedirs(os.path.dirname(OUT_C), exist_ok=True)
    open(OUT_C, "w").write("\n".join(L) + "\n")

    H = []
    H.append("/* SPDX-License-Identifier: GPL-2.0-only")
    H.append(" * GENERATED FILE -- do not edit by hand.")
    H.append(" * Regenerate with:  python3 icons/gen_so.py")
    H.append(" */")
    H.append("#ifndef NXICONS_GLYPHS_H")
    H.append("#define NXICONS_GLYPHS_H")
    H.append("#include <stdint.h>")
    H.append("")
    H.append("/* The grid every glyph is drawn on, and the stroke width used. */")
    H.append("#define NXICON_GLYPH_MAX 32")
    H.append("#define NXICON_GRID   1024.0f")
    H.append("#define NXICON_STROKE 88.0f")
    H.append("")
    H.append("typedef struct { const float *pts; int n; int closed; } nxicon_path_t;")
    H.append("typedef struct {")
    H.append("    uint32_t codepoint;")
    H.append("    const char *name;")
    H.append("    int n_paths;")
    H.append("    const nxicon_path_t *paths;")
    H.append("} nxicon_glyph_t;")
    H.append("")
    H.append("extern const nxicon_glyph_t nxicon_glyphs[];")
    H.append("extern const int nxicon_glyph_count;")
    H.append("#endif")
    open(OUT_H, "w").write("\n".join(H) + "\n")

    total = sum(len(p) for _, subs in glyphs for p, _ in subs)
    print("generated %d glyphs, %d polylines, %d points"
          % (len(glyphs), sum(len(s) for _, s in glyphs), total))
    for n, subs in glyphs:
        print("  %-28s %d path(s)" % (n, len(subs)))

if __name__ == "__main__":
    main()
