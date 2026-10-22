/* SimpleDock - "Dock" menu in the Workbench menu bar, and the settings window
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   workbench.library (V45+) can add a menu title with items to the Workbench
   menu strip. Workbench (3.2) keeps memory each time such a menu is removed
   and added again (measured: ~3.8 KB for a menu with sub-menus), so the menu
   is added once, with few items, and never rebuilt. The settings are in a
   window (GadTools, with ReAction choosers for the lists) that shows the
   current values; a change shows on the dock at once and is kept when the
   window closes; Undo changes goes back to how it was when the window opened. */
#include <string.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/sghooks.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/wb.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <gadgets/chooser.h>
#include <proto/chooser.h>
#include <gadgets/string.h>
#include <proto/string.h>
#include <clib/alib_protos.h>
#include "sd.h"

enum { AM_TOP = 1, AM_BOTTOM, AM_LEFT, AM_RIGHT, AM_TEXTBELOW, AM_TEXTHOVER, AM_TEXTOFF,
       AM_IS0, AM_IS1, AM_IS2, AM_IS3, AM_SP0, AM_SP1, AM_SP2, AM_SP3,
       AM_GAP0, AM_GAP1, AM_GAP2, AM_GAP3, AM_AH0, AM_AH1, AM_AH2, AM_AH3, AM_AH4,
       AM_GLASSOFF, AM_GLASSON, AM_FRONTOFF, AM_FRONTON, AM_COL0, AM_COL1, AM_COL2, AM_COL3, AM_COL4, AM_COL5,
       AM_OP0, AM_OP1, AM_OP2, AM_OP3, AM_OP4, AM_RAD0, AM_RAD1, AM_RAD2, AM_RAD3,
       AM_RELOAD, AM_SAVE, AM_QUIT, AM_SETTINGS, AM_ABOUT,
       AM_TEXTONLY, AM_CUSTOM, AM_TOGGLE };

static const LONG isize[4] = { 0, 32, 48, 64 };              /* SCALEICONW/H */
static const char *const isname[4] = { "Original", "Small", "Medium", "Large" };
static const LONG spacing[4] = { 0, 2, 6, 12 };             /* ICONDIST */
static const char *const spname[4] = { "Compact", "Normal", "Wide", "Extra wide" };
static const LONG gaps[4] = { 0, 4, 8, 16 };                 /* DISTANCE */
static const char *const gapname[4] = { "None", "Small", "Medium", "Large" };
static const LONG ahval[5] = { 0, 2, 3, 5, 8 };              /* AUTOHIDE */
static const char *const ahname[5] = { "Off", "Fast", "Normal", "Slow", "Very slow" };
static const char *const colval[6] = { "#383838", "#202020", "#000000", "#707070", "#b8b8b8", "#28406a" };   /* TINT */
static const ULONG colrgb[6] = { 0x383838, 0x202020, 0x000000, 0x707070, 0xb8b8b8, 0x28406a };
static const char *const colname[6] = { "Dark grey", "Graphite", "Black", "Grey", "Light grey", "Blue" };
static const LONG opval[5] = { 30, 45, 55, 70, 85 };         /* OPACITY (transparent only) */
static const char *const opname[5] = { "30 %", "45 %", "55 %", "70 %", "85 %" };
static const LONG radval[4] = { 0, 6, 12, 18 };              /* RADIUS */
static const char *const radname[4] = { "Square", "Small", "Medium", "Large" };

#define MAXITEMS 8
static struct MsgPort *amport;
static struct AppMenuItem *ami[MAXITEMS];
static LONG nami;

static void add(CONST_STRPTR text, ULONG id, ULONG usekey)
{
    struct AppMenuItem *m;
    if (nami < MAXITEMS && (m = AddAppMenuItem(id, 0, text, amport, WBAPPMENUA_UseKey, usekey, TAG_END)))
        ami[nami++] = m;
}

static void remove_all(void)
{
    while (nami > 0) RemoveAppMenuItem(ami[--nami]);
}

/* The Dock menu - added once (see above). */
void am_build(struct Dock *d)
{
    ULONG title = 0;
    struct AppMenuItem *t;

    if (!amport || nami) return;
    if (WorkbenchBase->lib_Version < 45) {           /* menu titles need workbench.library V45 (3.9): */
        static const struct { const char *text; ULONG id; } tool[4] = {   /* in the Tools menu instead */
            { "SimpleDock Show/Hide", AM_TOGGLE }, { "SimpleDock Settings...", AM_SETTINGS },
            { "SimpleDock About...", AM_ABOUT }, { "Quit SimpleDock", AM_QUIT } };
        LONG i;
        for (i = 0; i < 4 && nami < MAXITEMS; i++)
            if ((t = AddAppMenuItem(tool[i].id, 0, (STRPTR)tool[i].text, amport, TAG_END))) ami[nami++] = t;
        return;
    }
    t = AddAppMenuItem(0, 0, "Dock", amport, WBAPPMENUA_GetTitleKey, &title, TAG_END);
    if (!t) { sd_verbose(d, "Cannot add the Dock menu (is Workbench running?)"); return; }
    ami[nami++] = t;
    add("Show / Hide Dock", AM_TOGGLE, title);   /* (as the hotkey) */
    add("Settings...", AM_SETTINGS, title);      /* (Load... / Save as...: in the settings window) */
    add("About...", AM_ABOUT, title);
    add("---", 0, title);
    add("Quit", AM_QUIT, title);
}

struct MsgPort *am_port(void) { return amport; }

/* The port for menu choices and for icons dropped on the dock (AppWindow).
   Created before the dock opens; the menu follows in am_build. */
ULONG am_open(void)
{
    if (!(amport = CreateMsgPort())) return 0;
    return 1UL << amport->mp_SigBit;
}

static void prefs_open(struct Dock *d, BOOL more);
static void prefs_close(void);
static void about_open(struct Dock *d);

/* One menu or settings window choice; FALSE = quit. The caller reopens the
   dock when *reopen is set. */
static BOOL choose(struct Dock *d, ULONG id, BOOL *reopen)
{
    BOOL run = TRUE;
    if (id >= AM_TOP && id <= AM_RIGHT) {
        dock_set_pos(d, "TBLR"[id - AM_TOP]);
        *reopen = TRUE;
    } else if ((id >= AM_TEXTBELOW && id <= AM_TEXTOFF) || id == AM_TEXTONLY) {
        d->o->notext = id == AM_TEXTHOVER || id == AM_TEXTOFF;
        d->o->hovertext = id == AM_TEXTHOVER;
        d->o->noicon = id == AM_TEXTONLY;    /* labels only: the names, no icons */
        *reopen = TRUE;
    } else if (id >= AM_IS0 && id <= AM_IS3) {
        d->o->scalew = d->o->scaleh = isize[id - AM_IS0];
        *reopen = TRUE;
    } else if (id >= AM_SP0 && id <= AM_SP3) {
        d->o->icondist = spacing[id - AM_SP0];
        *reopen = TRUE;
    } else if (id >= AM_GAP0 && id <= AM_GAP3) {
        d->o->distance = gaps[id - AM_GAP0];
        *reopen = TRUE;
    } else if (id >= AM_AH0 && id <= AM_AH4) {
        d->o->autohide = ahval[id - AM_AH0];
        if (d->o->autohide > 0) d->o->ahspeed = d->o->autohide;
        ah_start(d);
    } else if (id == AM_FRONTOFF || id == AM_FRONTON) {
        d->o->ontop = id == AM_FRONTON;
        ah_start(d);                       /* the pointer watch */
        dock_raise_now(d);
    } else if (id == AM_GLASSOFF || id == AM_GLASSON) {   /* transparent: tinted with the colour */
        d->o->transparent = id == AM_GLASSON;
        *reopen = TRUE;
    } else if (id >= AM_COL0 && id <= AM_COL5) {
        d->o->tint = (STRPTR)colval[id - AM_COL0];
        *reopen = TRUE;
    } else if (id == AM_CUSTOM && d->o->customtint) {   /* the custom colour (colour picker) */
        d->o->tint = d->o->customtint;
        *reopen = TRUE;
    } else if (id >= AM_OP0 && id <= AM_OP4) {
        d->o->opacity = opval[id - AM_OP0];
        *reopen = TRUE;
    } else if (id >= AM_RAD0 && id <= AM_RAD3) {
        d->o->radius = radval[id - AM_RAD0];
        *reopen = TRUE;
    } else if (id == AM_TOGGLE) {
        ah_hotkey(d);
    } else if (id == AM_SETTINGS) {
        prefs_open(d, FALSE);
    } else if (id == AM_ABOUT) {
        about_open(d);
    } else if (id == AM_RELOAD) {           /* the dock's own file again (debug ARexx MENU) */
        config_reload(d, NULL);
    } else if (id == AM_SAVE) {             /* settings and icons into the dock's own file (the same) */
        config_save(d, NULL);
    } else if (id == AM_QUIT) {
        TRACE("quit: menu");
        run = FALSE;
    }
    return run;
}

/* The hotkey is shown in capitals ("CTRL ALT D") and kept in lowercase, as
   it is saved (an uppercase letter in a commodities description may mean
   Shift as well). */
static void key_case(char *dst, CONST_STRPTR src, LONG size, BOOL upper)
{
    LONG i;
    for (i = 0; i < size - 1 && src[i]; i++) {
        char c = src[i];
        if (upper && c >= 'a' && c <= 'z') c -= 32;
        else if (!upper && c >= 'A' && c <= 'Z') c += 32;
        dst[i] = c;
    }
    dst[i] = 0;
}

/* The key in use for Advanced row i (K_KEY), "none" if off. */
static CONST_STRPTR key_now(struct Dock *d, LONG i);

/* --- About --- */

static struct Window *aboutw;           /* the About requester while open (not modal) */

static void about_close(void)
{
    if (aboutw) { FreeSysRequest(aboutw); aboutw = NULL; }
}

static void about_open(struct Dock *d)
{
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, "About " SD_NAME,
        SD_NAME " " SD_VERSION SD_BUILD " (" SD_DATE ")\n"
        "A simple, macOS-inspired dock for AmigaOS 3.x\n\n"
        "Copyright \251 2026 renewable-intel\n"
        "https://github.com/renewable-intel/SimpleDock\n\n"
        "Free software under the GNU General Public License,\n"
        "version 3 or later. It comes with ABSOLUTELY NO WARRANTY.\n\n"
        "Hotkey to show the dock: %s", "Ok" };
    static char shown[64];
    CONST_STRPTR key = shown;
    struct Window *w;
    if (aboutw) { WindowToFront(aboutw); return; }
    key_case(shown, in_hotkey_valid(d->o->hotkey) ? d->o->hotkey : (CONST_STRPTR)"none", sizeof(shown), TRUE);
    w = BuildEasyRequestArgs(NULL, &es, 0, (APTR)&key);   /* on the Workbench screen, with the menu */
    if (w && w != (struct Window *)1) aboutw = w;
}

/* "n icons do not fit" as a requester that doesn't stop the dock (it may
   appear at boot, from WBStartup). From a Shell it is a line of output. */
static struct Window *notew;

static void note_close(void)
{
    if (notew) { FreeSysRequest(notew); notew = NULL; }
}

void am_note_overflow(struct Dock *d, LONG n)
{
    extern struct WBStartup *WBenchMsg;
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME, NULL, "Ok" };
    struct Window *w;
    es.es_TextFormat = n == 1 ? (STRPTR)"1 icon does not fit on the dock.\n"
                                        "It stays in the settings file; remove an icon,\n"
                                        "or choose smaller icons or spacing, to show it."
                              : (STRPTR)"%ld icons do not fit on the dock.\n"
                                        "They stay in the settings file; remove some icons,\n"
                                        "or choose smaller icons or spacing, to show them.";
    if (!WBenchMsg || d->o->quiet) { sd_msg(d, (CONST_STRPTR)es.es_TextFormat, n); return; }
    note_close();
    w = BuildEasyRequestArgs(NULL, &es, 0, (APTR)&n);
    if (w && w != (struct Window *)1) notew = w;
}

/* --- the settings window --- */

/* One window for all settings. It opens with the simple settings; Advanced >>
   folds out every other keyword of the settings file beside them (on a small
   screen, in their place). Both sides are in groups under headings. Changes
   show on the dock at once and are saved when the window closes; Undo
   changes goes back to how it opened. */
#define NGROUP 11                       /* cycle gadgets; then the hotkey (a string gadget) */
#define NROWS (NGROUP + 1)
enum { G_KEY = NGROUP, G_ADV = 20, G_BRECALL = 100, G_BMORE, G_BLOAD, G_BSAVEAS, G_BPICK, G_BHREM, G_BDIR };
static const char *const posname[] = { "Top", "Bottom", "Left", "Right" };
static const char *const textname[] = { "Below icons", "On hover", "Off", "Labels only" };
static const char *const frontname[] = { "Off", "On" };
static const struct { const char *name; const char *const *labels; LONG n; ULONG first; } group[NGROUP] = {
    { "Position", posname, 4, AM_TOP },       { "Text", textname, 4, AM_TEXTBELOW },
    { "Icon size", isname, 4, AM_IS0 },       { "Icon spacing", spname, 4, AM_SP0 },
    { "Gap to edge", gapname, 4, AM_GAP0 },   { "Auto-hide", ahname, 5, AM_AH0 },
    { "In front", frontname, 2, AM_FRONTOFF }, { "Colour", colname, 6, AM_COL0 },
    { "Transparent", frontname, 2, AM_GLASSOFF }, { "Opacity", opname, 5, AM_OP0 },
    { "Corners", radname, 4, AM_RAD0 } };
/* The simple side in its groups: a row (group[] index, or G_KEY), or a
   heading -1 - k. */
static const char *const shead[] = { "Place", "Icons", "Look" };
static const BYTE sorder[] = { -1, 0, 4, 5, 6, G_KEY, -2, 2, 3, 1, -3, 7, 8, 9, 10 };
#define NSORDER ((LONG)sizeof(sorder))

enum { K_ALIGN, K_INT, K_STR, K_SWITCH, K_ICON, K_MSG, K_LIST, K_HIDE, K_KEY };
static const char *const runname[] = { "Off", "Dot", "Dot and number", NULL };
static const LONG runval[] = { 0, 1, 2 };
static const char *const clickname[] = { "Bring to front", "New copy", NULL };
static const char *const startname[] = { "SimpleDock", "Workbench", NULL };
static const char *const askname[] = { "Start at once", "Ask first", NULL };
static const char *const sdropname[] = { "Off", "Type paths", NULL };
static const LONG sdropval[] = { 0, 1 };
static const char *const unpackname[] = { "Shell's drawer", "RAM:", "Unpack drawer", NULL };
static const LONG unpackval[] = { 0, 1, 2 };
static const char *const dropname[] = { "Copy", "Like Workbench", NULL };
static const char *const holdname[] = { "Normal (0.3 s)", "Long (0.4 s)", "Longer (0.5 s)", NULL };
static const LONG holdval[] = { 3, 4, 5 };
static const char *const othername[] = { "Show", "Hide", NULL };
static const char *const beepname[] = { "On", "Off", NULL };
/* K_LIST: a list of choices for a keyword, with values (KEY=n; the one at
   'def' when it is not set), or without (a switch: first choice = off).
   K_HIDE: the IGNORE names as a list, with Remove. K_KEY: a key (commodities
   description, shown in capitals; empty or NONE: off). A row with 'head'
   starts a group. (The keywords are in the ReadMe.) */
static const struct { const char *label, *key; UBYTE kind; const char *const *names; const LONG *vals; LONG def;
                      const char *head; } adv[] = {
    { "Alignment", "POS", K_ALIGN, NULL, NULL, 0, "Fine-tune" },
    { "Offset, pixels", "OFFSET", K_INT },
    { "Gap to edge, pixels", "DISTANCE", K_INT },
    { "Icon spacing, pixels", "ICONDIST", K_INT },
    { "Icon size, pixels", "SCALEICONW", K_ICON },
    { "Most icons", "LIMIT", K_INT },
    { "Corner radius, pixels", "RADIUS", K_INT },
    { "Opacity, %", "OPACITY", K_INT },
    { "Auto-hide speed", "AUTOHIDE", K_INT },
    { "Colour", "TINT", K_STR },
    { "Running programs", "RUNNING", K_LIST, runname, runval, 2, "Programs" },
    { "Click when running", "CLICKNEW", K_LIST, clickname, NULL, 0 },
    { "Start programs with", "WBSTART", K_LIST, startname, NULL, 0 },
    { "Shell icons", "SHELLASK", K_LIST, askname, NULL, 0 },
    { "Drop on a drawer", "DROPMOVE", K_LIST, dropname, NULL, 0 },
    { "Long press", "HOLDTIME", K_LIST, holdname, holdval, 0 },
    { "Programs not on the dock", "RUNHIDE", K_LIST, othername, NULL, 0 },
    { "Workbench icon", "WBHIDE", K_LIST, othername, NULL, 0 },
    { "Hidden programs", "IGNORE", K_HIDE },
    { "Icons into a Shell", "SHELLDROP", K_LIST, sdropname, sdropval, 1, "Shell drops" },
    { "Unpack archives to", "UNPACKTO", K_LIST, unpackname, unpackval, 0 },
    { "Unpack drawer", "UNPACKDIR", K_STR },
    { "Minimise window", "MINKEY", K_KEY, NULL, NULL, 0, "Keys" },
    { "Minimise all", "MINALLKEY", K_KEY },
    { "Auto-hide on/off", "AHKEY", K_KEY },
    { "Font", "FONT", K_STR, NULL, NULL, 0, "System" },
    { "Screen", "PUBSCREEN", K_STR },
    { "Start delay, 1/50 s", "DELAY", K_INT },
    { "Messages", "QUIET", K_MSG },
    { "Beep", "NOBEEP", K_LIST, beepname, NULL, 0 },
    { "Crash reports", "CRASHLOG", K_SWITCH },
    { "No border", "NOBORDER", K_SWITCH } };
#define HIDEROWS 6                      /* the Hidden programs list: rows high */
#define NADV ((LONG)(sizeof(adv) / sizeof(adv[0])))
static const char *const alignname[] = { "Centre", "Start", "End", NULL };
static const char *const msgname[] = { "Normal", "Quiet", "Verbose", NULL };

static STRPTR glabels[NGROUP][11];      /* cycle labels (+ "Other" for a value set elsewhere) */

/* The Colour list: the six colours and the custom one if there is one (from
   the colour picker, or a TINT in the file). The picker has its own button,
   "Custom...", next to the list: as a list entry, stepping through the
   cycle gadget (without chooser.gadget) would open it. */
static char custlabel[8];               /* "#RRGGBB" */
static LONG col_has(struct Dock *d) { return d->o->customtint != NULL; }
static LONG group_n(struct Dock *d, LONG i) { return i == 7 ? 6 + col_has(d) : group[i].n; }

/* "#rrggbb" as 0xRRGGBB; -1: not a colour. */
static LONG hexval(CONST_STRPTR s)
{
    LONG v = 0, n;
    if (!s || *s++ != '#') return -1;
    for (n = 0; n < 6; n++, s++) {
        char c = *s | 32;
        if (c >= '0' && c <= '9') v = v << 4 | (c - '0');
        else if (c >= 'a' && c <= 'f') v = v << 4 | (c - 'a' + 10);
        else return -1;
    }
    return *s ? -1 : v;
}

/* A TINT from the file that is none of the six is the custom colour. */
static void adopt_custom(struct Dock *d)
{
    LONG v[1], i;
    if (d->o->customtint || !d->o->tint) return;
    for (i = 0; i < 6; i++) if (d->tint == colrgb[i]) return;
    v[0] = d->tint;
    SD_FMT(d->custbuf, "#%06lx", v);
    d->o->customtint = d->custbuf;
}
static struct Window *pw;
static APTR vi;
static struct Screen *pscr;             /* the settings window's own lock on its screen */
static struct Gadget *glist, *keyg, *cyc[NGROUP], *ag[sizeof(adv) / sizeof(adv[0])];

/* The lists are ReAction choosers (chooser.gadget, OS 3.2: they drop down)
   or, if that class does not open, GadTools cycle gadgets (each click steps
   on). Choosers are not GadTools gadgets: they are added to the window once
   it is open, and their names are drawn here. Slot i: cycle i, or Advanced
   gadget i - NGROUP. */
#define NLIST (NGROUP + NADV)
static BOOL choosers;                   /* this window's lists are choosers */
static struct Gadget *chobj[NLIST];     /* the choosers (and ReAction text fields), in the order made */
static BOOL chstr[NLIST];               /* ... this one is a text field */
static LONG nch;
static struct List chlab[NLIST];        /* their labels */
static struct { WORD right, top; CONST_STRPTR name; } chname[NLIST];
static UWORD textpen = 1;
static struct TextFont *namefont;       /* the screen font, for those names */
static BOOL wide;                       /* the Advanced side is shown */
static WORD wleft = -1, wtop;           /* where the window was (it is rebuilt to fold out) */
static WORD divx = -1, divy, divh;      /* the divider between the two sides */
static WORD linex = -1, liney, linew;   /* the line above the buttons */
#define SNAPLEN 4096                    /* the settings lines (buffers below, allocated while needed) */
static char *snapbuf;                   /* the settings lines when the window opened (Undo changes) */
static BOOL snapopen;                   /* ... and the dock was open then */
static BOOL loaded;                     /* Load... read another file: kept when the window closes */
static BOOL keepsnap;                   /* Load... was cancelled: the window opens again, snapbuf stays */
static struct Gadget *recallg;
static struct Dock *held;               /* the dock held out while the window is open */
static void recall_state(struct Dock *d);
static char keyshown[64];               /* the hotkey as the gadget shows it (capitals) */

/* A side of the window, top to bottom: rows (some taller) and headings. */
struct Slot { WORD what; UBYTE h; };    /* what: a row's index, or a heading -1 - k */
#define MAXSLOT 40
#define MAXHEAD 8
static struct { WORD x, y, w; CONST_STRPTR name; } heads[MAXHEAD];   /* drawn by divider() */
static LONG nheads;

/* Columns for n slots, each at most maxrows high if maxcols allow. A column
   ends before a heading whose group would not fit in it (unless the group
   is taller than a column), never right after one; columns grow (up to
   maxrows) rather than split a group. Fills col[] and row[]; returns the
   columns, *rows the tallest. */
static LONG columns(const struct Slot *sl, LONG n, LONG maxrows, LONG maxcols, UBYTE *col, UBYTE *row, LONG *rows)
{
    LONG c, k, total = 0, used = 1;
    for (k = 0; k < n; k++) total += sl[k].h;
    if (maxcols < 1) maxcols = 1;
    for (c = 1; ; c++) {
        LONG limit = (total + c - 1) / c;
        for (;; limit++) {
            LONG y = 0, tall = 0;
            BOOL split = FALSE;
            used = 1;
            for (k = 0; k < n; k++) {
                LONG need = sl[k].h;
                if (sl[k].what < 0) {            /* a heading: its group, or at least its first row */
                    LONG g = 0, j;
                    for (j = k; j < n && (j == k || sl[j].what >= 0); j++) g += sl[j].h;
                    need = g <= limit ? g : sl[k].h + (k + 1 < n ? sl[k + 1].h : 0);
                }
                if (y && y + need > limit) { used++; y = 0; if (sl[k].what >= 0) split = TRUE; }
                col[k] = used - 1;
                row[k] = y;
                y += sl[k].h;
                if (y > tall) tall = y;
            }
            if (used <= c && (!split || limit >= maxrows)) { *rows = tall; break; }
        }
        if (*rows <= maxrows || c >= maxcols) return used;
    }
}

/* Both sides of the window as slots (compact: no headings, a shorter
   Hidden programs list). Returns the simple side's count, *na the other. */
static LONG slots(struct Slot *ss, struct Slot *as, LONG *na, BOOL compact)
{
    LONG i, ns = 0, n = 0;
    for (i = 0; i < NSORDER; i++)
        if (!compact || sorder[i] >= 0) { ss[ns].what = sorder[i]; ss[ns++].h = 1; }
    for (i = 0; i < NADV && n < MAXSLOT - 1; i++) {
        if (adv[i].head && !compact) { as[n].what = -1 - i; as[n++].h = 1; }   /* (a heading: its row's index) */
        as[n].what = i;
        as[n++].h = adv[i].kind != K_HIDE ? 1 : (compact ? 2 : HIDEROWS) + 1;   /* (+1: Remove beneath it) */
    }
    *na = n;
    return ns;
}

/* The Hidden programs list: the IGNORE names. */
#define MAXHID 32
static struct List hlist;
static struct Node hnode[MAXHID];
static char hbuf[SD_LINEMAX];
static LONG nhid, hsel = -1;            /* ... and the one picked (-1: none) */
static struct Gadget *hremg;            /* its Remove button */
static WORD hlabx = -1, hlaby;          /* its name: right end, top */

static void hidden_fill(struct Dock *d)
{
    CONST_STRPTR p = d->o->ignore, q;
    LONG k = 0, len;
    NewList(&hlist);
    nhid = 0;
    while (p && *p && nhid < MAXHID) {
        while (*p == ' ' || *p == ',' || *p == '"') p++;
        for (q = p; *q && *q != ','; q++) ;
        for (len = q - p; len > 0 && (p[len - 1] == ' ' || p[len - 1] == '"'); len--) ;
        if (len > 0 && k + len + 1 <= (LONG)sizeof(hbuf)) {
            memcpy(hbuf + k, p, len);
            hbuf[k + len] = 0;
            hnode[nhid].ln_Name = hbuf + k;
            AddTail(&hlist, &hnode[nhid++]);
            k += len + 1;
        }
        p = q;
    }
    hsel = -1;
}

/* The list shows IGNORE as it is now; nothing picked. */
static void hidden_show(struct Dock *d, struct Gadget *lv)
{
    GT_SetGadgetAttrs(lv, pw, NULL, GTLV_Labels, ~0UL, TAG_END);
    hidden_fill(d);
    GT_SetGadgetAttrs(lv, pw, NULL, GTLV_Labels, &hlist, GTLV_Selected, ~0UL, TAG_END);
    if (hremg) GT_SetGadgetAttrs(hremg, pw, NULL, GA_Disabled, TRUE, TAG_END);
}

/* Edit hook of the hotkey gadget: letters typed come out as capitals. Runs
   in input.device's task: no library calls. */
static __saveds ULONG key_hook(__reg("a0") struct Hook *h, __reg("a2") struct SGWork *sgw, __reg("a1") ULONG *msg)
{
    if (*msg != SGH_KEY) return 0;
    if ((sgw->EditOp == EO_INSERTCHAR || sgw->EditOp == EO_REPLACECHAR) && sgw->BufferPos > 0) {
        UBYTE *c = sgw->WorkBuffer + sgw->BufferPos - 1;
        if (*c >= 'a' && *c <= 'z') *c -= 32;
    }
    return ~0UL;
}
static struct Hook keyhook = { { NULL, NULL }, (ULONG (*)())key_hook, NULL, NULL };

static LONG find(LONG v, const LONG *tab, LONG n)
{
    LONG i;
    for (i = 0; i < n; i++) if (tab[i] == v) return i;
    return n;                                    /* "Other" */
}

/* The cycle position of group g for the settings in use. */
static LONG current(struct Dock *d, LONG g)
{
    struct Opts *o = d->o;
    LONG i;
    switch (g) {
    case 0: return d->edge == 'T' ? 0 : d->edge == 'L' ? 2 : d->edge == 'R' ? 3 : 1;
    case 1: return o->noicon ? 3 : !o->notext && !o->hovertext ? 0 : o->hovertext ? 1 : 2;
    case 2: return o->scalew == o->scaleh ? find(o->scalew, isize, 4) : 4;
    case 3: return find(o->icondist, spacing, 4);
    case 4: return find(o->distance, gaps, 4);
    case 5: return find(o->autohide, ahval, 5);
    case 6: return o->ontop ? 1 : 0;
    case 7: for (i = 0; i < 6; i++) if (d->tint == colrgb[i]) return i;
            return col_has(d) && d->tint == (ULONG)hexval(o->customtint) ? 6 : group_n(d, 7);
    case 8: return o->transparent ? 1 : 0;
    case 9: return find(o->opacity, opval, 5);
    default: return find(o->radius, radval, 4);
    }
}

/* The labels of cycle i: its choices, and "Other" while the value in use is
   none of them (set in the file, or on the Advanced side). */
static void labels_for(struct Dock *d, LONG i, LONG cur)
{
    LONG j, k;
    for (j = 0; j < group[i].n; j++) glabels[i][j] = (STRPTR)group[i].labels[j];
    if (i == 7 && col_has(d)) {                  /* Colour: the custom one, as #RRGGBB */
        for (k = 0; k < 7 && d->o->customtint[k]; k++)
            custlabel[k] = d->o->customtint[k] >= 'a' ? d->o->customtint[k] - 32 : d->o->customtint[k];
        custlabel[k] = 0;
        glabels[i][j++] = custlabel;
    }
    glabels[i][j] = cur == group_n(d, i) ? (STRPTR)"Other" : NULL;
    glabels[i][j + 1] = NULL;
}

/* What choice 'code' of cycle i does: a menu id (choose), 0 nothing ("Other"). */
static LONG map(struct Dock *d, LONG i, UWORD code)
{
    if (code >= group_n(d, i)) return 0;
    if (i == 1 && code == 3) return AM_TEXTONLY;
    if (i == 7 && code == 6) return AM_CUSTOM;
    return group[i].first + code;
}

/* The alignment letter of POS: 0 centre, 1 start (left / top), 2 end. */
static LONG align_of(CONST_STRPTR pos)
{
    char a = pos[0] && pos[1] ? pos[1] & ~32 : 0;
    return a == 'L' || a == 'T' ? 1 : a == 'R' || a == 'B' ? 2 : 0;
}

static void list_labels(LONG slot, const char *const *labels)
{
    struct TagItem t[2];
    struct Node *n;
    while ((n = RemHead(&chlab[slot]))) FreeChooserNode(n);
    for (; *labels; labels++) {
        t[0].ti_Tag = CNA_Text; t[0].ti_Data = (ULONG)*labels; t[1].ti_Tag = TAG_END;
        if ((n = AllocChooserNodeA(t))) AddTail(&chlab[slot], n);
    }
}

/* A list at ng (labels end with NULL) showing choice cur. Returns the
   GadTools gadget to go on from (NULL: failed); *out is the list. */
static struct Gadget *list_new(struct Gadget *g, struct NewGadget *ng, LONG slot, const char *const *labels,
                               LONG cur, struct Gadget **out)
{
    struct Gadget *c;
    if (!choosers)
        return *out = CreateGadget(CYCLE_KIND, g, ng, GTCY_Labels, labels, GTCY_Active, cur, TAG_END);
    list_labels(slot, labels);
    c = (struct Gadget *)NewObject(CHOOSER_GetClass(), NULL, GA_ID, ng->ng_GadgetID,
                                   GA_Left, ng->ng_LeftEdge, GA_Top, ng->ng_TopEdge,
                                   GA_Width, ng->ng_Width, GA_Height, ng->ng_Height,
                                   GA_TextAttr, ng->ng_TextAttr, GA_RelVerify, TRUE,
                                   nch ? GA_Previous : TAG_IGNORE, nch ? chobj[nch - 1] : NULL,   /* (NULL: 3.2 writes to 0) */
                                   CHOOSER_PopUp, TRUE, CHOOSER_Labels, &chlab[slot],
                                   CHOOSER_Selected, cur, TAG_END);
    if (!(*out = c)) return NULL;
    chname[nch].right = ng->ng_LeftEdge - 8;    /* the name, as GadTools places it */
    chname[nch].top = ng->ng_TopEdge + (ng->ng_Height - ng->ng_TextAttr->ta_YSize + 1) / 2;
    chname[nch].name = (CONST_STRPTR)ng->ng_GadgetText;
    chstr[nch] = FALSE;
    chobj[nch++] = c;
    return g;
}

/* A text field at ng: a ReAction string gadget with the choosers (a
   double-click marks all of it, so one key replaces it), else GadTools'. */
static struct Gadget *str_new(struct Gadget *g, struct NewGadget *ng, struct Gadget **out)
{
    struct Gadget *c;
    if (!choosers || !StringBase)
        return *out = CreateGadget(STRING_KIND, g, ng, GTST_MaxChars, SD_LINEMAX - 16, TAG_END);
    c = (struct Gadget *)NewObject(STRING_GetClass(), NULL, GA_ID, ng->ng_GadgetID,
                                   GA_Left, ng->ng_LeftEdge, GA_Top, ng->ng_TopEdge,
                                   GA_Width, ng->ng_Width, GA_Height, ng->ng_Height,
                                   GA_TextAttr, ng->ng_TextAttr, GA_RelVerify, TRUE, GA_Immediate, TRUE,
                                   GA_TabCycle, TRUE, nch ? GA_Previous : TAG_IGNORE, nch ? chobj[nch - 1] : NULL,
                                   STRINGA_MaxChars, SD_LINEMAX - 16, STRINGA_TextVal, "", TAG_END);
    if (!(*out = c)) return NULL;
    chname[nch].right = ng->ng_LeftEdge - 8;
    chname[nch].top = ng->ng_TopEdge + (ng->ng_Height - ng->ng_TextAttr->ta_YSize + 1) / 2;
    chname[nch].name = (CONST_STRPTR)ng->ng_GadgetText;
    chstr[nch] = TRUE;
    chobj[nch++] = c;
    return g;
}

/* Is g a ReAction text field (else GadTools)? */
static BOOL is_str(struct Gadget *g)
{
    LONG k;
    for (k = 0; k < nch; k++) if (chobj[k] == g) return chstr[k];
    return FALSE;
}

/* A text field's text. */
static STRPTR str_get(struct Gadget *g)
{
    ULONG v = 0;
    if (!is_str(g)) return (STRPTR)((struct StringInfo *)g->SpecialInfo)->Buffer;
    GetAttr(STRINGA_TextVal, (Object *)g, &v);
    return v ? (STRPTR)v : (STRPTR)"";
}

static void str_set(struct Gadget *g, CONST_STRPTR v)
{
    if (!is_str(g)) { GT_SetGadgetAttrs(g, pw, NULL, GTST_String, v, TAG_END); return; }
    if (SetGadgetAttrs(g, pw, NULL, STRINGA_TextVal, v, TAG_END)) RefreshGList(g, pw, NULL, 1);
}

/* Show choice cur (and new labels, if not NULL). */
static void list_set(struct Gadget *g, LONG slot, const char *const *labels, LONG cur)
{
    if (!choosers) {
        GT_SetGadgetAttrs(g, pw, NULL, labels ? GTCY_Labels : TAG_IGNORE, labels, GTCY_Active, cur, TAG_END);
        return;
    }
    if (labels) {
        SetGadgetAttrs(g, pw, NULL, CHOOSER_Labels, ~0UL, TAG_END);
        list_labels(slot, labels);
    }
    if (SetGadgetAttrs(g, pw, NULL, labels ? CHOOSER_Labels : TAG_IGNORE, &chlab[slot], CHOOSER_Selected, cur, TAG_END))
        RefreshGList(g, pw, NULL, 1);
}

/* The choice of a chooser that sent a message (else code as it is). */
static UWORD list_value(struct Gadget *g, UWORD code)
{
    ULONG v;
    LONG k;
    for (k = 0; k < nch; k++)
        if (chobj[k] == g && !chstr[k]) { GetAttr(CHOOSER_Selected, (Object *)g, &v); return (UWORD)v; }
    return code;
}

/* Greyed out (or not): a row that does nothing now. */
static void set_off(struct Gadget *g, BOOL off)
{
    LONG k;
    if (!g || !(g->Flags & GFLG_DISABLED) == !off) return;
    for (k = 0; k < nch && chobj[k] != g; k++) ;
    if (k < nch) { SetGadgetAttrs(g, pw, NULL, GA_Disabled, off, TAG_END); RefreshGList(g, pw, NULL, 1); }
    else GT_SetGadgetAttrs(g, pw, NULL, GA_Disabled, off, TAG_END);
}

/* The choice of K_LIST row i for its value v (as config_get gives it). */
static LONG list_choice(LONG i, CONST_STRPTR v)
{
    LONG n = 0, k;
    if (!adv[i].vals) return v[0] == '1';        /* a switch */
    if (!v[0]) return adv[i].def;                /* not set: the default */
    StrToLong((STRPTR)v, &n);
    for (k = 0; adv[i].names[k]; k++) if (adv[i].vals[k] == n) return k;
    return adv[i].def;
}

/* Every gadget shows the value in use (after a change on either side). */
static void sync(struct Dock *d)
{
    char v[SD_LINEMAX];
    LONG i, n;
    if (!pw) return;
    for (i = 0; i < NGROUP; i++)
        if (cyc[i]) {
            LONG cur = current(d, i);
            labels_for(d, i, cur);
            list_set(cyc[i], i, (const char *const *)glabels[i], cur);
        }
    if (keyg) {
        key_case(keyshown, d->o->hotkey, sizeof(keyshown), TRUE);
        GT_SetGadgetAttrs(keyg, pw, NULL, GTST_String, keyshown, TAG_END);
    }
    for (i = 0; i < NADV; i++) {
        if (!ag[i]) continue;
        config_get(d, adv[i].key, v, sizeof(v));
        n = 0;
        switch (adv[i].kind) {
        case K_ALIGN:  list_set(ag[i], NGROUP + i, NULL, align_of(v)); break;
        case K_MSG:                              /* Quiet wins over Verbose */
            n = v[0] == '1' ? 1 : (config_get(d, "VERBOSE", v, sizeof(v)), v[0] == '1') ? 2 : 0;
            list_set(ag[i], NGROUP + i, NULL, n);
            break;
        case K_ICON:
        case K_INT:    StrToLong(v, &n); GT_SetGadgetAttrs(ag[i], pw, NULL, GTIN_Number, n, TAG_END); break;
        case K_STR:    str_set(ag[i], v); break;
        case K_KEY:    { char kb[64]; key_case(kb, key_now(d, i), sizeof(kb), TRUE); str_set(ag[i], kb); } break;
        case K_LIST:   list_set(ag[i], NGROUP + i, NULL, list_choice(i, v)); break;
        case K_HIDE:   hidden_show(d, ag[i]); break;
        default:       GT_SetGadgetAttrs(ag[i], pw, NULL, GTCB_Checked, v[0] == '1', TAG_END); break;
        }
    }
    set_off(cyc[9], !d->o->transparent);         /* Opacity: only when transparent */
    for (i = 0; i < NADV; i++) if (!strcmp(adv[i].key, "OPACITY")) set_off(ag[i], !d->o->transparent);
}

/* Do settings lines 'lines' have keyword 'key' (on or off)? */
static BOOL has_key(CONST_STRPTR lines, CONST_STRPTR key)
{
    CONST_STRPTR p = lines;
    LONG kl = strlen(key);
    while (*p) {
        CONST_STRPTR q = *p == '(' ? p + 1 : p;
        if (!Strnicmp((STRPTR)q, (STRPTR)key, kl) && (q[kl] == '=' || q[kl] == ')' || q[kl] == '\n' || !q[kl]))
            return TRUE;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return FALSE;
}

/* Put saved settings lines back into use (Undo changes, a refused change).
   Keywords missing from the lines were not set then, so they are switched
   off. */
static BOOL put_lines(struct Dock *d, CONST_STRPTR lines)
{
    char *words = AllocVec(2 * SNAPLEN, MEMF_ANY), *now = words + SNAPLEN, key[32];
    LONG n = 0, k;
    CONST_STRPTR l = lines;
    BOOL ok;
    if (!words) return FALSE;
    words[0] = 0;
    config_get(d, NULL, now, SNAPLEN);           /* (the settings in use, as lines) */
    while (*l) {
        CONST_STRPTR e = l, eq;
        char one[512];                           /* (a settings file line can be this long) */
        LONG len;
        while (*e && *e != '\n') e++;
        len = e - l < (LONG)sizeof(one) - 1 ? e - l : (LONG)sizeof(one) - 1;
        memcpy(one, l, len); one[len] = 0;
        l = *e ? e + 1 : e;
        if (opts_key(one) <= 0 || n + len + 6 >= SNAPLEN) continue;   /* (CONFIG stays) */
        if (n) words[n++] = ' ';
        if (one[0] != '(' && (eq = strchr(one, '='))) {   /* KEY="value" (a file line may be quoted already) */
            LONG vl = strlen(eq + 1);
            CONST_STRPTR val = eq + 1;
            if (vl >= 2 && val[0] == '"' && val[vl - 1] == '"') { val++; vl -= 2; }
            memcpy(words + n, one, eq - one + 1); n += eq - one + 1;
            words[n++] = '"'; memcpy(words + n, val, vl); n += vl; words[n++] = '"';
        } else if (one[0] == '(' && strchr(one, ' ')) {   /* "(KEY=a b)": quoted, one word */
            words[n++] = '"'; strcpy(words + n, one); n += len; words[n++] = '"';
        } else { strcpy(words + n, one); n += len; }
        words[n] = 0;
    }
    for (k = 1; opts_name(k, key, sizeof(key)); k++) {   /* set since then (only those): off now - one */
        LONG kl = strlen(key);                   /* set only in the icon's tooltypes is in neither, and */
        if (!has_key(lines, key) && has_key(now, key) && n + kl + 4 < SNAPLEN) {   /* stays (1: not CONFIG) */
            if (n) words[n++] = ' ';
            words[n++] = '('; strcpy(words + n, key); n += kl; words[n++] = ')'; words[n] = 0;
        }
    }
    ok = config_set(d, words);
    FreeVec(words);
    return ok;
}

static void prefs_close_win(void)
{
    LONG k;
    struct Node *n;
    if (pw) {
        if (nch) RemoveGList(pw, chobj[0], nch);
        wleft = pw->LeftEdge; wtop = pw->TopEdge; CloseWindow(pw); pw = NULL;
    }
    for (k = 0; k < nch; k++) DisposeObject((Object *)chobj[k]);
    nch = 0;
    if (namefont) { CloseFont(namefont); namefont = NULL; }
    if (choosers)
        for (k = 0; k < NLIST; k++)
            while ((n = RemHead(&chlab[k]))) FreeChooserNode(n);
    if (glist) { FreeGadgets(glist); glist = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (pscr) { UnlockPubScreen(NULL, pscr); pscr = NULL; }   /* (after FreeVisualInfo) */
    keyg = recallg = hremg = NULL;
    nheads = 0;
    hlabx = -1;
    memset(cyc, 0, sizeof(cyc));
    memset(ag, 0, sizeof(ag));
    divx = linex = -1;
}

static void prefs_close(void)
{
    pick_close();
    prefs_close_win();
    if (held) { ah_show(held, FALSE); held = NULL; }   /* auto-hide as before */
    wide = FALSE;
    wleft = -1;
}

/* The dividers, the headings, and the names of the choosers (and of the
   Hidden programs list). */
static void divider(void)
{
    struct RastPort *rp = pw ? pw->RPort : NULL;
    LONG k;
    if (rp) {
        if (namefont) SetFont(rp, namefont);
        SetDrMd(rp, JAM1);
        SetAPen(rp, textpen);
        for (k = 0; k < nch; k++) {
            LONG n = strlen(chname[k].name);
            Move(rp, chname[k].right - TextLength(rp, (STRPTR)chname[k].name, n), chname[k].top + rp->TxBaseline);
            Text(rp, (STRPTR)chname[k].name, n);
        }
        if (hlabx >= 0) {
            Move(rp, hlabx - TextLength(rp, "Hidden programs", 15), hlaby + rp->TxBaseline);
            Text(rp, "Hidden programs", 15);
        }
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);   /* (headings: bold, also with a font that has none) */
        for (k = 0; k < nheads; k++) {          /* a heading: its name, then a line to the column's end */
            LONG n = strlen(heads[k].name), tw = TextLength(rp, (STRPTR)heads[k].name, n) + n;
            Move(rp, heads[k].x, heads[k].y + rp->TxBaseline);
            Text(rp, (STRPTR)heads[k].name, n);
            if (heads[k].w - tw - 8 > 4)
                DrawBevelBox(rp, heads[k].x + tw + 8, heads[k].y + rp->TxHeight / 2, heads[k].w - tw - 8, 2,
                             GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_END);
        }
        SetSoftStyle(rp, 0, FSF_BOLD);
    }
    if (pw && divx >= 0)
        DrawBevelBox(pw->RPort, divx, divy, 2, divh, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_END);
    if (pw && linex >= 0)
        DrawBevelBox(pw->RPort, linex, liney, linew, 2, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_END);
}

/* The window for 'wide', with the values in use. FALSE: could not open. */
static BOOL build(struct Dock *d)
{
    struct Screen *s = d->scr ? LockPubScreen(d->o->pubscreen) : NULL;   /* (its own lock: the dock may */
    struct RastPort *rp = &s->RastPort;
    struct NewGadget ng;
    struct Gadget *g;
    LONG i, k, lws = 0, cw = 0, lwa = 0, gw, bw, fh, top, step, rows, iw, ih, x0, xs, xa, t, room;
    LONG cs, rs, ca, ra, colws, colwa, sw, aw, maxh, gap, nb, pbw, ccw, nss = 0, nas = 0, rbw;
    BOOL showsimple = TRUE, showadv = wide;
    static struct Slot ss[MAXSLOT], as[MAXSLOT];
    static UBYTE scol[MAXSLOT], srow[MAXSLOT], acol[MAXSLOT], arow[MAXSLOT];
    static const char *const blab[] = { "Advanced >>", "<< Simple", "Undo changes", "Load...", "Save as...", NULL };

    if (s && s != d->scr) { UnlockPubScreen(NULL, s); s = NULL; }   /* move to another screen meanwhile) */
    pscr = s;
    if (!s || !(vi = GetVisualInfo(s, TAG_END))) return FALSE;
    fh = s->Font->ta_YSize;
    choosers = ChooserBase != NULL && (namefont = OpenFont(s->Font)) != NULL;
    for (i = 0; i < NLIST; i++) NewList(&chlab[i]);
    {
        struct DrawInfo *dri = GetScreenDrawInfo(s);
        if (dri) {
            textpen = dri->dri_Pens[TEXTPEN];
            FreeScreenDrawInfo(s, dri);
        }
    }
    nss = slots(ss, as, &nas, FALSE);           /* the two sides as slots */
    for (i = 0; i < NGROUP; i++) {
        LONG j, w = TextLength(rp, (STRPTR)group[i].name, strlen(group[i].name));
        if (w > lws) lws = w;
        for (j = 0; j < group[i].n; j++)
            if ((w = TextLength(rp, (STRPTR)group[i].labels[j], strlen(group[i].labels[j]))) > cw) cw = w;
    }
    if ((i = TextLength(rp, "Hotkey", 6)) > lws) lws = i;
    cw += 40;                                    /* the cycle symbol */
    pbw = TextLength(rp, "Custom...", 9) + 16;   /* the Colour list and its Custom... button */
    ccw = TextLength(rp, "#DDDDDD", 7) + 40;
    for (i = 0; i < group[7].n; i++)
        if ((gw = TextLength(rp, (STRPTR)group[7].labels[i], strlen(group[7].labels[i])) + 40) > ccw) ccw = gw;
    if (ccw + 4 + pbw > cw) cw = ccw + 4 + pbw;
    ccw = cw - 4 - pbw;
    for (i = 0; i < NADV; i++)
        if ((gw = TextLength(rp, (STRPTR)adv[i].label, strlen(adv[i].label))) > lwa) lwa = gw;
    gw = TextLength(rp, "XHelvetica/13 xx", 16) + 12;
    for (i = 0; i < NADV; i++)                   /* the lists' own choices fit */
        if (adv[i].kind == K_LIST) {
            LONG k, t;
            for (k = 0; adv[i].names[k]; k++)
                if ((t = TextLength(rp, (STRPTR)adv[i].names[k], strlen(adv[i].names[k])) + 40) > gw) gw = t;
        }
    for (i = 0, bw = 0; blab[i]; i++)
        if ((t = TextLength(rp, (STRPTR)blab[i], strlen(blab[i])) + 16) > bw) bw = t;
    rbw = TextLength(rp, "Remove", 6) + 16;
    top = s->WBorTop + fh + 1 + 6;
    step = fh + 6;
    colws = 10 + lws + 10 + cw;
    colwa = 10 + lwa + 10 + gw;
    nb = showadv ? 1 : 2;                        /* rows of buttons */
    maxh = s->Height - (top + 6 + (nb - 1) * step + fh + 4 + 6 + s->WBorBottom + 2);   /* room for rows */
    room = s->Width - s->WBorLeft - s->WBorRight - 10;   /* low screen: more columns (two always, */
    for (k = 0; k < 2; k++) {                    /* (too low even so: again without headings, a shorter list) */
        cs = columns(ss, nss, maxh / step, room / colws > 3 ? 3 : room / colws < 2 ? 2 : room / colws,   /*  as before) */
                     scol, srow, &rs);
        ca = columns(as, nas, maxh / step, room / colwa > 3 ? 3 : room / colwa < 2 ? 2 : room / colwa, acol, arow, &ra);
        if (k || (rs <= maxh / step && (!showadv || ra <= maxh / step))) break;
        nss = slots(ss, as, &nas, TRUE);
    }
    if (showadv) {                               /* side by side if it fits, else the Advanced side alone */
        LONG both = cs * colws + 12 + ca * colwa + 10 + s->WBorLeft + s->WBorRight;
        if (both > s->Width) showsimple = FALSE;
    }
    rows = showsimple && showadv ? (rs > ra ? rs : ra) : showadv ? ra : rs;
    sw = showsimple ? cs * colws : 0;
    aw = showadv ? ca * colwa : 0;
    iw = sw + (showsimple && showadv ? 12 : 0) + aw + 10;
    if (iw < 2 * bw + 8 + 20) iw = 2 * bw + 8 + 20;
    if (showadv && 4 * bw + 3 * 8 + 20 > iw) {   /* one row: a wider window if the screen allows, */
        if (4 * bw + 3 * 8 + 20 + s->WBorLeft + s->WBorRight <= s->Width) iw = 4 * bw + 3 * 8 + 20;
        else nb = 2;                             /* else two rows */
    }
    gap = showadv ? 0 : 8;                       /* the simple window: a little room above the line */
    ih = top - s->WBorTop - fh - 1 + rows * step + 6 + gap + (nb - 1) * step + fh + 4 + 6;
    x0 = s->WBorLeft + (iw - (sw + (showsimple && showadv ? 12 : 0) + aw + 10)) / 2;   /* centred */
    xs = x0;
    xa = x0 + (showsimple ? sw + 12 : 0);

    if (!(g = CreateContext(&glist))) return FALSE;
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = s->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_Height = fh + 4;
    ng.ng_Flags = PLACETEXT_LEFT;
    nheads = 0;
    hlabx = -1;
    for (k = 0; k < nss; k++)                    /* the headings */
        if (ss[k].what < 0 && showsimple && nheads < MAXHEAD) {
            heads[nheads].x = xs + scol[k] * colws + 10;
            heads[nheads].y = top + srow[k] * step + 2;
            heads[nheads].w = colws - 20;
            heads[nheads++].name = shead[-1 - ss[k].what];
        }
    for (k = 0; k < nas; k++)
        if (as[k].what < 0 && showadv && nheads < MAXHEAD) {
            heads[nheads].x = xa + acol[k] * colwa + 10;
            heads[nheads].y = top + arow[k] * step + 2;
            heads[nheads].w = colwa - 20;
            heads[nheads++].name = adv[-1 - as[k].what].head;
        }
    for (k = 0; showsimple && k < nss && g; k++) {
        LONG cur;
        if ((i = ss[k].what) < 0 || i >= NGROUP) continue;
        cur = current(d, i);
        labels_for(d, i, cur);
        ng.ng_LeftEdge = xs + scol[k] * colws + 10 + lws + 10;
        ng.ng_TopEdge = top + srow[k] * step;
        ng.ng_Width = i == 7 ? ccw : cw;
        ng.ng_GadgetText = (STRPTR)group[i].name;
        ng.ng_GadgetID = i;
        g = list_new(g, &ng, i, (const char *const *)glabels[i], cur, &cyc[i]);
        if (i == 7 && g) {                       /* Custom...: the colour picker */
            ng.ng_LeftEdge += cw - pbw;
            ng.ng_Width = pbw;
            ng.ng_GadgetText = (STRPTR)"Custom...";
            ng.ng_GadgetID = G_BPICK;
            ng.ng_Flags = PLACETEXT_IN;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
            ng.ng_Flags = PLACETEXT_LEFT;
        }
    }
    for (k = 0; k < nss && ss[k].what != G_KEY; k++) ;
    if (showsimple && g && k < nss) {            /* the hotkey, a commodities description */
        ng.ng_LeftEdge = xs + scol[k] * colws + 10 + lws + 10;
        ng.ng_TopEdge = top + srow[k] * step;
        ng.ng_Width = cw;
        ng.ng_GadgetText = (STRPTR)"Hotkey";
        ng.ng_GadgetID = G_KEY;
        key_case(keyshown, d->o->hotkey, sizeof(keyshown), TRUE);
        g = keyg = CreateGadget(STRING_KIND, g, &ng, GTST_String, keyshown, GTST_EditHook, &keyhook,
                                GTST_MaxChars, sizeof(d->keybuf) - 1, TAG_END);
    }
    for (k = 0; showadv && k < nas && g; k++) {
        if ((i = as[k].what) < 0) continue;
        ng.ng_LeftEdge = xa + acol[k] * colwa + 10 + lwa + 10;
        ng.ng_TopEdge = top + arow[k] * step;
        ng.ng_Width = adv[i].kind == K_SWITCH ? 26 : gw;
        ng.ng_GadgetText = (STRPTR)adv[i].label;
        ng.ng_GadgetID = G_ADV + i;
        if (adv[i].kind == K_HIDE) {             /* the list (its name drawn by divider()), the full field */
            hidden_fill(d);                      /* width - names can be long; Remove beneath it, at the right */
            hlabx = ng.ng_LeftEdge - 8;
            hlaby = ng.ng_TopEdge + 2;
            ng.ng_GadgetText = NULL;
            ng.ng_Height = (as[k].h - 2) * step + fh + 4;
            g = ag[i] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, &hlist, GTLV_ShowSelected, NULL, TAG_END);
            if (!g) break;
            ng.ng_TopEdge += (as[k].h - 1) * step;
            ng.ng_Height = fh + 4;
            ng.ng_LeftEdge += gw - rbw;
            ng.ng_Width = rbw;
            ng.ng_GadgetText = (STRPTR)"Remove";
            ng.ng_GadgetID = G_BHREM;
            ng.ng_Flags = PLACETEXT_IN;
            g = hremg = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, TRUE, TAG_END);
            ng.ng_Flags = PLACETEXT_LEFT;
            continue;
        }
        switch (adv[i].kind) {
        case K_ALIGN:  g = list_new(g, &ng, NGROUP + i, alignname, 0, &ag[i]); break;
        case K_MSG:    g = list_new(g, &ng, NGROUP + i, msgname, 0, &ag[i]); break;
        case K_LIST:   g = list_new(g, &ng, NGROUP + i, adv[i].names, adv[i].def, &ag[i]); break;
        case K_ICON:
        case K_INT:    g = ag[i] = CreateGadget(INTEGER_KIND, g, &ng, GTIN_MaxChars, 6, TAG_END); break;
        case K_STR:
            if (!strcmp(adv[i].key, "UNPACKDIR")) {   /* a drawer: a "..." button beside it opens a requester */
                LONG dbw = TextLength(rp, "...", 3) + 16;
                ng.ng_Width = gw - dbw - 4;
                g = str_new(g, &ng, &ag[i]);
                if (!g) break;
                ng.ng_LeftEdge += gw - dbw;
                ng.ng_Width = dbw;
                ng.ng_GadgetText = (STRPTR)"...";
                ng.ng_GadgetID = G_BDIR;
                ng.ng_Flags = PLACETEXT_IN;
                g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
                ng.ng_Flags = PLACETEXT_LEFT;
                break;
            }
            /* fall through */
        case K_KEY:    g = str_new(g, &ng, &ag[i]); break;
        default:       g = ag[i] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Scaled, TRUE, TAG_END); break;
        }
    }
    {   /* The simple window: a block of two columns (Undo changes and Advanced >>,
           Load... and Save as...). With the Advanced side: one row across
           the whole window, or two if the screen is too narrow. */
        static const struct { const char *name; UWORD id; UBYTE col, row; } but[4] = {
            { "Undo changes", G_BRECALL, 0, 0 }, { NULL, G_BMORE, 1, 0 },
            { "Load...", G_BLOAD, 0, 1 }, { "Save as...", G_BSAVEAS, 1, 1 } };
        static const UBYTE row1[4] = { 0, 2, 3, 1 };   /* Undo Load Save-as Simple */
        static const UBYTE row2[4] = { 0, 1, 2, 3 };   /* Undo Simple / Load Save-as */
        LONG gx = s->WBorLeft + (iw - (2 * bw + 8)) / 2, per = 4 / nb;
        for (i = 0; i < 4 && g; i++) {
            LONG k = showadv ? (nb == 1 ? row1 : row2)[i] : i;
            if (showadv) {
                ng.ng_LeftEdge = s->WBorLeft + 10 + (i % per) * (iw - 20 - bw) / (per - 1);
                ng.ng_TopEdge = top + rows * step + 6 + (i / per) * step;
            } else {
                ng.ng_LeftEdge = gx + but[k].col * (bw + 8);
                ng.ng_TopEdge = top + rows * step + 6 + gap + but[k].row * step;
            }
            ng.ng_Width = bw;
            ng.ng_GadgetText = (STRPTR)(but[k].name ? but[k].name : wide ? "<< Simple" : "Advanced >>");
            ng.ng_GadgetID = but[k].id;
            ng.ng_Flags = PLACETEXT_IN;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
            if (but[k].id == G_BRECALL) recallg = g;
        }
    }
    if (!g) return FALSE;
    if (wleft < 0) { wleft = (s->Width - iw) / 2; wtop = (s->Height - ih - fh) / 2; }
    if (wleft + iw + s->WBorLeft + s->WBorRight > s->Width) wleft = s->Width - iw - s->WBorLeft - s->WBorRight;
    if (wtop + ih + fh + 4 > s->Height) wtop = s->Height - ih - fh - 4;
    if (wleft < 0) wleft = 0;
    if (wtop < 0) wtop = 0;
    pw = OpenWindowTags(NULL, WA_PubScreen, s, WA_Title, SD_NAME " " SD_VERSION SD_BUILD " Settings",
                        WA_InnerWidth, iw, WA_InnerHeight, ih, WA_Left, wleft, WA_Top, wtop,
                        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                        WA_SimpleRefresh, TRUE, WA_Gadgets, glist, WA_AutoAdjust, TRUE,
                        WA_IDCMP, CYCLEIDCMP | BUTTONIDCMP | INTEGERIDCMP | STRINGIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP |
                                  IDCMP_GADGETDOWN | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW, TAG_END);
    if (!pw) return FALSE;
    GT_RefreshWindow(pw, NULL);
    if (nch) { AddGList(pw, chobj[0], ~0, nch, NULL); RefreshGList(chobj[0], pw, NULL, nch); }
    linex = s->WBorLeft + 4; liney = top + rows * step + 1 + gap / 2; linew = iw - 8;   /* across the window */
    if (showsimple && showadv) { divx = xs + sw + 5; divy = top - 2; divh = liney - divy; }   /* down to the line */
    divider();
    sync(d);                                     /* (the Advanced side's values) */
    recall_state(d);
    return TRUE;
}

/* Open the window (or bring it to the front), with the values in use. */
static void prefs_open(struct Dock *d, BOOL more)
{
    extern struct Library *GadToolsBase;
    if (pw) { WindowToFront(pw); ActivateWindow(pw); return; }
    if (!GadToolsBase) { sd_msg(d, "Cannot open the settings window"); return; }
    wide = more;
    adopt_custom(d);                             /* a TINT from the file: the custom colour */
    if (!keepsnap || !snapbuf) {
        if (!snapbuf && !(snapbuf = AllocVec(SNAPLEN, MEMF_ANY))) { sd_msg(d, "Cannot open the settings window"); return; }
        config_get(d, NULL, snapbuf, SNAPLEN);   /* for Undo changes (freed when the window closes) */
        snapopen = !d->hidden;
    }
    keepsnap = FALSE;
    if (!build(d)) { prefs_close(); sd_msg(d, "Cannot open the settings window"); return; }
    held = d;                                    /* out while the window is open: changes show */
    ah_show(d, TRUE);
}

/* Take the hotkey gadget's text (in lowercase) if it is a valid description;
   otherwise beep and show the hotkey in use again. */
static void key_apply(struct Dock *d)
{
    char k[sizeof(d->keybuf)];
    key_case(k, ((struct StringInfo *)keyg->SpecialInfo)->Buffer, sizeof(k), FALSE);
    if (!Stricmp(k, d->o->hotkey)) return;
    if (in_hotkey_valid(k)) {
        strcpy(d->keybuf, k);
        d->o->hotkey = d->keybuf;
        in_set_hotkey(d->keybuf);
    } else {
        sd_beep(d);
        key_case(keyshown, d->o->hotkey, sizeof(keyshown), TRUE);
        GT_SetGadgetAttrs(keyg, pw, NULL, GTST_String, keyshown, TAG_END);
    }
}

static void apply_line(struct Dock *d, CONST_STRPTR line);

/* An Advanced gadget changed. Its value goes through the same code as ARexx
   SET (apply_line). */
static void adv_apply(struct Dock *d, LONG i, UWORD code)
{
    char line[SD_LINEMAX + 32], v[SD_LINEMAX];
    struct StringInfo *si = (struct StringInfo *)ag[i]->SpecialInfo;
    LONG a[2];
    switch (adv[i].kind) {
    case K_ALIGN:                                /* the edge stays, only the letter after it */
        config_get(d, "POS", v, sizeof(v));
        line[0] = 'P'; line[1] = 'O'; line[2] = 'S'; line[3] = '='; line[4] = v[0] ? ToUpper(v[0]) : 'B';
        line[5] = code == 0 ? 0 : (line[4] == 'T' || line[4] == 'B') ? (code == 1 ? 'L' : 'R')
                                                                     : (code == 1 ? 'T' : 'B');
        line[6] = 0;
        break;
    case K_INT:                                  /* (the text: LongInt may not be set yet) */
        a[0] = (LONG)adv[i].key; a[1] = 0;
        StrToLong((STRPTR)si->Buffer, &a[1]);
        SD_FMT(line, "%s=%ld", a);
        break;
    case K_ICON:                                 /* one size for both */
        a[0] = 0;
        StrToLong((STRPTR)si->Buffer, &a[0]);
        a[1] = a[0];
        SD_FMT(line, "SCALEICONW=%ld SCALEICONH=%ld", a);
        break;
    case K_MSG:
        strcpy(line, code == 1 ? "QUIET (VERBOSE)" : code == 2 ? "(QUIET) VERBOSE" : "(QUIET) (VERBOSE)");
        break;
    case K_LIST:
        if (adv[i].vals) {
            LONG nn;
            for (nn = 0; adv[i].names[nn]; nn++) ;
            if (code >= nn) code = 0;            /* (a chooser value out of range: the first) */
            a[0] = (LONG)adv[i].key; a[1] = adv[i].vals[code]; SD_FMT(line, "%s=%ld", a);
        } else SD_FMT(line, code ? "%s" : "(%s)", &adv[i].key);
        break;
    case K_KEY:                                  /* (a key not valid: beep, the one in use shown again) */
        key_case(v, str_get(ag[i]), 64, FALSE);
        if (!Stricmp(v, key_now(d, i)) || (in_key_off(v) && in_key_off(key_now(d, i)))) { sync(d); return; }
        if (!in_key_valid(v)) { sd_beep(d); sync(d); return; }
        a[0] = (LONG)adv[i].key; a[1] = (LONG)(in_key_off(v) ? "none" : v);
        SD_FMT(line, "%s=\"%s\"", a);
        break;
    case K_STR:
        if (!str_get(ag[i])[0]) SD_FMT(line, "(%s)", &adv[i].key);
        else { strcpy(line, adv[i].key); strcat(line, "=\""); strncat(line, (char *)str_get(ag[i]), SD_LINEMAX - 1); strcat(line, "\""); }
        break;
    default:
        SD_FMT(line, ag[i]->Flags & GFLG_SELECTED ? "%s" : "(%s)", &adv[i].key);
        break;
    }
    apply_line(d, line);
}

/* Remove: the picked name out of IGNORE (its program shows again). */
static void hidden_remove(struct Dock *d)
{
    static char line[600];
    CONST_STRPTR name, p = d->o->ignore, q;
    LONG n = 0, len;
    if (hsel < 0 || hsel >= nhid) return;
    name = hnode[hsel].ln_Name;
    strcpy(line, "IGNORE=\"");
    while (p && *p) {                            /* IGNORE as it is now (a name hidden meanwhile, or */
        while (*p == ' ' || *p == ',' || *p == '"') p++;   /* more than the list holds), without it */
        for (q = p; *q && *q != ','; q++) ;
        for (len = q - p; len > 0 && (p[len - 1] == ' ' || p[len - 1] == '"'); len--) ;
        if (len > 0 && !(len == (LONG)strlen(name) && !Strnicmp((STRPTR)p, name, len)) &&
            strlen(line) + len + 3 < sizeof(line)) {
            if (n++) strcat(line, ",");
            strncat(line, (char *)p, len);
        }
        p = q;
    }
    strcat(line, "\"");
    apply_line(d, n ? line : "(IGNORE)");
}

/* A settings line from the window, into use. A value that is not valid, or
   one that would push icons off the dock, is refused with a beep. */
static BOOL nosync;                     /* adv_pending: the fields are not reset between its lines */
static void apply_line(struct Dock *d, CONST_STRPTR line)
{
    char *before = AllocVec(SNAPLEN, MEMF_ANY);
    LONG over = dock_extra(d);
    BOOL told = d->overtold, wasopen = !d->hidden;
    if (!before || (config_get(d, NULL, before, SNAPLEN), !config_set(d, line))) {
        FreeVec(before);
        sd_beep(d);
        if (!nosync) sync(d);
        return;
    }
    d->overtold = TRUE;                          /* no "do not fit" message for a try */
    if (wasopen) dock_reopen(d);
    /* Not taken if the dock could not open again (a screen that isn't
       there) or icons would no longer fit: */
    if ((wasopen && d->hidden) || dock_extra(d) > over) {
        put_lines(d, before);
        if (wasopen) dock_reopen(d);
        sd_beep(d);
    }
    FreeVec(before);
    d->overtold = told;
    if (!nosync) sync(d);
    if (pw) WindowToFront(pw);                   /* a new dock window opens in front */
}

static CONST_STRPTR key_now(struct Dock *d, LONG i)
{
    CONST_STRPTR k = !strcmp(adv[i].key, "MINKEY") ? d->o->minkey : !strcmp(adv[i].key, "MINALLKEY") ? d->o->minallkey
                     : d->o->ahkey;
    return in_key_off(k) ? (CONST_STRPTR)"none" : k;
}

/* Take numbers and text typed on the Advanced side without Return, before
   closing, folding, Load... and Save as... */
static void adv_pending(struct Dock *d)
{
    char v[SD_LINEMAX];
    LONG i, n, m;
    nosync = TRUE;                               /* (a sync after the first would reset the others: lost) */
    for (i = 0; pw && i < NADV; i++) {
        struct StringInfo *si;
        if (ag[i] && adv[i].kind == K_KEY) {
            key_case(v, str_get(ag[i]), 64, FALSE);
            if (Stricmp(v, key_now(d, i)) && !(in_key_off(v) && in_key_off(key_now(d, i)))) adv_apply(d, i, 0);
            continue;
        }
        if (!ag[i] || (adv[i].kind != K_INT && adv[i].kind != K_ICON && adv[i].kind != K_STR)) continue;
        config_get(d, adv[i].key, v, sizeof(v));
        if (adv[i].kind == K_STR) { if (strcmp((char *)str_get(ag[i]), v)) adv_apply(d, i, 0); continue; }
        si = (struct StringInfo *)ag[i]->SpecialInfo;
        n = m = 0;
        StrToLong(v, &n);
        StrToLong((STRPTR)si->Buffer, &m);
        if (m != n) adv_apply(d, i, 0);
    }
    nosync = FALSE;
    sync(d);
}

struct Window *am_prefs_win(void) { return pw; }

/* The settings changed elsewhere (a program dragged off the dock: IGNORE):
   the window shows them. */
void am_prefs_sync(struct Dock *d) { if (pw) { sync(d); recall_state(d); } }

/* The next settings line from *p that is not a bare "(KEY)": a keyword
   switched off is the same as one not set (Undo changes adds those). */
static CONST_STRPTR next_line(CONST_STRPTR *p, LONG *len)
{
    CONST_STRPTR l = *p, e;
    while (*l) {
        for (e = l; *e && *e != '\n'; e++) ;
        *p = *e ? e + 1 : e;
        if (!(*l == '(' && e[-1] == ')' && !memchr(l, '=', e - l))) { *len = e - l; return l; }
        l = *p;
    }
    return NULL;
}

/* The same settings in both (as settings lines)? */
static BOOL same(CONST_STRPTR a, CONST_STRPTR b)
{
    CONST_STRPTR x, y;
    LONG xl, yl;
    for (;;) {
        x = next_line(&a, &xl);
        y = next_line(&b, &yl);
        if (!x || !y) return x == y;
        if (xl != yl || memcmp(x, y, xl)) return FALSE;
    }
}

/* Undo changes is only there to press when something changed. */
static void recall_state(struct Dock *d)
{
    char *now;
    if (!recallg || !snapbuf || !(now = AllocVec(SNAPLEN, MEMF_ANY))) return;
    config_get(d, NULL, now, SNAPLEN);
    GT_SetGadgetAttrs(recallg, pw, NULL, GA_Disabled, same(now, snapbuf), TAG_END);
    FreeVec(now);
}

/* Back to the settings of the moment the window opened (or of Load...). */
static void recall(struct Dock *d)
{
    pick_close();
    if (!snapbuf || !put_lines(d, snapbuf)) sd_beep(d);   /* (not taken: too many settings lines) */
    if (snapopen || !d->hidden) dock_reopen(d);   /* (also the hotkey: ah_start) */
    sync(d);
    if (pw) WindowToFront(pw);                   /* a new dock window opens in front */
}

/* Close the window, keeping its changes: they go into the dock's own file
   (if there are any). */
static void prefs_store(struct Dock *d)
{
    char *now;
    if (!pw) return;
    if (keyg) key_apply(d);                      /* typed, but Return not pressed */
    adv_pending(d);
    if ((now = AllocVec(SNAPLEN, MEMF_ANY))) config_get(d, NULL, now, SNAPLEN);
    prefs_close();
    if (loaded || !now || !snapbuf || !same(now, snapbuf)) config_store(d);   /* (no memory to compare: stored) */
    FreeVec(now);
    FreeVec(snapbuf);                            /* (closed for good: the next open takes a new snapshot) */
    snapbuf = NULL;
    loaded = FALSE;
}

void am_prefs_close(struct Dock *d) { prefs_store(d); }   /* ARexx, or quitting: its changes kept */

ULONG am_prefs_sig(void)
{
    return (pw ? 1UL << pw->UserPort->mp_SigBit : 0) | (aboutw ? 1UL << aboutw->UserPort->mp_SigBit : 0) |
           (notew ? 1UL << notew->UserPort->mp_SigBit : 0) | pick_sig();
}

void am_settings(struct Dock *d, BOOL more) { prefs_open(d, more); }   /* ARexx SETTINGS [ADVANCED] */

/* Messages of the settings window and the requesters. */
void am_prefs_handle(struct Dock *d)
{
    struct IntuiMessage *im;
    BOOL close = FALSE, fold = FALSE, load = FALSE;
    ULONG rgb;
    LONG r;
    BOOL acted = FALSE;                          /* (a change: Undo changes looked at again) */
    if (d->dragging) drag_cancel(d, TRUE);       /* (our drag holds the layers: no window changes) */

    if (pick_sig() && (r = pick_handle(&rgb)) != 0) {   /* the colour picker closed */
        acted = TRUE;
        if (r > 0) {                             /* OK: the custom colour, in use */
            LONG v[1];
            v[0] = rgb;
            SD_FMT(d->custbuf, "#%06lx", v);
            d->o->customtint = d->o->tint = d->custbuf;
            if (!d->hidden) dock_reopen(d);
            if (pw) WindowToFront(pw);
        }
        sync(d);                                 /* (Cancel: the Colour list back as it was) */
    }
    if (aboutw) {
        do r = SysReqHandler(aboutw, NULL, FALSE);
        while (r == -2 && !IsMsgPortEmpty(aboutw->UserPort));
        if (r != -2) about_close();
    }
    if (notew) {
        do r = SysReqHandler(notew, NULL, FALSE);
        while (r == -2 && !IsMsgPortEmpty(notew->UserPort));
        if (r != -2) note_close();
    }
    while (pw && (im = GT_GetIMsg(pw->UserPort))) {
        static ULONG dsec, dmic;                 /* the last click into a text field ... */
        static struct Gadget *dgad;              /* ... and which one */
        ULONG class = im->Class, sec = im->Seconds, mic = im->Micros;
        UWORD code = im->Code;
        struct Gadget *gad = (struct Gadget *)im->IAddress;
        GT_ReplyIMsg(im);
        if (class == IDCMP_GADGETDOWN && is_str(gad)) {   /* a double-click marks all the text */
            LONG n = strlen((char *)str_get(gad));
            if (gad == dgad && DoubleClick(dsec, dmic, sec, mic) && n > 0)
                SetGadgetAttrs(gad, pw, NULL, STRINGA_Mark, (ULONG)(n - 1 < 0xFFFF ? n - 1 : 0xFFFE), TAG_END);
            dgad = gad; dsec = sec; dmic = mic;
            continue;
        }
        if (class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(pw);
            divider();
            if (nch) RefreshGList(chobj[0], pw, NULL, nch);
            GT_EndRefresh(pw, TRUE);
        }
        else if (class == IDCMP_CLOSEWINDOW) close = TRUE;
        else if (class == IDCMP_GADGETUP) {
            acted = TRUE;
            LONG id = gad->GadgetID;
            code = list_value(gad, code);
            if (id == G_BRECALL) recall(d);
            else if (id == G_BMORE) fold = TRUE;
            else if (id == G_BLOAD) load = TRUE;
            else if (id == G_BSAVEAS) { if (keyg) key_apply(d); adv_pending(d); config_choose(d, TRUE); }
            else if (id == G_KEY) key_apply(d);
            else if (id >= G_ADV && id < G_ADV + NADV && adv[id - G_ADV].kind == K_HIDE) {   /* a name picked */
                hsel = code < nhid ? code : -1;
                if (hremg) GT_SetGadgetAttrs(hremg, pw, NULL, GA_Disabled, hsel < 0, TAG_END);
            }
            else if (id >= G_ADV && id < G_ADV + NADV) adv_apply(d, id - G_ADV, code);
            else if (id == G_BHREM) hidden_remove(d);
            else if (id == G_BDIR) {             /* ...: a drawer for Unpack drawer */
                char dir[SD_LINEMAX];
                LONG k;
                for (k = 0; k < NADV && strcmp(adv[k].key, "UNPACKDIR"); k++) ;
                if (k < NADV && ag[k]) {
                    strncpy(dir, (char *)str_get(ag[k]), sizeof(dir) - 1);
                    dir[sizeof(dir) - 1] = 0;
                    if (config_choose_dir(d, dir, sizeof(dir))) { str_set(ag[k], dir); adv_apply(d, k, 0); }
                }
            }
            else if (id == G_BPICK) {            /* Custom...: the colour picker */
                ULONG start = d->o->customtint ? hexval(d->o->customtint) : d->tint;
                if (!pick_open(d, start)) sd_beep(d);
            }
            else if (id >= 0 && id < NGROUP && map(d, id, code) > 0) {   /* a change: on the dock at once */
                BOOL reopen = FALSE, told = d->overtold;
                ULONG mid = map(d, id, code);
                struct Opts was = *d->o;
                char wpos[sizeof(d->posbuf)];
                LONG over = dock_extra(d);
                memcpy(wpos, d->posbuf, sizeof(wpos));
                choose(d, mid, &reopen);
                if (reopen && !d->hidden) {      /* (closed by ARexx HIDE: at its SHOW) */
                    d->overtold = TRUE;          /* no "do not fit" message for a try */
                    dock_reopen(d);
                    if (dock_extra(d) > over) {  /* icons would not fit any more: not taken */
                        *d->o = was;
                        memcpy(d->posbuf, wpos, sizeof(wpos));
                        dock_reopen(d);
                        sd_beep(d);
                    }
                    d->overtold = told;
                }
                sync(d);                         /* (the other side too) */
                if (pw) WindowToFront(pw);       /* a new dock window opens in front */
            } else if (id >= 0 && id < NGROUP) sync(d);   /* "Other": nothing to choose */
        }
    }
    if (load && pw) {                            /* another settings file; the window shows its values */
        BOOL more = wide;
        if (keyg) key_apply(d);
        adv_pending(d);
        pick_close();
        prefs_close_win();                       /* (the dock stays out meanwhile) */
        if (config_choose(d, FALSE)) loaded = TRUE;
        else keepsnap = TRUE;                    /* (cancelled: Undo changes still goes back as far) */
        prefs_open(d, more);
        return;
    }
    if (fold && pw) {                            /* the window again, folded out or in */
        if (keyg) key_apply(d);
        adv_pending(d);
        prefs_close_win();
        wide = !wide;
        if (!build(d)) { prefs_close(); sd_msg(d, "Cannot open the settings window"); }
        return;
    }
    if (close) prefs_store(d);
    else if (pw && acted) recall_state(d);       /* (not for the ticks and moves the list asks for) */
}

BOOL am_handle(struct Dock *d)
{
    struct AppMessage *am;
    BOOL run = TRUE, reopen = FALSE, dropped;
    if (d->dragging) drag_cancel(d, TRUE);       /* (our drag holds the layers: no window changes) */

    while ((am = (struct AppMessage *)GetMsg(amport))) {
        ULONG id = am->am_Type == AMTYPE_APPMENUITEM ? am->am_ID : 0;
#ifdef SD_DEBUG
        { char t[64]; LONG v[3]; v[0] = am->am_Type; v[1] = am->am_ID; v[2] = am->am_NumArgs;
          SD_FMT(t, "appmsg type %ld id %ld args %ld", v); TRACE(t); }
#endif
        struct Window *sh = am->am_Type == AMTYPE_APPWINDOW && am->am_ID == 0x5344 ? run_shell_drop(d, am) : NULL;
        dropped = !sh && am->am_ID != 0x5344 && (am->am_Type == AMTYPE_APPWINDOW || am->am_Type == AMTYPE_APPWINDOWZONE) &&
                  d->win && drag_drop_wb(d, am);
        ReplyMsg((struct Message *)am);
        if (sh) run_shell_type(d, sh);           /* (icons dropped onto a Shell window: running.c) */
        run_shell_pending(d);                    /* (... onto the dock's Shell icon: into the Shell in front) */
        if (dropped) {                             /* the dock grows, icons make way */
            dock_reopen_glide(d);
            config_store(d);
        }
        if (id && !choose(d, id, &reopen)) run = FALSE;
    }
    if (reopen && run) dock_reopen(d);
    return run;
}

#ifdef SD_DEBUG
/* Debug ARexx MENU <name>: a menu choice by name (tests, no clicking). */
BOOL am_choose(struct Dock *d, CONST_STRPTR name)
{
    static const struct { const char *name; ULONG id; } names[] = {
        { "TOP", AM_TOP }, { "BOTTOM", AM_BOTTOM }, { "LEFT", AM_LEFT }, { "RIGHT", AM_RIGHT },
        { "TEXTBELOW", AM_TEXTBELOW }, { "TEXTHOVER", AM_TEXTHOVER }, { "TEXTOFF", AM_TEXTOFF },
        { "SOLID", AM_GLASSOFF }, { "TRANSPARENT", AM_GLASSON }, { "INFRONT", AM_FRONTON },
        { "NOTINFRONT", AM_FRONTOFF }, { "RELOAD", AM_RELOAD }, { "SAVE", AM_SAVE }, { NULL, 0 } };
    BOOL run = TRUE, reopen = FALSE;
    LONG i;
    for (i = 0; names[i].name; i++)
        if (!Stricmp((STRPTR)name, (STRPTR)names[i].name)) {
            run = choose(d, names[i].id, &reopen);
            if (reopen && run) dock_reopen(d);
            return TRUE;
        }
    return FALSE;
}
#endif

void am_close(void)
{
    struct Message *m;
    prefs_close();
    FreeVec(snapbuf);
    snapbuf = NULL;
    about_close();
    note_close();
    remove_all();
    if (amport) {
        while ((m = GetMsg(amport))) ReplyMsg(m);
        DeleteMsgPort(amport);
        amport = NULL;
    }
}
