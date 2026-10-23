/* SimpleDock - the input handler: pointer and left button
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   input.device calls inhandler.s for every batch of input events (at least
   ten times a second, also when nothing moves). in_watch() notes the left
   button and the active window; while auto-hide, hover labels, labels only
   or "in front" are on, in_check() compares the pointer with the dock. The dock
   task is signalled only when something it waits for changes - no polling,
   no timer while idle. */
#include <string.h>
#include <exec/interrupts.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/commodities.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include "sd.h"

extern struct IntuitionBase *IntuitionBase;
static BOOL in_lmb;                     /* left button, from the button events */
static BOOL in_lmb_seen;                /* ... as in_check last saw it */
static IX in_ix;                        /* the hotkey (commodities description, ParseIX) */
static volatile BOOL in_ixok;
static UWORD in_hotcode = 0xFFFF;       /* its key while down: the key-up is swallowed too */

/* A hotkey of modifiers only ("ctrl alt shift"): they are held together and
   let go with no other key or button pressed in between (so Ctrl+Alt+Shift+key
   in a program never brings the dock out); moving the mouse meanwhile is
   fine. Groups: 1 Shift, 2 Alt, 4 Ctrl, 8 left Amiga, 16 right Amiga. */
static UWORD in_tapneed;                /* the groups wanted (0: a normal hotkey) */
LONG hk_stat[HK_N];                     /* (ARexx HOTKEYINFO) */
static BOOL in_taparmed;                /* all of them held, nothing else yet */
static UWORD in_held;                   /* the groups held, from the modifier keys' own press and release
                                           events (the qualifier field can lag or be wrong on some
                                           keyboards); either way arms */

static UWORD tap_groups(UWORD q)
{
    return (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT) ? 1 : 0) | (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT) ? 2 : 0) |
           (q & IEQUALIFIER_CONTROL ? 4 : 0) | (q & IEQUALIFIER_LCOMMAND ? 8 : 0) | (q & IEQUALIFIER_RCOMMAND ? 16 : 0);
}

/* The groups of a description made of modifier names only (0: not one). */
static UWORD tap_parse(CONST_STRPTR desc)
{
    static const struct { const char *name; UBYTE g; } q[] = {
        { "shift", 1 }, { "lshift", 1 }, { "rshift", 1 }, { "alt", 2 }, { "lalt", 2 }, { "ralt", 2 },
        { "ctrl", 4 }, { "control", 4 }, { "lamiga", 8 }, { "lcommand", 8 }, { "ramiga", 16 }, { "rcommand", 16 } };
    UWORD g = 0;
    LONG i, n;
    while (desc && *desc) {
        while (*desc == ' ') desc++;
        if (!*desc) break;
        for (n = 0; desc[n] && desc[n] != ' '; n++) ;
        for (i = 0; i < (LONG)(sizeof(q) / sizeof(q[0])); i++)
            if ((LONG)strlen(q[i].name) == n && !Strnicmp((STRPTR)desc, (STRPTR)q[i].name, n)) break;
        if (i == (LONG)(sizeof(q) / sizeof(q[0]))) return 0;   /* a key: a normal hotkey */
        g |= q[i].g;
        desc += n;
    }
    return g;
}

static BOOL ix_parse(CONST_STRPTR desc, IX *ix);

/* The middle-button forms: "mbutton" (pressed and let go, with nothing else),
   "ctrl mbutton" (the same with those modifiers held), or "mbutton 1" /
   "mbutton f1" / "mbutton ctrl 1": the middle button held while that key is
   pressed (and not the bare click). Every dock whose hotkey it is answers. Returns
   0x8000 + the modifier groups (0: not this form); *keyix and *haskey say
   whether a key belongs to it. */
static UWORD mb_parse(CONST_STRPTR desc, IX *keyix, BOOL *haskey)
{
    char rest[48];
    LONG n, m, i, k = 0;
    UWORD g = 0;
    *haskey = FALSE;
    if (!desc) return 0;
    n = strlen((char *)desc);
    for (i = 0; i + 7 <= n; i++) {               /* the word "mbutton" anywhere; the rest around it */
        if (!Strnicmp((STRPTR)desc + i, (STRPTR)"mbutton", 7) && (i == 0 || desc[i - 1] == ' ') &&
            (i + 7 == n || desc[i + 7] == ' ')) break;
    }
    if (i + 7 > n || n - 7 > (LONG)sizeof(rest) - 1) return 0;   /* (too long for rest[]: not this form) */
    for (m = 0; m < n; m++) {
        if (m == i) { m += 6; continue; }
        if (k < (LONG)sizeof(rest) - 1) rest[k++] = desc[m];
    }
    rest[k] = 0;
    for (m = 0; rest[m] == ' '; m++) ;           /* (only spaces left: the button alone) */
    while (k > m && rest[k - 1] == ' ') rest[--k] = 0;
    if (k == m) return 0x8000;
    if ((g = tap_parse(rest + m))) return 0x8000 | g;
    if (ix_parse(rest + m, keyix) && keyix->ix_Class == IECLASS_RAWKEY) { *haskey = TRUE; return 0x8000; }
    return 0;
}

/* A commodities key description into *ix. FALSE: not one (or no commodities),
   or one of Intuition's own: Left Amiga + N / M (screen to back / front),
   which the handler would take away from it. */
static BOOL ix_parse(CONST_STRPTR desc, IX *ix)
{
    extern struct Library *CxBase;
    if (!CxBase || !desc || !*desc) return FALSE;
    ix->ix_Version = IX_VERSION;
    if (ParseIX((STRPTR)desc, ix) != 0) return FALSE;
    if (ix->ix_Class != IECLASS_RAWKEY) return FALSE;   /* (only keys are matched: "rbutton", "timer"... would never fire) */
    if ((ix->ix_Code == 0x36 || ix->ix_Code == 0x37) && (ix->ix_Qualifier & IEQUALIFIER_LCOMMAND)) return FALSE;
    return TRUE;
}

static volatile UWORD in_mbneed;        /* the hotkey is the middle button: 0x8000 + its modifier groups */
static IX in_mbix;                      /* ... with this key pressed while it is held (in_mbkey) */
static volatile BOOL in_mbkey;
static volatile BOOL in_mbheld, in_mbused;   /* the button is down; a key was pressed meanwhile */
static UWORD in_mbqual;                 /* the modifier groups held when it went down */
static UWORD in_mbcode = 0xFFFF;        /* the chord key while down: its repeats and release are swallowed */
static BOOL in_mbqualok;                /* this system marks events with IEQUALIFIER_MIDBUTTON while it is held */
#define SD_NULLED 0x5D                  /* ie_SubClass of an event a dock took (other docks follow the chord by it) */

/* The hotkey from its description ("ctrl alt d", "ctrl alt shift", or
   "mbutton" / "ctrl mbutton"); matched in in_watch. */
BOOL in_set_hotkey(CONST_STRPTR desc)
{
    BOOL ok;
    Forbid();                                    /* (the handler runs as a task: it sees one state or the other) */
    in_ixok = FALSE;
    in_taparmed = FALSE;
    in_mbneed = 0; in_mbheld = FALSE; in_mbcode = 0xFFFF;
    if ((in_tapneed = tap_parse(desc))) ok = TRUE;
    else if ((in_mbneed = mb_parse(desc, &in_mbix, (BOOL *)&in_mbkey))) ok = TRUE;
    else ok = in_ixok = ix_parse(desc, &in_ix);
    Permit();
    return ok;
}

/* The other keys (minimise, minimise all, auto-hide on/off): ordinary
   commodities descriptions, "none" (or empty) for off. */
static IX in_kix[NKEYS];
static volatile BOOL in_kok[NKEYS];
static UWORD in_kcode = 0xFFFF;         /* the key of one while down: its key-up is swallowed too */

BOOL in_key_off(CONST_STRPTR desc)
{
    return !desc || !*desc || !Stricmp((STRPTR)desc, "none");
}

/* Can desc be one of them ("none" can; modifiers alone only the hotkey)? */
BOOL in_key_valid(CONST_STRPTR desc)
{
    IX ix;
    return in_key_off(desc) || (!tap_parse(desc) && ix_parse(desc, &ix));
}

/* Returns the bits of those that are not valid descriptions (they are off). */
ULONG in_set_keys(CONST_STRPTR *desc)
{
    ULONG bad = 0;
    LONG k;
    for (k = 0; k < NKEYS; k++) {
        in_kok[k] = FALSE;
        if (in_key_off(desc[k])) continue;
        if (tap_parse(desc[k]) || !ix_parse(desc[k], &in_kix[k])) bad |= 1UL << k;   /* (modifiers only: */
        else in_kok[k] = TRUE;                                                         /*  only the hotkey can be a tap) */
    }
    return bad;
}

/* Does the hotkey in use reach every dock by itself? A modifier tap is only
   watched; the middle-button forms are a chord every dock's handler follows
   (nulled events keep their codes). A plain key is taken by the first dock
   that matches it, so that dock passes it on (main.c). */
BOOL in_hotkey_shared(void)
{
    return in_tapneed != 0 || in_mbneed != 0;
}

BOOL in_hotkey_valid(CONST_STRPTR desc)
{
    IX ix;
    BOOL k;
    return tap_parse(desc) || mb_parse(desc, &ix, &k) || ix_parse(desc, &ix);
}


void in_handler(void);                 /* inhandler.s */

volatile WORD in_on;                    /* read by inhandler.s */
static struct Dock *in_dock;
static struct Task *in_task;
static ULONG in_sigmask;

/* The hotkey fired: the dock task does the rest. */
static void hk_fire(void)
{
    in_dock->hotkey = TRUE;
    hk_stat[HK_FIRED]++;
    Signal(in_task, in_sigmask);
}
static struct MsgPort *in_port;
static struct IOStdReq *in_io;
static struct Interrupt in_int;
#ifdef SD_DEBUG
LONG in_calls, in_sigs, in_lastx, in_lasty;
#endif


/* The active window when the button went down: a click activates the dock
   (with its empty menu bar), and dock.c gives the focus back on release. */
struct Window *volatile in_prevwin;
static WORD in_raise;                   /* batches left until the "in front" check */
static BOOL in_near;                    /* pointer over the dock (last batch) */
static struct Layer *in_top_seen;       /* the screen's front layer at the last batch */

/* Ask the dock task to check "in front" (ONTOP) and what lies behind the
   dock (dock_settle): about 0.2 s after a button release, or at once when
   the pointer comes onto the dock. */
static void raise_check(void)
{
    in_dock->raisecheck = TRUE;
    Signal(in_task, in_sigmask);
}

/* Check again a few batches from now (the dock just brought itself forward;
   Intuition does that a moment later). */
void in_settle_again(void) { in_raise = 3; }

/* The held list stays open after the button went up (running.c): clicks on
   it, Del and Esc are taken here (Intuition never sees them, so the list
   window is not activated); pointer moves and clicks elsewhere are reported
   and passed on. Only Signal() here. */
static BOOL in_eatup;                   /* the button-up of a click taken here */
static struct Window *in_active_seen;   /* the active window at the last batch */
static BOOL in_delup;                   /* the key-up of a Del taken here */

/* The dock cell under the pointer (-1: none). */
static WORD pointer_cell(struct Dock *d)
{
    struct Screen *s = d->scr;
    WORD x, y, c, r;
    if (!s || !d->win || s != IntuitionBase->FirstScreen || d->vis != d->full || d->dragging) return -1;
    x = s->MouseX - d->left; y = s->MouseY - d->top;
    if (x < d->pad || y < d->pad || x >= d->w - d->pad || y >= d->h - d->pad) return -1;
    if (dock_in_gap(d, d->horiz ? x : y)) return -1;   /* (a divider: no icon) */
    if (d->horiz) x = dock_along(d, x);
    else y = dock_along(d, y);
    c = (x - d->pad) / d->cw; r = (y - d->pad) / d->ch;
    if (d->horiz ? c >= d->per : r >= d->per) return -1;
    c = d->horiz ? r * d->per + c : c * d->per + r;
    return c < d->ncells ? c : -1;
}
static BOOL in_awaydown;                /* a click elsewhere: close on its button-up (Workbench may be dragging) */
static void list_watch(struct InputEvent *ev)
{
    struct Dock *d = in_dock;
    UBYTE e = 0;
    if (!d->scr) { in_eatup = in_awaydown = FALSE; return; }
    for (; ev; ev = ev->ie_NextEvent) {
        if (ev->ie_Class == IECLASS_RAWKEY) {
            UWORD k = ev->ie_Code & ~IECODE_UP_PREFIX;
            if (k == 0x46 || k == 0x45) {        /* Del, Esc: taken (held down: once, not repeated - */
                if (!(ev->ie_Code & IECODE_UP_PREFIX) && !(ev->ie_Qualifier & IEQUALIFIER_REPEAT))   /* the next */
                    e |= k == 0x46 ? LE_DEL : LE_ESC;   /* row moves under the pointer after each close) */
                ev->ie_Class = IECLASS_NULL;
            }
        } else if (ev->ie_Class == IECLASS_RAWMOUSE || ev->ie_Class == IECLASS_NEWPOINTERPOS) {
            if (ev->ie_Class == IECLASS_NEWPOINTERPOS || ev->ie_X || ev->ie_Y) e |= LE_MOVE;
            if (ev->ie_Class != IECLASS_RAWMOUSE) continue;
            if (ev->ie_Code == IECODE_LBUTTON) {
                WORD x = d->scr->MouseX, y = d->scr->MouseY;
                if (x >= d->lbx && x < d->lbx + d->lbw && y >= d->lby && y < d->lby + d->lbh) {
                    e |= LE_PICK;                /* on the list: taken */
                    ev->ie_Code = IECODE_NOBUTTON;
                    in_eatup = TRUE;
                } else if (!(d->win && x >= d->left && x < d->left + d->w && y >= d->top && y < d->top + d->h))
                    in_awaydown = TRUE;          /* passed on (on the dock: the dock closes the list itself) */
            } else if (ev->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                if (in_eatup) {
                    ev->ie_Code = IECODE_NOBUTTON;
                    in_eatup = FALSE;
                }
                if (in_awaydown) { e |= LE_AWAY; in_awaydown = FALSE; }
            }
        }
    }
    if (e) { d->listev |= e; Signal(in_task, in_sigmask); }
}

void in_watch(struct InputEvent *ev)
{
    if (in_dock && in_task && (in_dock->list && in_dock->liststicky || in_eatup || in_awaydown)) list_watch(ev);
    for (; ev; ev = ev->ie_NextEvent) {
        if (in_tapneed && in_dock && in_task) {  /* a hotkey of modifiers: held, then let go */
            if (ev->ie_Class == IECLASS_RAWKEY) {
                UWORD k = ev->ie_Code & ~IECODE_UP_PREFIX;
                static const UWORD kq[8] = { IEQUALIFIER_LSHIFT, IEQUALIFIER_RSHIFT, 0, IEQUALIFIER_CONTROL,
                                             IEQUALIFIER_LALT, IEQUALIFIER_RALT, IEQUALIFIER_LCOMMAND,
                                             IEQUALIFIER_RCOMMAND };
                if (k < 0x60 || k > 0x67) {
                    if (!(ev->ie_Code & IECODE_UP_PREFIX)) {
                        if (in_taparmed) hk_stat[HK_KEYOFF]++;
                        in_taparmed = FALSE;
                        if (!tap_groups(ev->ie_Qualifier)) in_held = 0;   /* (a release missed: cleared) */
                    }
                } else if (!(ev->ie_Code & IECODE_UP_PREFIX)) {
                    in_held |= tap_groups(kq[k - 0x60]);
                    if (!in_taparmed && (in_held == in_tapneed || tap_groups(ev->ie_Qualifier | kq[k - 0x60]) == in_tapneed)) {
                        in_taparmed = TRUE;
                        hk_stat[HK_ARMED]++;
                    }
                } else {
                    in_held &= ~tap_groups(kq[k - 0x60]);
                    if (in_taparmed && k != 0x62) {   /* (Caps Lock does not count) */
                        in_taparmed = FALSE;
                        in_dock->hotkey = TRUE;
                        hk_stat[HK_FIRED]++;
                        Signal(in_task, in_sigmask);
                    }
                }
            } else if ((ev->ie_Class == IECLASS_RAWMOUSE || (ev->ie_Class == IECLASS_NULL && ev->ie_SubClass == SD_NULLED)) &&
                       !(ev->ie_Code & IECODE_UP_PREFIX) &&
                       (ev->ie_Code == IECODE_LBUTTON || ev->ie_Code == IECODE_RBUTTON || ev->ie_Code == IECODE_MBUTTON)) {
                if (in_taparmed) hk_stat[HK_BTNOFF]++;   /* a button pressed meanwhile: no tap (movement */
                in_taparmed = FALSE;                 /* is fine: its code is NOBUTTON, 0xFF - never */
            }                                        /* mask the up bit off it) */
        }
        if (ev->ie_Class == IECLASS_RAWKEY && !(ev->ie_Code & IECODE_UP_PREFIX) && in_dock)
            in_dock->userin = TRUE;              /* (someone is at the machine: dock_raise) */
        if (ev->ie_Class == IECLASS_RAWKEY && in_lmb && !(ev->ie_Qualifier & IEQUALIFIER_LEFTBUTTON)) {
            in_lmb = FALSE;                      /* a button release missed (the key's qualifier says so): */
            hk_stat[HK_LMBFIX]++;                /* else the hotkey (and auto-hide) would wait for a click */
        }
        if (in_mbneed && in_dock && in_task) {   /* the middle-button chord (another dock may have nulled */
            UWORD code = ev->ie_Code;            /* the event already: its code still says what it was; */
            BOOL ours = ev->ie_Class == IECLASS_NULL && ev->ie_SubClass == SD_NULLED;   /* only a dock's mark counts) */
            BOOL raw = ev->ie_Class == IECLASS_RAWMOUSE || ours;
            BOOL key = ev->ie_Class == IECLASS_RAWKEY || ours;
            if (in_mbheld) {                     /* a release missed: not held any more once the left or right */
                if (ev->ie_Qualifier & IEQUALIFIER_MIDBUTTON) in_mbqualok = TRUE;   /* button is pressed (no */
                if ((ev->ie_Class == IECLASS_RAWMOUSE && (code == IECODE_LBUTTON || code == IECODE_RBUTTON)) ||
                    (in_mbqualok && ev->ie_Class == IECLASS_RAWMOUSE && code == IECODE_NOBUTTON &&   /* chord */
                     !(ev->ie_Qualifier & IEQUALIFIER_MIDBUTTON))) in_mbheld = FALSE;   /* uses them), or by the */
            }                                    /* qualifier where this system is seen to keep it (time and */
                                                 /* movement say nothing: a chord may be held as long as wanted) */
            if (in_mbcode != 0xFFFF && ev->ie_Class == IECLASS_RAWKEY && (code & ~IECODE_UP_PREFIX) == in_mbcode) {
                if (code & IECODE_UP_PREFIX) in_mbcode = 0xFFFF;   /* the chord key's repeats and release: swallowed too */
                ev->ie_Class = IECLASS_NULL; ev->ie_SubClass = SD_NULLED;
                continue;
            }
            if (raw && code == IECODE_MBUTTON) { /* down: held from now; what follows decides */
                in_mbheld = TRUE; in_mbused = FALSE;
                in_mbqual = tap_groups(ev->ie_Qualifier);
                ev->ie_Class = IECLASS_NULL; ev->ie_SubClass = SD_NULLED;
                continue;
            }
            if (raw && code == (IECODE_MBUTTON | IECODE_UP_PREFIX) && in_mbheld) {   /* up: alone = the hotkey */
                in_mbheld = FALSE;
                if (!in_mbused && !in_mbkey && in_mbqual == (in_mbneed & 0x1F) && tap_groups(ev->ie_Qualifier) == in_mbqual)
                    hk_fire();
                ev->ie_Class = IECLASS_NULL; ev->ie_SubClass = SD_NULLED;
                continue;
            }
            if (in_mbheld && key && (code & ~IECODE_UP_PREFIX) < 0x60) {   /* a key while held: this dock's, if it */
                if (!(code & IECODE_UP_PREFIX)) {                         /* names it; every key is kept from the */
                    struct InputEvent e = *ev;                            /* active window meanwhile */
                    e.ie_Class = IECLASS_RAWKEY;
                    e.ie_Qualifier &= ~(IEQUALIFIER_MIDBUTTON | IEQUALIFIER_LEFTBUTTON | IEQUALIFIER_RBUTTON);
                    in_mbused = TRUE;
                    if (ev->ie_Class == IECLASS_RAWKEY) in_mbcode = code;   /* (its repeats and release: above) */
                    if (in_mbkey && !(ev->ie_Qualifier & IEQUALIFIER_REPEAT) && MatchIX(&e, &in_mbix)) hk_fire();
                }
                ev->ie_Class = IECLASS_NULL; ev->ie_SubClass = SD_NULLED;
                continue;
            }
        }
        if (ev->ie_Class == IECLASS_RAWKEY && in_dock && in_task) {   /* Del over a running icon: it closes */
            if (ev->ie_Code == (0x46 | IECODE_UP_PREFIX) && in_delup) {
                in_delup = FALSE;
                ev->ie_Class = IECLASS_NULL;
                continue;
            }
            if (ev->ie_Code == 0x46 && !in_lmb && !in_dock->list &&
                !(ev->ie_Qualifier & (IEQUALIFIER_REPEAT | IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT |
                                      IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
                WORD c = pointer_cell(in_dock);
                if (c >= 0 && c < (WORD)sizeof(in_dock->runcell) && in_dock->runcell[c]) {
                    in_dock->delitem = in_dock->cellitem[c];   /* (which icon: checked again there) */
                    in_dock->delquit = c + 1;    /* the dock task does it */
                    Signal(in_task, in_sigmask);
                    in_delup = TRUE;
                    ev->ie_Class = IECLASS_NULL; /* (else the key goes to the active window as usual) */
                    continue;
                }
            }
        }
        if (ev->ie_Class == IECLASS_RAWKEY && in_ixok && in_dock && in_task) {
            if (ev->ie_Code == (in_hotcode | IECODE_UP_PREFIX)) {   /* its key-up */
                in_hotcode = 0xFFFF;
                ev->ie_Class = IECLASS_NULL;
                continue;
            }
            if (!(ev->ie_Code & IECODE_UP_PREFIX) && MatchIX(ev, &in_ix)) {   /* (MatchIX only compares: */
                                                                                /*  safe here, as commodities' own handler) */
                if (!(ev->ie_Qualifier & IEQUALIFIER_REPEAT)) {      /* held down: once */
                    in_dock->hotkey = TRUE;      /* the dock task does the rest */
                    hk_stat[HK_FIRED]++;
                    Signal(in_task, in_sigmask);
                }
                in_hotcode = ev->ie_Code;
                ev->ie_Class = IECLASS_NULL;     /* nobody else gets the key */
                continue;
            }
        }
        if (ev->ie_Class == IECLASS_RAWKEY && in_dock && in_task) {   /* the other keys */
            LONG k;
            if (ev->ie_Code == (in_kcode | IECODE_UP_PREFIX)) {
                in_kcode = 0xFFFF;
                ev->ie_Class = IECLASS_NULL;
                continue;
            }
            for (k = 0; k < NKEYS; k++)
                if (in_kok[k] && !(ev->ie_Code & IECODE_UP_PREFIX) && MatchIX(ev, &in_kix[k])) break;
            if (k < NKEYS) {
                if (!(ev->ie_Qualifier & IEQUALIFIER_REPEAT)) {   /* held down: once */
                    in_dock->keyact |= 1 << k;   /* the dock task does it (main.c) */
                    Signal(in_task, in_sigmask);
                }
                in_kcode = ev->ie_Code;
                ev->ie_Class = IECLASS_NULL;     /* nobody else gets the key */
                continue;
            }
        }
        if (ev->ie_Class == IECLASS_RAWMOUSE) {
            if (ev->ie_Code == IECODE_LBUTTON) {
                struct Window *aw = IntuitionBase->ActiveWindow;
                in_lmb = TRUE;
                if (aw && in_dock && aw != in_dock->win && aw != in_dock->tip && aw != in_dock->list) in_prevwin = aw;
            } else if (ev->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX) ||
                       ev->ie_Code == (IECODE_RBUTTON | IECODE_UP_PREFIX)) {
                if (ev->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) in_lmb = FALSE;
                if (in_dock) { in_raise = 3; in_dock->userin = TRUE; }   /* "in front" and "behind changed" checks */
            }
        }
    }
    if (in_dock) in_dock->lmb = in_lmb;         /* always current, also with no pointer watch */
    if (in_dock && in_task && in_dock->win && IntuitionBase->ActiveWindow != in_active_seen) {
        struct Window *aw = in_active_seen = IntuitionBase->ActiveWindow;   /* a program may have opened or */
        if (aw != in_dock->win && aw != in_dock->tip && aw != in_dock->list) {   /* closed a window (only compared) */
            in_dock->winchange = TRUE;
            Signal(in_task, in_sigmask);
        }
    }
    if (in_dock && in_dock->o->ontop && in_dock->win && in_dock->scr->LayerInfo.top_layer != in_top_seen) {
        struct Layer *top = in_top_seen = in_dock->scr->LayerInfo.top_layer;   /* a window came to the front */
        struct Window *tip = in_dock->tip, *lst = in_dock->list;   /* (read once: the dock task may clear them) */
        if (top != in_dock->win->WLayer && !(tip && top == tip->WLayer) && !(lst && top == lst->WLayer) &&
            in_raise <= 0) in_raise = 2;         /* (once it's settled) */
    }
    if (in_raise > 0 && --in_raise == 0 && in_dock && in_dock->win) {
        if (in_lmb) in_raise = 1;                /* (the button is down: once it is up) */
        else raise_check();
    }
}

/* Runs in the input.device task: short, no waiting, no calls but Signal()
   (and MatchIX, which only compares - commodities' own handler does the
   same). */
void in_check(struct InputEvent *ev)
{
    struct Dock *d = in_dock;
    struct Screen *s = d->scr;
    WORD x, y, sw, sh, hover = -1;
    BOOL want, lmbwake;

#ifdef SD_DEBUG
    in_calls++;
#endif
    if (!s || !d->win) { in_lmb_seen = in_lmb; return; }
    if (s != IntuitionBase->FirstScreen) want = FALSE;
    else {
        x = s->MouseX - d->left; y = s->MouseY - d->top;
        if ((d->o->hovertext || d->o->noicon) && !d->dragging && d->vis == d->full && x >= d->pad && y >= d->pad &&
            x < d->w - d->pad && y < d->h - d->pad) {        /* icon under the pointer */
            WORD c, r;
            if (d->horiz) x = dock_along(d, x);  /* (without the divider's space) */
            else y = dock_along(d, y);
            c = (x - d->pad) / d->cw; r = (y - d->pad) / d->ch;
            if (d->horiz ? c < d->per : r < d->per) {
                hover = d->horiz ? r * d->per + c : c * d->per + r;
                if (hover >= d->ncells) hover = -1;
            }
        }
        x = s->MouseX; y = s->MouseY; sw = s->Width; sh = s->Height;
        if (d->vis > 0) {               /* shown or sliding: dock box, gap to the edge */
            /* an icon's size beyond the dock on every side: easy to overshoot
               the first or last icon, or to drift off the edge while using it */
            WORD mx = d->cw, my = d->ch;
            WORD x0 = d->left - mx, x1 = d->left + d->w + mx;
            WORD y0 = d->top - my, y1 = d->top + d->h + my;
            switch (d->edge) {
            case 'B': y1 = sh; break;
            case 'T': y0 = 0; break;
            case 'L': x0 = 0; break;
            default:  x1 = sw; break;
            }
            want = x >= x0 && x < x1 && y >= y0 && y < y1;
        }
        else switch (d->edge) {         /* hidden: pointer at the screen edge over the dock */
            case 'B': want = y >= sh - 2 && x >= d->left && x < d->left + d->w; break;
            case 'T': want = y <= 1 && x >= d->left && x < d->left + d->w; break;
            case 'L': want = x <= 1 && y >= d->top && y < d->top + d->h; break;
            default:  want = x >= sw - 2 && y >= d->top && y < d->top + d->h; break;
        }
    }
    if (d->o->ontop) {                  /* the pointer came onto the dock: is it covered? */
        BOOL near = s == IntuitionBase->FirstScreen && want;
        if (near && !in_near && !in_lmb) raise_check();
        in_near = near;
    }
    if (d->pinned) want = TRUE;         /* out by the hotkey: until the hotkey hides it again */
    if (d->parked) {                    /* hidden by the hotkey: until the pointer has left the edge and come back */
        if (!want) d->parkleft = TRUE;
        else if (d->parkleft) d->parked = FALSE;
        if (d->parked) want = FALSE;
    }
    if (d->showing || d->list) want = TRUE;      /* (the settings window, the held list) */
    if (!d->o->autohide) want = TRUE;
    /* The button state is always tracked (auto-hide and labels need it), but a
       click only wakes the dock if it ends a wait: a slide, hide or label
       update held back while the button was down. */
    lmbwake = in_lmb_seen && !in_lmb &&
              ((d->o->autohide && (d->want ? d->vis != d->full : d->vis > 0)) ||
               ((d->o->hovertext || d->o->noicon) && d->hover != d->tipcell));
    in_lmb_seen = in_lmb;
    if (want != d->want || hover != d->hover || lmbwake) {
        d->want = want;
        d->hover = hover;
#ifdef SD_DEBUG
        in_sigs++;
#endif
        Signal(in_task, in_sigmask);
    }
#ifdef SD_DEBUG
    in_lastx = s->MouseX; in_lasty = s->MouseY;
#endif
}

void in_active(BOOL on) { in_on = on; }

/* Intuition applies ChangeWindowBox on its next input event (ten times a
   second when nothing moves); an empty event makes it act at once. */
void in_poke(void)
{
    static struct InputEvent ie;
    if (!in_io) return;
    ie.ie_NextEvent = NULL;
    ie.ie_Class = IECLASS_NULL;
    ie.ie_Code = IECODE_NOBUTTON;                /* (no key or button: the chord code reads nulled events' codes) */
    in_io->io_Command = IND_WRITEEVENT;
    in_io->io_Data = &ie;
    in_io->io_Length = sizeof(ie);
    DoIO((struct IORequest *)in_io);
}

/* Ctrl+\ typed into window w: end-of-file, which ends an idle Shell in a
   console window (CON:, KingCON ...) as its close gadget does - console
   windows don't take a close request through their port. The caller has
   made it active (key 0x0D is \ on the usual keymaps). FALSE: it did not
   become active. */
BOOL in_send_eof(struct Window *w)
{
    static struct InputEvent ie;
    LONG t;
    if (!in_io) return FALSE;
    for (t = 0; t < 10 && IntuitionBase->ActiveWindow != w; t++) Delay(1);
    ie.ie_NextEvent = NULL;
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_SubClass = 0;
    ie.ie_Qualifier = IEQUALIFIER_CONTROL;
    in_io->io_Command = IND_WRITEEVENT;
    in_io->io_Data = &ie;
    in_io->io_Length = sizeof(ie);
    ie.ie_Code = 0x0D;
    if (IntuitionBase->ActiveWindow != w) return FALSE;   /* (checked again: it goes to the active window) */
    DoIO((struct IORequest *)in_io);
    ie.ie_Code = 0x0D | IECODE_UP_PREFIX;
    DoIO((struct IORequest *)in_io);
    return TRUE;
}

/* Returns the signal mask the input handler uses to wake the dock (0: none). */
ULONG in_open(struct Dock *d)
{
    BYTE sig;
    if ((sig = AllocSignal(-1)) < 0) return 0;
    in_dock = d;
    in_task = FindTask(NULL);
    in_sigmask = 1UL << sig;
    if (!(in_port = CreateMsgPort()) ||
        !(in_io = (struct IOStdReq *)CreateIORequest(in_port, sizeof(*in_io))) ||
        OpenDevice("input.device", 0, (struct IORequest *)in_io, 0)) {
        if (in_io) { DeleteIORequest((struct IORequest *)in_io); in_io = NULL; }
        sd_msg(d, "Cannot open input.device: no auto-hide");
        return in_sigmask;
    }
    in_int.is_Node.ln_Type = NT_INTERRUPT;
    in_int.is_Node.ln_Pri = 51;
    in_int.is_Node.ln_Name = SD_NAME;
    in_int.is_Data = NULL;
    in_int.is_Code = (void (*)(void))in_handler;
    in_io->io_Command = IND_ADDHANDLER;
    in_io->io_Data = &in_int;
    DoIO((struct IORequest *)in_io);
    return in_sigmask;
}

void in_close(void)
{
    in_on = FALSE;
    if (in_io) {
        in_io->io_Command = IND_REMHANDLER;
        in_io->io_Data = &in_int;
        DoIO((struct IORequest *)in_io);
        CloseDevice((struct IORequest *)in_io);
        DeleteIORequest((struct IORequest *)in_io);
        in_io = NULL;
    }
    if (in_port) { DeleteMsgPort(in_port); in_port = NULL; }
    if (in_sigmask) {
        LONG b = 0;
        while (!(in_sigmask & (1UL << b))) b++;
        FreeSignal(b);
        in_sigmask = 0;
    }
}
