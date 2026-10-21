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
  - Closing a window from the dock waits until the window is really in
    front; a program that closed at once (3.1's Clock) left Intuition
    reading the freed window.
  - Closing a minimised window on 3.1-based systems waits a moment after
    showing it again (a program could crash, and the dock with it).
  - Programs started from the dock get the same start as from Workbench
    (no input/output handles): a program that closes its own (iGame) made
    the system close them twice, and the dock crashed when it ended.
- Project icons (documents, DOSDriver icons such as a Mount icon) open as
  with a double-click: the dock starts their default tool with the
  project. Workbench's own call refused some of them.
- A default tool given without a path is looked for as Workbench does:
  in the project's drawer, then in Workbench's path (then in C:).
- Icons set to start from a Shell (CLI tooltype, "Start from: Shell"),
  ARexx icons (REXX tooltype) and programs with no icon of their own are
  handed to Workbench, which starts them its own way (its "Execute"
  requester unless the icon has DONOTPROMPT). If Workbench refuses one,
  the dock runs it from a Shell itself, in an output window that opens on
  the first output.
- With Messages set to Verbose, the dock says why icons could not be
  shrunk to the icon size, if that happens.
- Force Quit: once Close was tried on a program, its long-press list
  offers Force Quit... It first sends the break signals (Ctrl+C, D, E,
  F), which stop Shell programs and ports; if the program is still there
  after two seconds, it asks, and ends the program (every copy, also one
  with no window): its tasks are removed
  and their windows (and a screen of its own) closed, and a Shell
  process's entry in the Shell list cleared (Status showed a ghost, and
  Break on it would have touched freed memory). As with
  Scout's Remove, what the program held stays in use until a reboot, and
  a program ended inside a system call can take the system down - the
  requester says so.
- The hotkey tap (Ctrl+Alt+Shift) no longer fails when the mouse is
  moving while the keys are pressed (every movement event cancelled it:
  a wrong comparison with the "no button" code).
- The hotkey tap (Ctrl+Alt+Shift) is recognised from the modifier keys'
  own press and release events, not only from the qualifier field the
  keyboard driver fills in (a USB keyboard through a PiStorm could leave
  it wrong, and taps were lost); a mouse-button release the dock missed
  no longer blocks the hotkey (the next key event corrects it).
- The hotkey toggles on what is seen: a dock that is out (and not
  covered) hides and stays hidden until the pointer has left the edge;
  otherwise it comes out in front. Before, a dock out because the pointer
  was near, or out but covered, was only "pinned" by a press, and hidden
  by the next, so presses looked lost.
- Keys: Ctrl+Alt+M minimises the active window; Ctrl+Alt+Shift+M
  minimises all - with a drawer active all drawer windows, with a
  program's window all its windows (every copy; all Shells for a Shell),
  with nothing active (the Workbench backdrop) every window on the screen;
  with nothing showing, it brings them back. Ctrl+Alt+H switches auto-hide
  on or off. All three can be changed or switched off in Advanced, Keys
  (MINKEY, MINALLKEY, AHKEY).
- On 3.1-based systems (CaffeineOS, 3.9), the windows under a window
  that was minimised are redrawn at once (they showed a blank box until
  one was moved).
- Advanced, Shell icons: Ask first shows an "Execute a file" requester
  with the command before an icon set to start from a Shell starts, as a
  double-click does (Workbench skips it when the dock asks it); icons with
  DONOTPROMPT start at once (SHELLASK).
- Icons dropped onto a Shell window have their paths typed at the cursor
  (on 3.1/3.9-based systems such as CaffeineOS, also in KingCON; 3.2's
  Shell does it itself). A single archive gets the command to unpack it, if its tool
  is there (lha, lzx, unzip, tar, gzip, bzip2, xz, 7z, unrar), into the
  Shell's drawer, RAM: or a drawer of your choice (Advanced, Shell drops).
  Icons dropped onto the dock's Shell icon go into the Shell window in
  front the same way (any system, any console) instead of starting a new
  Shell, which is brought to the front; with no Shell open, a new one
  starts and gets them.
- Smaller and lighter: the program is about 18 KB smaller on disk (small
  data model), and it uses about 40 KB less memory while idle: the
  settings window's buffers, the crash report's spare stack and ARexx
  SET's buffers are only taken while needed.

## 1.0 (15.10.2026)

First release.
