/* SimpleDock - auto-hide: the dock slides out of / into its screen edge
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   AmigaOS windows cannot leave the screen, so the window shrinks towards its
   edge and shows the matching part of the finished dock image (dock.c
   dock_show); fully out, a 1-pixel strip stays. The slide eases in
   and out and is timed by the clock, not by frames: a slow machine shows
   fewer frames but the same speed. AUTOHIDE=n: n x 70 ms for a full slide,
   1 = instant. The timer runs only while sliding or waiting to hide. */
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <intuition/screens.h>
#include <proto/timer.h>
#include <proto/dos.h>
#include "sd.h"

#define FRAME_US   20000               /* 50 frames a second */
#define HIDE_DELAY 150000              /* pointer gone this long: hide (us) */
#define STEP_MS    70                  /* AUTOHIDE=n: full slide n x 70 ms */

struct Device *TimerBase;

static struct MsgPort *tport;
static struct timerequest *tio;
static BOOL pending;
static enum { T_IDLE, T_DELAY, T_FRAME } tstate;
static struct timeval t0;
static LONG from, to, dur;              /* slide: vis from -> to in dur ms */
#ifdef SD_DEBUG
LONG ah_frames, ah_ms;                  /* last slide: frames shown, time taken */
#endif

static void tstart(ULONG us)
{
    tio->tr_node.io_Command = TR_ADDREQUEST;
    tio->tr_time.tv_secs = us / 1000000;
    tio->tr_time.tv_micro = us % 1000000;
    SendIO((struct IORequest *)tio);
    pending = TRUE;
}

static void tstop(void)
{
    if (pending) {
        AbortIO((struct IORequest *)tio);
        WaitIO((struct IORequest *)tio);
        pending = FALSE;
    }
    tstate = T_IDLE;
}

static LONG elapsed_ms(void)
{
    struct timeval now;
    GetSysTime(&now);
    SubTime(&now, &t0);
    return now.tv_secs * 1000 + now.tv_micro / 1000;
}


/* vis is how far the dock's far side is from the screen edge, the gap
   included. Hidden (vis 0) the window is a 1-pixel strip at the edge rather
   than HideWindow(), which needs intuition V46; Workbench also cancels an icon
   drag when a window appears. (The strip could later take drops on a hidden
   dock, see drag.c.) */
/* The window's box when vis pixels of the dock show. */
static void box_at(struct Dock *d, LONG vis, WORD *l, WORD *t, WORD *w, WORD *h)
{
    WORD v = vis < 1 ? 1 : vis, back = d->full - v;   /* how far it is pulled towards the edge */
    *l = d->left; *t = d->top; *w = d->w; *h = d->h;
    switch (d->edge) {
    case 'B': if (v < *h) *h = v; *t = d->top + back; break;
    case 'T': if (v < *h) *h = v; *t = d->top + d->h - back - *h; break;
    case 'L': if (v < *w) *w = v; *l = d->left + d->w - back - *w; break;
    default:  if (v < *w) *w = v; *l = d->left + back; break;
    }
}

/* What the hidden strip shows: the screen behind it, so it can't be seen.
   Taken with the background snapshot, and again when the dock starts to hide
   if there is a gap to the edge. */
void ah_strip_grab(struct Dock *d, BOOL hiding)
{
    WORD l, t, w, h;
    BOOL under;
    box_at(d, 0, &l, &t, &w, &h);
    under = l >= d->left && t >= d->top && l + w <= d->left + d->w && t + h <= d->top + d->h;
    if (hiding && under) return;                /* under the dock now: keep the one taken with the snapshot */
    if (!d->strip && !(d->strip = AllocBitMap(w, h, GetBitMapAttr(d->scr->RastPort.BitMap, BMA_DEPTH),
                                              BMF_MINPLANES, d->scr->RastPort.BitMap))) return;
    if (under && d->raw) BltBitMap(d->raw, l - d->left, t - d->top, d->strip, 0, 0, w, h, 0xC0, 0xFF, NULL);
    else {
        LockLayerInfo(&d->scr->LayerInfo);
        BltBitMap(d->scr->RastPort.BitMap, l, t, d->strip, 0, 0, w, h, 0xC0, 0xFF, NULL);
        UnlockLayerInfo(&d->scr->LayerInfo);
    }
    WaitBlit();
}

static void apply(struct Dock *d, LONG vis)
{
    WORD l, t, w, h;

    if (!d->win) return;
    if (vis == 0 && d->vis == d->full) ah_strip_grab(d, TRUE);   /* still fully out */
    d->vis = vis;
    box_at(d, vis, &l, &t, &w, &h);
    if (vis == d->full) dz_add(d);              /* drop zones only on the full dock */
    else dz_remove(d);
    ChangeWindowBox(d->win, l, t, w, h);        /* IDCMP_CHANGEWINDOW redraws (dock.c) */
    in_poke();
}

/* The hotkey. With auto-hide it brings the dock out until the pointer has been
   on it and left, or until the hotkey is pressed again; press it before
   dragging an icon to a hidden dock. Without auto-hide it brings the dock to
   the front. */
void ah_hotkey(struct Dock *d)
{
    if (!d->win || d->dragging || d->lmb) return;   /* a window change now: hang (own drag) or end a WB drag */
    if (d->o->autohide <= 0 || !tio) { WindowToFront(d->win); return; }
    if (d->pinned) { d->pinned = FALSE; d->want = FALSE; }
    else { d->pinned = TRUE; d->pinentered = FALSE; d->want = TRUE; }
    ah_pointer(d);
}

/* While the settings window is open the dock stays out, so changes can be
   seen; afterwards it hides again as the pointer says. */
void ah_show(struct Dock *d, BOOL on)
{
    d->showing = on;
    in_poke();                                  /* the input handler looks again */
}

/* The windows behind the dock changed: step aside to the edge strip, let
   them redraw, take a new snapshot, come back (a short blink). */
#ifdef SD_DEBUG
LONG ah_peeks;                          /* debug ARexx PEEKS */
#endif
void ah_peek(struct Dock *d)
{
    if (!d->win || d->vis != d->full) return;
#ifdef SD_DEBUG
    ah_peeks++;
#endif
    tip_hide(d);
    apply(d, 0);
    Delay(6);                                   /* Intuition moves it, the windows behind redraw */
    if (d->lmb) {                               /* a click meanwhile, maybe a Workbench drag: a window */
        d->peekwait = TRUE;                     /* change now would end it - back after the release */
        return;
    }
    ah_peek_end(d);
}

/* The rest of ah_peek: new snapshot, back out (also after a button release). */
void ah_peek_end(struct Dock *d)
{
    d->peekwait = FALSE;
    if (!d->win) return;
    render_resnap(d);
    apply(d, d->full);
    dock_show(d);
    d->behind = dock_behind(d);
}

static void slide(struct Dock *d, LONG target)
{
    LONG n = d->o->autohide;
    tstop();
    if (target == d->vis) return;               /* already there: no window change at all */
    if (d->vis == 0 && target > 0) render_resnap(d);   /* coming out: the screen under it as it is now */
    if (d->vis == d->full && target < d->full) ah_strip_grab(d, TRUE);   /* going in: the edge as it is now */
    from = d->vis; to = target;
    dur = d->full ? n * STEP_MS * (to > from ? to - from : from - to) / d->full : 0;
    if (n <= 1 || dur <= 0) { apply(d, to); return; }
    GetSysTime(&t0);
#ifdef SD_DEBUG
    ah_frames = 0;
#endif
    tstate = T_FRAME;
    tstart(FRAME_US);
}

/* While the left button is held, Workbench may be dragging icons, or we are. A
   window change would cancel Workbench's drag or deadlock our own, so nothing
   slides or hides until the button is released. */
static BOOL hold(struct Dock *d)
{
    return d->lmb || d->dragging;
}

ULONG ah_open(struct Dock *d)
{
    (void)d;
    if (!(tport = CreateMsgPort())) return 0;
    if (!(tio = (struct timerequest *)CreateIORequest(tport, sizeof(*tio))) ||
        OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tio, 0)) {
        if (tio) { DeleteIORequest((struct IORequest *)tio); tio = NULL; }
        DeleteMsgPort(tport); tport = NULL;
        return 0;
    }
    TimerBase = tio->tr_node.io_Device;
    return 1UL << tport->mp_SigBit;
}

/* After dock_open or a settings change. The dock starts shown; the input
   handler reports the pointer, and the dock hides if it is elsewhere. */
void ah_start(struct Dock *d)
{
    BOOL on = d->o->autohide > 0 && tio != NULL;
    if (tio) tstop();
    d->want = TRUE;
    in_active(on || d->o->hovertext || d->o->noicon || d->o->ontop);   /* the pointer watch serves them all */
    crash_set(d->o->crashlog);                   /* crash reports on or off */
    if (!in_set_hotkey(d->o->hotkey)) sd_verbose(d, "HOTKEY=%s is not a valid key description", (LONG)d->o->hotkey);
    if (!on && d->vis != d->full) apply(d, d->full);
}

/* The input handler saw the pointer change between "show" and "hide". */
#ifdef SD_DEBUG
LONG ah_calls;
#endif
void ah_pointer(struct Dock *d)
{
#ifdef SD_DEBUG
    ah_calls++;
#endif
    if (!tio || d->o->autohide <= 0 || !d->win) return;
    if (hold(d)) { tstop(); return; }           /* re-evaluated when the button goes up */
    if (d->want) {
        if (tstate == T_DELAY) tstop();
        if (d->vis != d->full && !(tstate == T_FRAME && to == d->full)) slide(d, d->full);
    } else if (d->vis > 0 && (tstate != T_FRAME || to != 0)) {
        tstop();
        tstate = T_DELAY;
        tstart(HIDE_DELAY);
    }
}

void ah_timer(struct Dock *d)
{
    if (!pending || !CheckIO((struct IORequest *)tio)) return;
    WaitIO((struct IORequest *)tio);
    pending = FALSE;
    if (hold(d)) { tstate = T_IDLE; return; }   /* paused; ah_pointer resumes on release */
    if (tstate == T_DELAY) {
        tstate = T_IDLE;
        if (!d->want) slide(d, 0);
    } else if (tstate == T_FRAME) {
        LONG t = elapsed_ms();
#ifdef SD_DEBUG
        ah_frames++; ah_ms = t;
#endif
        if (t >= dur || t < 0) { tstate = T_IDLE; apply(d, to); }   /* t < 0: the clock was set back */
        else { apply(d, from + (to - from) * SD_EASE(t * 1024 / dur) / 1024); tstart(FRAME_US); }
    }
}

void ah_close(void)
{
    in_active(FALSE);
    if (tio) {
        tstop();
        CloseDevice((struct IORequest *)tio);
        DeleteIORequest((struct IORequest *)tio);
        tio = NULL;
        TimerBase = NULL;
    }
    if (tport) { DeleteMsgPort(tport); tport = NULL; }
}
