# NX System On Linux (NXSOL)

**NX System** is an operating system. **NXSOL** is the code name for its
current base: the Linux kernel underneath, everything above it ours.

**NX** means NX. It is not an acronym and it does not expand to anything.

- Base: Linux kernel, booted through its own EFI stub -- **no GRUB**
- License: GPL-2.0-only
- Init: `nxinit`, written for this project
- Configuration: **NX Vault**, a signed store owned by one daemon
- Package layout: `apt` owns `/usr`, our own `nxpkg` owns `/nx`

---

## Version

`26-1.0.0` -- see `VERSION.md` for the scheme and the history.

The short version: the year, then major/minor/patch, where major means "a
change you would have to notice". 1.0.0 was earned because the interface now
actually reads its appearance from NX Vault. It claimed to before and did
not: the loaders were stubs, so every value stayed at its compiled-in
default and setting `caret-bounce` to 0.5 changed nothing.

---

## Why Linux underneath

The project began as a kernel written from scratch. It booted -- UEFI entry,
page tables, GDT/IDT, long mode -- and that work is archived rather than
deleted, under `../NXS-archive/NX-Kernel-archived/`.

It moved to Linux for one reason: a desktop nobody can run software on is a
demonstration, not a system. Every piece that users actually touch -- the
window buttons, the configuration editor, the boot animation, the typewriter
bounce -- is ours either way. The kernel underneath is now the one piece
where "ours" bought nothing a user could feel.

Nothing written for the old kernel was thrown away. `window-controls.c` and
`caret-bounce.c` compile unchanged and now also build as `libnxui.so`.

---

## How it boots

```
UEFI firmware
  -> \EFI\BOOT\BOOTX64.EFI        NX Boot Manager (ours)
       reads \vmlinuz and \initrd.img from the ESP
       publishes the initramfs on a Linux initrd media device path
       LoadImage + StartImage
  -> Linux kernel (EFI stub)
       runs /init from the initramfs
  -> nxinit (PID 1)
       mounts /proc /sys /dev
       starts nxvaultd
       reads /system/boot/target out of NX Vault
       continues, or stops if the vault cannot be reached
```

There is no GRUB in the image. The kernel is a PE/COFF EFI application
(`CONFIG_EFI_STUB`), so firmware can execute it directly.

---

## NX Vault is a boot dependency

Not a convenience. `nxinit` reads `/system/boot/target` from the vault before
it decides what to start, and **halts with a message if it cannot**. A system
that boots without its configuration is worse than one that says it could
not.

### What the vault is

A set of `.nxv` files owned by one daemon, `nxvaultd`. Nothing else reads or
writes them. Every other program -- `nxvault` the CLI, the GUI, the desktop --
is a client that sends a line on an abstract Unix socket:

```
GET /system/boot/target        ->  OK normal
SET /system/boot/target rescue ->  ERR 1   (permission denied)
```

That socket is abstract (a name in kernel space, not a path) precisely so the
initramfs can use it before any filesystem exists.

### Tiers

| tier | who may write | examples |
|------|---------------|----------|
| user | the owning account | `/user/interface/typing/caret-bounce` |
| admin | root | `/system/display/theme` |
| **immutable** | maintenance mode only | `/system/boot/*`, `/system/kernel/*`, `/system/security/*` |

Being root is not enough for an immutable key. The boot is verified at
startup: `nxinit` tries to write `/system/boot/target` and the write is
refused, which is asserted rather than assumed.

### Honesty about the guarantee

On the old kernel, `.nxv` inodes were marked so `open()` failed for everyone
but `nxvaultd`, root included. Linux has no such mark, so the guarantee is
weaker and stated plainly:

- files are 0600 and held open by the daemon
- each file carries a checksum and a backup
- a write is refused by tier before it is applied
- a file failing its checksum is not loaded; the previous version is

Root with a hex editor can still corrupt a file. The system then refuses to
trust it and falls back -- which is the property that matters.

### Not a registry

It is not called a registry editor and it is not one. There is no text file
to hand-edit, no hive to load, and no way to reach the values except through
the daemon.

---

## Boot modes

| mode | how you get there | what runs |
|------|------------------|-----------|
| normal | default | full system |
| rescue | `nxinit` target | minimal; `nxvaultd` not started, configuration read-only |
| maintenance | NX+Alt then reboot from the power menu, or the USB image | everything writable, including immutable keys |

Leaving `fs-borderless-locked` fullscreen is `Ctrl+Alt+Esc`.

---

## NXIcons

Fourteen glyphs: three window buttons, five window modes, three terminal
states, volume, volume-mute, settings. Codepoints U+E100-E10D, Private Use
Area. No emoji anywhere -- every icon is geometry.

The outlines are **compiled into `libnxicons.so`**, not loaded from a font.
A font file can be missing, corrupt, or slower than the first frame, and any
of those leaves a window drawn with a blank gap where the close button
should be. The frame must not have that state. `nxicons.ttf` is still
generated for documents and for applications that want a real font, but
nothing in the compositor reads it.

Because of that the library is installed immutable: root cannot delete or
replace it, and maintenance mode is the only place it can be touched.
See [docs/nxicons-immutable.md](docs/nxicons-immutable.md) for what removing
it there actually does.

No emoji is used as a functional icon anywhere. Every icon is a geometric
glyph in `nxicons.ttf`, in the Private Use Area from U+E100, drawn in the
current text colour and scaled by the font engine like any other character.

Shipped as two shared objects, so a program that only needs the window
controls does not carry the system set:

| library | contents |
|---------|----------|
| `libnxicons-core.so` | traffic light (3), window modes (5) |
| `libnxicons-system.so` | terminal state (3), volume (2), settings (1) |

`icons/gen_svg.py` draws the glyphs, `icons/gen_font.py` builds the font.

---

## The NX key

The NX key **is** the Windows key -- the physical key between Ctrl and Alt.
NX System calls it the NX key. It is not `N` then `X`. See
[docs/nx-key.md](docs/nx-key.md) for the chord list and for how recovery
mode is reached (`NX + Alt`, then the power icon, then restart).

---

## Window controls

Buttons sit **top right**, in the order minimise, maximise, close. That is
the Windows arrangement on purpose: a migration cost of zero is worth more
than being different for its own sake.

The *display logic* is a hybrid of macOS and minimalism -- colour is always
faintly present, so you can tell there are buttons and which is which, but it
never competes with the window's content:

| state | colour | size | icon |
|-------|--------|------|------|
| active, pointer away | desaturated | 8px | none |
| pointer in title bar | full | 10px | none |
| pointer on the button | full | 14px | shown |
| unfocused | grey, 0.40 opacity | 8px | none |
| privileged | red/orange | 10px | never fades |

Values live in the vault under `/user/interface/window/controls/`:

| key | meaning |
|-----|---------|
| `style` | `hybrid` (default), `macos`, `minimal` |
| `color-mode` | `fixed` (default) or `saturation` |
| `idle-saturation` | how far the idle dots are pulled towards grey |
| `hover-saturation` | saturation once the pointer enters the bar |
| `inactive-opacity` | fade applied to unfocused windows |
| `dot-idle-px`, `dot-hover-px`, `dot-focus-px` | the three sizes |
| `anim-enter-ms`, `anim-leave-ms` | transition lengths |

`color-mode` decides what `idle-saturation` does:

- **`fixed`** (default) uses the hand-picked pale values -- `E8DCA0`,
  `A0D8A0`, `E8A0A0`. The designed appearance is preserved exactly, and the
  saturation key does not move it.
- **`saturation`** computes the idle colour from the full-strength colour at
  `idle-saturation`, so the slider becomes a live control: `1.0` gives
  `FFBD2F` / `28C840` / `FF5F57`, `0.0` gives grey.

`fixed` is the default so that the appearance is identical to before the key
existed; anyone who wants the slider to bite opts in.

---

## Window modes

Fullscreen, frame and draggable are three axes collapsed into one enum,
because combinations that make no sense are not worth representing:

| mode | fullscreen | frame | draggable | buttons |
|------|-----------|-------|-----------|---------|
| `normal` | no | yes | yes | shown |
| `borderless` | no | no | yes | on pointer at top |
| `fullscreen` | yes | yes | no | on pointer at top |
| `fs-borderless` | yes | no | no | on pointer at top |
| `fs-borderless-locked` | yes | no | no | never drawn |

---

## Building and running

```sh
./build.sh
```

It compiles `nxvaultd` and `nxinit`, packs the initramfs, compiles the boot
manager, builds the disk image and runs it in QEMU. Missing tools are
installed if the script is allowed to.

Expected output:

```
Hello NXSOL!
Linux 5.15.0-198-generic
nxinit: starting NX Vault ... nxvaultd: listening on @nxvault
ok
NX Vault: target = normal
nxinit: boot target is immutable (even for root)
nxinit: milestone 2 reached, staying resident
```

On Windows 10, `.sh` files do not run; `tools/windows/` has `fetch.ps1` and
`fetch.cmd`, which download the official installers into
`tools/windows/environment/` and stop. They install nothing and touch no
PATH -- you run the three installers yourself, in the order given, then
`fetch.ps1 -CheckPath` tells you what worked.

---

## Layout

```
boot/       NX Boot Manager (UEFI application)
init/       nxinit, PID 1
vault/      nxvaultd, libnxvault, protocol
icons/      NXIcons: SVG source, font generator, shared objects
userland/   libnxui (window controls, caret bounce)
oobe/       first-boot animation
tools/      mkinitramfs.py, mkdisk.py, windows/
```

## Building

Three workflows, in `.github/workflows/`:

| workflow | trigger | produces |
|----------|---------|----------|
| `minimal` | every push | `nxsol-minimal.img` -- boot code + kernel + initramfs, booted in QEMU and asserted |
| `full`    | button or tag | complete system as squashfs, split into hashed volumes |
| `iso`     | button or tag | `nxsol.iso` (offline) and `nxsol-netinstall.iso` |

The minimal one fails the build if the serial log does not contain
`Hello NXSOL`, `nxvaultd` and `boot target is immutable`. "It compiled" is
not the same as "it boots".

Installer and first-boot design live in [docs/installer.md](docs/installer.md)
and [docs/oobe.md](docs/oobe.md).

## Sibling directories

The zip ships four top-level trees, not one:

| directory | what it is |
|-----------|-----------|
| `NXSOL/` | this -- the system that boots |
| `NX-GUI-Spec/` | the desktop blueprint: NXDisplay, NXShell, NXDock, NXMenu, NXExplorer; IPC protocol; wireframes |
| `NX-Ports/` | notes on bringing software across: `.deb` compatibility, `nxpkg` |
| `NXS-archive/` | the from-scratch kernel, archived and not deleted |

The GUI spec is a sibling rather than a subdirectory because it is not part
of the build -- nothing here compiles it -- but it is also not archived: it
is the document the desktop milestone is written from. Filing an active
blueprint under a directory called "archive" was a mistake, and it is
corrected.

## PATH

Linux already provides `PATH` and NX uses it unchanged. One directory is
added: `/nx/bin` for NX components, ahead of `/usr/bin` which `apt` owns.
The two prefixes never overwrite each other.
