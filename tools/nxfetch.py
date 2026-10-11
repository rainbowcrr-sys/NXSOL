#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Download the NX system image, volume by volume, verifying each one.

Used by the installer when it is doing a network install (the ISO carries
only boot code plus this script; the system image comes from the network).

The safety property
-------------------
Nothing is unpacked or written to a disk until the assembled file's hash
matches the manifest. Concretely:

    1. manifest.json is fetched first and is the source of truth
    2. each volume is downloaded, then hashed on arrival
       - mismatch -> that volume alone is re-fetched, up to --retries
       - still wrong -> stop. Not "warn and continue".
    3. volumes are concatenated
    4. the assembled file is hashed and compared to manifest["whole"]
       - mismatch -> stop before anything is unpacked
       - match   -> and only then is it handed to the installer

A download that loses a byte therefore cannot produce an installed system.
It produces an error message and a partial file in a staging directory,
which is annoying and not dangerous.

Resuming
--------
Each volume is fetched with HTTP Range support, so an interrupted download
continues from where it stopped instead of restarting. Per-volume hashing is
what makes this safe: a resumed file is checked as a whole when it completes,
so a resumption that went wrong is caught the same way a clean download
gone wrong would be.

Usage
    nxfetch.py --manifest-url https://.../manifest.json --dest /tmp/nx
    nxfetch.py --manifest manifest.json --base-url https://... --dest /tmp/nx
"""

import argparse
import hashlib
import json
import os
import sys
import time
import urllib.request
import urllib.error

CHUNK = 1024 * 1024


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def fetch(url, dest, resume=True, retries=3, label=""):
    """Download url to dest, resuming if dest already exists.

    Returns True if the file on disk is complete by Content-Length, or if
    the server does not tell us its length (in which case the hash decides).
    """
    for attempt in range(retries):
        have = os.path.getsize(dest) if os.path.exists(dest) else 0
        headers = {}
        if resume and have > 0:
            headers["Range"] = "bytes=%d-" % have
        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                # 206 means the server honoured Range; 200 with a Range
                # request means it did not and is sending from zero, so the
                # partial file must be discarded before appending.
                if have > 0 and r.status != 206:
                    # Asked for "from byte N" and got the whole file back.
                    # Appending would produce a doubled file, so the partial
                    # copy is discarded. This is the normal case against a
                    # server with no Range support, not an error.
                    print("      server ignored Range -- restarting this volume")
                    os.unlink(dest)
                    have = 0
                total = r.headers.get("Content-Length")
                total = int(total) if total else None
                mode = "ab" if (have > 0 and r.status == 206) else "wb"
                got = have if mode == "ab" else 0
                with open(dest, mode) as f:
                    while True:
                        b = r.read(CHUNK)
                        if not b:
                            break
                        f.write(b)
                        got += len(b)
                if total is None or got == total or (have and r.status == 206):
                    return True
                print("    short read (%d/%s), retrying"
                      % (got, total if total else "?"))
        except urllib.error.HTTPError as e:
            # 416 = range past EOF, i.e. the local file is already complete
            # by the server's reckoning. Treat as done.
            if e.code == 416:
                return True
            print("    HTTP %s, retrying" % e.code)
        except Exception as e:
            print("    %s, retrying" % e)
        time.sleep(1 + attempt)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest-url", help="URL of manifest.json")
    ap.add_argument("--manifest", help="local manifest.json instead")
    ap.add_argument("--base-url", default="",
                    help="prefix for volume names when manifest is local")
    ap.add_argument("--dest", required=True)
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("--no-resume", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.dest, exist_ok=True)

    if args.manifest_url:
        mpath = os.path.join(args.dest, "manifest.json")
        print("manifest: %s" % args.manifest_url)
        if not fetch(args.manifest_url, mpath, resume=False):
            print("error: could not download the manifest", file=sys.stderr)
            return 1
        base = args.manifest_url.rsplit("/", 1)[0] + "/"
    elif args.manifest:
        mpath = args.manifest
        base = args.base_url
        if base and not base.endswith("/"):
            base += "/"
    else:
        print("error: need --manifest-url or --manifest", file=sys.stderr)
        return 1

    with open(mpath) as f:
        man = json.load(f)

    vols = man["volumes"]
    whole = man["whole"]
    print("image:   %s" % man["image"])
    print("version: %s" % (man.get("version") or "?"))
    print("volumes: %d" % len(vols))
    print("whole:   %d bytes, sha256 %s" % (whole["size"], whole["sha256"][:16]))
    print()

    for v in vols:
        name = v["name"]
        url = base + name
        dest = os.path.join(args.dest, name)
        want_hash = v["sha256"]

        # Already present and correct? Skip. This is what makes re-running
        # cheap after a failure.
        if os.path.exists(dest) and os.path.getsize(dest) == v["size"]:
            if sha256_file(dest) == want_hash:
                print("  %-32s already complete" % name)
                continue

        ok = False
        for attempt in range(args.retries + 1):
            have = os.path.getsize(dest) if os.path.exists(dest) else 0
            # A file that exists but already failed its hash is not a resume
            # point, it is a wrong file. Saying "resuming" there made a
            # healthy outcome read like a damaged one being accepted.
            # Two different situations that both show up as "a file that is
            # not yet the volume": it is short (an interrupted download --
            # resume it), or it is the full length but hashes wrong (a
            # corrupted download -- discard it). Calling both "wrong" made a
            # healthy resume read like damage being tolerated.
            if have == 0:
                why = "downloading"
            elif have < v["size"]:
                why = "resuming"
            elif attempt == 0 and sha256_file(dest) != want_hash:
                why = "re-fetching (local copy is wrong)"
            else:
                why = "resuming"
            print("  %-32s %s (%d/%d bytes)"
                  % (name, why, have, v["size"]))
            if not fetch(url, dest, resume=not args.no_resume, retries=2):
                continue
            got = sha256_file(dest)
            if got == want_hash:
                print("    sha256 OK")
                ok = True
                break
            # Wrong hash: the local file is not the volume, whatever its
            # length. Delete it so the retry cannot append to garbage.
            print("    sha256 MISMATCH (%s != %s)" % (got[:16], want_hash[:16]))
            os.unlink(dest)
        if not ok:
            print("error: %s could not be verified after %d attempts"
                  % (name, args.retries + 1), file=sys.stderr)
            return 1

    print()
    print("assembling %s ..." % man["image"])
    out = os.path.join(args.dest, man["image"])
    with open(out, "wb") as o:
        for v in vols:
            with open(os.path.join(args.dest, v["name"]), "rb") as f:
                while True:
                    b = f.read(CHUNK)
                    if not b:
                        break
                    o.write(b)

    size = os.path.getsize(out)
    got = sha256_file(out)
    print("  %d bytes, sha256 %s" % (size, got))
    if size != whole["size"]:
        print("error: size %d != manifest %d" % (size, whole["size"]),
              file=sys.stderr)
        return 1
    if got != whole["sha256"]:
        print("error: sha256 mismatch", file=sys.stderr)
        print("  got      %s" % got, file=sys.stderr)
        print("  expected %s" % whole["sha256"], file=sys.stderr)
        print("the image is NOT usable and was not unpacked", file=sys.stderr)
        return 1

    print()
    print("verified. image: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
