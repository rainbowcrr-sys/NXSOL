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
    ap.add_argument("--efi-image-mb", type=int, default=16,
                    help="size of the FAT EFI boot image, MiB (default 48)")
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
        # The EFI entry points at a FAT image, not at a file in the ISO9660
        # tree.
        #
        # This was the whole bug. Pointing -e straight at
        # EFI/BOOT/BOOTX64.EFI inside the ISO produced an image that listed
        # a boot entry, had both files present, and verified clean with
        # xorriso -- and which firmware refused to load, saying only "Not
        # Found". A reference ISO built by grub-mkrescue booted fine in the
        # same VM, and the difference was that it points -e at /efi.img: a
        # FAT filesystem image carried as a file, holding the boot
        # application. Firmware reads that as an EFI system partition.
        #
        # Doing it this way also means the kernel and initramfs are read
        # from FAT, which is what nxboot expects and what is already proven
        # by the disk-image path.
        # The EFI boot image is a FAT filesystem holding the boot
        # application, the kernel and the initramfs. All three have to be in
        # here: firmware does not expose the ISO9660 tree to EFI
        # applications as a readable filesystem, so nxboot cannot go looking
        # for the kernel outside the image it was loaded from.
        #
        # It has to be built with mkfs.vfat rather than the mkdisk.py used
        # for disk images: an image from mkdisk.py was rejected with "Not
        # Found" while a same-purpose mkfs.vfat one of the same size loaded
        # fine, so something in how mkdisk.py lays the volume out is not
        # acceptable to firmware as a boot image.
        efidir = os.path.join(work, "EFI", "BOOT")
        os.makedirs(efidir)
        shutil.copy(args.efi, os.path.join(efidir, "BOOTX64.EFI"))
        shutil.copy(args.kernel, os.path.join(work, "vmlinuz"))
        shutil.copy(args.initrd, os.path.join(work, "initrd.img"))

        efi_img = os.path.join(work, "efi.img")
        r = subprocess.run(["mkfs.vfat", "-F", "12", "-C",
                            efi_img, str(args.efi_image_mb * 1024)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr or "", file=sys.stderr)
            print("error: mkfs.vfat failed (apt install dosfstools)",
                  file=sys.stderr)
            return 1
        r = subprocess.run(["mmd", "-i", efi_img, "::/EFI"],
                           capture_output=True, text=True)
        r = subprocess.run(["mmd", "-i", efi_img, "::/EFI/BOOT"],
                           capture_output=True, text=True)
        for src, dst in ((args.efi, "::/EFI/BOOT/BOOTX64.EFI"),
                         (args.kernel, "::/vmlinuz"),
                         (args.initrd, "::/initrd.img")):
            r = subprocess.run(["mcopy", "-i", efi_img, src, dst],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stderr or "", file=sys.stderr)
                print("error: could not place %s in efi.img" % src,
                      file=sys.stderr)
                return 1
        print("efi.img: %.1f MiB, boot application + kernel + initramfs"
              % (os.path.getsize(efi_img) / 1024 / 1024))

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
        # boot-load-size must cover the whole EFI boot image. xorriso left it
        # at 0 here, and firmware then loaded nothing -- the same "Not Found"
        # as before. grub-mkrescue passes 5760 (its image is ~2.88 MiB);
        # ours is bigger because nxboot reads the kernel out of the same FAT
        # image it was loaded from, so the kernel and initramfs have to be in
        # there too. The number is sectors of 512 bytes.
        boot_sectors = (os.path.getsize(efi_img) + 511) // 512

        cmd = [
            "xorriso", "-as", "mkisofs",
            "-iso-level", "3",
            "-volid", args.volume_label,
            "-e", "/efi.img",
            "-no-emul-boot",
            "-boot-load-size", str(boot_sectors),
            # Marks the image as an EFI system partition in the GPT. The
            # reference ISO grub-mkrescue builds carries this; without it
            # the image is an ordinary file and firmware does not treat it
            # as bootable.
            "--efi-boot-part", "--efi-boot-image",
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
