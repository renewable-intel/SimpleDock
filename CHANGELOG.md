# Changelog

## 1.1 (16.10.2026)

- Running programs: a dot under the icon, and the number of copies on it,
  from 2 up to 99 (or in the hover label with Running programs set to
  Dot).
- A click on a running program brings all its windows (every copy) to the
  front; clicking again goes through them. Shift+click starts a new copy.
- Long press on a running program: a list of its windows, oldest first.
  Let go over a window to bring it forward; let go elsewhere and the list
  stays open: click a window, or point at one and press Del to close it (a
  Shell gets Ctrl+\). The last row, below a line, is Close (one window) or
  Close All (several: every window of the program).
- Del with the pointer over a running program's icon closes the program
  (not over a divider). Del held down in the list acts once.
  A window being closed from the dock is made active first (IBrowse left
  a small box behind when it was not).
- The long-press list also offers Keep in Dock / Remove from Dock, Show in
  Workbench and Minimise / Show (hides a program's windows; pulsing dot
  while minimised). A click on the icon brings its showing windows
  forward and leaves minimised ones alone - unless all are minimised,
  then it brings them back. With several
  windows, Minimise... / Show... / Close... list them to pick one at a
  time (the list stays open for more), or All. Show gives the number
  minimised ("Show (2)..."); with some minimised and some not, the list
  has both Minimise and Show. A minimised drawer opened again from
  Workbench (or a minimised window its program makes active) is shown.
  On 3.1-based systems a window shown again
  when SimpleDock quits is redrawn properly (IBrowse came back broken).
- Workbench icon: while drawer windows are open, a Workbench icon sits
  after the divider, in front of the other programs (behind a second
  divider), with the number of windows. Click: all drawer windows to the
  front; long press: their list, with Minimise / Show and Close.
- Docked drawers and disks that are open in Workbench get a dot, a click
  brings the window forward, and a long press lists their windows.
- Shell windows on the standard console are found too (each Shell's
  console is asked for its window): the Shell icon's long press lists
  them, with Minimise / Show and Close; Del over the icon closes them.
- In front: also checked when another window comes to the front (a program
  opening its window late, e.g. IBrowse, no longer stays over the dock),
  every 2 s while the dock shows, and after a click; the long-press list
  is kept in front too.
- New default hotkey: Ctrl+Alt+Shift, pressed together and let go (any
  combination of modifiers can be set this way).
- SimpleDock starts program icons itself, as Workbench does, so it knows at
  once when a program ends. Programs started elsewhere are noticed when one
  of their windows becomes active, or within 2 seconds.
- Programs that are not on the dock show at its end, behind a divider, and
  work like dock icons. Drag one in front of the divider to keep it (a file
  requester asks where it is if needed), or off the dock to hide it (after
  asking). Title bar clocks and similar strips are left out. When the dock
  is full, its last place lists the rest. With auto-hide the dock is
  updated while it is hidden.
- The Shell icon counts open Shells and works like a program icon (also with
  KingCON and other console replacements).
- Files dropped onto a drawer or disk icon are copied into it (or moved on
  the same disk, like Workbench, with DROPMOVE). A file of the same name is
  replaced only after asking; drawers are not merged.
- New Advanced settings: Running programs (RUNNING), Click when running
  (CLICKNEW), Start programs with (WBSTART), Drop on a drawer (DROPMOVE),
  Long press (HOLDTIME), Programs not on the dock (RUNHIDE), Workbench icon
  (WBHIDE), Hidden programs (IGNORE, a list with Remove).
- Settings window: both sides in groups with headings, labels without the
  keywords, Opacity greyed out while Transparent is Off, Recall renamed
  Undo changes. Dock menu: Show / Hide Dock. About shows the version only.
- Auto-hide: the dock stays out up to one icon beyond its ends.
- Bigger "?" on missing icons when the icons are large.
- Labels stay sharp when the icon cells change width.
- Transparent dock on 16-bit screens: no faint square behind each icon.
- Runs on AmigaOS 3.1.4, 3.5 and 3.9 too (best on 3.2): the few 3.2-only
  extras fall back or are left out there.
- A long press that opened the window list can still turn into a drag.
- Settings: a double-click marks all of a text field.
- The dock is 3 pixels taller (room for the running dot).
- Fixes from a code audit:
  - A program started from the dock and still running when the dock quit
    kept its code in memory after it ended (about 14 KB for Clock).
  - A program that does not answer close requests (Clock on 3.1) could
    no longer be closed from the dock once a new copy got the same
    address.
  - The settings window wrote to address 0 when it opened on 3.2.
  - A setting given twice in the settings file made the dock ignore all
    of them; now the later line counts.
  - Too little memory while reading the settings file no longer drops
    the settings (and the next save no longer writes them away).
  - Low memory could make a drop onto a docked drawer copy from the boot
    disk instead; such an item is now refused (beep).
  - Del over an icon closes only the program that was under the pointer
    when the key was pressed.
  - Nothing changes windows while you drag a dock icon (a Close All
    waits); quitting ends a drag first.
  - The hotkey and Show / Hide Dock bring the dock back after it was
    closed (ARexx HIDE, or a reopen that failed).
  - The settings window and the colour picker keep their screen while
    they are open.
  - ARexx: messages that are not ARexx messages are answered unread; SET
    drops a newline inside a value; Undo changes works with a disabled
    setting whose value has a space.
  - On a 68000 or 68010 the dock says it needs a 68020 instead of
    crashing.

## 1.0 (15.10.2026)

First release.
