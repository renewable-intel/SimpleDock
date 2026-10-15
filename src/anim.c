/* SimpleDock - icons glide to new places (make way, settle, close up)
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   Every icon has where it is drawn (x, y), where it goes (tx, ty) and where
   the glide started (ox, oy). A glide takes GLIDE_MS with ease-in/ease-out,
   timed by the clock (a slow machine shows fewer frames, not a slower
   glide). Each frame redraws the part of the dock image where icons moved;
   only the dock's own window is drawn into, no window is changed. The timer
   runs only while gliding. */
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include "sd.h"

#define FRAME_US 20000

extern struct Device *TimerBase;        /* autohide.c */

static struct MsgPort *aport;
static struct timerequest *aio;
static BOOL pending, running;
static struct timeval t0;
static LONG dur;
#ifdef SD_DEBUG
LONG an_frames, an_ms, an_glides;
#endif

/* Only the part where icons moved is drawn again (old and new places). */
static void frame(struct Dock *d, LONG p)          /* p: 0..1024 */
{
    struct Item *it;
    struct Box b = { 0, 0, 0, 0 };
    FOR_ITEMS(d, it) {
        WORD x = it->ox + (it->tx - it->ox) * p / 1024, y = it->oy + (it->ty - it->oy) * p / 1024;
        if (x == it->x && y == it->y) continue;
        if (!(d->dragging && it == d->drag)) item_box(d, it, &b);   /* the dragged icon: drag.c */
        it->x = x; it->y = y;
        if (!(d->dragging && it == d->drag)) item_box(d, it, &b);
    }
    if (d->dragging) drag_redraw(d, &b);
    else if (b.x1 > b.x0) dock_compose_box(d, &b);
}

ULONG an_open(void)
{
    if (!(aport = CreateMsgPort())) return 0;
    if (!(aio = (struct timerequest *)CreateIORequest(aport, sizeof(*aio))) ||
        OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)aio, 0)) {
        if (aio) { DeleteIORequest((struct IORequest *)aio); aio = NULL; }
        DeleteMsgPort(aport); aport = NULL;
        return 0;
    }
    return 1UL << aport->mp_SigBit;
}

static void tstart(void)
{
    aio->tr_node.io_Command = TR_ADDREQUEST;
    aio->tr_time.tv_secs = 0;
    aio->tr_time.tv_micro = FRAME_US;
    SendIO((struct IORequest *)aio);
    pending = TRUE;
}

/* Stop: icons jump to their targets (not drawn). */
void an_stop(struct Dock *d)
{
    struct Item *it;
    if (pending) { AbortIO((struct IORequest *)aio); WaitIO((struct IORequest *)aio); pending = FALSE; }
    running = FALSE;
    FOR_ITEMS(d, it) {
        it->x = it->tx; it->y = it->ty;
    }
}

/* Glide from where the icons are drawn now (x, y) to their targets (tx, ty). */
void an_start(struct Dock *d, LONG ms)
{
    struct Item *it;
    BOOL moves = FALSE;
    if (pending) { AbortIO((struct IORequest *)aio); WaitIO((struct IORequest *)aio); pending = FALSE; }
    FOR_ITEMS(d, it) {
        it->ox = it->x; it->oy = it->y;
        if (it->x != it->tx || it->y != it->ty) moves = TRUE;
    }
    if (!aio || ms <= 0 || !moves || !TimerBase) { frame(d, 1024); running = FALSE; return; }
    dur = ms;
    GetSysTime(&t0);
#ifdef SD_DEBUG
    an_frames = 1; an_ms = 0; an_glides++;
#endif
    running = TRUE;
    frame(d, 0);
    tstart();
}

void an_timer(struct Dock *d)
{
    struct timeval now;
    LONG t;
    if (!pending || !CheckIO((struct IORequest *)aio)) return;
    WaitIO((struct IORequest *)aio);
    pending = FALSE;
    if (!running) return;
    GetSysTime(&now);
    SubTime(&now, &t0);
    t = now.tv_secs * 1000 + now.tv_micro / 1000;
#ifdef SD_DEBUG
    an_frames++; an_ms = t;
#endif
    if (t >= dur || t < 0) { running = FALSE; frame(d, 1024); }   /* t < 0: the clock was set back */
    else { frame(d, SD_EASE(t * 1024 / dur)); tstart(); }
}

BOOL an_running(void) { return running; }

void an_close(void)
{
    if (aio) {
        if (pending) { AbortIO((struct IORequest *)aio); WaitIO((struct IORequest *)aio); }
        CloseDevice((struct IORequest *)aio);
        DeleteIORequest((struct IORequest *)aio);
        aio = NULL;
    }
    if (aport) { DeleteMsgPort(aport); aport = NULL; }
    pending = running = FALSE;
}
