#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build the NX initramfs.

A cpio "newc" archive, which is the format the kernel's built-in unpacker
reads. Written in Python rather than shelling out to cpio for the same
reason mkdisk.py exists: the build should need nothing beyond Python 3,
because cpio is not present on a stock Windows machine.

Format, per entry:

    "070701"                     magic
    13 fields of 8 hex digits    inode, mode, uid, gid, nlink, mtime,
                                 filesize, devmajor, devminor, rdevmajor,
                                 rdevminor, namesize, check
    name (namesize bytes, NUL-terminated), padded to 4
    data (filesize bytes), padded to 4

The archive ends with an entry named "TRAILER!!!".
"""

import os
import struct
import sys

MAGIC = b"070701"
TRAILER = b"TRAILER!!!\0"


def pad_name(nb):
    """Pad the name so the WHOLE entry header is 4-aligned.

    The header is 110 bytes and 110 % 4 == 2, so aligning the name to a
    multiple of four on its own still leaves the following data at an odd
    offset. The kernel's unpacker checks this and reports
    "Initramfs unpacking failed: broken padding" -- which is exactly the
    failure this function exists to avoid.
    """
    total = 110 + len(nb)
    r = total % 4
    return nb + (b"\0" * (4 - r) if r else b"")


def pad_data(buf):
    """Data starts 4-aligned already, so only its own length matters."""
    r = len(buf) % 4
    return buf + (b"\0" * (4 - r) if r else b"")


def cpio_header(ino, mode, uid, gid, nlink, mtime, filesize, namesize):
    fields = (ino, mode, uid, gid, nlink, mtime, filesize,
              0, 0,            # devmajor, devminor
              0, 0,            # rdevmajor, rdevminor
              namesize, 0)     # namesize, check
    h = MAGIC
    for v in fields:
        h += ("%08X" % (v & 0xFFFFFFFF)).encode("ascii")
    return h


def add_entry(out, ino, mode, uid, gid, nlink, mtime, name, data):
    nb = name.encode("utf-8") + b"\0"
    out += cpio_header(ino, mode, uid, gid, nlink, mtime,
                       len(data), len(nb))
    out += pad_name(nb)
    out += pad_data(data)


def build(root, out_path, entries):
    """entries: list of (archive path, source path, mode)."""
    out = bytearray()
    ino = 1

    # Directories first, parents before children, so extraction can create
    # them in order.
    dirs = set()
    for apath, _, _ in entries:
        parts = apath.strip("/").split("/")
        for i in range(1, len(parts)):
            dirs.add("/" + "/".join(parts[:i]))
    for d in sorted(dirs, key=lambda p: p.count("/")):
        add_entry(out, ino, 0o040755, 0, 0, 2, 0, d, b"")
        ino += 1

    for apath, src, mode in entries:
        with open(src, "rb") as f:
            data = f.read()
        add_entry(out, ino, mode, 0, 0, 1, 0, "/" + apath.lstrip("/"), data)
        print("  %-20s %8d bytes" % ("/" + apath.lstrip("/"), len(data)))
        ino += 1

    # Terminator.
    out += cpio_header(0, 0, 0, 0, 0, 0, 0, len(TRAILER))
    out += pad_name(TRAILER)
    # The trailer carries no data; its name block is already padded.

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(out)
    print("initramfs: %s (%d bytes)" % (out_path, len(out)))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build(root, os.path.join(root, "build", "nxinitrd.img"), [
        ("init",     os.path.join(root, "build", "nxinit"),   0o100755),
        ("nxvaultd", os.path.join(root, "build", "nxvaultd"), 0o100755),
    ])


if __name__ == "__main__":
    sys.exit(main())
