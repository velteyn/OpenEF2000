# fixedclock

A tiny DOS TSR that pins `INT 21h` date/time so the EF2000 campaign generator
starts from a **known seed** on every run. Use it when capturing the original:
two runs with the same input then differ only where the input differs, which
makes value, target-assignment and timetable questions testable as A/B
experiments instead of guesses.

## Build

```bash
nasm -f bin -o fixedclock.com fixedclock.asm
```

The `.com` is a build product, not part of the repository. Keep the built file
in your private capture directory.

## Use

Copy the built file next to your game data (or mount a separate directory) as
`FIXEDCLK.COM` — DOS resolves the 8.3 upper-case name; a lower-case file on a
Linux-mounted directory may give `Bad command or filename` — and start it
before the game in the DOS session:

```dos
C:\> FIXEDCLK.COM
fixedclock: DOS clock pinned to 1996-04-19 12:00:00
C:\> _ef2000.exe gui
```

`autoexec` example:

```ini
[autoexec]
mount c /path/to/EF2000
mount d /path/to/captures
d:
FIXEDCLK.COM
c:
set DID=.
set quick_flag=1
_ef2000.exe gui
```

Then generate a campaign (main menu → Campaign → Enter on the level dialog),
quit back to the menu, and copy `CAMPAIGN.CG`/`MISSION.CFG` out. The header
timestamp is the seed; it should be the same on every run.

## What it hooks

| Call | Returned |
|---|---|
| `INT 21h` `AH=2Ah` | 1996-04-19, Friday |
| `INT 21h` `AH=2Ch` | 12:00:00.00 |

All other `INT 21h` calls go to the previous handler. The date/time are
constants in `fixedclock.asm`; change and rebuild to use another seed.

If a particular build seeds from the BIOS tick counter (`INT 1Ah`) instead,
this TSR is not enough — extend it to hook `INT 1Ah` as well. The v2.0
executable as captured uses `INT 21h`.
