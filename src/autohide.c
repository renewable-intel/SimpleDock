/* SimpleDock - auto-hide: the dock slides out of / into its screen edge
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   AmigaOS windows cannot leave the screen, so the window shrinks towards its
   edge and shows the matching part of the finished dock image (dock.c
   dock_show); fully in, a 1-pixel strip stays. The slide eases in
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
static struct MsgPort *pport;           /* short waits while the dock steps aside (ah_peek) */
static struct timerequest *pio;
static void short_wait(ULONG us);
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


/* The window's box when vis pixels of the dock show (vis: how far its far
   side is from the screen edge, the gap included). Hidden (vis 0) it is a
   1-pixel strip at the edge rather than HideWindow(), which needs intuition
   V46; Workbench also cancels an icon drag when a window appears. */
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
    if (d->strip && (GetBitMapAttr(d->strip, BMA_WIDTH) < w || GetBitMapAttr(d->strip, BMA_HEIGHT) < h)) {
        WaitBlit();                              /* (the dock grew: a bigger one) */
        FreeBitMap(d->strip);
        d->strip = NULL;
    }
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

/* The dock was laid out again while hidden: the strip window follows. */
void ah_hidden_moved(struct Dock *d)
{
    WORD l, t, w, h;
    if (!d->win || d->vis != 0) return;
    box_at(d, 0, &l, &t, &w, &h);
    ChangeWindowBox(d->win, l, t, w, h);
    in_poke();
}

/* The hotkey, a toggle on what is seen. With auto-hide: the dock fully out
   and not covered is hidden, and kept hidden until the pointer has left the
   edge and come back (parked - else a pointer near the edge would bring it
   straight back); otherwise it comes out and in front, and stays out until
   the hotkey hides it (pinned) - so it can be used, dragged to, and left.
   Without auto-hide it brings the dock to the front. */
void ah_hotkey(struct Dock *d)
{
    hk_stat[HK_HANDLED]++;
    if (d->dragging || d->lmb) { hk_stat[HK_BUSY]++; return; }   /* a window change now: hang (own drag) or end a WB drag */
    if (!d->win) {                               /* closed (ARexx HIDE, or a reopen that failed): open it */
        hk_stat[HK_REOPEN]++;
        if (d->hidden && dock_open(d)) ah_start(d);
        return;
    }
    if (d->o->autohide <= 0 || !tio) { hk_stat[HK_FRONT]++; WindowToFront(d->win); return; }
    if (d->vis == d->full && !d->parked && !dock_covered(d)) {   /* seen: hidden */
        hk_stat[HK_UNPIN]++;
        d->pinned = FALSE;
        d->parkleft = FALSE;                     /* (before parked: the input handler reads them in that order) */
        d->parked = TRUE;
        d->want = FALSE;
    } else {                                     /* hidden, sliding, covered: out and in front */
        hk_stat[HK_PIN]++;
        d->parked = FALSE;
        d->pinned = TRUE;
        d->want = TRUE;
        WindowToFront(d->win);
        if (d->tip) WindowToFront(d->tip);
    }
    ah_pointer(d);
}

/* The auto-hide key: auto-hide on (at the speed it had) or off, as if set in
   the settings window - kept in the settings file. */
void ah_key(struct Dock *d)
{
    char line[32];
    LONG v[1];
    if (d->dragging || d->lmb) return;           /* (as the hotkey) */
    v[0] = d->o->autohide > 0 ? d->o->autohide : d->o->ahspeed > 0 ? d->o->ahspeed : 3;
    SD_FMT(line, d->o->autohide > 0 ? "(AUTOHIDE=%ld)" : "AUTOHIDE=%ld", v);
    if (!config_set(d, line)) { sd_beep(d); return; }
    if (!d->hidden) dock_reopen(d);              /* (shown: with auto-hide on it slides away as usual) */
    else ah_start(d);
    config_store(d);
    am_prefs_sync(d);                            /* an open settings window shows it */
}

/* While the settings window is open the dock stays out, so changes can be
   seen; afterwards it hides again as the pointer says. */
void ah_show(struct Dock *d, BOOL on)
{
    d->showing = on;
    in_poke();                                  /* the input handler looks again */
}

#ifdef SD_DEBUG
LONG ah_peeks;                          /* debug ARexx PEEKS */
#endif
/* Does a window under the dock still have to redraw (damage not repaired)? */
static BOOL behind_busy(struct Dock *d)
{
    struct Layer *l;
    BOOL busy = FALSE;
    WORD x0 = d->left, y0 = d->top, x1 = d->left + d->w - 1, y1 = d->top + d->h - 1;
    LockLayerInfo(&d->scr->LayerInfo);
    for (l = d->scr->LayerInfo.top_layer; l && !busy; l = l->back) {
        if (l == d->win->WLayer || (d->tip && l == d->tip->WLayer)) continue;
        busy = (l->Flags & LAYERREFRESH) && l->bounds.MinX <= x1 && l->bounds.MaxX >= x0 &&
               l->bounds.MinY <= y1 && l->bounds.MaxY >= y0;
    }
    UnlockLayerInfo(&d->scr->LayerInfo);
    return busy;
}

/* The windows behind the dock changed: step aside to the edge strip, wait
   until they have redrawn (usually a few ms), take a new snapshot, come back
   (a short blink: AmigaOS 3.x cannot show what is under a window). */
void ah_peek(struct Dock *d)
{
    LONG t;
    if (!d->win || d->vis != d->full) return;
#ifdef SD_DEBUG
    ah_peeks++;
#endif
    tip_hide(d);
    apply(d, 0);
    for (t = 0; t < 60 && behind_busy(d); t++)  /* the windows behind redraw: back as soon as */
        short_wait(2000);                       /* they are done (checked every 2 ms, at most 0.12 s) */
    if (d->lmb) {                               /* a click meanwhile, maybe a Workbench drag: a window */
        d->peekwait = TRUE;                     /* change now would end it - back after the release */
        return;
    }
    ah_peek_end(d);
}

/* The rest of ah_peek: new snapshot, back out (also after a button release,
   unless a slide began meanwhile: slide() drops the wait). */
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
    d->peekwait = FALSE;                        /* (a peek left waiting: the slide takes its own snapshot) */
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
    if ((pport = CreateMsgPort()) &&
        (pio = (struct timerequest *)CreateIORequest(pport, sizeof(*pio)))) *pio = *tio;   /* same device, own port */
    if (pio) pio->tr_node.io_Message.mn_ReplyPort = pport;
    return 1UL << tport->mp_SigBit;
}

/* Wait us microseconds (a tick if there is no timer of our own). */
static void short_wait(ULONG us)
{
    if (!pio) { Delay(1); return; }
    pio->tr_node.io_Command = TR_ADDREQUEST;
    pio->tr_time.tv_secs = 0;
    pio->tr_time.tv_micro = us;
    DoIO((struct IORequest *)pio);
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
    {                                            /* the other keys */
        static const char *const kn[NKEYS] = { "MINKEY", "MINALLKEY", "AHKEY" };
        CONST_STRPTR kv[NKEYS];
        ULONG bad;
        LONG k;
        kv[0] = d->o->minkey; kv[1] = d->o->minallkey; kv[2] = d->o->ahkey;
        bad = in_set_keys(kv);
        for (k = 0; k < NKEYS; k++)
            if (bad & 1UL << k) sd_verbose(d, "%s=%s is not a valid key description", (LONG)kn[k], (LONG)kv[k]);
    }
    if (!on && d->vis != d->full) apply(d, d->full);
}

/* Show or hide as d->want now says (input handler, hotkey, after a drag). */
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
    if (pio) { DeleteIORequest((struct IORequest *)pio); pio = NULL; }   /* (tio's device, not opened again) */
    if (pport) { DeleteMsgPort(pport); pport = NULL; }
    if (tport) { DeleteMsgPort(tport); tport = NULL; }
}
