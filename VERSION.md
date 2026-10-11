# NXSOL version scheme

```
26-1.0.0
│  │ │ │
│  │ │ └── PATCH  bug fixes; behaviour is unchanged
│  │ └──── MINOR  new capability; everything that worked still works
│  └────── MAJOR  a change you would have to notice
└───────── YEAR   the release year, two digits (26 = 2026)
```

`VERSION` in the repository root holds exactly this string and nothing else.

## What earns a major bump

A major bump is not about how big the diff was. It is about whether someone
who used the previous release would have to change anything:

- a key was renamed or removed, so a stored value stops matching
- a default changed, so the system looks or behaves differently on a fresh
  install
- something that silently did nothing now takes effect (or the reverse)

The third one is the subtle case, and it is what happened at 1.0.0: the UI
read its appearance from compiled-in constants while claiming to read them
from NX Vault. Fixing that makes configuration start working, which is a
behaviour change even though the defaults were left alone.

## History

### 26-1.0.0

Configuration actually drives the interface now.

- `nxui_window_load_config()` and `nxui_bounce_load_config()` read NX Vault
  over IPC. They were stubs: every appearance value stayed at its
  compiled-in default no matter what the Vault said, so setting
  `caret-bounce` to 0.5 did nothing at all.
- The daemon grew the window-appearance keys it never had
  (`controls/style`, `controls/color-mode`, the saturation values, the dot
  sizes, the animation lengths). Without them the reads above would all have
  come back `NOENT`.
- `controls/color-mode` added: `fixed` (default, the hand-picked pale dots)
  and `saturation` (computed live, so the slider bites). The old normalising
  divide cancelled itself out and made the slider inert.
- Caret keys unified on hyphens. The UI code and the daemon disagreed
  (`caret_bounce` vs `caret-bounce`), invisible while both sides were stubs
  and fatal the moment they were connected.
- Backup indices survive a daemon restart, and old backups are pruned
  instead of accumulating forever.

### 26-0.0.1

First two milestones: the boot manager, `nxinit`, `nxvaultd`, the abstract
socket, immutable boot keys, `libnxui` window controls and caret bounce, and
the icon font.
