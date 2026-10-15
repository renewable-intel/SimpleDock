# SimpleDock — specification

What SimpleDock does, its settings, files and ARexx commands.

## 1. Scope

- **Platform:** AmigaOS 3.2; minimum a 68030 with fast RAM (the code is
  68020 code and runs on a stock A1200, but its animations are too slow
  there); recommended a graphics card or a fast accelerator.
- **Look:** a rounded bar of icons at a screen edge, in the style of the macOS
  dock: solid colour or transparent, labels below the icons or on hover, no
  magnification.

## 2. Settings

The same keywords work on the command line, as tooltypes of the program's
icon and as settings lines in the config file (section 3).

| Keyword | Meaning |
|---|---|
| CONFIG | config file or drawer (default `ENVARC:SimpleDock.config`) |
| POS=POSITION | edge T/B/L/R, plus an optional alignment letter (default B, centred) |
| DISTANCE/K/N | gap between the dock and its screen edge in pixels, 0-64 (default 4) |
| OFFSET/K/N | shift along the edge in pixels |
| FONT/K | `name/size` for the labels |
| PUBSCREEN/K | public screen to open on (default Workbench) |
| NOTEXT/S | no labels |
| HOVERTEXT/S | labels as a small label over the icon under the pointer |
| NOICON/S | labels only |
| NOBORDER/S | no thin border around the dock |
| ICONDIST/K/N | extra space between the icons, along the dock only, 0-64 (default 2) |
| SCALEICONW/K/N, SCALEICONH/K/N | shrink bigger icons to this size (0 = own size) |
| LIMIT/K/N | at most this many icons (default: as many as fit) |
| AUTOHIDE/K/N | hide when the pointer leaves; a full slide takes n x 70 ms (1 = instant) |
| ONTOP/S | stay in front of other windows |
| TRANSPARENT/S | transparent (tinted) background instead of the solid colour |
| TINT/K | background colour `#rrggbb` (default solid #383838, transparent #202020) |
| OPACITY/K/N | how dark the transparent background is, 0-100 (default 55) |
| RADIUS/K/N | corner radius, 0-32 (default 12) |
| DELAY/K/N | wait n/50 s before opening |
| QUIET/S, VERBOSE/S | fewer / more messages |
| HOTKEY/K | key to bring the dock out, a commodities description (default `ctrl alt d`) |
| CUSTOMTINT/K | the settings window's custom colour `#rrggbb` (its Colour list) |
| CRASHLOG/S | crash reports: a trap handler for the dock's task (no cost until an error); a crash writes `ENVARC:SimpleDock-crash.txt` (error, section and offset - the linker map `build/SimpleDock.map` names the function - registers, system, settings), tells the user and closes, the input handler first |

Where settings come from: the icon's tooltypes, then the config file's
settings lines over them (a switch in parentheses there, `(NOTEXT)`, is off even
if the icon has it), then a Shell command line over both.

Several docks: a config file with a project icon whose default tool is
SimpleDock starts a dock with that file; the file name is then its ARexx port
name.

## 3. Config file: settings and icons

One text file holds everything, `ENVARC:SimpleDock.config` unless CONFIG
names another: settings lines first (a keyword in parentheses is disabled), then the
icons.

```
; SimpleDock settings - a keyword in parentheses is disabled
POS=B
(AUTOHIDE=3)                     ; off; the speed is kept for Auto-hide on
HOVERTEXT
TINT=#383838
; icons
SYS:Utilities/MultiView          ; a line = path of an icon, without .info
LABEL Viewer                     ; replaces the label of the previous entry
IMAGE SYS:Icons/viewer.png       ; another icon's image for the previous entry
```

- Lines starting with ';' are comments. A line is a setting when it starts
  with a keyword of section 2 (optionally in parentheses) and has no ':' or '/'
  before '='.
- A file without settings lines: the icon's tooltypes are the settings.
- CONFIG may be a drawer: every icon in it becomes an entry; its settings are
  kept in the icon's tooltypes.
- Lines longer than 255 characters are reported, not truncated.
- No file yet: the standard icons that exist (the SYS: disk named after
  its volume, Shell, MultiView, Clock, Calculator, IconEdit, Exchange, the
  Prefs, Tools and Utilities drawers, Ram Disk), as many as fit the screen
  (the rest are left out, without the "do not fit" note), labels on hover,
  in front; the file is created.
- Missing files: an entry whose file (or its icon, or its disk) is not
  found when the icons load is drawn with a standard icon, dimmed with a
  "?" badge; checked without "Please insert volume" requesters. Clicking it
  checks again: still missing, a requester offers Remove / Keep; found, it
  opens. Entries are never removed on their own.
  A file that exists but cannot be read is never overwritten; the dock starts
  empty and reports the error.

## 4. ARexx (port `SIMPLEDOCK`, or the config file name)

| Command | Result |
|---|---|
| QUIT | close the dock and exit |
| HIDE / SHOW | close / reopen the dock window, the program keeps running |
| REVEAL | as the hotkey |
| REFRESH | close and reopen the dock (new background snapshot) |
| GETPOS | RESULT = `x y w h` of the dock, plus `x y w h` of the last clicked icon |
| LIST [NAMES] | RESULT = the icons' paths (labels with NAMES), one per line, in dock order |
| ADD path [AT n \| AFTER icon \| BEFORE icon] [LABEL text] | add an icon (at the end by default; the file must exist) |
| REMOVE icon | remove an icon |
| MOVE icon AT n \| AFTER icon \| BEFORE icon | move an icon (glides) |
| SET line ... | settings lines (as in the config file, `(KEY)` = off, quotes for spaces) over the settings in use; kept as the file's settings lines; the dock reopens; closes an open settings window |
| GET [key] | RESULT = a setting's value, `1`/`0` for a switch on/off, empty when not set; no key: every settings line, one per line |
| SAVE [file] | settings and icons into the dock's own file (or file) |
| RELOAD / LOAD file | read the dock's own file again / another file |
| SETTINGS [ADVANCED] | open the settings window (or bring it to the front), folded out with ADVANCED |
| BENCH | RESULT = CPU, time to open the dock, a full redraw, an animation frame (one icon's place), the background, and the screen (the dock flickers while it runs) |

An icon is named by its label, file name or path (any case), or `#n` for
its place (1 = first). Arguments are parsed with DOS templates, so quotes
work (`ADD "Ram Disk:"`).

Each message gets exactly one reply: RC 0, or RC 5 while the dock's own icon
drag holds the screen's layers (commands that only ask - GETPOS, LIST, GET -
still answer), or RC 10 for an unknown command, icon, file, keyword or value.
RESULT only on success. A second instance with a port name already in use
does not start. Settings given on a Shell command line stay in force over
SET.

## 5. Look

AmigaOS 3.x windows are opaque rectangles (no compositing, and 3.2's
layers.library has no window shapes). The background starts as a snapshot of
the screen under the dock:

- **Solid** (default, dark grey): the colour fills the dock; only outside the
  rounded corners does the snapshot show.
- **Transparent:** the whole background is the snapshot, tinted.

The snapshot is taken when the dock opens and, with auto-hide, every time it
comes out (the screen behind is uncovered then); something that changes
behind a dock that stays shown is seen when it reopens. A thin lighter border
follows the rounded shape. Icons sit 6 pixels from the top and bottom border
(RADIUS 12); ICONDIST widens only the gaps between icons.

## 6. Using the dock

- **Click** an icon: Workbench opens it (program, project, drawer, disk).
- **Drag** an icon along the dock: the others make way; release outside the
  dock: removed (an empty dock stays as a bar with three places to drop on).
- **One row:** the dock never wraps. It holds as many icons as fit along its
  screen edge at the icon size and spacing in use, keeping the gap to the
  edge (DISTANCE) at both ends too; LIMIT allows fewer. A full dock takes no
  more: Workbench icons over a gap show the dock greyed (a 50 % dither in the
  dock colour, as a disabled gadget) and a drop there only beeps; onto the
  middle of an icon still opens them with it. ARexx ADD returns RC 10.
- **Too many icons** (a settings file with more than fit): the first ones are
  shown, the rest wait at the end of the list - kept in the file, shown when
  there is room - and a message says how many once per file. In the settings
  window a choice that would leave more icons out than before (bigger icons,
  wider spacing, another edge) beeps and goes back to the old value.
- **Drop Workbench icons** on the dock: between two icons (the outer fifth of
  each, and the space between them) the icons make way and the dropped ones are
  added there; onto the middle of an icon nothing moves, the icon is shown
  pressed, and the dropped icons are opened with it (a text file onto an
  editor), as with the macOS dock.
- **Hotkey** (HOTKEY, default Ctrl+Alt+D): with auto-hide the dock comes out and
  stays out until the pointer has been on it and left (or the hotkey again) -
  press it before dragging an icon onto a hidden dock (Workbench ends an icon
  drag when a window changes, so the dock cannot come out during the drag);
  without auto-hide the dock comes to the front.
- **Focus:** the dock never keeps it - after a click or drag, the window that
  was active before gets it back.
- **In front** (ONTOP): about 0.2 s after a click, and when the pointer comes
  onto the dock, a window covering it is put behind it. Never while the left
  button is held (it would end a Workbench icon drag).
- **Auto-hide:** the dock slides into its screen edge 0.15 s after the pointer
  has left it and slides out when the pointer touches that edge over the
  dock; a 1-pixel strip stays at the edge. While the left button is held
  nothing moves (a window change would end a Workbench icon drag). Drops on
  the hidden strip are ignored: Workbench locks the screen during an icon
  drag, so the dock cannot come out to show where the icon would go.

## 7. Dock menu and settings window

A "Dock" menu title in the Workbench menu strip (workbench.library V45+); no
right-click menu on the dock (the right button stays with the system):

- Settings... - opens the settings window (below)
- About... - version, copyright, licence, the project page and the hotkey
  (a requester that does not stop the dock)
- Quit

The menu is added once and never changed: Workbench 3.2 keeps about 3.8 KB
each time an AppMenu with sub-menus is removed and added again (measured), so
the settings are not in the menu.

The settings window (GadTools, on the dock's screen, two columns on low
screens) has one list per setting, showing the values in use. The lists are
ReAction choosers (chooser.gadget, part of OS 3.2) that drop down; if that
class does not open, GadTools cycle gadgets take their place (each click
steps on). Choosers are BOOPSI gadgets outside the GadTools list: added
after the window opens, their names drawn by SimpleDock.

- Position: Top / Bottom / Left / Right
- Text: Below icons / On hover / Off / Labels only (NOTEXT, HOVERTEXT, NOICON)
- Icon size: Original / Small / Medium / Large (SCALEICONW/H 0 / 32 / 48 / 64)
- Icon spacing: Compact / Normal / Wide / Extra wide (ICONDIST 0 / 2 / 6 / 12)
- Gap to edge: None / Small / Medium / Large (DISTANCE 0 / 4 / 8 / 16)
- Auto-hide: Off / Fast / Normal / Slow / Very slow (AUTOHIDE 0 / 2 / 3 / 5 / 8)
- In front: Off / On (ONTOP)
- Colour: Dark grey / Graphite / Black / Grey / Light grey / Blue (TINT) /
  the custom colour (CUSTOMTINT, if there is one, shown as #RRGGBB). The
  Custom... button beside the list (not a list entry, so stepping through
  the cycle-gadget fallback never opens it) opens a colour picker (red, green, blue sliders,
  preview); OK makes it the custom colour
  (one only) and uses it. A TINT from the file that is none of the six
  becomes the custom colour. The colour is used solid or, with Transparent
  on, as the tint.
- Transparent: Off / On (TRANSPARENT)
- Opacity: 30 / 45 / 55 / 70 / 85 % (OPACITY, transparent only)
- Corners: Square / Small / Medium / Large (RADIUS 0 / 6 / 12 / 18)
- Hotkey: a text field, a commodities description such as `ctrl alt d`
  (HOTKEY); takes effect with Return or when the window closes; a description that is not
  valid beeps and the one in use comes back

A value set in the file or tooltypes that is not one of these shows as
"Other". **Advanced >>** folds out every other keyword to the right of a
divider (on a screen too small for both, in place of the simple settings;
**<< Simple** goes back): alignment (POS letter), OFFSET, DISTANCE,
ICONDIST, icon size (SCALEICONW and SCALEICONH together), LIMIT, RADIUS,
OPACITY, AUTOHIDE, DELAY, TINT, FONT, PUBSCREEN, NOBORDER, Messages
(Normal / Quiet / Verbose = QUIET, VERBOSE), Crash reports (CRASHLOG) - each
labelled with its keyword
and applied through the code of ARexx SET. Both sides show the values in use
after every change. A change shows on the dock at once. Buttons: Recall and
Advanced >> / Load... and Save as... (with the Advanced side one row: Recall,
Load..., Save as..., << Simple). Closing the window keeps the changes: if
the settings differ from those of the moment it opened (or a file was
loaded), the dock's own config file is written. Recall restores a copy of
all settings lines taken when the window opened (keywords set meanwhile are
switched off) and stays greyed out while nothing differs from it. Load...
reads another config file into the dock (the dock's own file stays the one
saved to and read at start; the window reopens with its values, which Recall
then goes back to); Save as... writes a copy elsewhere. ARexx SET, LOAD and
RELOAD close the window first, keeping its changes; so does quitting. Icons
added, moved or removed with the mouse (and the missing-app Remove) are
written to the file at once; ARexx changes only with SAVE or the next change
by hand. While the window is open it stays in front of the dock (also with
In front on), and an auto-hide dock is held out (ah_show: the input handler
treats it as the pointer on the dock); closing the window lets it hide again.

## 8. Design requirements

| # | Requirement |
|---|---|
| D1 | The program sleeps in Wait() unless something needs time: an auto-hide slide or delay, a glide. |
| D2 | The input handler only compares the pointer with the dock box and signals on changes. |
| D3 | One off-screen image per dock; a redraw or an animation frame is one blit. |
| D4 | Shrunk icons are rendered once per size; masks in chip RAM on planar screens. |
| D5 | Nothing in the main task waits with Delay(); animations are timer-driven. |
| D6 | One task; data shared with the input handler or a Workbench hook is read-only there or locked. |
| D7 | Every allocation checked; every error path frees exactly what it allocated. |
| D8 | Exit always completes. |
| D9 | No fixed-size buffers for paths or lines without a length check; no user text used as a format string. |
| D10 | 68020+ code, small data, no debug output in release builds. |
| D11 | Drawing into the dock's own bitmaps (no layer, no clipping) is always clamped to the bitmap. |

## 9. Targets

Measured in FS-UAE (A1200, 68020, 8 MB fast RAM, AGA 8-bit Workbench 3.2)
with a 16-icon dock: idle CPU with the pointer away from the dock at the noise
floor (measured 0.06 %); start-up to dock visible a few seconds at most on
that machine.
