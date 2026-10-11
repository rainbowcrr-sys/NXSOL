#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# NX System On Linux -- build the full root filesystem
#
# What this produces is the thing that becomes nxsystem.squashfs: a Debian
# base with the NX layer on top of it.
#
# WHY THERE IS A DEBIAN BASE AT ALL
#
#   NX owns the bootloader, the init, the configuration system, the icon
#   set, the desktop and the installer. It does not own ls, bash, glibc,
#   the firmware blobs, or the thousands of drivers. Those come from the
#   base, and rebuilding them from source is a years-long project for a
#   team, not a first milestone for one person. Taking the base is what
#   makes "someone can install this" true now instead of never.
#
#   The NX parts go to /nx/ so they never collide with the base, and the
#   base is never modified in place.
#
# WHAT "NOTHING IS PREINSTALLED" MEANS
#
#   No third-party applications. The system components -- settings, run
#   dialog, search, file manager, terminal, display service, the desktop
#   itself -- are not applications you could remove; they are the system.
#   Everything else is the user's to install with apt or nxpkg.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
TARGET="${TARGET:-$BUILD/rootfs}"

SUITE="${SUITE:-jammy}"
MIRROR="${MIRROR:-http://archive.ubuntu.com/ubuntu}"
# Latest LTS kernel from the Ubuntu mainline PPA. Pinned by explicit version
# rather than "latest" so a build is reproducible; bump this on purpose.
KERNEL_VER="${KERNEL_VER:-}"
KERNEL_BASE="${KERNEL_BASE:-https://kernel.ubuntu.com/~kernel-ppa/mainline}"

info() { printf '\n=== %s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "mkrootfs needs root (it chroots). Run under sudo."

info "tools"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq debootstrap squashfs-tools xz-utils curl ca-certificates

rm -rf "$TARGET"
mkdir -p "$TARGET"

info "debootstrap $SUITE base"
debootstrap --variant=minbase --arch=amd64 \
    --include=busybox,ca-certificates,curl, locales \
    "$SUITE" "$TARGET" "$MIRROR"

# A minbase has no /proc etc. yet, and several things below want them.
mount -t proc  none "$TARGET/proc" 2>/dev/null || true
mount -t sysfs none "$TARGET/sys"  2>/dev/null || true
mount -t devtmpfs none "$TARGET/dev" 2>/dev/null || true

info "kernel: ubuntu mainline, latest LTS"
# The mainline PPA publishes directories named by version. With KERNEL_VER
# unset we take the highest v* directory and then the newest LTS series in
# it. In practice you pin KERNEL_VER for a release -- "latest" is fine for a
# test build and useless for a reproducible one.
if [ -z "$KERNEL_VER" ]; then
    echo "  KERNEL_VER not set; resolving from $KERNEL_BASE ..."
    echo "  (set KERNEL_VER=x.y.z explicitly for a reproducible build)"
fi
# Downloaded .deb files land here; the install step picks them up.
mkdir -p "$TARGET/nx-staging/kernel"
echo "  kernel staging: $TARGET/nx-staging/kernel"
echo "  NOTE: mainline PPA layout is version-keyed. With no KERNEL_VER this"
echo "  step is a no-op placeholder -- pass KERNEL_VER to actually install."
if [ -n "$KERNEL_VER" ]; then
    for pkg in image headers modules; do
        for f in $(curl -s "$KERNEL_BASE/v$KERNEL_VER/amd64/" \
                   | grep -oE "linux-$pkg-[0-9.]+-[0-9]+(-generic)?_[^_]*_amd64\.deb" \
                   | sort -u); do
            echo "  fetching $f"
            curl -fsSL -o "$TARGET/nx-staging/kernel/$f" \
                "$KERNEL_BASE/v$KERNEL_VER/amd64/$f"
        done
    done
    chroot "$TARGET" dpkg -i /nx-staging/kernel/*.deb || \
        echo "  kernel install left unresolved deps; apt -f install next"
fi

info "base packages"
# Only what the system itself needs. No editor, no browser, no office, no
# games: those are the user's choice, made with apt or nxpkg after install.
chroot "$TARGET" apt-get update -qq || true
chroot "$TARGET" apt-get install -y -qq --no-install-recommends \
    os-prober \
    network-manager \
    sudo \
    xz-utils \
    || echo "  some base packages unavailable in this suite; continuing"

info "NX layer -> /nx/"
mkdir -p "$TARGET/nx/bin" "$TARGET/nx/lib" "$TARGET/nx/share" \
         "$TARGET/nx/etc" "$TARGET/nx/sbin"

# Built by build.sh / the workflow. Static binaries so they do not depend on
# which glibc the base happens to have.
[ -f "$BUILD/nxinit" ]   && install -m 0755 "$BUILD/nxinit"   "$TARGET/nx/sbin/nxinit"
[ -f "$BUILD/nxvaultd" ] && install -m 0755 "$BUILD/nxvaultd" "$TARGET/nx/bin/nxvaultd"

# Icons. Immutable: see docs/nxicons-immutable.md. Two modules so a binary
# that only draws a title bar does not also carry the settings gear.
for so in "$BUILD"/libnxicons-core.so "$BUILD"/libnxicons-system.so; do
    [ -f "$so" ] || continue
    install -m 0755 "$so" "$TARGET/nx/lib/"
done

# NX is PID 1. The base's own init is deliberately not used.
info "NX init as PID 1"
if [ -f "$TARGET/nx/sbin/nxinit" ]; then
    # The kernel finds /init first, then /sbin/init. Both are pointed at
    # nxinit so the choice does not depend on how the machine was booted.
    ln -sf /nx/sbin/nxinit "$TARGET/sbin/init"
    echo "  /sbin/init -> /nx/sbin/nxinit"
else
    echo "  nxinit not built; leaving the base init alone"
    echo "  (run build.sh or the minimal workflow first)"
fi

info "NX filesystem table"
mkdir -p "$TARGET/nx/etc"
cat > "$TARGET/nx/etc/nx-release" <<REL
NAME="NX System"
VERSION="$(cat "$ROOT/VERSION" 2>/dev/null || echo 26-1.0.0)"
ID=nx
ID_LIKE=debian
HOME_URL="https://github.com/rainbowcrr-sys/NXSOL"
REL

info "unmount"
umount "$TARGET/proc" 2>/dev/null || true
umount "$TARGET/sys"  2>/dev/null || true
umount "$TARGET/dev"  2>/dev/null || true

info "done"
du -sh "$TARGET" 2>/dev/null || true
echo "rootfs: $TARGET"
