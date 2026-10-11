#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build the NX System On Linux EFI system partition image.

Why this exists: mkfs.vfat and mtools are not present on a stock Windows 10
machine, and the build should not depend on them. This writes an MBR
partition table and a FAT16 file system by hand, then places:

    \\EFI\\BOOT\\BOOTX64.EFI   NX Boot Manager  (what the firmware runs)
    \\vmlinuz                  Linux kernel, CONFIG_EFI_STUB
    \\initrd.img               NX initramfs

FAT16 rather than FAT32: the image is tens of megabytes, and a FAT32 that
small would carry fewer clusters than the specification requires (>65525).
FAT16 covers this size comfortably, and its root directory is a fixed-size
region rather than a cluster chain, which removes a moving part.

Works on Windows, Linux and macOS with nothing but Python 3.
"""

import argparse
import os
import struct
import sys

SECTOR = 512

# MBR partition types
MBR_FAT16_LBA = 0x0E

# FAT16 constraints
FAT16_MIN_CLUSTERS = 4085
FAT16_MAX_CLUSTERS = 65524

ATTR_READ_ONLY = 0x01
ATTR_DIRECTORY = 0x10
ATTR_ARCHIVE = 0x20


# ------------------------------------------------------------------ maths
def fat16_layout(total_sectors, spc, reserved=1, root_entries=512):
    """Solve for (fat_sectors, data_sectors, clusters).

    The FAT's size depends on the cluster count, which depends on how much
    room the FAT itself takes. Start from an estimate and grow it until the
    numbers agree.
    """
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    est_clusters = (total_sectors - reserved - root_sectors) // spc
    fat_sectors = ((est_clusters + 2) * 2 + SECTOR - 1) // SECTOR

    for _ in range(8192):
        data = total_sectors - reserved - 2 * fat_sectors - root_sectors
        clusters = data // spc
        if clusters < FAT16_MIN_CLUSTERS:
            return None
        if clusters > FAT16_MAX_CLUSTERS:
            return None
        if (clusters + 2) * 2 <= fat_sectors * SECTOR:
            return fat_sectors, data, clusters, root_sectors
        fat_sectors += 1
    return None


def choose_cluster_size(total_sectors):
    for spc in (1, 2, 4, 8, 16, 32, 64):
        lay = fat16_layout(total_sectors, spc)
        if lay:
            return spc, lay
    return None, None


# -------------------------------------------------------------------- BPB
def build_bpb(total_sectors, spc, reserved, fat_sectors, root_entries,
              root_sectors, volume_label=b"NXSOL      "):
    b = bytearray(SECTOR)

    b[0:3] = b"\xeb\x3c\x90"
    b[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", b, 11, SECTOR)       # bytes per sector
    b[13] = spc                                 # sectors per cluster
    struct.pack_into("<H", b, 14, reserved)     # reserved sectors
    b[16] = 2                                   # number of FATs
    struct.pack_into("<H", b, 17, root_entries) # root directory entries
    struct.pack_into("<H", b, 19, 0)            # total sectors 16 (0 -> 32)
    b[21] = 0xF8                                # media descriptor
    struct.pack_into("<H", b, 22, fat_sectors)  # sectors per FAT
    struct.pack_into("<H", b, 24, 63)           # sectors per track
    struct.pack_into("<H", b, 26, 255)          # heads
    struct.pack_into("<I", b, 28, 0)            # hidden sectors
    struct.pack_into("<I", b, 32, total_sectors)

    b[38] = 0x29                                # boot signature (FAT16)
    struct.pack_into("<I", b, 39, 0x4E58534C)   # volume serial "NXSL"
    b[43:54] = volume_label                     # 11 bytes
    b[54:62] = b"FAT16   "                      # FS type

    b[510] = 0x55
    b[511] = 0xAA
    return bytes(b)


def build_mbr(total_sectors, part_start, part_sectors):
    m = bytearray(SECTOR)
    struct.pack_into("<I", m, 440, 0x4E58534C)  # disk signature "NXSL"
    m[444:446] = b"\x00\x00"

    e = 446
    m[e + 0] = 0x80                             # bootable
    m[e + 1] = 0                                # CHS begin head
    m[e + 2] = 1                                # CHS begin sector/cyl
    m[e + 3] = 0
    m[e + 4] = MBR_FAT16_LBA
    m[e + 5] = 254                              # CHS end (safe max)
    m[e + 6] = 255
    m[e + 7] = 255
    struct.pack_into("<I", m, e + 8, part_start)
    struct.pack_into("<I", m, e + 12, part_sectors)

    m[510] = 0x55
    m[511] = 0xAA
    return bytes(m)


# --------------------------------------------------------------- entries
def short_name(name):
    """8.3 name -> 11-byte FAT field. 'initrd.img' -> 'INITRD  IMG'."""
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base = base.upper()[:8].ljust(8)
    ext = ext.upper()[:3].ljust(3)
    return (base + ext).encode("ascii")


def fat_date(year, month, day):
    return ((year - 1980) << 9) | (month << 5) | day


def fat_time(hour, minute, second):
    return (hour << 11) | (minute << 5) | (second // 2)


def dirent(name83, attr, cluster, size):
    d = bytearray(32)
    d[0:11] = name83
    d[11] = attr
    struct.pack_into("<H", d, 14, fat_time(0, 0, 0))     # create time
    struct.pack_into("<H", d, 16, fat_date(2026, 1, 1))  # create date
    struct.pack_into("<H", d, 18, fat_date(2026, 1, 1))  # last access
    # 20..21 stay zero: that field is the high half of the cluster number,
    # which only exists on FAT32.
    struct.pack_into("<H", d, 22, fat_time(0, 0, 0))     # modify time
    struct.pack_into("<H", d, 24, fat_date(2026, 1, 1))  # modify date
    struct.pack_into("<H", d, 26, cluster & 0xFFFF)      # first cluster
    struct.pack_into("<I", d, 28, size)
    return bytes(d)


class FAT16:
    def __init__(self, size_mb):
        self.size_mb = size_mb
        total = size_mb * 1024 * 1024 // SECTOR

        self.spc, lay = choose_cluster_size(total)
        if not lay:
            raise SystemExit("no legal FAT16 layout for %d MiB" % size_mb)
        self.fat_sectors, self.data_sectors, self.clusters, self.root_sectors \
            = lay

        self.reserved = 1
        self.root_entries = 512
        self.total_sectors = total

        self.fat_start = self.reserved
        self.root_start = self.fat_start + 2 * self.fat_sectors
        self.data_start = self.root_start + self.root_sectors

        self.img = bytearray(size_mb * 1024 * 1024)
        self.next_cluster = 2
        # FAT entries 0 and 1 are reserved; mark them as end-of-chain.
        self.fat = [0xFFF8, 0xFFFF]

    # -- cluster handling --
    def cluster_sector(self, c):
        return self.data_start + (c - 2) * self.spc

    def alloc(self, count=1):
        first = self.next_cluster
        for _ in range(count):
            c = self.next_cluster
            self.next_cluster += 1
            while len(self.fat) <= c:
                self.fat.append(0)
            self.fat[c] = 0
        for i in range(first, first + count - 1):
            self.fat[i] = i + 1
        self.fat[first + count - 1] = 0xFFFF
        return first

    def write_cluster_chain(self, data, first_cluster):
        """Lay `data` across the chain starting at first_cluster."""
        chunk = self.spc * SECTOR
        off = 0
        c = first_cluster
        while off < len(data):
            if c >= 0xFFF8:
                raise SystemExit("cluster chain ran out")
            sec = self.cluster_sector(c) * SECTOR
            piece = data[off:off + chunk]
            self.img[sec:sec + len(piece)] = piece
            off += chunk
            c = self.fat[c]

    # -- writing --
    def put_file(self, name, data):
        nclusters = max(1, (len(data) + self.spc * SECTOR - 1)
                        // (self.spc * SECTOR))
        first = self.alloc(nclusters)
        if data:
            self.write_cluster_chain(data, first)
        return dirent(short_name(name), ATTR_ARCHIVE, first, len(data))

    def put_dir(self, name, entries, parent_cluster):
        """A directory is stored as a file whose contents are entries."""
        body = b"".join(entries)
        nclusters = max(1, (len(body) + self.spc * SECTOR - 1)
                        // (self.spc * SECTOR))
        first = self.alloc(nclusters)

        # Every directory starts with "." pointing at itself and ".." at the
        # parent; the root's parent is recorded as 0.
        head = dirent(b".          ", ATTR_DIRECTORY, first, 0)
        if parent_cluster == 0:
            head += dirent(b"..         ", ATTR_DIRECTORY, 0, 0)
        else:
            head += dirent(b"..         ", ATTR_DIRECTORY, parent_cluster, 0)

        if body:
            self.write_cluster_chain(head + body, first)
        return dirent(short_name(name), ATTR_DIRECTORY, first, 0)

    def write_root(self, entries):
        off = self.root_start * SECTOR
        data = b"".join(entries)
        self.img[off:off + len(data)] = data

    def finalize(self, path):
        # BPB
        bpb = build_bpb(self.total_sectors, self.spc, self.reserved,
                        self.fat_sectors, self.root_entries, self.root_sectors)
        self.img[0:SECTOR] = bpb

        # FATs
        fatbytes = bytearray()
        for v in self.fat:
            fatbytes += struct.pack("<H", v)
        pad = self.fat_sectors * SECTOR - len(fatbytes)
        fatbytes += b"\x00" * pad
        f0 = self.fat_start * SECTOR
        self.img[f0:f0 + len(fatbytes)] = fatbytes
        f1 = (self.fat_start + self.fat_sectors) * SECTOR
        self.img[f1:f1 + len(fatbytes)] = fatbytes

        # MBR: partition starts at sector 1, leaving room for it.
        part_start = 1
        part_sectors = self.total_sectors - 1
        mbr = build_mbr(self.total_sectors, part_start, part_sectors)
        # Shift the volume up by one sector so the MBR can precede it.
        body = bytearray(self.img[:-SECTOR])
        out = bytearray(SECTOR) + body
        out[0:SECTOR] = mbr
        # The BPB's hidden-sectors field must now say where it really lives.
        struct.pack_into("<I", out, SECTOR + 28, part_start)

        with open(path, "wb") as f:
            f.write(out)
        return len(out)


def norm(path):
    return [p for p in path.replace("\\", "/").split("/") if p]


def build(files, out, size_mb):
    """files: list of (destination path, source path)."""
    fs = FAT16(size_mb)

    # Group destinations by parent directory.
    tree = {"": {}}          # dir path -> {name: ("f", data) or ("d", dict)}
    for dest, src in files:
        parts = norm(dest)
        name = parts[-1]
        parent = "/" + "/".join(parts[:-1]) if len(parts) > 1 else ""
        tree.setdefault(parent, {})[name] = ("f", src)

    # Every directory mentioned, including intermediates.
    dirs = set()
    for dest, _ in files:
        parts = norm(dest)
        for i in range(1, len(parts)):
            dirs.add("/" + "/".join(parts[:i]))
    dirs.add("")

    # Depth-first so children exist before their parent is written.
    def emit(dirpath, parent_cluster):
        entries = []
        for name in sorted(tree.get(dirpath, {})):
            kind, src = tree[dirpath][name]
            if kind != "f":
                continue
            with open(src, "rb") as f:
                data = f.read()
            entries.append(fs.put_file(name, data))
            print("  %-24s %8d bytes" % (
                (dirpath + "/" + name).lstrip("/"), len(data)))

        for sub in sorted([d for d in dirs
                           if d != dirpath and _parent(d) == dirpath]):
            ent = emit(sub, None)          # cluster assigned below
            entries.append(ent)
        return entries

    def _parent(d):
        if d == "":
            return None
        return "/" + "/".join(norm(d)[:-1]) if len(norm(d)) > 1 else ""

    # Simpler: build bottom-up by path depth.
    def build_dir(dirpath):
        entries = []
        # files directly in this directory
        for name in sorted(tree.get(dirpath, {})):
            kind, src = tree[dirpath][name]
            if kind != "f":
                continue
            with open(src, "rb") as f:
                data = f.read()
            entries.append(fs.put_file(name, data))
            print("  %-26s %8d bytes" % (
                (dirpath + "/" + name).lstrip("/") or name, len(data)))
        # subdirectories
        for sub in sorted([d for d in dirs if d and _parent(d) == dirpath]):
            sub_entries = build_dir(sub)
            entries.append(fs.put_dir(norm(sub)[-1], sub_entries, 0))
        return entries

    root_entries = build_dir("")
    fs.write_root(root_entries)

    size = fs.finalize(out)
    print("image: %s (%d bytes, FAT16, %d clusters of %d sectors)"
          % (out, size, fs.clusters, fs.spc))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="disk/nxsol.img")
    ap.add_argument("--size-mb", type=int, default=48)
    ap.add_argument("--efi", default="build/nxboot.efi")
    ap.add_argument("--kernel", default="/boot/vmlinuz")
    ap.add_argument("--initrd", default="build/nxinitrd.img")
    a = ap.parse_args()

    for p in (a.efi, a.kernel, a.initrd):
        if not os.path.exists(p):
            sys.exit("missing: %s" % p)

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    build([
        ("EFI/BOOT/BOOTX64.EFI", a.efi),
        ("vmlinuz", a.kernel),
        ("initrd.img", a.initrd),
    ], a.out, a.size_mb)


if __name__ == "__main__":
    main()
