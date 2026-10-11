# Out-of-box experience

OOBE runs once, the first time an installed NX System boots. It is not the
installer -- see `installer.md` for the difference.

## Steps

| # | step | notes |
|---|------|-------|
| 1 | Language | sets the system default |
| 2 | Keyboard | |
| 3 | Network | may be skipped |
| 4 | Local account | no online sign-in exists |
| 5 | Privacy | see below |
| 6 | "Please keep your computer on" | full-screen panel, like Windows |
| 7 | The greeting animation | the one-time multi-language hello |

### Privacy defaults

Basic diagnostics are **on** by default and can be switched off on this
screen. The rest -- detailed usage statistics, location, remote crash
reporting -- are off. Every item is listed individually rather than hidden
behind one toggle, so the choice is visible.

### Local accounts only

There is no online account. Not "optional", not "recommended" -- absent. The
first user of the machine is an account on the machine.

## The greeting

After the settings are applied and the account exists, a full-screen panel
shows the system finishing setup. Then the greeting plays, once:

```
    hello -> 你好 -> привет -> こんにちは -> 你好 -> ...
```

Each one fades in, holds, fades out. Below it: a circle with an upward arrow
and the instruction to swipe up to skip, in whichever language is showing.

If a language was chosen during installation, that language's greeting comes
first. The sequence still cycles through the others -- the point is that the
machine says hello in more than one tongue, once, when it first meets you.

Skipping does not skip the greeting entirely. It cuts the sequence short and
then shows `Welcome Use NX System!` for two seconds. The welcome is not
optional; only the length of the hello is.

The animation never runs again. A marker file is written when it completes.
