/* SimpleDock - a small application dock for AmigaOS 3.2
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SD_H
#define SD_H

#include <exec/types.h>
#include <exec/lists.h>
#include <graphics/rastport.h>

#define SD_NAME    "SimpleDock"
#define SD_VERSION "1.1"
#define SD_BUILD   " T77"            /* test build ("" for a release): shown with the version */
#define SD_DATE    "21.10.2026"

#ifdef SD_DEBUG              /* trace to Output(), flushed (survives a crash) */
void sd_trace(CONST_STRPTR s);
#define TRACE(s) sd_trace(s)
/* time spent in a few drawing steps (debug ARexx PROF) */
#include <devices/timer.h>
extern ULONG sd_prof_us[12], sd_prof_n[12];
void sd_prof(LONG slot, struct timeval *t0);
#define PROF_START(v) struct timeval v; sd_prof(-1, &v)
#define PROF_STOP(slot, v) sd_prof(slot, &v)
#else
#define TRACE(s) ((void)0)
#define PROF_START(v)
#define PROF_STOP(slot, v)
#endif

#define SD_PORTNAME "SIMPLEDOCK"      /* ARexx port; a project config file uses its name */
#define SD_LINEMAX  256               /* config file line, incl. NUL */
#define SD_CONFIG   "ENVARC:SimpleDock.config"   /* settings + icons, when CONFIG is not given */

struct Opts {
    STRPTR config, pos, font, pubscreen, tint, hotkey, customtint, ignore;
    STRPTR minkey, minallkey, ahkey;  /* keys: minimise window / all, auto-hide on/off ("none": off) */
    STRPTR unpackdir;                 /* UNPACKTO=2: archives dropped into a Shell unpack there */
    LONG   shelldrop, unpackto;       /* icons dropped into a Shell typed (1); unpack: here, RAM:, UNPACKDIR */
    LONG   distance, offset, icondist, delay, limit, scalew, scaleh;
    LONG   autohide, ahspeed, radius, opacity;
    BOOL   notext, hovertext, noicon, noborder, ontop, transparent, quiet, verbose, crashlog;
    LONG   running, holdtime;         /* RUNNING 0-2, HOLDTIME tenths of a second */
    BOOL   clicknew, wbstart, dropmove, runhide, wbhide, shellask, nobeep;
};

/* A box in dock image coordinates, x1 / y1 exclusive; empty if x1 <= x0. */
struct Box { WORD x0, y0, x1, y1; };

struct Item {
    struct MinNode node;
    char  *path;                      /* icon path without .info */
    char  *label;                     /* LABEL, or NULL = file part of path */
    char  *image;                     /* IMAGE, or NULL */
    struct DiskObject *dobj;
    WORD   x, y, w, h;                /* cell box in window coordinates (drawn here now) */
    WORD   tx, ty, ox, oy;            /* glide: target and start (anim.c) */
    WORD   sx, sy;                    /* screen position kept over a reopen */
    BOOL   keep;                      /* sx, sy valid */
    BOOL   over;                      /* did not fit: waits in d->extra (dock.c layout) */
    BOOL   missing;                   /* its file was not found: drawn dimmed with a "?" */
    UBYTE *argb;                      /* shrunk icon, > 8 bits: alpha + premultiplied RGB (scale.c) */
    struct BitMap *sbm;               /* shrunk icon, <= 8 bits, with ... */
    UBYTE *smask;                     /* ... its mask */
    struct BitMap *cbm;               /* solid dock: the icon on the dock colour (scale.c) */
    struct BitMap *lbm;               /* solid dock: the label on the dock colour (render.c) ... */
    WORD   lbmw;                      /* ... made for this cell width */
    WORD   iw, ih;                    /* icon image size */
    WORD   lw;                        /* label width in pixels */
    WORD   running;                   /* copies of its program that run (running.c) */
    UBYTE  temp;                      /* not kept: RUN_TEMP a program running, RUN_MORE the "more" cell,
                                         RUN_WB Workbench's drawer windows */
    BOOL   mini;                      /* windows of its program are minimised (running.c) */
    BOOL   asked;                     /* a close request went to its program: Force Quit offered (running.c) */
    BOOL   nofile;                    /* RUN_TEMP: its program file is not known (path = its name) */
    char   wbname[32];                /* a docked drawer or disk open in Workbench: its windows' title */
};
#define RUN_TEMP 1
#define RUN_MORE 2
#define RUN_WB   3

struct Dock {
    struct Opts      *o;
    struct MinList    items;          /* the icons shown, in order */
    struct MinList    extra;          /* then those that do not fit (kept in the file) */
    struct MinList    runs;           /* programs running, not on the dock, not shown (running.c) */
    char              portname[32];
    char              edge, align;    /* T/B/L/R, and T/B/L/R or 'C' */
    struct Screen    *scr;
    struct Window    *win;
    struct TextFont  *font;
    struct BitMap    *raw;            /* the screen under the dock, as last copied (no tint) */
    struct BitMap    *bg;             /* translucent background, window size */
    struct BitMap    *img;            /* bg + icons + labels: what the window shows */
    LONG              openms;         /* the last dock_open took this long (ARexx BENCH) */
    ULONG             gen;            /* dock window generation: drops for an older window are ignored */
    UBYTE            *bgrgb;          /* transparent, > 8 bits: bg as R8G8B8 in fast RAM (render.c) */
    struct BitMap    *strip;          /* hidden: what the 1-pixel strip shows (autohide.c) */
    BOOL              stripshown;     /* the window shows the strip (dock_show) */
    BOOL              relayout;       /* resized in place: all again once Intuition has done it */
    BOOL              overtold;       /* "n icons do not fit" said for this file */
    BOOL              peekwait;       /* stepped aside, a button went down: back after its release */
    WORD              left, top, w, h;
    WORD              radius, pad;
    WORD              spx, spy;       /* space around an icon in its cell: ICONDIST along
                                         the dock, none across it (dock.c) */
    WORD              maxih;          /* tallest icon: icons sit on a common baseline */
    ULONG             tint;           /* 0xRRGGBB */
    LONG              opacity;        /* 0..100 */
    LONG              textpen, shadowpen;
    LONG              fillpen, borderpen; /* solid background: colour and border (-1: none) */
    struct Item      *pressed, *last;
    BOOL              hidden;
    BOOL              cfgdrawer;      /* CONFIG is a drawer: cannot be saved */
    BOOL              cfgfailed;      /* the dock's own file could not be read: never saved over */
    BOOL              cfgcreate;      /* missing: write it once the dock is open (POS, colour known) */
    char              posbuf[4];      /* POS changed in the settings window */
    char              keybuf[64];     /* HOTKEY changed in the settings window */
    char              custbuf[8];     /* CUSTOMTINT (and TINT) from the colour picker: "#rrggbb" */
    char              cfgbuf[SD_LINEMAX]; /* the dock's own config file (o->config) */
    UBYTE            *lut[2];         /* pen tables (8-bit screens), see render.c */
    LONG              lutop[2];       /* their opacities */
    /* auto-hide (autohide.c); vis/full/want are also read by the input handler */
    volatile WORD     vis;            /* visible extent along the slide axis */
    WORD              full;           /* h (top/bottom) or w (left/right) plus the gap to the edge */
    volatile BOOL     want;           /* show (the input handler from the pointer; the dock: hotkey, lists) */
    /* grid (also read by the input handler) */
    WORD              cw, ch, per, ncells;
    WORD              nkept;          /* the first cells: icons kept on the dock; then the running others */
    WORD              ndiv;           /* the cells before the first divider (the kept icons) */
    WORD              gapc[2];        /* the dividers' places at rest: after the kept icons, after the
                                         Workbench icon when programs follow it (-1: none) */
    WORD              pushed;         /* kept icons out of room because of the "more" cell */
    WORD              divb[2];        /* the dividers: in front of these places (-1: none) ... */
    WORD              divx[2], divox[2], divtx[2];   /* ... drawn here along the dock: now, glide start, target */
    BOOL              dragkeep;       /* a running other dragged to be kept (drag.c): the divider after it */
    BOOL              fromlist;       /* the drag began as a long press (window list): off the dock = cancel */
    LONG              divpen;         /* its colour */
    LONG              pulsepen[4];    /* a minimised program's dot: bright to dim (pulse) */
    UBYTE             pulse;          /* its step, 0..7 */
    WORD              fit;            /* icons per row the screen (or LIMIT) allows */
    BOOL              horiz;
    volatile WORD     hover;          /* cell under the pointer, -1 = none (input handler) */
    volatile BOOL     raisecheck;     /* input handler: windows may have changed (dock_settle) */
    volatile BOOL     hotkey;         /* input handler: the hotkey was pressed (ah_hotkey) */
    volatile UBYTE    keyact;         /* input handler: other keys pressed (KEY_MIN ...) */
    volatile BOOL     pinned;         /* auto-hide: out by the hotkey until the pointer was on it and left */
    volatile BOOL     pinentered;
    volatile BOOL     parked;         /* auto-hide: hidden by the hotkey until the pointer has left the edge */
    volatile BOOL     parkleft;
    volatile BOOL     showing;        /* auto-hide: held out while the settings window is open */
    struct Window    *list;           /* the held list of a program's windows (running.c) */
    volatile BOOL     liststicky;     /* ... still open after the button went up */
    volatile UBYTE    listev;         /* ... what the input handler saw then (LE_...) */
    WORD              lbx, lby, lbw, lbh;   /* ... where it is (screen) */
    UWORD             holdticks;      /* how long the button is held on an icon */
    ULONG             behind;         /* the windows behind the dock when the snapshot was taken */
    volatile BOOL     lmb;            /* left button held: maybe a Workbench drag (input handler) */
    volatile BOOL     winchange;      /* input handler: another window became active */
    volatile BOOL     userin;         /* input handler: a key or button was used (dock_raise) */
    UBYTE             runcell[64];    /* the cells of running programs (read by the input handler) */
    struct Item      *cellitem[64];   /* the icon in each cell (read by the input handler) */
    struct Item * volatile delitem;   /* input handler: the icon under the pointer at that Del */
    volatile WORD     delquit;        /* input handler: Del over running cell delquit - 1 (0: none) */
    /* hover label (tooltip.c) */
    struct Window    *tip;
    WORD              tipcell;
    /* drag and drop (drag.c) */
    struct AppWindow *appwin;
    struct Item      *drag;
    volatile BOOL     dragging;
    WORD              downx, downy;   /* where the button went down (window) */
};

#define LABELS(d) ((d)->o->noicon || (!(d)->o->notext && !(d)->o->hovertext))   /* names in the dock (NOICON: */
                                                                               /* always, they are all it shows) */
#define RUNBAND 3                     /* under every icon: room for the running dot */
#define DIVGAP  8                     /* the space at the divider, its line in the middle */
#define SECTION(d) ((d)->ncells > (d)->ndiv)    /* running others shown: the first divider's space is there */
#define NGAPS(d) (((d)->gapc[0] >= 0) + ((d)->gapc[1] >= 0))   /* dividers at rest */
#define LE_MOVE 1                     /* the open list (input handler): the pointer moved */
#define LE_PICK 2                     /*   a click on it */
#define LE_AWAY 4                     /*   a click elsewhere */
#define LE_DEL  8                     /*   Del */
#define LE_ESC  16                    /*   Esc */
/* Icon top in its cell: on a common baseline above labels, else centred. */
#define ICON_DY(d, it) (2 + (d)->spy + (LABELS(d) ? (d)->maxih - (it)->ih : ((d)->maxih - (it)->ih) / 2))
#define FOR_ITEMS(d, it) for ((it) = (struct Item *)(d)->items.mlh_Head; (it)->node.mln_Succ; \
                              (it) = (struct Item *)(it)->node.mln_Succ)
#define SD_EASE(t) ((((t) * (t)) >> 10) * (3072 - 2 * (t)) >> 10)   /* smoothstep, 0..1024 */
#define DOCKFULL(d) ((d)->nkept + (d)->pushed >= (d)->fit)   /* as long as the screen (or LIMIT) allows; */
                                                 /* (pushed: a kept icon gave its place to the "more" cell) */

/* main.c */
void sd_putch(void);                                        /* rawfmt.s, for RawDoFmt */
void sd_beep(struct Dock *d);                               /* DisplayBeep on the dock's screen, unless NOBEEP */
#define SD_FMT(buf, fmt, args) RawDoFmt((STRPTR)(fmt), (APTR)(args), (void (*)())sd_putch, (APTR)(buf))
void sd_msg(struct Dock *d, CONST_STRPTR fmt, ...);
void sd_verbose(struct Dock *d, CONST_STRPTR fmt, ...);
char *sd_strdup(CONST_STRPTR s);

/* options.c */
struct RDArgs *opts_read(struct Opts *o, char *portname, LONG portlen);
BOOL opts_save(struct Dock *d);                             /* drawer docks: into the icon */
BOOL opts_apply(struct Dock *d, STRPTR *lines, CONST_STRPTR from);
LONG opts_lines(struct Dock *d, STRPTR *lines);
BOOL opts_is_setting(CONST_STRPTR line);
BOOL opts_nomem(void);                                     /* last parse failed for want of memory */
LONG opts_key(CONST_STRPTR line);                           /* keyword's template index, -1: none */
BOOL opts_switch(LONG k);
BOOL opts_name(LONG k, char *out, LONG size);               /* template keyword k's name */                                   /* template keyword k is a switch (/S) */
BOOL opts_check(STRPTR *lines);                             /* settings lines valid? (changes nothing) */
BOOL opts_saved(CONST_STRPTR line);                         /* written by a save itself */
void config_created(struct Dock *d);                        /* after dock_open: write a new file */
void opts_free(struct RDArgs *rd);

/* config.c */
BOOL config_load(struct Dock *d, CONST_STRPTR path);         /* NULL = the dock's own file */
BOOL config_save(struct Dock *d, CONST_STRPTR path);
BOOL config_reload(struct Dock *d, CONST_STRPTR path);
BOOL config_set(struct Dock *d, CONST_STRPTR args);         /* ARexx SET: settings lines over those in use */
LONG config_other_lines(STRPTR *out, LONG max);             /* the file's lines the window does not set */
BOOL config_choose_dir(struct Dock *d, char *buf, LONG size);   /* an ASL drawer requester into buf */
BOOL config_get(struct Dock *d, CONST_STRPTR key, char *out, LONG size);   /* ARexx GET */
BOOL config_choose(struct Dock *d, BOOL save);               /* ASL requester, then load / save (TRUE: done) */
void config_store(struct Dock *d);                          /* a change by hand: into the dock's own file */
struct Item *config_add(struct Dock *d, CONST_STRPTR path, struct Item *before);
void config_remove(struct Dock *d, struct Item *it);
void config_free(struct Dock *d);

/* dock.c */
BOOL dock_open(struct Dock *d);
void dock_close(struct Dock *d);
void dock_show_item(struct Dock *d, struct Item *it, BOOL selected);   /* one cell again, shown */
BOOL dock_exists(CONST_STRPTR path);                        /* without "Please insert volume" requesters */
BOOL dock_event(struct Dock *d, struct IntuiMessage *im);   /* FALSE = quit */
void dock_reopen(struct Dock *d);
void dock_set_pos(struct Dock *d, char edge);
BOOL dock_raise(struct Dock *d);                            /* TRUE: brought forward (a moment later) */
BOOL dock_covered(struct Dock *d);                          /* a window in front overlaps the dock */
void dock_raise_now(struct Dock *d);                        /* ... also with the settings window open */
void dock_settle(struct Dock *d);                           /* after a click: in front, new snapshot */
ULONG dock_behind(struct Dock *d);                          /* signature of the windows behind it */
void ah_peek(struct Dock *d);                               /* step aside, new snapshot, back */
void ah_peek_end(struct Dock *d);
void ah_strip_grab(struct Dock *d, BOOL hiding);            /* what the hidden strip shows */
void ah_hotkey(struct Dock *d);
/* hotkey counters (ARexx HOTKEYINFO): input handler, then what ah_hotkey did */
enum { HK_ARMED, HK_FIRED, HK_KEYOFF, HK_BTNOFF, HK_LMBFIX, HK_HANDLED, HK_BUSY, HK_REOPEN, HK_FRONT, HK_PIN, HK_UNPIN, HK_N };
extern LONG hk_stat[HK_N];
void ah_show(struct Dock *d, BOOL on);                      /* held out (the settings window) or not */
BOOL in_set_hotkey(CONST_STRPTR desc);                      /* commodities description; FALSE: not valid */
BOOL in_hotkey_valid(CONST_STRPTR desc);                    /* the same check, the hotkey in use unchanged */
#define NKEYS       3                 /* the other keys (MINKEY, MINALLKEY, AHKEY), in that order */
#define KEY_MIN     1                 /* their bits in keyact */
#define KEY_MINALL  2
#define KEY_AH      4
ULONG in_set_keys(CONST_STRPTR *desc);                      /* the NKEYS keys; bits: those not valid */
BOOL in_key_off(CONST_STRPTR desc);                         /* "none" (or empty): a key switched off */
BOOL in_key_valid(CONST_STRPTR desc);                       /* a valid MINKEY/MINALLKEY/AHKEY (or "none") */
void ah_key(struct Dock *d);                                /* the auto-hide key: auto-hide on / off */
LONG ask_command(struct Dock *d, char *buf, LONG size);     /* ask.c: "Execute a file"; 1 Execute, 0 Cancel, -1 could not ask */
struct Window *run_shell_drop(struct Dock *d, struct AppMessage *am);   /* icons onto a Shell: the text */
void run_shell_type(struct Dock *d, struct Window *w);      /* ... typed (after the drop is answered) */
void run_shell_check(struct Dock *d);                       /* RUNNING=0: Shell windows looked at */
BOOL run_shell_front(struct Dock *d, struct Item *it, struct AppMessage *am);   /* onto the Shell icon */
void run_shell_pending(struct Dock *d);                     /* ... typed into the Shell in front */
void run_note_front(BOOL ok);                               /* DROPINFO: a new Shell for it started? */
void run_shell_off(void);                                   /* no Shell window a drop target (quitting) */
void run_key_min(struct Dock *d);                           /* the minimise key: the active window */
void run_key_minall(struct Dock *d);                        /* minimise all: its program, drawers, or every window */
void dock_show(struct Dock *d);                             /* blit img into the window */
void dock_place(struct Dock *d);                            /* cell positions from list order */
void dock_targets(struct Dock *d, struct Item *skip, LONG gap); /* tx, ty only */
void dock_compose(struct Dock *d);                          /* img from bg + icons at x, y, shown */
void dock_compose_box(struct Dock *d, struct Box *b);       /* only that part, shown */
void dock_show_box(struct Dock *d, struct Box *b);
void box_add(struct Box *b, WORD x, WORD y, WORD w, WORD h);
void box_clip(struct Dock *d, struct Box *b);               /* to the dock image */
void item_box(struct Dock *d, struct Item *it, struct Box *b);   /* add where it is drawn */
void box_cells(struct Dock *d, struct Box *b);              /* grow to whole cells */
void dock_reopen_glide(struct Dock *d);                     /* reopen, icons glide from where they were */
BOOL dock_relayout_hidden(struct Dock *d);                  /* the same while auto-hide keeps it away (FALSE: not now) */
void ah_hidden_moved(struct Dock *d);                       /* ... the edge strip follows (autohide.c) */
struct Item *dock_item(struct Dock *d, LONG cell);
void dock_unhide(struct Dock *d);                            /* the icons that did not fit back into items */
LONG dock_extra(struct Dock *d);                             /* how many icons do not fit */
LONG dock_cell(struct Dock *d, WORD x, WORD y);             /* window coords -> cell, -1 */

/* input.c */
ULONG in_open(struct Dock *d);
void in_active(BOOL on);
void in_settle_again(void);                                 /* another dock_settle soon */
void in_poke(void);
void in_close(void);

/* autohide.c */
ULONG ah_open(struct Dock *d);                              /* timer signal mask */
void ah_start(struct Dock *d);                              /* after dock_open */
void ah_pointer(struct Dock *d);                            /* input handler signalled */
void ah_timer(struct Dock *d);
void ah_close(void);

/* scale.c */
void scale_icon(struct Dock *d, struct Item *it);
void scale_free(struct Item *it);
extern LONG scale_why;                                      /* scale.c: why icons were not shrunk */
void icon_image(struct Dock *d, struct RastPort *rp, struct Item *it, WORD x, WORD y, BOOL selected);
void icon_dock(struct Dock *d, struct RastPort *rp, struct Item *it, WORD x, WORD y, BOOL selected);
BOOL in_solid(struct Dock *d, WORD x, WORD y, WORD w, WORD h);   /* inside the solid colour */

/* anim.c */
#define GLIDE_MS 120
ULONG an_open(void);
void an_start(struct Dock *d, LONG ms);
void an_stop(struct Dock *d);
void an_timer(struct Dock *d);
BOOL an_running(void);
void an_close(void);

/* tooltip.c */
void tip_update(struct Dock *d);
void tip_hide(struct Dock *d);

/* drag.c */
BOOL drag_event(struct Dock *d, struct IntuiMessage *im);   /* TRUE = handled */
void drag_cancel(struct Dock *d, BOOL glide);                /* glide: icons back (not when closing) */
void drag_redraw(struct Dock *d, struct Box *b);            /* a glide frame during a drag: b changed */
BOOL drag_drop_wb(struct Dock *d, struct AppMessage *am);   /* TRUE = reopen the dock */

/* dropzone.c */
void dz_add(struct Dock *d);                                /* dock fully shown */
void dz_remove(struct Dock *d);                             /* dock hidden or closing */
LONG dz_gap(ULONG id);                                      /* drop zone id -> gap, -1 */
LONG dz_icon(ULONG id);                                     /* drop zone id -> the icon it is on, -1 */
void dz_reset(struct Dock *d);
void dz_lock(void);                                         /* drawing / list changes vs the zone hook */
void dz_unlock(void);
void dz_suspend(void);                                      /* the item list is about to change */
void dz_resume(struct Dock *d);                             /* ... same count, new order */

/* appmenu.c */
struct MsgPort *am_port(void);
ULONG am_open(void);                                        /* port signal mask */
void am_build(struct Dock *d);
BOOL am_handle(struct Dock *d);                             /* FALSE = quit */
BOOL am_choose(struct Dock *d, CONST_STRPTR name);          /* debug ARexx MENU <name> */
ULONG am_prefs_sig(void);                                   /* the settings window's signal (0: closed) */
void am_prefs_handle(struct Dock *d);
struct Window *am_prefs_win(void);                          /* the settings window (NULL: closed) */
void am_prefs_sync(struct Dock *d);                         /* ... shows settings changed elsewhere */
void am_prefs_close(struct Dock *d);                        /* close it, keeping its changes (ARexx, quitting) */
void am_note_overflow(struct Dock *d, LONG n);              /* "n icons do not fit" (not modal) */
void am_settings(struct Dock *d, BOOL more);                /* the settings window (more: Advanced) */
void am_close(void);
void dock_bench(struct Dock *d, char *res);                 /* ARexx BENCH (bench.c) */

/* running.c - programs on the dock that are running */
BOOL run_is_tool(struct Item *it);                          /* a program icon (not drawer, disk, project) */
BOOL run_trackable(struct Item *it);                        /* a program icon, the Shell, a drawer, Workbench */
BOOL run_is_drawer(struct Item *it);                        /* a docked drawer, disk or Trash */
void run_list_input(struct Dock *d);                        /* the list stays open: clicks, Del, Esc */
BOOL in_send_eof(struct Window *w);                         /* Ctrl+\ into a console window (input.c) */
ULONG run_open(void);                                       /* timer signal (0: none) */
void run_close(void);
void run_check(struct Dock *d);                             /* the dock was shown or hidden: look, or stop */
void run_timer(struct Dock *d);
void run_clicked(struct Dock *d);                           /* look soon (a program may start or close) */
BOOL run_launch(struct Dock *d, struct Item *it, struct AppMessage *am);   /* start it as Workbench does */
void run_forget(struct Item *it);                           /* the icon goes away */
BOOL run_front(struct Dock *d, struct Item *it);            /* click: to the front (FALSE: not running) */
void run_hold(struct Dock *d, struct Item *it);             /* button held: the list of its windows */
void run_list_move(struct Dock *d, WORD sx, WORD sy);
BOOL run_list_pick(struct Dock *d, WORD sx, WORD sy);       /* button up: the window picked (FALSE: no list) */
void run_list_close(struct Dock *d);
void run_soon(struct Dock *d);                              /* a window became active: look in a moment */
void run_layout_out(struct Dock *d);                        /* the running others out of the icon list */
void run_layout_all(struct Dock *d);                        /* ... all of them in (to load their icons) */
void run_layout_in(struct Dock *d, LONG room);              /* ... as many as fit, then the "more" cell */
void run_free_all(struct Dock *d);                          /* forget the running others */void run_free_hidden(struct Dock *d);
void run_wake(struct Dock *d);                              /* a minimised window made active: shown */
BOOL run_hide(struct Dock *d, struct Item *it);             /* dragged off: not shown again (IGNORE), asked first */
BOOL run_locate(struct Dock *d, struct Item *it);           /* its file not known: ask (TRUE: found) */
void run_images_free(struct Dock *d);                       /* icons of the running others not shown (dock closing) */
void run_crash_unlock(void);                                /* crash recovery: locks collect() held */
void run_show_all(void);                                    /* minimised windows shown again (quit, crash) */
void run_quit(struct Dock *d, LONG cell);                   /* Del over a running icon: it closes */
void dock_runcells(struct Dock *d);                         /* runcell[] from the icons now */

/* dropcopy.c - icons dropped onto a drawer or disk icon: copied into it */
BOOL drop_copy(struct Dock *d, struct Item *it, struct AppMessage *am);
void drop_copy_wait(void);                                  /* quitting: a copy in progress finishes first */

/* picker.c - the colour picker */
BOOL pick_open(struct Dock *d, ULONG rgb);
void pick_close(void);
ULONG pick_sig(void);
LONG pick_handle(ULONG *rgb);                               /* 0 open, 1 OK (rgb), -1 cancelled */

/* crash.c - crash reports (CRASHLOG) */
void crash_context(struct Dock *d, struct MsgPort **rexx, struct RDArgs **rd);
void crash_set(BOOL on);                                    /* the trap handler on / off */
void crash_illegal(void);                                   /* crashtrap.s: an illegal instruction (tests) */
extern struct RexxMsg *crash_rexx;                          /* the ARexx command being handled */
void sd_close_libs(void);                                   /* main.c */

/* render.c */
BOOL render_background(struct Dock *d);
struct BitMap *render_glass(struct Dock *d, struct BitMap *src, WORD left, WORD top, WORD w, WORD h, WORD rad,
                            LONG opacity);                  /* src NULL: the screen */
void render_arrow(char edge, LONG pos, LONG size);           /* a point on the next render_glass shape (0: none) */
void render_items(struct Dock *d, struct RastPort *rp, struct Item *only, BOOL selected);
void render_items_in(struct Dock *d, struct RastPort *rp, struct Box *b);   /* those touching b */
UBYTE *render_rowbuf(LONG bytes);                           /* scratch buffer, kept (render.c) */

/* pix.c: graphics-card pixels as R8G8B8 (w * 3 bytes a row) */
void pix_read(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, UBYTE *rgb);
void pix_write(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, UBYTE *rgb);

/* pixel loops: pixel.s, or the C versions in render.c (-DSD_NOASM, to compare) */
#ifdef SD_NOASM
void sd_lut3_c(UBYTE *p, LONG n, const UBYTE *lut);
void sd_blend_c(UBYTE *p, const UBYTE *s, LONG n, LONG m);
#define SD_LUT3(p, n, lut)   sd_lut3_c(p, n, lut)
#define SD_BLEND(p, s, n, m) sd_blend_c(p, s, n, m)
#else
void sd_lut3(__reg("a0") UBYTE *p, __reg("d0") LONG n, __reg("a1") const UBYTE *lut);
void sd_blend(__reg("a0") UBYTE *p, __reg("a1") const UBYTE *s, __reg("d0") LONG n, __reg("d1") LONG m);
#define SD_LUT3(p, n, lut)   sd_lut3(p, n, lut)
#define SD_BLEND(p, s, n, m) sd_blend(p, s, n, m)
#endif
void render_item(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy, BOOL selected);
void render_divider(struct Dock *d, struct RastPort *rp, struct Box *b);   /* the divider, inside b */
void render_dot(struct Dock *d, struct Item *it);
void render_clamp(struct Dock *d, WORD *x, WORD *y, WORD w, WORD h);   /* a box kept inside the image */
void cell_at(struct Dock *d, struct Item *it, WORD *cx, WORD *cy);     /* where the cell of 'it' is drawn */           /* a minimised program's dot again (pulse) */
void render_divider_at(struct Dock *d, struct RastPort *rp, struct Box *b, WORD at);   /* ... drawn at 'at' */
void divider_box(struct Dock *d, WORD at, struct Box *b);   /* add where it is drawn at 'at' */
WORD dock_along(struct Dock *d, WORD a);                    /* along the dock: without the divider's space */BOOL dock_in_gap(struct Dock *d, WORD a);                   /* in a divider's space (input handler too) */
BOOL render_image(struct Dock *d);
void render_free(struct Dock *d);
BOOL render_resnap(struct Dock *d);
ULONG render_border_colour(ULONG tint);
void render_tables_free(struct Dock *d);

/* launch.c */
BOOL launch_item(struct Dock *d, struct Item *it);
BOOL launch_with(struct Dock *d, struct Item *it, struct AppMessage *am);   /* dropped onto an icon */

/* arexx.c */
struct MsgPort *rexx_open(struct Dock *d);
BOOL rexx_handle(struct Dock *d, struct MsgPort *port);     /* FALSE = quit */
void rexx_close(struct MsgPort *port);

#endif
