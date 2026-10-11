#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build an installable ISO (El Torito, UEFI).

Two kinds come out of this:

    offline      carries the whole system image. Self-contained; the size of
                 the ISO is the size of the system plus boot code.

    netinstall   carries boot code plus tools/nxfetch.py. The system image is
                 downloaded during installation. A few tens of megabytes.

Both boot the same way and run the same installer. The installer looks for
the image next to itself first and falls back to the network, so the same
ISO layout serves both.

Why an ISO rather than the .img
-------------------------------
The .img is a raw disk image: fine for QEMU, fine to dd, but not what
firmware on real hardware expects to find on removable media. Firmware
looks for El Torito. This is also what Windows tools (Rufus, Etcher)
expect. Neither kind is "more correct" -- they are for different uses, and
this repository produces both.

Usage
    mkiso.py --output build/nxsol-26.1.0.iso \\
             --efi build/nxboot.efi \\
             --kernel vendor/kernel/vmlinuz \\
             --initrd build/nxinitrd.img \\
             [--system-image build/dist/nxsystem.squashfs.part00 ...] \\
             [--manifest build/dist/manifest.json]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def need(tool, package):
    if shutil.which(tool):
        return
    print("error: %s not found (apt install %s)" % (tool, package),
          file=sys.stderr)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", required=True)
    ap.add_argument("--efi", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--initrd", required=True)
    ap.add_argument("--system-image", nargs="*", default=[],
                    help="volumes of the system image; omit for netinstall")
    ap.add_argument("--manifest", default="")
    ap.add_argument("--volume-label", default="NXSOL")
    ap.add_argument("--installer", default="",
                    help="installer binary to place at /nx/sbin/nx-installer")
    args = ap.parse_args()

    need("xorriso", "xorriso")

    for f in [args.efi, args.kernel, args.initrd]:
        if not os.path.isfile(f):
            print("error: missing %s" % f, file=sys.stderr)
            return 1

    work = tempfile.mkdtemp(prefix="nxiso-")
    try:
        # EFI boot path. Firmware looks here first on removable media.
        efidir = os.path.join(work, "EFI", "BOOT")
        os.makedirs(efidir)
        shutil.copy(args.efi, os.path.join(efidir, "BOOTX64.EFI"))

        # Kernel and initramfs sit at the root, not under boot/ -- nxboot
        # opens \\vmlinuz and \\initrd.img, which is where mkdisk.py puts
        # them too. Placing them under boot/ here made the ISO unbootable
        # while looking entirely correct: the files were present and the
        # firmware reported only "Not Found".
        shutil.copy(args.kernel, os.path.join(work, "vmlinuz"))
        shutil.copy(args.initrd, os.path.join(work, "initrd.img"))

        images = os.path.join(work, "nx", "images")
        os.makedirs(images)

        if args.system_image:
            print("offline ISO: %d volume(s)" % len(args.system_image))
            for v in args.system_image:
                shutil.copy(v, os.path.join(images, os.path.basename(v)))
            if args.manifest:
                shutil.copy(args.manifest, os.path.join(images, "manifest.json"))
        else:
            print("netinstall ISO: no system image bundled")
            # The downloader goes in too, so a netinstall media can fetch
            # the image without needing anything else present.
            src = os.path.join(ROOT, "tools", "nxfetch.py")
            if os.path.isfile(src):
                shutil.copy(src, os.path.join(images, "nxfetch.py"))
            if args.manifest:
                shutil.copy(args.manifest, os.path.join(images, "manifest.json"))

        if args.installer:
            sbin = os.path.join(work, "nx", "sbin")
            os.makedirs(sbin, exist_ok=True)
            shutil.copy(args.installer, os.path.join(sbin, "nx-installer"))

        os.makedirs(os.path.dirname(args.output), exist_ok=True)

        # -e, not -b. -eltorito-boot (-b) declares the entry as BIOS/80x86;
        # pointing it at an EFI application produces an ISO that lists a boot
        # entry and then fails to load it, with the firmware reporting only
        # "Not Found". -e declares it as EFI, which is what it is.
        #
        # -isohybrid-gpt-basdat lets the result also boot when written
        # straight to a USB stick, which is how most people will use it.
        cmd = [
            "xorriso", "-as", "mkisofs",
            "-iso-level", "3",
            "-volid", args.volume_label,
            "-e", "EFI/BOOT/BOOTX64.EFI",
            "-no-emul-boot",
            "-isohybrid-gpt-basdat",
            "-o", args.output,
            work,
        ]
        # An MBR template is what makes the result bootable after being
        # written straight to a USB stick (isohybrid). Without it the GPT
        # hybrid is not written and the image only works as a real CD/DVD.
        # Absent on a machine without isolinux installed -- skipped rather
        # than failed, because everything else about the ISO is still valid.
        for mbr in ("/usr/lib/ISOLINUX/isohdpfx.bin",
                    "/usr/share/isolinux/isohdpfx.bin",
                    "/usr/lib/syslinux/mbr/isohdpfx.bin"):
            if os.path.isfile(mbr):
                # After "-as mkisofs", not before. -isohybrid-mbr is an
                # option of the mkisofs emulation; placed ahead of "-as
                # mkisofs" xorriso reads it as a native command and aborts
                # with "Not a known command".
                cmd[3:3] = ["-isohybrid-mbr", mbr]
                print("isohybrid MBR: %s" % mbr)
                break
        else:
            print("isohybrid MBR template not found -- ISO is CD/DVD only")
        print(" ".join(cmd[:6]) + " ...")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr, file=sys.stderr)
            return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)

    size = os.path.getsize(args.output)
    kind = "offline" if args.system_image else "netinstall"
    print()
    print("%s ISO: %s (%.1f MiB)" % (kind, args.output, size / 1024 / 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
