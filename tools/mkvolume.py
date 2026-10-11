#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Split the system image into volumes and emit a manifest.

Why volumes exist at all
------------------------
GitHub Releases caps a single file at 2 GiB. The system image is around
1.2-2 GiB compressed, which is close enough to that limit that it will be
crossed at some point -- probably at an inconvenient moment.

Why splitting is not a corruption risk
--------------------------------------
The fear is fair: an image missing one byte is a system that installs and
then does not boot, which is worse than one that refuses to install. Splitting
alone would not fix that. What fixes it is that every volume is checked on
arrival and the reassembled file is checked again before it is used:

    per volume    sha256 checked the moment that volume finishes downloading
                  -> a bad volume is re-fetched alone, not the whole image
    whole image   sha256 checked after concatenation, before anything is
                  unpacked or written to disk

So the outcome is binary: either the hash matches and the image is complete,
or it does not and the installer stops with an error. There is no path where
a damaged image gets installed. That is the property that matters, and it is
independent of whether the file was ever split.

Manifest
--------
Written next to the volumes. It carries the size and hash of every volume
plus the hash of the assembled whole, so the downloader can verify each piece
and the result without trusting the transport.

Usage
    mkvolume.py --input build/nxsystem.squashfs --output-dir build/dist
    mkvolume.py --input ... --volume-size 1900   # MiB, default 1900
"""

import argparse
import hashlib
import json
import os
import sys
import time

# 1900 MiB rather than the full 2 GiB: leaves headroom so a file that grows
# slightly does not silently cross the hosting limit.
DEFAULT_VOLUME_MIB = 1900


def sha256_file(path, chunk=1024 * 1024):
    h = hashlib.sha256()
    total = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
            total += len(b)
    return h.hexdigest(), total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--volume-size", type=int, default=DEFAULT_VOLUME_MIB,
                    help="MiB per volume (default %d)" % DEFAULT_VOLUME_MIB)
    ap.add_argument("--version", default="")
    ap.add_argument("--name", default="nxsystem.squashfs")
    args = ap.parse_args()

    src = args.input
    if not os.path.isfile(src):
        print("error: %s not found" % src, file=sys.stderr)
        return 1

    os.makedirs(args.output_dir, exist_ok=True)

    print("hashing %s ..." % src)
    whole_hash, whole_size = sha256_file(src)
    print("  %d bytes, sha256 %s" % (whole_size, whole_hash))

    vol_bytes = args.volume_size * 1024 * 1024
    vols = []
    base = os.path.basename(src)

    # One volume is the common case today. The naming is numbered anyway so
    # that moving to several does not change any consumer.
    n = 0
    with open(src, "rb") as f:
        while True:
            name = "%s.part%02d" % (base, n)
            out = os.path.join(args.output_dir, name)
            written = 0
            h = hashlib.sha256()
            with open(out, "wb") as o:
                while written < vol_bytes:
                    want = min(1024 * 1024, vol_bytes - written)
                    b = f.read(want)
                    if not b:
                        break
                    o.write(b)
                    h.update(b)
                    written += len(b)
            if written == 0:
                os.unlink(out)
                break
            vols.append({
                "name": name,
                "index": n,
                "size": written,
                "sha256": h.hexdigest(),
            })
            print("  %-34s %10d bytes  %s" % (name, written, h.hexdigest()[:16]))
            n += 1
            if written < vol_bytes:
                break

    manifest = {
        "format": "nx-image-manifest/1",
        "image": base,
        "version": args.version,
        "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "whole": {
            "size": whole_size,
            "sha256": whole_hash,
        },
        "volume_size": vol_bytes,
        "volume_count": len(vols),
        "volumes": vols,
        # How to put the pieces back. cat is deliberately the whole story --
        # anything that can concatenate can reassemble these.
        "reassemble": "cat " + " ".join(v["name"] for v in vols) + " > " + base,
    }

    mpath = os.path.join(args.output_dir, "manifest.json")
    with open(mpath, "w") as m:
        json.dump(manifest, m, indent=2)
        m.write("\n")

    print()
    print("%d volume(s), manifest: %s" % (len(vols), mpath))
    print("whole sha256: %s" % whole_hash)
    print()
    print("verify a reassembled image against this manifest:")
    print("  sha256sum %s   # -> %s" % (base, whole_hash))
    return 0


if __name__ == "__main__":
    sys.exit(main())
