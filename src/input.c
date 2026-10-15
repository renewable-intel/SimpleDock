/* SimpleDock - the input handler: pointer and left button
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   input.device calls inhandler.s for every batch of input events (at least
   ten times a second, also when nothing moves). in_watch() notes the left
   button and the window it activates; while auto-hide, hover labels or "in
   front" are on, in_check() compares the pointer with the dock. The dock
   task is signalled only when something it waits for changes - no polling,
   no timer while idle. */
#include <exec/interrupts.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/commodities.h>
#include "sd.h"

extern struct IntuitionBase *IntuitionBase;
static BOOL in_lmb;                     /* left button, from the button events */
static BOOL in_lmb_seen;                /* ... as in_check last saw it */
static IX in_ix;                        /* the hotkey (commodities description, ParseIX) */
static volatile BOOL in_ixok;
static UWORD in_hotcode = 0xFFFF;       /* its key while down: the key-up is swallowed too */

/* The hotkey from its description ("ctrl alt d"); matched in in_watch. */
BOOL in_set_hotkey(CONST_STRPTR desc)
{
    extern struct Library *CxBase;
    in_ixok = FALSE;
    if (!CxBase || !desc || !*desc) return FALSE;
    in_ix.ix_Version = IX_VERSION;
    if (ParseIX((STRPTR)desc, &in_ix)) return FALSE;
    in_ixok = TRUE;
    return TRUE;
}

BOOL in_hotkey_valid(CONST_STRPTR desc)
{
    extern struct Library *CxBase;
    IX ix;
    if (!CxBase || !desc || !*desc) return FALSE;
    ix.ix_Version = IX_VERSION;
    return ParseIX((STRPTR)desc, &ix) == 0;
}


void in_handler(void);                 /* inhandler.s */

volatile WORD in_on;                    /* read by inhandler.s */
static struct Dock *in_dock;
static struct Task *in_task;
static ULONG in_sigmask;
static struct MsgPort *in_port;
static struct IOStdReq *in_io;
static struct Interrupt in_int;
#ifdef SD_DEBUG
LONG in_calls, in_sigs, in_lastx, in_lasty;
#endif

#define MARGIN 6                        /* stay shown this close to the dock */

/* Called for every batch of input events, before Intuition sees them. Notes
   the left button and which window was active when it went down: a click
   activates the dock (with its empty menu bar), and dock.c gives the focus
   back on release. */
struct Window *volatile in_prevwin;
static WORD in_raise;                   /* batches left until the "in front" check */
static BOOL in_near;                    /* pointer over the dock (last batch) */

/* After a button release a window may have moved, opened, closed or come
   forward. About 0.2 s later the dock task re-checks "in front" (ONTOP) and
   what lies behind the dock. */
static void raise_check(void)
{
    in_dock->raisecheck = TRUE;
    Signal(in_task, in_sigmask);
}

/* Check again a few batches from now (the dock just brought itself forward;
   Intuition does that a moment later). */
void in_settle_again(void) { in_raise = 3; }

void in_watch(struct InputEvent *ev)
{
    for (; ev; ev = ev->ie_NextEvent) {
        if (ev->ie_Class == IECLASS_RAWKEY && in_ixok && in_dock && in_task) {
            if (ev->ie_Code == (in_hotcode | IECODE_UP_PREFIX)) {   /* its key-up */
                in_hotcode = 0xFFFF;
                ev->ie_Class = IECLASS_NULL;
                continue;
            }
            if (!(ev->ie_Code & IECODE_UP_PREFIX) && MatchIX(ev, &in_ix)) {
                if (!(ev->ie_Qualifier & IEQUALIFIER_REPEAT)) {      /* held down: once */
                    in_dock->hotkey = TRUE;      /* the dock task does the rest */
                    Signal(in_task, in_sigmask);
                }
                in_hotcode = ev->ie_Code;
                ev->ie_Class = IECLASS_NULL;     /* nobody else gets the key */
                continue;
            }
        }
        if (ev->ie_Class == IECLASS_RAWMOUSE) {
            if (ev->ie_Code == IECODE_LBUTTON) {
                struct Window *aw = IntuitionBase->ActiveWindow;
                in_lmb = TRUE;
                if (aw && in_dock && aw != in_dock->win && aw != in_dock->tip) in_prevwin = aw;
            } else if (ev->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX) ||
                       ev->ie_Code == (IECODE_RBUTTON | IECODE_UP_PREFIX)) {
                if (ev->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) in_lmb = FALSE;
                if (in_dock) in_raise = 3;                /* "in front" and "behind changed" checks */
            }
        }
    }
    if (in_dock) in_dock->lmb = in_lmb;         /* always current, also with no pointer watch */
    if (in_raise > 0 && --in_raise == 0 && !in_lmb && in_dock && in_dock->win) raise_check();
}

/* Runs in the input.device task: short, no waiting, no calls but Signal(). */
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
            WORD c = (x - d->pad) / d->cw, r = (y - d->pad) / d->ch;
            if (d->horiz ? c < d->per : r < d->per) {
                hover = d->horiz ? r * d->per + c : c * d->per + r;
                if (hover >= d->ncells) hover = -1;
            }
        }
        x = s->MouseX; y = s->MouseY; sw = s->Width; sh = s->Height;
        if (d->vis > 0) {               /* shown or sliding: dock box, gap to the edge */
            WORD x0 = d->left - MARGIN, x1 = d->left + d->w + MARGIN;
            WORD y0 = d->top - MARGIN, y1 = d->top + d->h + MARGIN;
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
    if (d->pinned) {                    /* out by the hotkey: until the pointer was on it and left */
        if (want) d->pinentered = TRUE;
        else if (d->pinentered) d->pinned = FALSE;
        if (d->pinned) want = TRUE;
    }
    if (d->showing) want = TRUE;
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

/* Intuition applies window changes (ChangeWindowBox, Hide/ShowWindow) on its
   next input event, which is only ten times a second when nothing moves. An
   empty event makes it act at once, so every slide frame shows. Used only
   while sliding. */
void in_poke(void)
{
    static struct InputEvent ie;
    if (!in_io) return;
    ie.ie_NextEvent = NULL;
    ie.ie_Class = IECLASS_NULL;
    in_io->io_Command = IND_WRITEEVENT;
    in_io->io_Data = &ie;
    in_io->io_Length = sizeof(ie);
    DoIO((struct IORequest *)in_io);
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
