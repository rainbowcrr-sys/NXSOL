# libnxicons is not removable

`libnxicons.so` (and its split variants) is part of the window frame. It is
installed immutable: no account can delete or replace it, **root included**,
and maintenance mode is the only place where it can be touched.

This is not a permissions policy that could be tightened later. It is a
statement about what these files are: the frame cannot be drawn without
them, so the system does not allow a state where they are absent.

---

## Why the geometry is compiled in

The obvious way to ship icons is a font file, and a font file is what
`icons/gen_font.py` still produces. But a font is a file, and a file has a
failure mode:

- it can be missing
- it can be corrupt
- it can be slower to load than the first frame needs

Any of those leaves a window drawn with a blank gap where the close button
should be, or with the "missing glyph" box. A window frame must not have
that state, so the outlines are static arrays inside the library instead.
`nxicons.ttf` is still built, for documents and for applications that want a
real font, but nothing in the compositor reads it.

---

## What happens if you delete it in maintenance mode

Maintenance mode will let you. It is the only place that will. These are the
three outcomes, in the order they tend to be noticed:

### 1. The icons stop being readable

Every icon becomes either a blank space or the missing-glyph box. The
traffic light is three empty circles. You can still guess which is which by
position, and that is the only reason the window remains usable at all.

### 2. Windows deform

A window that contains an icon reserves space for it. Remove the icon and
the layout engine still has the reservation but nothing to measure, so
elements shift to fill it. The run dialog (`NX + R`) is the clearest
example: it has an icon beside the text field, and without that icon the
field stretches to occupy the gap -- the dialog becomes visibly too long.

### 3. It crashes

This is the common one, not the rare one. Drawing code paths call into the
library expecting a glyph back. When the library is gone, the call fails at
load time and the process that needed it dies. Because the compositor needs
it, that tends to mean the desktop rather than one application.

---

## How to get it back

Boot the installation USB, choose maintenance, and reinstall the package.
There is no in-place repair path, because the tool that would perform it is
the thing that is missing.

---

## The rule, stated once

| | |
|---|---|
| who can delete it | nobody, root included |
| who can replace it | nobody, root included |
| where it can be touched | maintenance mode only |
| why | the frame is not drawable without it |

If you want a different icon, change the SVG in `icons/svg/` and regenerate.
That is the supported way, and it does not involve removing the library.
