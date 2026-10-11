#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# NX System On Linux -- build everything and run it in QEMU
# Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SIZE_MB="${SIZE_MB:-48}"

# The sandbox this is developed in drops installed packages between runs, so
# rather than failing on a missing compiler the script puts it back. On a
# normal machine every check here is already satisfied.
ensure_tool() {
    command -v "$1" >/dev/null 2>&1 && return 0
    echo "--- installing $2 (provides $1)"
    if [ "$(id -u)" -eq 0 ]; then
        apt-get install -y -q "$2" >/dev/null 2>&1
    else
        sudo apt-get install -y -q "$2" >/dev/null 2>&1
    fi
    command -v "$1" >/dev/null 2>&1
}

ensure_tool clang clang || { echo "clang unavailable"; exit 1; }
ensure_tool lld-link lld || { echo "lld unavailable"; exit 1; }
ensure_tool qemu-system-x86_64 qemu-system-x86 || { echo "qemu unavailable"; exit 1; }

# Kernel bzImage. Same story as the firmware: prefer the copy vendored in
# the repository, fall back to the running system's. Hard-coding /boot/vmlinuz
# meant the build died at the disk-image step on any machine that did not
# happen to have one there -- and the failure said "missing: /boot/vmlinuz",
# which sounds like the kernel is the problem when really the path is.
KERNEL="${KERNEL:-}"
if [ -z "$KERNEL" ]; then
    for c in "$ROOT/vendor/kernel/vmlinuz" /boot/vmlinuz; do
        [ -f "$c" ] && { KERNEL="$c"; break; }
    done
fi
if [ -z "$KERNEL" ] || [ ! -f "$KERNEL" ]; then
    echo "no kernel image found (looked in vendor/kernel and /boot)"
    exit 1
fi
echo "--- kernel:  $KERNEL"

echo "=== NX Vault daemon ==="
gcc -std=c17 -Wall -Wextra -O2 -static -o build/nxvaultd vault/nxvaultd.c

echo "=== NX init ==="
gcc -std=c17 -Wall -Wextra -O2 -static -o build/nxinit \
    init/nxinit.c vault/libnxvault.c

echo "=== initramfs ==="
python3 tools/mkinitramfs.py

echo "=== NX Boot Manager ==="
clang -target x86_64-unknown-windows \
  -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone \
  -Iboot/include -O2 -Wall -Wextra \
  -c boot/nxboot.c -o build/nxboot.o
lld-link /subsystem:efi_application /entry:efi_main \
  /out:build/nxboot.efi build/nxboot.o

echo "=== disk image ==="
python3 tools/mkdisk.py --out disk/nxsol.img --size-mb "$SIZE_MB" \
  --efi build/nxboot.efi --kernel "$KERNEL" --initrd build/nxinitrd.img

echo "=== run ==="
cp -f disk/nxsol.img disk/nxsol-run.img
rm -f build/serial.log

# Firmware. The copy vendored into the repository is used in preference to
# the distribution's: this script used to hard-code /usr/share/OVMF, which
# simply is not present on a machine that has not installed the ovmf
# package, and then the build failed at the last step for no reason related
# to the code. Vendor first, system second, and say which one won.
OVMF_CODE=""
OVMF_VARS=""
# Several names per directory, not one. Ubuntu 24.04 ships only the 4M
# variants (OVMF_CODE_4M.fd / OVMF_VARS_4M.fd); looking solely for the
# plain name fails there even though the firmware is installed. Trying
# each known name works on both.
for d in "$ROOT/vendor/firmware" /usr/share/OVMF /usr/share/edk2/ovmf; do
    for n in OVMF_CODE_4M.fd OVMF_CODE.fd; do
        [ -n "$OVMF_CODE" ] || { [ -f "$d/$n" ] && OVMF_CODE="$d/$n"; }
    done
    for n in OVMF_VARS_4M.fd OVMF_VARS.fd; do
        [ -n "$OVMF_VARS" ] || { [ -f "$d/$n" ] && OVMF_VARS="$d/$n"; }
    done
done
if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS" ]; then
    echo "no OVMF firmware found (looked in vendor/firmware, /usr/share/OVMF,"
    echo "/usr/share/edk2/ovmf). Install the ovmf package, or put the .fd"
    echo "files in vendor/firmware."
    exit 1
fi
echo "--- firmware: $OVMF_CODE"

# Each machine needs its own writable copy of the variables template. Sharing
# one between runs, or pointing the vars slot at the code image, stops the
# guest from reaching POST at all.
cp -f "$OVMF_VARS" build/OVMF_VARS.fd

qemu-system-x86_64 \
  -machine q35 -m 1024 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE",file.locking=off \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd,file.locking=off \
  -drive format=raw,file=disk/nxsol-run.img,file.locking=off \
  -display none -serial file:build/serial.log -no-reboot &

QPID=$!
# Give the firmware and kernel time; the run is checked afterwards rather
# than waited on, because the guest never exits by design.
sleep "${RUN_SECONDS:-75}"
kill "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true

echo "=== serial output ==="
tr -d '\r' < build/serial.log | grep -aE \
  "Hello NXSOL|Linux [0-9]|Run /init|loading|starting|publishing|failed|not found" \
  | head -20
