#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build nxicons.ttf from glyph outlines.

The glyphs are filled shapes rather than stroked lines: a font carries
outlines, not strokes, and turning a stroke into an outline needs path
offsetting that the font library does not do. The visual result is the same
at the sizes these are drawn at.

Codepoints live in the Private Use Area, so they cannot collide with real
text and cannot be typed by accident.
"""

import os
import math
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
class MiniPath:
    """Parse the subset of SVG path syntax these glyphs use: M, L, Z.

    fontTools' SVGPath expects XML rather than a bare `d` string, and the
    arcs it supports are not needed here because every curve is emitted as
    a polygon. Handling only M/L/Z keeps the dependency surface at zero.
    """

    def __init__(self, d):
        self.d = d
        self.cmds = []
        self._parse()

    def _parse(self):
        import re
        for cmd, args in re.findall(r"([MLZmlz])([^MLZmlz]*)", self.d):
            nums = [float(x) for x in re.findall(r"-?[0-9.]+", args or "")]
            self.cmds.append((cmd, nums))

    def draw(self, pen):
        for cmd, nums in self.cmds:
            if cmd in ("M", "m"):
                for i in range(0, len(nums), 2):
                    if i == 0:
                        pen.moveTo((nums[0], nums[1]))
                    else:
                        pen.lineTo((nums[i], nums[i + 1]))
            elif cmd in ("L", "l"):
                for i in range(0, len(nums), 2):
                    pen.lineTo((nums[i], nums[i + 1]))
            elif cmd in ("Z", "z"):
                pen.closePath()

EM = 1024
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "build", "nxicons.ttf")

# Private Use Area, from U+E100.
BASE = 0xE100

ORDER = [
    # traffic light (top-right window buttons)
    "window-minimise",
    "window-maximise",
    "window-close",
    # window modes
    "window-mode-normal",
    "window-mode-borderless",
    "window-mode-fullscreen",
    "window-mode-fs-borderless",
    "window-mode-locked",
    # terminal state
    "term-ok",
    "term-err",
    "term-warn",
    # system
    "volume",
    "volume-mute",
    "settings",
]


def rect(x, y, w, h):
    return "M %d %d L %d %d L %d %d L %d %d Z" % (
        x, y, x + w, y, x + w, y + h, x, y + h)


def poly_circle(cx, cy, r, steps=48, reverse=False):
    """A circle as a polygon.

    The outline language here is M/L/Z only, which keeps the parser small;
    at the sizes these icons are drawn a 48-gon is indistinguishable from a
    true arc.
    """
    import math as m
    pts = []
    for i in range(steps):
        a = m.radians(i * 360.0 / steps) * (-1 if reverse else 1)
        pts.append((cx + r * m.cos(a), cy + r * m.sin(a)))
    return ("M %.1f %.1f " % pts[0]) + " ".join(
        "L %.1f %.1f" % q for q in pts[1:]) + " Z"


def ring(cx, cy, outer, inner):
    """Two concentric circles; the inner one wound the other way so it
    reads as a hole under the non-zero winding rule."""
    return (poly_circle(cx, cy, outer, reverse=False) + " " +
            poly_circle(cx, cy, inner, reverse=True))


def cross(cx, cy, arm, half):
    """An X, as two crossing bars drawn as one path."""
    import math as m
    a = m.radians(45)
    pts = []
    for sx, sy in ((1, 1), (1, -1), (-1, -1), (-1, 1)):
        pass
    # bar along +45 deg
    dx, dy = arm * m.cos(a), arm * m.sin(a)
    px, py = -half * m.sin(a), half * m.cos(a)
    b1 = [(cx - dx + px, cy - dy + py), (cx + dx + px, cy + dy + py),
          (cx + dx - px, cy + dy - py), (cx - dx - px, cy - dy - py)]
    dx2, dy2 = arm * m.cos(-a), arm * m.sin(-a)
    px2, py2 = -half * m.sin(-a), half * m.cos(-a)
    b2 = [(cx - dx2 + px2, cy - dy2 + py2), (cx + dx2 + px2, cy + dy2 + py2),
          (cx + dx2 - px2, cy + dy2 - py2), (cx - dx2 - px2, cy - dy2 - py2)]
    def poly(p):
        return ("M %.1f %.1f " % p[0]) + " ".join(
            "L %.1f %.1f" % q for q in p[1:]) + " Z"
    return poly(b1) + " " + poly(b2)


def check(cx, cy, size):
    return ("M %d %d L %d %d L %d %d L %d %d L %d %d Z"
            % (cx - size, cy,
               cx - size // 3, cy + size // 2,
               cx + size, cy - size // 2,
               cx + size - size // 4, cy - size,
               cx - size // 3, cy - size // 4))


def triangle(cx, cy, r):
    import math as m
    p = []
    for i in range(3):
        a = m.radians(-90 + i * 120)
        p.append((cx + r * m.cos(a), cy + r * m.sin(a)))
    return ("M %.1f %.1f " % p[0]) + " ".join(
        "L %.1f %.1f" % q for q in p[1:]) + " Z"


def gear(cx, cy, hub, outer, teeth=8, tw=90):
    import math as m
    d = []
    for i in range(teeth):
        a0 = m.radians(i * 360 / teeth - tw / 2)
        a1 = m.radians(i * 360 / teeth + tw / 2)
        a2 = m.radians((i + 0.5) * 360 / teeth - tw / 2)
        a3 = m.radians((i + 0.5) * 360 / teeth + tw / 2)
        pts = [(cx + hub * m.cos(a0), cy + hub * m.sin(a0)),
               (cx + outer * m.cos(a1), cy + outer * m.sin(a1)),
               (cx + outer * m.cos(a3), cy + outer * m.sin(a3)),
               (cx + hub * m.cos(a2), cy + hub * m.sin(a2))]
        d.append(("M %.1f %.1f " % pts[0]) +
                 " ".join("L %.1f %.1f" % q for q in pts[1:]) + " Z")
    return " ".join(d) + " " + ring(cx, cy, hub * 0.45, hub * 0.2)


def speaker(cx, cy, s):
    return ("M %d %d L %d %d L %d %d L %d %d L %d %d L %d %d Z"
            % (cx - s, cy - s // 3, cx - s // 3, cy - s // 3,
               cx + s // 3, cy - s,
               cx + s // 3, cy + s,
               cx - s // 3, cy + s // 3, cx - s, cy + s // 3))


def wave(cx, cy, r):
    """A crescent: outer arc forward, inner arc back."""
    import math as m
    outer, inner = [], []
    for i in range(24):
        a = m.radians(-70 + i * 140.0 / 23)
        outer.append((cx + r * m.cos(a), cy + r * m.sin(a)))
    for i in range(24):
        a = m.radians(-70 + (23 - i) * 140.0 / 23)
        inner.append((cx + (r + 60) * m.cos(a), cy + (r + 60) * m.sin(a)))
    pts = outer + inner
    return ("M %.1f %.1f " % pts[0]) + " ".join(
        "L %.1f %.1f" % q for q in pts[1:]) + " Z"


GLYPH_D = {
    "window-minimise": rect(362, 468, 300, 88),
    "window-maximise": ring(512, 512, 212, 124),
    "window-close":    cross(512, 512, 190, 52),
    "window-mode-normal": rect(180, 220, 664, 584),
    "window-mode-borderless": rect(220, 260, 584, 504),
    "window-mode-fullscreen": rect(120, 160, 784, 704),
    "window-mode-fs-borderless": rect(140, 180, 744, 664),
    "window-mode-locked": rect(160, 300, 704, 424),
    "term-ok":   check(512, 512, 200),
    "term-err":  cross(512, 512, 200, 56),
    "term-warn": triangle(512, 500, 300),
    "volume":      speaker(420, 512, 180) + " " + wave(650, 512, 90),
    "volume-mute": speaker(420, 512, 180) + " " + cross(700, 512, 90, 40),
    "settings":  gear(512, 512, 250, 380),
}


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    glyph_order = [".notdef"]
    char_map = {}
    glyphs = {}

    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()

    for i, name in enumerate(ORDER):
        cp = BASE + i
        glyph_order.append(name)
        char_map[cp] = name
        p = TTGlyphPen(None)
        MiniPath(GLYPH_D[name]).draw(p)
        glyphs[name] = p.glyph()
        print("  U+%04X  %s" % (cp, name))

    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(char_map)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({g: (EM, 0) for g in glyph_order})
    fb.setupHorizontalHeader(ascent=EM, descent=0)
    fb.setupNameTable({
        "familyName": "NXIcons",
        "styleName": "Regular",
        "fullName": "NXIcons Regular",
        "psName": "NXIcons-Regular",
        "version": "1.0",
    })
    fb.setupOS2(sTypoAscender=EM, usWinAscent=EM, usWinDescent=0)
    fb.setupPost()
    fb.save(OUT)
    print("font: %s (%d glyphs)" % (OUT, len(ORDER)))


if __name__ == "__main__":
    main()
