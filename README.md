# SimpleDock

A simple, macOS-inspired application dock for **AmigaOS 3.x** (best on 3.2): small and fast.

A rounded bar of icons at a screen edge, solid or transparent, in the style of
the macOS dock.

> **Status: v1.2.** Running programs (a dot and the count; click to front,
> Shift+click for a copy, Del to close), the long-press window list
> (minimise, show, close, Force Quit), programs not on the dock shown at its
> end, keys (minimise, minimise all, auto-hide), drops onto programs and
> drawers and into Shell windows (archives get their unpack command), a
> settings window with a simple side and Advanced, auto-hide with a hotkey,
> ARexx, crash reports. New in 1.2: menu in Tools or as a Dock title, the
> hotkey on the middle mouse button (alone or with a key), more docks from
> Save as... (placed beside the first, reached from its Other docks list),
> the SimpleDock icon as a settings button on the dock, a new icon.
>
> **User guide:** [docs/SimpleDock.html](docs/SimpleDock.html) - the same
> guide ships as `SimpleDock.guide` (AmigaGuide, for MultiView) and `ReadMe`
> in the archive; all three come from [docs/guide.txt](docs/guide.txt) via
> `scripts/mkguide.py`. Developers: [docs/SPEC.md](docs/SPEC.md),
> [CHANGELOG.md](CHANGELOG.md).
>
> SimpleDock is free software (GPL-3). If it earns a coffee:
> https://ko-fi.com/re_intel_soft/donate

## Goals

- **Idle means idle.** No periodic wake-ups while nothing on the dock needs
  them. The exception is the check for running programs (every 2 s while the
  dock is on screen; off with `RUNNING=0`).
- **Fast drawing**: one off-screen image, so a redraw or an animation frame is
  one blit.
- **Small and lean**: 68020+ code; the test commands and traces exist only in
  debug builds (`-DSD_DEBUG`).
- **Careful**: every allocation checked, clean shutdown, bounded string handling.

## Settings

Everything is in one text file, `ENVARC:SimpleDock.config`: the settings
first (`POS=B`, `ONTOP`, `(NOTEXT)` = off ...), then the icons. Dock >
Settings... opens the settings window: a change shows on the dock at once
and is kept when the window closes; Undo changes goes back to how it was; Load...
and Save as... for other settings files; Advanced >> for every other keyword.
Icons added, moved or removed with the mouse are saved at once. The same keywords also work as
tooltypes and on the command line; see [docs/SPEC.md](docs/SPEC.md).

ARexx port `SIMPLEDOCK`: the dock (SHOW, HIDE, REVEAL, REFRESH, GETPOS, QUIT),
its icons (LIST, ADD, REMOVE, MOVE) and its settings (SET, GET, SAVE, RELOAD,
LOAD, SETTINGS) - see [docs/SPEC.md](docs/SPEC.md). BENCH gives drawing times
on your machine, to compare:

```
rx "options results; address SIMPLEDOCK 'BENCH'; say result"
```

## Requirements

- **Minimum:** AmigaOS 3.2, or 3.1.4, 3.5 or 3.9 (workbench.library and
  icon.library 44 or newer; on the older ones a few extras fall back, see
  the ReadMe), a 68030 with fast RAM. (A stock 68020 A1200 runs it, but the animations
  are too slow to be pleasant.)
- **Recommended:** a 68040 or 68060 accelerator, or an accelerated Amiga
  such as a PiStorm / Emu68 system, and a graphics card (RTG, Picasso96)
  with a 16- or 24-bit screen mode - animations are smoothest there, and
  the transparent dock costs the least.
- AGA (8-bit) screens work too, more slowly. The settings window's lists
  are ReAction choosers (chooser.gadget, part of OS 3.2), with GadTools
  cycle gadgets as a fallback.

## Building

With vbcc (m68k-amigaos target) and the NDK 3.2 headers:

```
VBCC_BIN=... VBCC_TARGET=... NDK_INCLUDE=... scripts/build.sh
```

The result is `build/SimpleDock`. Its icon is made by
`python3 scripts/mkicon.py build/SimpleDock.info` (`scripts/iconcheck.py`
reads an icon back and shows its images). `scripts/package.sh` then makes
`build/release/SimpleDock.lha` (the SimpleDock drawer: program, ReadMe, LICENSE and their icons).
The program icon is the pixel map in `scripts/icon.txt`.

## Source map

| File | What it does |
|---|---|
| `src/main.c` | start-up, libraries, the main loop (one `Wait()` for everything) |
| `src/sd.h` | shared structures (`Opts`, `Dock`, `Item`) and every prototype |
| `src/options.c` | keywords: command line, tooltypes and settings lines, one template |
| `src/config.c` | the config file: read, save (via `.new` / `.bak`), Load / Save as |
| `src/dock.c` | layout, the dock window, its events, focus and "in front" |
| `src/render.c` | the rounded background, icons and labels into one off-screen image |
| `src/scale.c` | icon images made once: shrunk, blended, cached |
| `src/pix.c`, `src/p96.h` | pixel rows in and out of graphics-card bitmaps (Picasso96) |
| `src/pixel.s` | the inner pixel loops in assembly (C versions in `render.c`) |
| `src/anim.c` | glides: icons moving to new places |
| `src/autohide.c` | auto-hide slide, the hidden strip, the hotkey, new snapshots |
| `src/input.c`, `src/inhandler.s` | input handler: pointer, button and hotkey, before Intuition |
| `src/drag.c` | dragging dock icons; Workbench icons dropped on the dock |
| `src/dropzone.c` | drop zones: the dock makes way while Workbench drags icons over it |
| `src/launch.c` | a click or a drop on an icon: start, open, or copy into a drawer |
| `src/dropcopy.c` | files dropped onto a drawer or disk icon: copied into it |
| `src/running.c` | running programs: the dock's own launcher, dot and count, click to the front, the held list of windows (Close, Close All), programs not on the dock |
| `src/tooltip.c` | the label shown on hover |
| `src/appmenu.c` | the Dock menu, the settings window (simple and Advanced), About |
| `src/picker.c` | the colour picker of the settings window (its Custom... button) |
| `src/crash.c`, `src/crashtrap.s` | crash reports (CRASHLOG): the trap handler and the report |
| `src/arexx.c` | the `SIMPLEDOCK` ARexx port |
| `src/bench.c` | ARexx `BENCH`: drawing times on this machine |
| `src/rawfmt.s` | character output for `RawDoFmt()` |
| `scripts/` | build script, icon maker and checker, `package.sh` + `lha.py` (release archive) |
| `dist/` | the ReadMe and the Aminet `.readme` that go with a release |

Builds with `-DSD_DEBUG` add a trace and test commands to the ARexx port.

## Licence

GNU General Public License v3.0, see [LICENSE](LICENSE).

## Trademarks

AmigaOS is a trademark of its owner; macOS is a trademark of Apple Inc. No AmigaOS artwork is included;
SimpleDock's icons are its own.
