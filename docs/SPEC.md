# SimpleDock — specification

What SimpleDock does, its settings, files and ARexx commands.

## 1. Scope

- **Platform:** AmigaOS 3.2, or 3.1.4 / 3.5 / 3.9 (workbench.library and
  icon.library 44 needed; chooser.gadget, string.gadget V47, HideWindow
  V46 / HideLayer V45 and the V45 Workbench menu title are used only where
  present, with fallbacks); minimum a 68030 with fast RAM (the
  code is 68020 code and runs on a stock A1200, but its animations are too
  slow there); recommended a 68040/68060 or an accelerated Amiga (PiStorm /
  Emu68) with a graphics card (Picasso96) and a 16- or 24-bit screen for the
  transparent dock. AGA (8-bit) works, more slowly.
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
| HOTKEY/K | key to bring the dock out: a commodities description, or modifier names only (held together, then let go with nothing else between; default `ctrl alt shift`), or `mbutton` / modifiers then `mbutton` (the middle mouse button pressed with exactly those modifiers; the press and its release are swallowed) |
| MINKEY/K, MINALLKEY/K, AHKEY/K | the same form: minimise the active window (default `ctrl alt m`), minimise all - by what is active: a drawer, all drawer windows; a program's window, all its windows; nothing, every window; nothing showing, bring them back (default `ctrl alt shift m`), auto-hide on/off (default `ctrl alt h`); `none` switches one off |
| SHELLASK/S | icons set to start from a Shell show the "Execute a file" requester first (DONOTPROMPT icons start at once) |
| SHELLDROP/K/N | icons dropped into a Shell window (or onto the dock's Shell icon) have their paths typed at the cursor: 1 on (default), 0 off |
| UNPACKTO/K/N | where the unpack command for a dropped archive points: 0 the Shell's current drawer (default), 1 RAM:, 2 UNPACKDIR |
| UNPACKDIR/K | the drawer for UNPACKTO=2 |
| NOBEEP/S | no beep (screen flash) on a refused action or a failure |
| TOOLSMENU/S | the Dock menu's four items in the Tools menu, no Dock title (always so on workbench.library < 45); the menu is rebuilt when the setting changes |
| CUSTOMTINT/K | the settings window's custom colour `#rrggbb` (its Colour list) |
| RUNNING/K/N | running programs: 0 off (no dots, no looks), 1 dot, 2 dot and number (default) |
| CLICKNEW/S | a click on a running program starts a new copy; Shift+click brings it to the front |
| WBSTART/S | Workbench starts program icons (OpenWorkbenchObject), not the dock's launcher |
| DROPMOVE/S | drops onto a dock drawer move (rename) when on the same disk (SameDevice), like Workbench |
| HOLDTIME/K/N | long press in tenths of a second (Intuition ticks), default 3, 1-50 |
| RUNHIDE/S | programs running that are not on the dock are not shown at its end |
| IGNORE/K | programs never shown there, "Name,Name" (any case); a running other dragged off the dock is added |
| WBHIDE/S | no Workbench icon for Workbench's drawer windows |
| CRASHLOG/S | crash reports: a trap handler for the dock's task (no cost until an error); a crash writes `ENVARC:SimpleDock-crash.txt` (error, section and offset - the linker map `build/SimpleDock.map` names the function - registers, system, settings), tells the user and closes, the input handler first |

Where settings come from: the icon's tooltypes, then the config file's
settings lines over them (a switch in parentheses there, `(NOTEXT)`, is off even
if the icon has it), then a Shell command line over both.

Several docks: a config file with a project icon whose default tool is
SimpleDock starts a dock with that file; the file name is then its ARexx port
name. Save as... writes such an icon next to the new file (default tool: this
program's full path; an existing icon is kept), so a copy made there is a dock
on double-click. CONFIG=file from a Shell does the same: when the port
SIMPLEDOCK is taken and the file is not the default one, the port is the file's
name.

Keys: a description naming Left Amiga with N or M (Intuition's screen keys) is
refused like an invalid one (the dock's handler, at priority 51, would take it
from Intuition).

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
- Lines longer than 510 characters are reported and skipped, not truncated.
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
| SAVE [file] | settings and icons into the dock's own file, or into file |
| RELOAD / LOAD file | read the dock's own file again / another file |
| SETTINGS [ADVANCED] | open the settings window (or bring it to the front), folded out with ADVANCED |
| BENCH | RESULT = CPU, time to open the dock, a full redraw, an animation frame (one icon's place), the background, and the screen (the dock flickers while it runs) |
| DROPINFO | RESULT = what the dock sees of the Shell windows (console task, window, AppWindow) and the last drops, for bug reports |
| HOTKEYINFO | RESULT = the hotkey's counters (presses seen, cancelled by a button, fired) and the dock's state, for bug reports |

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
comes out (the screen behind is uncovered then). When the windows behind a
dock that stays shown change (a click later, dock_settle), it steps aside to
its edge strip, waits until the windows behind have redrawn (no LAYERREFRESH
pending over its box, checked every 2 ms with timer.device, at most 0.12 s),
takes a new snapshot and comes back: a short blink, as 3.2 cannot show what
is under a window. A thin lighter border
follows the rounded shape. Icons sit 6 pixels from the top and bottom border
(RADIUS 12); ICONDIST widens only the gaps between icons.

## 6. Using the dock

- **Click** an icon: Workbench opens it (program, project, drawer, disk). A
  program icon (WBTOOL) whose program runs is brought to the front instead:
  all its windows (every copy; WindowToFront back to front, so their order
  stays), the frontmost active, its screen to the front; if they were all
  in front already (no other program's window above one of them on its
  screen) and it was the active program, the backmost one comes forward,
  so clicks go through all its windows. **Shift+click** starts a new copy (CLICKNEW swaps click and Shift+click). **Holding** the button
  HOLDTIME (default 0.3 s; IDCMP_INTUITICKS, only while pressed) on a running program opens a
  list of its windows beside the icon (dock look), in the order the dock first
  saw them (a table of window + task -> number, kept by every look; gone
  windows dropped), so rows and "(2)" numbers stay put. Button up over a
  title brings that window to the front (over Close / Close All: that).
  Released anywhere else, the list stays open: the input handler then takes
  clicks on it (not
  passed on, so the list window is never activated), Del and Esc, and
  reports pointer moves and clicks elsewhere (LE_* flags). Click on a title
  = to the front; Esc closes the list; Del away from the rows closes it too (and,
  over a running icon, that program); a press on the dock closes a list left
  open and does nothing else (its icon is not clicked); Del on a title = (the list of one program's windows ends with a divider and
  "Close" for one window or "Close All" for several: every window of the
  program, also beyond the rows shown;
  one request per task at a time - further windows of the same task wait
  in a queue until it answered plus 0.5 s (Workbench's: at once), gone ones
  skipped, 10 s at most, never twice; an unanswered request is never sent
  again (pending_to) until answered or its program ends (close_prune: then
  its message, still out, is kept in orphan[] so quitting can set its reply
  port to NULL);
  beep if one could not be closed) the window shown if minimised, put to
  the front and made active (not Workbench's: some programs, IBrowse,
  only finish quitting when active), then a fake IDCMP_CLOSEWINDOW IntuiMessage put to that window's
  port under LockIBase (only if it has CLOSEWINDOW in its IDCMP), reply to
  the dock's port; console windows (Shells) take no such message, so a
  Shell window is activated and gets Ctrl+\ (end-of-file, raw key 0x0D with
  CTRL through input.device), which ends an idle Shell as its close gadget
  does. The row goes, the list shrinks; Esc closes it, a click elsewhere
  closes it when the button comes up (a window closed while the button is
  held would wait for Workbench's drag or selection). Auto-hide keeps the
  dock out while the list is open.
- **List actions** (after the windows and a divider): Keep in Dock (a running
  other becomes kept, at the end of the kept icons; the requester if its
  file is unknown) / Remove from Dock (kept; it comes back as a running
  other if it runs), Show in Workbench (OpenWorkbenchObject of its drawer;
  only with a file in a drawer), Minimise / Show (HideWindow on each of
  its windows with intuition V46+, else HideLayer on the window's layers
  with layers V45+ and the Workbench backdrop made active; kept in
  mini[64] (an entry stays after a Show, marked shown, until a look sees
  the window showing - so quitting shows it if the Show did not take; one
  shown by someone else goes after two looks; a new window at the same
  address is not ours); on the layers path HideLayer/ShowLayer run with
  the screen's LayerInfo locked and the window checked again; a window
  counts as minimised when its layer has LAYERHIDDEN;
  Show, a picked row, quitting/crash recovery, or a click on the icon when
  all its windows are minimised (with some showing, a click brings only
  those forward: run_front -> front(it, FALSE)) shows
  them again the way they were hidden: ShowWindow (V46+), else ShowLayer
  followed by WindowToFront (a minimised window made active by someone
  else - Workbench opening a drawer that is minimised - is shown again:
  run_wake on the input handler's active-window change; the dock never
  makes a minimised window active itself: hide_win activates the
  backdrop if it was active, unfocus skips hidden windows) (Intuition 41 knows nothing of ShowLayer:
  without it the program never got the refresh it needed - broken
  windows after quitting the dock on CaffeineOS); never both;
  pulsing dot; a minimised program's icon is never dragged, as our drag
  locks the screen's layers; Minimise while one of its listed windows
  shows, Show "Show (n)" while n are minimised - both when both; Del of a
  row drops the ones no longer true). And Close. With more than one window to
  hide, show or close the action reads Minimise... / Show... / Close...
  (lmode): the list is rebuilt with just those windows and an All row
  (Minimise All / Show All / Close All) and stays open (sticky); a row
  does that window only and goes off the list (pick_row, list_drop; a
  window gone meanwhile just goes), the list closes when no row is left;
  All does them all; Del there closes in Close... and does nothing in the
  others; Esc or a click elsewhere closes it. A kept program that is not running gets the list
  with its actions only.
- **Del over a running icon** (the pointer over its cell, the dock fully out,
  no list, no button held, no Ctrl/Alt/Amiga): the input handler takes the
  key (and its key-up) when runcell[] marks the cell (kept up to date by
  the dock task from the icons' counts) and signals the dock, which closes
  the program as Close All does. Any other Del goes on to the active window.
- **Running:** a 5x3 dot in a 3-pixel band under every cell (RUNBAND), and on
  the icon's top right a count badge from two copies on (up to 99). With
  RUNNING=1 (dot only) the hover label reads "Name (n running)" instead
  (redrawn when the count changes). A
  program is known by its windows: the task an IDCMP port signals, by its
  name (a Shell command's name while it runs); own and Workbench windows
  excluded. The Shell icon (file name Shell) counts the interactive,
  non-background CLIs instead (FindCliProc), as Shell windows belong to the
  console handler (CON:, KingCON ...); its windows are those owned by the
  console handlers (pr_ConsoleTask->mp_SigTask) of those Shells, so click,
  Shift+click and the held list work as for programs.
  **Launcher:** program icons are started by the dock itself, as Workbench
  does (run_launch): LoadSeg, CreateNewProc (no NP_FreeSeglist: the dock
  unloads sm_Segment when the message comes back, so a program can keep its
  code by setting it to 0; stack from the icon, at least Workbench's default
  stack, TOOLPRI, NP_HomeDir = the program's drawer for PROGDIR:,
  NP_CurrentDir likewise), and a WBStartup (arg 0 = drawer lock + name, then
  dropped files with DupLocks) whose reply port is the dock's: when the
  program ends the message comes back, its locks are freed and its icon
  updated at once. Programs the dock started count by their task (whatever
  their name). If the dock quits first, the outstanding messages get a NULL
  reply port (Exec marks them free) and stay in memory; their processes get
  PRF_FREESEGLIST so AmigaDOS frees the code when they end (only if the task
  is still there with the dock's seglist and sm_Segment is not 0). A program
  whose task is gone without answering (a Shell-only tool) is noticed at the
  next look and freed. No program file
  (the Shell icon), no memory or 64 running: Workbench starts it instead.
  **Running others** (RUNNING not 0, no RUNHIDE): programs with a window
  that belong to no kept icon (also those in d->extra) get an item of their
  own at the dock's end (Item.temp = RUN_TEMP, never saved, not seen by
  ARexx LIST / REMOVE), behind the divider: an 8-pixel space (DIVGAP) with
  a light grey line in its middle (dark grey on a light dock colour), drawn
  separately from the cells (render_divider; it glides with the icons, and
  hit tests, hover labels, drag gaps and drop zones skip its space through
  dock_along). While a running other is dragged to be kept the divider
  moves behind its place (dragkeep). Grouped by program name (a Shell command's name; launches by
  the dock by task); console windows that are not a Shell's (CON, RAW,
  KingCON, ... or a Shell console handler's name) are left out, so are
  widgets (borderless, no close/depth/drag/size gadget, less than half the
  screen in one direction: title-bar clocks) and IGNORE names. Shells: one
  "Shell" item while AmigaDOS knows an interactive Shell and no Shell icon
  is kept. The standard console handler's windows have no IDCMP port, so
  they can't be found by window: collect() asks each Shell's console port
  (pr_ConsoleTask) with an ACTION_DISK_INFO packet, put without waiting
  (con_ask, under the Forbid of the Shell walk); the answer (id_VolumeNode
  = its window) is taken at the next look (con_reap, cport); a port-less
  window in the layers whose console answered with it is collected with
  that console's task (con_task; same_win checks it the same way). Asked
  again when the number of Shells changes; never twice to a console that
  hasn't answered; a long press on the Shell icon waits up to 0.1 s for
  answers; at quit up to 1 s, then a question still out keeps the port
  (PA_IGNORE) so its late answer goes nowhere harmful. Workbench: its windows (task
  "Workbench", not the backdrop) are collected with Own.wb and no name, so
  no program matches them; while there is one, a "Workbench" item
  (RUN_WB, nofile, standard drawer icon, number = its windows) is first of
  the running others (run_layout_in moves it to the front of d->runs),
  right after the first divider; a second divider follows it when
  programs do (divb[2] / divx[2] / divtx[2] glide; gapc[2] are their places
  at rest for dock_along and the drop zones; DIVGAP each). Click:
  run_front, never a new copy; list actions Minimise / Show and Close (no
  Keep, no Show in Workbench); never dragged; a drop onto it is a new icon
  after the kept ones; docked drawers, disks and the Trash
  (run_is_drawer) are linked to their windows: Workbench's open-drawer list
  (WBCTRLA_GetOpenDrawerList, V44) compared with the icon's path by
  SameLock gives the title (wbname: last part, or the disk's name) its
  windows have (title_is: that, followed by the end, a space or ':', or
  the last part of a full-path title); looked up when Workbench's windows
  or the icons change, once more on the next look; a click brings them to
  the front (else opens it), the long press lists them; Workbench windows without a
  close gadget are not sent a close; WBHIDE or IGNORE "Workbench" hides it (run_free_hidden); Close All sends its windows their close one right after the other answer (Own.wb in cq[]), programs 0.5 s apart. Program file, looked up once: Workbench's
  program list (WBCTRLA_GetProgramList, full paths), a Shell command with a
  full path, SYS:Utilities, Tools, System, Prefs, Tools/Commodities, the
  dock's own Shell path; else the item has no file (default tool icon,
  Shift+click beeps; kept only after an ASL requester "Where is name?"
  finds it). Order: first seen, new ones glide in at the end. Layout: kept
  icons first; then as many running others as fit; if not all fit, the
  last place is the "more" item (RUN_MORE: three dots, the number of those
  not shown as its badge; a click opens the held list of their frontmost
  windows, "Name (n running)"); a full dock with programs to show gives the
  last kept icon's place to it (d->pushed, not counted in the "do not fit"
  note). Dragging: kept icons stay before the divider; a running other
  dropped before the divider (pointer along the dock < pad + nkept * cell +
  cell / 4) becomes kept (saved), into the gap nearest the pointer (right
  half of an icon: after it), off the dock it is hidden after a requester
  (Hide / Cancel; its name added to IGNORE, saved), elsewhere it glides back;
  a press held until the window list opened becomes a drag when the pointer
  moves along the dock (inside it, more along than across, over half a cell;
  the list closes), and such a drag let go off the dock goes back (nothing
  removed or hidden); a kept icon dragged
  off while its program runs comes back as a running other. Drops from
  Workbench: gaps only up to the divider, onto a running other = open
  with it. The section changes while the dock is fully out (no button,
  drag or glide, no held list: dock_reopen_glide) or, with auto-hide,
  fully hidden (dock_relayout_hidden: layout, new snapshot, the edge strip
  moved - no regular looks while hidden, one look 0.15 s after another
  window became active), so it comes out finished. Cached label images are remade when the cell width
  changes.
  For programs started elsewhere AmigaOS sends no notice, so the windows are
  looked at (under Forbid, copying only) when another window becomes active
  (the input handler watches IntuitionBase->ActiveWindow; 0.15 s later), at
  a click, at a hold, every 2 s
  while the dock is on screen (not while auto-hide keeps it away), every
  0.5 s for 3 s after a click, and 0.3 s later when a look is put off
  (sliding, button held); when the windows, Shells and icons are the same as
  last time nothing more is done. Icons are redrawn only when a count changed.
  Not seen: renamed tasks, scripts, programs without windows. Held list:
  same titles numbered ("Clock", "Clock (2)").
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
  editor), as with the macOS dock. Onto a **drawer or disk** icon they are
  copied into it (dropcopy.c, its own copy, no Shell: drawers with all they
  hold, dates, protection bits and comments kept; drawers reached through
  links are left out, at most 32 deep), then the `.info` if there is one,
  then UpdateWorkbench so an open window shows it; in a process of its own
  (the dock waits for it when quitting, also after a crash). A file of the
  same name already there: requester Replace / Skip; a drawer of the same
  name (or file vs drawer): beep, left alone. Refused with a beep: whole disks, objects
  already in that drawer, the drawer itself or one of its parents (SameLock
  up the ParentDir chain). Onto the Trash: beep, nothing.
- **Hotkey** (HOTKEY, default Ctrl+Alt+Shift tapped): with auto-hide the dock comes out and
  stays out until the pointer has been on it and left (or the hotkey again) -
  press it before dragging an icon onto a hidden dock (Workbench ends an icon
  drag when a window changes, so the dock cannot come out during the drag);
  without auto-hide the dock comes to the front.
- **Focus:** the dock never keeps it - after a click or drag, the window that
  was active before gets it back.
- **In front** (ONTOP): about 0.2 s after a click, when the pointer comes
  onto the dock, and shortly after another window becomes the screen's
  front layer (the input handler compares LayerInfo.top_layer each batch),
  after a background snapshot (ah_peek_end), and at every look of the
  running programs (2 s while shown: a window that grew or moved over it),
  a window covering the dock or its held list is put behind them. A check
  due while the button is down waits for its release. Minimised (hidden)
  layers and the dock's own windows (its task's, and the copy process's
  requesters) don't count. Never while the left button is held (it would
  end a Workbench icon drag), while the settings window is open, or for
  the hidden dock's edge strip; at most 3 raises with no key or button
  used in between (userin, set by the input handler), so two windows that
  both stay in front can't fight forever. Switching In front on in the
  settings window raises at once (dock_raise_now).
- **Auto-hide:** the dock slides into its screen edge 0.15 s after the pointer
  has left it (its box, the gap to the edge, 6 px across and one icon cell
  beyond each end along it) and slides out when the pointer touches that edge
  over the dock; a 1-pixel strip stays at the edge. While the left button is held
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

The settings window (GadTools, on the dock's screen; more columns on low
screens, up to three, breaking before a heading when its group would not
fit: columns()) has one list per setting, showing the values in use, in
groups under bold headings with a line: simple side Place (Position, Gap
to edge, Auto-hide, In front, Hotkey), Icons (Icon size, Icon spacing,
Text), Look (Colour, Transparent, Opacity - greyed out while Transparent
is off -, Corners). The lists are
ReAction choosers (chooser.gadget, part of OS 3.2) that drop down; if that
class does not open, GadTools cycle gadgets take their place (each click
steps on). Choosers are BOOPSI gadgets outside the GadTools list: added
after the window opens, their names drawn by SimpleDock. The Advanced text
fields (TINT, FONT, PUBSCREEN) are ReAction string gadgets when
string.gadget V47 (OS 3.2's) opens, so a double-click marks all of the
text; else GadTools string gadgets.

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
**<< Simple** goes back), in three groups. Fine-tune: alignment (POS
letter), OFFSET, DISTANCE, ICONDIST, icon size (SCALEICONW and SCALEICONH
together), LIMIT, RADIUS, OPACITY (greyed out while not transparent),
AUTOHIDE, TINT. Programs: Running programs (RUNNING: Off / Dot / Dot and
number), Click when running (CLICKNEW: Bring to front / New copy), Start
programs with (WBSTART: SimpleDock / Workbench), Drop on a drawer (DROPMOVE:
Copy / Like Workbench), Long press (HOLDTIME: Normal 0.3 / Long 0.4 / Longer
0.5 s), Programs not on the dock (RUNHIDE: Show / Hide), Workbench icon
(WBHIDE: Show / Hide), Hidden programs (IGNORE: a GadTools listview of the
names, 3 rows, with Remove, which writes IGNORE without the picked name).
System: FONT, PUBSCREEN, DELAY (1/50 s), Messages (Normal / Quiet / Verbose =
QUIET, VERBOSE), Crash reports (CRASHLOG), NOBORDER. Labels carry no
keywords (the ReadMe lists them); every change is applied through the code
of ARexx SET. Both sides show the values in use
after every change. A change shows on the dock at once. Buttons: Undo changes and
Advanced >> / Load... and Save as... (with the Advanced side one row: Undo changes,
Load..., Save as..., << Simple). Closing the window keeps the changes: if
the settings differ from those of the moment it opened (or a file was
loaded), the dock's own config file is written. Undo changes restores a copy of
all settings lines taken when the window opened (keywords set meanwhile are
switched off) and stays greyed out while nothing differs from it. Load...
reads another config file into the dock (the dock's own file stays the one
saved to and read at start; the window reopens with its values, which Undo changes
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
| D1 | The program sleeps in Wait() unless something needs time: an auto-hide slide or delay, a glide, the look for running programs (every 2 s while the dock is on screen; none with RUNNING=0). |
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
