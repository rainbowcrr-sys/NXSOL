# Windows 10 build tools

`.sh` files do not run on Windows 10. This directory is the Windows path: two
scripts that download official installers, and nothing else. They do not
install anything and do not touch PATH — you run each installer yourself.

## Fetch

```
fetch.cmd              download everything into environment\
fetch.cmd -List        show what would be downloaded
fetch.cmd -CheckPath   check whether the tools are already on PATH
fetch.cmd -OpenPages   open the official download pages
```

`fetch.cmd` exists only because double-clicking a `.ps1` does nothing useful
on a default Windows 10. It launches `fetch.ps1` with
`-ExecutionPolicy Bypass` scoped to that one process, so no machine policy is
changed.

Or in PowerShell directly:

```powershell
.\fetch.ps1
```

## What gets downloaded

| tool | what it is for |
|------|----------------|
| `LLVM-19.1.0-win64.exe` | `clang` + `lld-link` — compiles `nxboot.efi` (a PE32+ EFI application) |
| `qemu-w64-setup-YYYYMMDD.exe` | `qemu-system-x86_64` — runs NXSOL. **Also brings OVMF.** |
| `python-3.12.10-amd64.exe` | runs `mkinitramfs.py` and `mkdisk.py` |

### OVMF is not downloaded, on purpose

The QEMU installer already ships it:

```
C:\Program Files\qemu\share\edk2-x86_64-code.fd
C:\Program Files\qemu\share\edk2-i386-vars.fd
```

That pairing is not a typo — an x86_64 **code** image goes with an i386
**vars** image. Each virtual machine needs its **own writable copy** of the
vars file; never point two machines at the same one, and never put the code
image in the vars slot, or the guest will not POST.

### About the links

LLVM and Python have URLs that never move, so they are written into the
script and the LLVM download is checked against the published SHA-256.

QEMU cannot be pinned: those builds are published as
`qemu-w64-setup-YYYYMMDD.exe` and the date changes on every rebuild, so a
hard-coded file name would rot within weeks. The script reads the directory
listing and takes the newest. If that fails it says so and prints the page to
use by hand — it never leaves a partial file behind pretending to have worked.

## Install

Run the three installers in this order:

1. **LLVM** — tick *Add LLVM to the system PATH for all users*
2. **QEMU** — accept the default location (`C:\Program Files\qemu`)
3. **Python** — tick *Add python.exe to PATH*

Then open a **new** terminal, because PATH changes never apply to a terminal
that is already open, and check:

```powershell
.\fetch.ps1 -CheckPath
```

## Building

The equivalents of what `build.sh` does on Linux:

```powershell
python tools\mkinitramfs.py

clang -target x86_64-unknown-windows -ffreestanding -fno-stack-protector `
      -fshort-wchar -mno-red-zone -Iboot\include -O2 `
      -c boot\nxboot.c -o build\nxboot.o

lld-link /subsystem:efi_application /entry:efi_main `
         /out:build\nxboot.efi build\nxboot.o

python tools\mkdisk.py --out disk\nxsol.img --size-mb 48
```

The `-target x86_64-unknown-windows` and `-mno-red-zone` flags look odd
together but are what `build.sh` actually uses and are known to produce a
working `nxboot.efi` — do not "correct" them without testing.

`nxinit` and `nxvaultd` are ordinary Linux programs; build them with whatever
host compiler you have (gcc or clang). They need no Windows cross toolchain.

## Running

Copy the vars template once, then boot:

```powershell
copy "C:\Program Files\qemu\share\edk2-i386-vars.fd" build\OVMF_VARS.fd

qemu-system-x86_64 -machine q35 -m 1024 ^
  -drive if=pflash,format=raw,unit=0,readonly=on,file="C:\Program Files\qemu\share\edk2-x86_64-code.fd" ^
  -drive if=pflash,format=raw,unit=1,file=build\OVMF_VARS.fd ^
  -drive format=raw,file=disk\nxsol.img ^
  -display none -serial stdio
```

`^` continues a line in CMD. In PowerShell use a backtick, `` ` ``, instead.

You need a Linux kernel bzImage (`vmlinuz`) to put on the disk image, taken
from a distribution or built with `CONFIG_EFI_STUB=y`.

## Does NX have its own PATH?

No — Linux already provides `PATH` and NX uses it unchanged. What NX adds is
one directory of its own:

```
/nx/bin      NX components (nxvault, nxpkg, ...)
/usr/bin     everything apt installs
```

`/nx/bin` comes first, so an NX component is found before a distribution
package of the same name and the two never overwrite each other — which is the
point of keeping them in separate prefixes.
