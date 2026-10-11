#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Draw the NXIcons glyphs as SVG.

No emoji anywhere. Each glyph is a small geometric figure on a 1024-unit
grid with a uniform stroke, so the whole set reads as one family and the
colour comes from the renderer rather than the file.
"""

import os

EM = 1024
C = 512          # centre
S = 88           # stroke width -- identical for every glyph
R = 300          # nominal radius

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "svg")


def svg(name, body):
    p = os.path.join(OUT, name + ".svg")
    with open(p, "w") as f:
        f.write(
            '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
            'viewBox="0 0 %d %d">\n'
            '  <g fill="none" stroke="#000" stroke-width="%d" '
            'stroke-linecap="round" stroke-linejoin="round">\n'
            '%s\n  </g>\n</svg>\n' % (EM, EM, EM, EM, S, body))
    return p


def line(x1, y1, x2, y2):
    return '    <line x1="%d" y1="%d" x2="%d" y2="%d"/>' % (x1, y1, x2, y2)


def rect(x, y, w, h, r=0):
    return ('    <rect x="%d" y="%d" width="%d" height="%d" rx="%d"/>'
            % (x, y, w, h, r))


def circle(cx, cy, r):
    return '    <circle cx="%d" cy="%d" r="%d"/>' % (cx, cy, r)


def path(d):
    return '    <path d="%s"/>' % d


# ------------------------------------------------- window: traffic light
GLYPHS = {}

# minimise: a horizontal bar
GLYPHS["window-minimise"] = line(C - 150, C, C + 150, C)
# maximise: a square
GLYPHS["window-maximise"] = rect(C - 150, C - 150, 300, 300)
# close: an X
GLYPHS["window-close"] = (line(C - 150, C - 150, C + 150, C + 150) + "\n" +
                          line(C + 150, C - 150, C - 150, C + 150))

# -------------------------------------------------------- window modes
# normal: framed window
GLYPHS["window-mode-normal"] = (
    rect(180, 220, 664, 584, 24) + "\n" + line(180, 340, 844, 340))
# borderless: window with no frame, only corner ticks
GLYPHS["window-mode-borderless"] = (
    rect(220, 260, 584, 504, 0) + "\n" +
    line(220, 260, 220, 260))
# fullscreen: frame filling the grid
GLYPHS["window-mode-fullscreen"] = (
    rect(120, 160, 784, 704, 16) + "\n" + line(120, 300, 904, 300))
# fullscreen borderless: filled rectangle
GLYPHS["window-mode-fs-borderless"] = rect(140, 180, 744, 664, 0)
# locked: rectangle plus a padlock body
GLYPHS["window-mode-locked"] = (
    rect(160, 300, 704, 424, 16) + "\n" +
    path("M 372 300 V 232 a 140 140 0 0 1 280 0 V 300"))

# --------------------------------------------------------- terminal state
GLYPHS["term-ok"] = path("M 300 530 L 450 680 L 740 360")
GLYPHS["term-err"] = (path("M 360 360 L 664 664") + "\n" +
                      path("M 664 360 L 360 664"))
GLYPHS["term-warn"] = (path("M 512 210 L 850 780 L 174 780 Z") + "\n" +
                       line(512, 420, 512, 620) + "\n" +
                       line(512, 690, 512, 690))

# ---------------------------------------------------------------- volume
# speaker cone plus one sound wave
GLYPHS["volume"] = (path("M 250 430 L 380 430 L 540 300 V 724 L 380 594 L "
                         "250 594 Z") + "\n" +
                    path("M 640 440 a 130 130 0 0 1 0 144"))
GLYPHS["volume-mute"] = (path("M 250 430 L 380 430 L 540 300 V 724 L 380 594 "
                              "L 250 594 Z") + "\n" +
                         line(650, 440, 790, 584) + "\n" +
                         line(790, 440, 650, 584))

# --------------------------------------------------------------- settings
# gear: eight teeth around a hub
def gear():
    import math
    parts = []
    hub = 190
    outer = 330
    for i in range(8):
        a = math.radians(i * 45)
        x1 = C + hub * math.cos(a); y1 = C + hub * math.sin(a)
        x2 = C + outer * math.cos(a); y2 = C + outer * math.sin(a)
        parts.append(line(int(x1), int(y1), int(x2), int(y2)))
    return "\n".join(parts) + "\n" + circle(C, C, hub)


GLYPHS["settings"] = gear()

def main():
    os.makedirs(OUT, exist_ok=True)
    for name, body in GLYPHS.items():
        svg(name, body)
        print("  %-28s" % name)
    print("%d glyphs" % len(GLYPHS))


if __name__ == "__main__":
    main()
