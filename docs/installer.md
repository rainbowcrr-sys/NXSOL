# The NX installer

The installer is what turns a running ISO into an installed system: it
partitions, writes, installs the boot loader, and creates the first user.

It is not the same as the out-of-box experience. The installer runs *before*
the system exists on disk; OOBE runs the first time that installed system
boots. Both ask for a language, and that is deliberate -- see "Why the
language is asked twice".

---

## How it is reached

The ISO boots into a live desktop. On it there is one icon:

```
    Install NXSOL
```

The icon is a disc -- drawn with NXIcons, not an emoji. Double-clicking it
starts the installer. Nothing else on that desktop installs anything.

Why a live desktop first, rather than booting straight into the installer:
someone who has never seen NX can look at it before committing their disk to
it. The installer is one icon among many, not the whole personality of the
medium.

---

## The steps

Ubuntu-shaped, because that shape is familiar and because each step is
genuinely a different decision:

| # | step | notes |
|---|------|-------|
| 1 | Language | defaults to English. Choosing one applies it to the installer **immediately** -- the next screen is already in the new language, with no restart. |
| 2 | Keyboard layout | offered as layout + variant |
| 3 | Network | needed for a netinstall; skipped silently on the offline ISO |
| 4 | Installation type | minimal / full |
| 5 | Disks | see below |
| 6 | Time zone | map or list |
| 7 | User | local account only |
| 8 | Confirm | summary, then install |

### Language switching in place

Step 1 is the one place the installer reloads itself. Every string lives in a
translation table and every widget is rebuilt from it, so choosing 简体中文
re-labels the window you are looking at. There is no "restart to apply".

### Local accounts only

Step 7 creates a local account. There is no online sign-in, no cloud account,
no requirement to have a network connection to create a user. Whatever the
machine is used for, the first account on it belongs to the person holding
it.

---

## Disks

`os-prober` does the scanning. The installer does not parse partition tables
itself -- that problem has more edge cases (OEM recovery partitions, Windows
in various states, existing Linux installs, encrypted volumes) than are worth
reimplementing, and getting it wrong means offering to overwrite something.

What the installer shows when it finds an existing Windows:

```
    (•) Install alongside Windows        <- preselected
    ( ) Replace Windows with NX System
    ( ) Manual partitioning
```

Coexistence is preselected but not forced: all three remain one click away.
Nothing is written until the confirm step.

---

## Where the system image comes from

| media | source |
|-------|--------|
| offline ISO | the image sits next to the installer on the ISO |
| netinstall ISO | downloaded during step 3-5 by `tools/nxfetch.py` |

Both end up in the same place, so the rest of the installer does not care
which it was.

### Downloading, and why it cannot install a damaged system

The image is split into volumes so it can be hosted under a 2 GiB per-file
limit. Splitting is not what makes it safe; the checking is:

1. `manifest.json` is fetched first and is the only source of truth
2. each volume is hashed on arrival -- a bad volume is re-fetched alone
3. volumes are concatenated
4. the assembled image is hashed and compared to the manifest
5. **only if it matches** is anything unpacked or written

A transfer that loses a byte therefore cannot produce an installed system. It
produces an error and a partial file in a staging directory. Annoying, not
dangerous.

Interrupted downloads resume. A volume that is short is resumed; a volume
that is full but wrong is discarded and re-fetched. Both are detected the
same way, by the hash.

---

## After it finishes

Reboot. The installed system boots and runs OOBE, which is a separate
program with a separate purpose -- see `oobe/`.

### Why the language is asked twice

They are different questions wearing the same clothes:

| | installer | OOBE |
|---|---|---|
| runs | before the system exists | on the installed system, once |
| the language chosen is | the installer's own | the system's default |
| for | the person doing the install | everyone who uses the machine |

The installer needs a language to talk to you at all. OOBE sets the default
for a system that will outlive the installation.
