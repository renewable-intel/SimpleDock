/* SimpleDock - make way while Workbench icons are dragged over the dock
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   During an icon drag Workbench locks every window, so the dock's own task
   cannot draw. Workbench (V44+) does call an AppWindow drop zone hook from
   inside its drag, which may draw with graphics.library. Each icon has an
   "onto" zone in its middle (shown pressed; a drop opens the files with it)
   and "between" zones over its outer fifths, where the icons make way
   (squeezed a little: the window cannot grow during the drag). A full dock
   opens no gap and is shown greyed. New icons always go in front of the
   running others. The hook plays the glide itself (GLIDE_MS, so Workbench's
   drag pauses that long), draws into a bitmap of its own and records where
   the icons were drawn (sx, sy, keep) for the glide after the drop. */
#include <string.h>
#include <utility/hooks.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include <proto/wb.h>
#include <proto/timer.h>
#include <intuition/intuitionbase.h>
#include "sd.h"

#define GAP_ID   1000                  /* zone id: GAP_ID + 2 * place (between) / 2 * icon + 1 (onto) */
#define MAXZONES 256

extern struct IntuitionBase *IntuitionBase;
extern struct Device *TimerBase;

static struct Hook hook;
static struct AppWindowDropZone *zone[MAXZONES];
static LONG nzones;
static struct BitMap *hbm;              /* the hook's drawing buffer */
static WORD *pos;                       /* where the hook drew each icon: x, y pairs */
static LONG shown = -1;                 /* the gap shown now (-1: none) */
static LONG shownhi = -1;               /* the icon shown pressed (a drop opens with it), -1: none */
static BOOL ghosted;                    /* the full dock shown greyed (icons over a gap) */
static struct SignalSemaphore lock;     /* dz_remove vs the hook (Workbench's task) */
static BOOL valid;                      /* zones, hbm, pos usable (under lock) */

/* Held by the dock task while it draws or changes the icon list. The hook
   shares the icon caches and scratch buffer, so it only tries the lock and
   skips a frame if it is busy. The lock nests. */
void dz_lock(void)
{
    if (!lock.ss_Link.ln_Type) InitSemaphore(&lock);
    ObtainSemaphore(&lock);
}

void dz_unlock(void) { ReleaseSemaphore(&lock); }

/* The item list is about to change: the hook must not walk it until the
   zones are made again (dz_add) or dz_resume. */
void dz_suspend(void)
{
    dz_lock();
    valid = FALSE;
    dz_unlock();
}

static void place(struct Dock *d, LONG k, LONG gap, WORD *x, WORD *y);

/* Same number of icons, new order (a rearrange): the zones still fit. */
void dz_resume(struct Dock *d)
{
    LONG k;
    dz_lock();
    if (hbm && pos && nzones) {
        for (k = 0; k < d->ncells; k++) place(d, k, -1, &pos[2 * k], &pos[2 * k + 1]);
        shown = shownhi = -1;
        ghosted = FALSE;
        valid = TRUE;
    }
    dz_unlock();
}

LONG dz_gap(ULONG id)
{
    LONG v = (LONG)id - GAP_ID;
    return v >= 0 && v < 4000 && !(v & 1) ? v / 2 : -1;          /* the place this zone opens */
}

LONG dz_icon(ULONG id)
{
    LONG v = (LONG)id - GAP_ID;
    return v >= 0 && v < 4000 && (v & 1) ? v / 2 : -1;           /* the icon this zone is on */
}


/* After a drop that changed nothing (opened with an icon, or refused), the
   gap or pressed icon the hook drew is replaced by the dock image again. */
void dz_reset(struct Dock *d)
{
    struct Item *it;
    LONG k = 0;
    dz_lock();
    shown = shownhi = -1;
    ghosted = FALSE;
    if (pos) FOR_ITEMS(d, it) { place(d, k, -1, &pos[2 * k], &pos[2 * k + 1]); k++; }
    FOR_ITEMS(d, it) it->keep = FALSE;
    dz_unlock();
}

/* Where icon k is drawn with place 'gap' kept free (-1: none). */
static void place(struct Dock *d, LONG k, LONG gap, WORD *x, WORD *y)
{
    LONG n = d->ncells, lines = (n + d->per - 1) / d->per, per = d->per, s = k, b;
    WORD along, across, size = d->horiz ? d->cw : d->ch;
    WORD len = (d->horiz ? d->w : d->h) - 2 * d->pad - NGAPS(d) * DIVGAP;

    if (gap >= 0 && k >= gap) s = k + 1;
    if (gap >= 0 && n + 1 > lines * per) {        /* the window cannot grow now: one more place, squeezed */
        per++;
        size = len / per;
    }
    b = gap >= 0 ? 1 : 0;                         /* the running others: behind the dividers' space */
    along = d->pad + (s % per) * size - ((d->horiz ? d->cw : d->ch) - size) / 2 +
            (d->gapc[0] >= 0 && s >= d->gapc[0] + b ? DIVGAP : 0) + (d->gapc[1] >= 0 && s >= d->gapc[1] + b ? DIVGAP : 0);
    across = d->pad + (s / per) * (d->horiz ? d->ch : d->cw);
    *x = d->horiz ? along : across;
    *y = d->horiz ? across : along;
}

/* Where divider i is drawn with place 'gap' kept free: the same cell size
   as place() (squeezed when the window cannot grow); -1: none. */
static WORD div_at(struct Dock *d, LONG gap, LONG i)
{
    LONG n = d->ncells, lines = (n + d->per - 1) / d->per, per = d->per;
    WORD size = d->horiz ? d->cw : d->ch;
    WORD len = (d->horiz ? d->w : d->h) - 2 * d->pad - NGAPS(d) * DIVGAP;
    if (d->gapc[i] < 0) return -1;
    if (gap >= 0 && n + 1 > lines * per) { per++; size = len / per; }
    return d->pad + (d->gapc[i] + (gap >= 0 ? 1 : 0)) * size + i * DIVGAP + DIVGAP / 2;
}

/* One frame: icon k between pos[] (p = 0) and its place for 'gap' (p = 1024). */
static void frame(struct Dock *d, struct RastPort *wrp, LONG gap, LONG p, BOOL last, LONG hi)
{
    struct RastPort rp;
    struct Item *it;
    LONG k = 0;
    PROF_START(t0);

    BltBitMap(d->bg, 0, 0, hbm, 0, 0, d->w, d->h, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = hbm;
    SetFont(&rp, d->font);
    SetDrMd(&rp, JAM1);
    FOR_ITEMS(d, it) {
        WORD x, y;
        place(d, k, gap, &x, &y);
        x = pos[2 * k] + (x - pos[2 * k]) * p / 1024;
        y = pos[2 * k + 1] + (y - pos[2 * k + 1]) * p / 1024;
        render_item(d, &rp, it, x, y, k == hi);   /* hi: shown pressed */
        if (last) {
            pos[2 * k] = x; pos[2 * k + 1] = y;
            it->sx = d->left + x; it->sy = d->top + y;   /* the glide after a drop starts here */
            it->keep = gap >= 0;
        }
        k++;
    }
    if (SECTION(d)) {                             /* the divider: after a new icon's place */
        struct Box all;
        all.x0 = all.y0 = 0; all.x1 = d->w; all.y1 = d->h;
        render_divider_at(d, &rp, &all, div_at(d, gap, 0));
        render_divider_at(d, &rp, &all, div_at(d, gap, 1));
    }
    WaitBlit();
    BltBitMapRastPort(hbm, 0, 0, wrp, 0, 0, d->w, d->h, 0xC0);
    PROF_STOP(4, t0);
}

/* Full dock with icons over a gap: grey it with a 50% dither in the dock
   colour, like a disabled gadget, instead of opening a gap. */
static void ghost(struct Dock *d, struct RastPort *wrp)
{
    static UWORD pat[2] = { 0x5555, 0xAAAA };
    struct RastPort rp = *wrp;                   /* (the pattern only on this copy) */
    LONG pen = d->fillpen >= 0 ? d->fillpen : d->shadowpen;
    if (pen < 0) return;
    SetAfPt(&rp, pat, 1);
    SetDrMd(&rp, JAM1);
    SetAPen(&rp, pen);
    RectFill(&rp, d->pad, d->pad, d->w - d->pad - 1, d->h - d->pad - 1);
}

/* Glide the icons to their places with a free place at 'gap'; icon 'hi'
   shown pressed (-1: none). */
static void glide(struct Dock *d, struct RastPort *wrp, LONG gap, LONG hi)
{
    struct timeval t0, now;
    LONG t;
    BOOL grey = DOCKFULL(d) && gap >= 0;
    if (DOCKFULL(d)) gap = -1;                    /* no room for one more: nothing opens */
    if (!hbm || !pos || (gap == shown && hi == shownhi && grey == ghosted)) return;
    if (grey != ghosted) {                        /* greyed or back: at once */
        ghosted = grey;
        shown = gap; shownhi = hi;
        frame(d, wrp, gap, 1024, TRUE, hi);
        if (grey) ghost(d, wrp);
        return;
    }
    shownhi = hi;
    if (gap == shown || !TimerBase) { shown = gap; frame(d, wrp, gap, 1024, TRUE, hi); return; }
    shown = gap;
    GetSysTime(&t0);
    for (;;) {
        GetSysTime(&now);
        SubTime(&now, &t0);
        t = now.tv_secs * 1000 + now.tv_micro / 1000;
        if (t >= GLIDE_MS || t < 0) break;          /* also if the clock was set back */
        frame(d, wrp, gap, SD_EASE(t * 1024 / GLIDE_MS), FALSE, hi);
    }
    frame(d, wrp, gap, 1024, TRUE, hi);
}

/* Called by Workbench, inside its drag: graphics.library drawing only. */
static ULONG zone_hook(__reg("a0") struct Hook *h, __reg("a2") APTR reserved,
                       __reg("a1") struct AppWindowDropZoneMsg *m)
{
    struct Dock *d = (struct Dock *)h->h_Data;
    (void)reserved;
    if (!AttemptSemaphore(&lock)) return 0;      /* the dock is changing: no drawing now */
    if (valid) {
        if (m->adzm_Action == ADZMACTION_Enter) {
            LONG icon = dz_icon(m->adzm_ID);
            if (icon >= 0) glide(d, m->adzm_RastPort, -1, icon);  /* onto this icon */
            else glide(d, m->adzm_RastPort, dz_gap(m->adzm_ID), -1);
        } else if (m->adzm_Action == ADZMACTION_Leave) {
            struct Screen *s = d->scr;            /* still over the dock: the next zone follows */
            WORD x = s->MouseX - d->left, y = s->MouseY - d->top;
            if (x < 0 || y < 0 || x >= d->w || y >= d->h) glide(d, m->adzm_RastPort, -1, -1);
        }
    }
    ReleaseSemaphore(&lock);
    return 0;
}

static void add_zone(struct Dock *d, ULONG id, WORD a0, WORD a1, WORD b0, WORD b1)
{
    struct IBox box;
    WORD alen = d->horiz ? d->w : d->h;
    struct AppWindowDropZone *z;
    if (a0 < 0) a0 = 0;
    if (a1 > alen) a1 = alen;
    if (nzones >= MAXZONES || a1 <= a0 || b1 <= b0) return;
    box.Left = d->horiz ? a0 : b0;  box.Width = d->horiz ? a1 - a0 : b1 - b0;
    box.Top = d->horiz ? b0 : a0;   box.Height = d->horiz ? b1 - b0 : a1 - a0;
    if ((z = AddAppWindowDropZone(d->appwin, id, d->gen, WBDZA_Box, &box, WBDZA_Hook, &hook, TAG_END)))
        zone[nzones++] = z;
}

/* Zones for the dock as laid out now (fully shown): between (outer fifths
   of each icon) and onto (the middle); none for the "more" cell. */
void dz_add(struct Dock *d)
{
    LONG n = d->ncells, k, lines;
    WORD size = d->horiz ? d->cw : d->ch, csize = d->horiz ? d->ch : d->cw;
    WORD alen = d->horiz ? d->w : d->h, blen = d->horiz ? d->h : d->w;
    struct Item *it;

    if (!lock.ss_Link.ln_Type) InitSemaphore(&lock);
    if (!d->appwin || nzones) return;
    if (!(hbm = AllocBitMap(d->w, d->h, GetBitMapAttr(d->bg, BMA_DEPTH), BMF_MINPLANES, d->bg))) return;
    if (!(pos = AllocVec((n + 1) * 2 * sizeof(WORD), MEMF_ANY))) { FreeBitMap(hbm); hbm = NULL; return; }
    for (k = 0; k < n; k++) place(d, k, -1, &pos[2 * k], &pos[2 * k + 1]);
    shown = shownhi = -1;
    hook.h_Entry = (HOOKFUNC)zone_hook;
    hook.h_Data = d;
    lines = (n + d->per - 1) / d->per;
    if (n == 0) add_zone(d, GAP_ID, 0, alen, 0, blen);   /* empty dock: place 0 anywhere */
    k = 0;
    FOR_ITEMS(d, it) {                           /* between (place k) | onto icon k | between (k + 1) */
        LONG col = k % d->per, row = k / d->per;
        WORD start = d->pad + col * size + (d->gapc[0] >= 0 && k >= d->gapc[0] ? DIVGAP : 0) +
                     (d->gapc[1] >= 0 && k >= d->gapc[1] ? DIVGAP : 0), band = size / 5;
        WORD b0 = row == 0 ? 0 : d->pad + row * csize, b1 = row == lines - 1 ? blen : d->pad + (row + 1) * csize;
        if (k <= d->nkept)                       /* (new icons go in front of the running others) */
            add_zone(d, GAP_ID + 2 * k, col == 0 ? 0 : start - band - (k == d->ndiv && k && SECTION(d) ? DIVGAP : 0),
                     start + band, b0, b1);
        if (!it->temp) add_zone(d, GAP_ID + 2 * k + 1, start + band, start + size - band, b0, b1);
        else if (it->temp == RUN_TEMP)           /* onto a running other: opened with it */
            add_zone(d, GAP_ID + 2 * k + 1, k == d->nkept ? start + band : start, k == n - 1 ? alen : start + size, b0, b1);
        else if (it->temp == RUN_WB)             /* onto the Workbench icon: a new icon after the kept ones */
            add_zone(d, GAP_ID + 2 * k, start + band, k == n - 1 ? alen : start + size, b0, b1);
        if (!it->temp && (k == n - 1 || col == d->per - 1))   /* after the last of a row: between it and the next */
            add_zone(d, GAP_ID + 2 * (k + 1), start + size - band, alen, b0, b1);
        k++;
    }
    if (!nzones) { dz_remove(d); return; }
    ObtainSemaphore(&lock);
    valid = TRUE;
    ReleaseSemaphore(&lock);
}

void dz_remove(struct Dock *d)
{
    if (lock.ss_Link.ln_Type) { ObtainSemaphore(&lock); valid = FALSE; ReleaseSemaphore(&lock); }
    while (nzones > 0) {
        nzones--;
        if (d->appwin) RemoveAppWindowDropZone(d->appwin, zone[nzones]);
    }
    if (hbm) { WaitBlit(); FreeBitMap(hbm); hbm = NULL; }
    if (pos) { FreeVec(pos); pos = NULL; }
    shown = -1;
}
