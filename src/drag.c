/* SimpleDock - drag and drop
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   Hold an icon and move: the icon (only the icon, drawn on the screen like a
   Workbench icon drag) follows the pointer and the other icons make way.
   Release inside the dock: it takes the free place. Release outside: it is
   removed and the dock shrinks (an empty dock stays as a bar to drop on).
   During the drag the screen's layers are locked, as Workbench does: no
   window changes. Icons dragged from Workbench onto the dock are added where
   they are dropped. Nothing is written to disk until Save in the settings window. */
#include <string.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/wb.h>
#include <proto/layers.h>
#include <proto/icon.h>
#include <workbench/icon.h>
#include "sd.h"

#define DRAG_START 6                   /* pixels of movement before a press becomes a drag */
#define MARGIN     8                   /* drop this close to the dock = inside */

static LONG index_of(struct Dock *d, struct Item *it);

static WORD offx, offy;                 /* pointer position inside the icon */
static struct BitMap *save;             /* screen pixels under the dragged icon */
static struct RastPort srp;             /* the screen bitmap, no layer: draws over everything */
static WORD bx, by, bw, bh;             /* the dragged icon on the screen */
static BOOL drawn;
static LONG gap = -2;                   /* cell kept free for it (-1: none, outside the dock) */
static struct BitMap *base;             /* the dock without the dragged icon */
static struct BitMap *tile;             /* labels-only dock: the dragged label's cell, as a picture */
static BOOL inside;                     /* over the dock: the icon is drawn into the dock image */
static WORD px, py;                     /* last pointer position (window) */
static struct Box last;                 /* where it was drawn into the dock image last */

static WORD absw(WORD v) { return v < 0 ? -v : v; }

/* --- the dragged icon, drawn on the screen like Workbench's icon drags --- */

static void icon_restore(void)
{
    if (drawn) BltBitMap(save, 0, 0, srp.BitMap, bx, by, bw, bh, 0xC0, 0xFF, NULL);
    drawn = FALSE;
}

static void icon_draw(struct Dock *d, WORD x, WORD y)
{
    if (!save) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + bw > d->scr->Width) x = d->scr->Width - bw;
    if (y + bh > d->scr->Height) y = d->scr->Height - bh;
    bx = x; by = y;
    BltBitMap(srp.BitMap, bx, by, save, 0, 0, bw, bh, 0xC0, 0xFF, NULL);
    if (tile) BltBitMap(tile, 0, 0, srp.BitMap, bx, by, bw, bh, 0xC0, 0xFF, NULL);
    else icon_image(d, &srp, d->drag, bx, by, FALSE);
    drawn = TRUE;
}

/* --- the dock while dragging: the other icons make way --- */

/* The cell kept free moves: the other icons glide to make way. */
static void show_gap(struct Dock *d, LONG g)
{
    if (g == gap) return;
    gap = g;
    dock_targets(d, d->drag, g);
    an_start(d, GLIDE_MS);
}

/* The dock without the dragged icon, icons where they are now: box b of it
   (NULL: all). */
static void base_build(struct Dock *d, struct Box *b)
{
    struct RastPort rp;
    struct Box all = { 0, 0, 0, 0 };
    if (!base) return;
    if (!b) { all.x1 = d->w; all.y1 = d->h; b = &all; }
    box_clip(d, b);
    if (b->x1 <= b->x0 || b->y1 <= b->y0) return;
    box_cells(d, b);                             /* whole cells: a blend is never drawn twice */
    BltBitMap(d->bg, b->x0, b->y0, base, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = base;
    render_items_in(d, &rp, b);                  /* skips the dragged icon */
}

/* The dock without the dragged icon on the screen (box b of it, NULL: all). */
static void show_base(struct Dock *d, struct Box *b)
{
    struct Box all = { 0, 0, 0, 0 };
    if (!base) { dock_compose(d); return; }
    if (!b) { all.x1 = d->w; all.y1 = d->h; b = &all; }
    box_clip(d, b);
    if (b->x1 > b->x0 && b->y1 > b->y0) {
        BltBitMap(base, b->x0, b->y0, d->img, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0, 0xC0, 0xFF, NULL);
        WaitBlit();
        dock_show_box(d, b);
    }
    last.x1 = last.x0;                           /* the dragged icon is not in the image now */
}

/* Over the dock: draw the dragged icon onto a copy of the dock off-screen,
   then blit only what changed (box b, and its old and new place). Nothing is
   erased on screen, so there's no flicker. */
static void show_frame(struct Dock *d, struct Box *b)
{
    struct RastPort rp;
    struct Box dirty = *b;
    WORD x = px - offx, y = py - offy;
    if (!base) { dock_compose(d); return; }
    if (bw > d->w || bh > d->h) return;          /* would not fit the dock image */
    if (x > d->w - bw) x = d->w - bw;
    if (y > d->h - bh) y = d->h - bh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (last.x1 > last.x0) box_add(&dirty, last.x0, last.y0, last.x1 - last.x0, last.y1 - last.y0);
    box_add(&dirty, x, y, bw, bh);
    box_clip(d, &dirty);
    BltBitMap(base, dirty.x0, dirty.y0, d->img, dirty.x0, dirty.y0, dirty.x1 - dirty.x0, dirty.y1 - dirty.y0,
              0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = d->img;
    if (d->o->noicon) {                          /* labels only: the label itself */
        SetFont(&rp, d->font);
        SetDrMd(&rp, JAM1);
        render_item(d, &rp, d->drag, x, y, TRUE);
    } else icon_dock(d, &rp, d->drag, x, y, FALSE);
    WaitBlit();
    dock_show_box(d, &dirty);
    last.x0 = x; last.y0 = y; last.x1 = x + bw; last.y1 = y + bh;
}

/* A glide frame during the drag: box b of the dock changed under the dragged icon. */
void drag_redraw(struct Dock *d, struct Box *b)
{
    PROF_START(t0);
    base_build(d, b);
    if (inside) show_frame(d, b);
    else if (b->x1 > b->x0) {
        icon_restore();
        show_base(d, b);
        icon_draw(d, bx, by);
    }
    PROF_STOP(2, t0);
}

/* The cell under window coordinates, for a drop or a gap (-1: outside the dock). */
static LONG cell_at(struct Dock *d, WORD x, WORD y)
{
    LONG c;
    if (x < -MARGIN || y < -MARGIN || x >= d->w + MARGIN || y >= d->h + MARGIN) return -1;
    if (x < d->pad) x = d->pad;
    if (y < d->pad) y = d->pad;
    if (x >= d->w - d->pad) x = d->w - d->pad - 1;
    if (y >= d->h - d->pad) y = d->h - d->pad - 1;
    c = dock_cell(d, x, y);
    return c < 0 ? d->ncells - 1 : c;            /* past the last icon: the end */
}

static void finish(struct Dock *d)
{
    icon_restore();
    WaitBlit();
    if (save) { FreeBitMap(save); save = NULL; }
    if (base) { FreeBitMap(base); base = NULL; }
    if (tile) { FreeBitMap(tile); tile = NULL; }
    inside = FALSE;
    UnlockLayers(&d->scr->LayerInfo);
    Forbid();
    d->win->Flags &= ~WFLG_RMBTRAP;
    Permit();
    d->dragging = FALSE;
    gap = -2;
}

void drag_cancel(struct Dock *d, BOOL glide)
{
    if (!d->dragging) return;
    finish(d);
    d->drag = NULL;
    if (d->win) dock_compose(d);                 /* the dragged icon may still be drawn in the image */
    if (d->win && glide) { dock_targets(d, NULL, -1); an_start(d, GLIDE_MS); }
}

static void start(struct Dock *d)
{
    struct Item *it = d->pressed;
    WORD ix = it->x + (it->w - it->iw) / 2, iy = it->y + ICON_DY(d, it);

    tip_hide(d);                                 /* window changes before the lock */
    d->drag = it;
    d->pressed = NULL;
    d->dragging = TRUE;
    if (d->o->noicon) { ix = it->x; iy = it->y; }   /* labels only: the whole cell is dragged */
    offx = d->downx - ix;
    offy = d->downy - iy;
    bw = d->o->noicon ? it->w : it->iw > 0 ? it->iw : 1;
    bh = d->o->noicon ? it->h : it->ih > 0 ? it->ih : 1;
    save = it->dobj || d->o->noicon ? AllocBitMap(bw, bh, GetBitMapAttr(d->scr->RastPort.BitMap, BMA_DEPTH),
                                                  BMF_MINPLANES, d->scr->RastPort.BitMap) : NULL;
    if (d->o->noicon && save &&                  /* off the dock: the label on its own little tile */
        (tile = AllocBitMap(bw, bh, GetBitMapAttr(d->bg, BMA_DEPTH), BMF_MINPLANES, d->bg))) {
        struct RastPort trp;
        InitRastPort(&trp);
        trp.BitMap = tile;
        BltBitMap(d->bg, it->x, it->y, tile, 0, 0, bw, bh, 0xC0, 0xFF, NULL);
        SetFont(&trp, d->font);
        SetDrMd(&trp, JAM1);
        render_item(d, &trp, it, 0, 0, TRUE);
        WaitBlit();
    }
    base = AllocBitMap(d->w, d->h, GetBitMapAttr(d->bg, BMA_DEPTH), BMF_MINPLANES, d->bg);
    inside = TRUE;                               /* it starts over the dock */
    px = d->downx; py = d->downy;
    last.x0 = last.x1 = 0;                       /* its whole cell (with the label) is redrawn first */
    item_box(d, it, &last);
    InitRastPort(&srp);
    srp.BitMap = d->scr->RastPort.BitMap;
    /* From here until finish() there must be no window changes (they need
       these locks), and the right button must not open the menu bar (it would
       wait for them). */
    Forbid();
    d->win->Flags |= WFLG_RMBTRAP;
    Permit();
    LockLayers(&d->scr->LayerInfo);
    base_build(d, NULL);
    gap = -2;
    show_gap(d, index_of(d, it));                /* its own cell stays free */
}

static void move(struct Dock *d, WORD mx, WORD my)
{
    LONG g = cell_at(d, mx, my);
    PROF_START(t0);
    px = mx; py = my;
    show_gap(d, g);                              /* may start a glide (drag_redraw) */
    if (g >= 0) {                                /* over the dock: drawn into its image */
        struct Box none = { 0, 0, 0, 0 };
        if (!inside) { icon_restore(); inside = TRUE; }
        show_frame(d, &none);
    } else {                                     /* elsewhere: on the screen */
        if (inside) {
            inside = FALSE;
            show_base(d, NULL);
        }
        icon_restore();
        icon_draw(d, d->win->LeftEdge + mx - offx, d->win->TopEdge + my - offy);
    }
    PROF_STOP(6, t0);
}

static LONG index_of(struct Dock *d, struct Item *it)
{
    struct Item *i;
    LONG n = 0;
    FOR_ITEMS(d, i) {
        if (i == it) return n;
        n++;
    }
    return -1;
}

static void drop(struct Dock *d, WORD x, WORD y)
{
    struct Item *it = d->drag, *before = NULL, *i;
    LONG cell = cell_at(d, x, y), k = 0;
    BOOL was_inside = inside;                    /* finish() clears it */

    finish(d);
    d->drag = NULL;
    /* the dragged icon starts its glide where it was drawn last */
    {
        WORD ix = was_inside ? px - offx : bx - d->win->LeftEdge, iy = was_inside ? py - offy : by - d->win->TopEdge;
        it->x = d->o->noicon ? ix : ix - (it->w - it->iw) / 2;   /* (labels only: ix, iy is the cell) */
        it->y = d->o->noicon ? iy : iy - ICON_DY(d, it);
        if (it->x > d->w - it->w) it->x = d->w - it->w;   /* the glide starts inside the dock */
        if (it->y > d->h - it->h) it->y = d->h - it->h;
        if (it->x < 0) it->x = 0;
        if (it->y < 0) it->y = 0;
    }
    dz_suspend();                                /* the list changes: not for the zone hook now */
    if (cell >= 0) {                             /* in the dock: to the free cell */
        dock_compose(d);                         /* the image without the drag: it is drawn at x, y */
        Remove((struct Node *)it);
        FOR_ITEMS(d, i)
            if (k++ == cell) { before = i; break; }
        if (before) Insert((struct List *)&d->items, (struct Node *)it, (struct Node *)before->node.mln_Pred);
        else AddTail((struct List *)&d->items, (struct Node *)it);
        dock_targets(d, NULL, -1);
        dz_resume(d);
        an_start(d, GLIDE_MS);
    } else {
        config_remove(d, it);
        dock_reopen_glide(d);                    /* smaller dock, the rest close up */
    }
    config_store(d);
    ah_pointer(d);                               /* the pointer may have left meanwhile */
}

/* Mouse events of the dock window during a press; TRUE = handled here. */
BOOL drag_event(struct Dock *d, struct IntuiMessage *im)
{
    if (im->Class == IDCMP_MOUSEMOVE) {
        if (!d->dragging) {
            if (!d->pressed || absw(im->MouseX - d->downx) + absw(im->MouseY - d->downy) < DRAG_START)
                return FALSE;
            start(d);
        }
        move(d, im->MouseX, im->MouseY);
        return TRUE;
    }
    if (im->Class == IDCMP_MOUSEBUTTONS && d->dragging) {
        if (im->Code == SELECTUP) drop(d, im->MouseX, im->MouseY);
        return TRUE;
    }
    return FALSE;
}

static BOOL wbarg_path(struct WBArg *wa, char *buf, LONG len)
{
    if (!NameFromLock(wa->wa_Lock, buf, len)) return FALSE;
    return !wa->wa_Name || !*wa->wa_Name || AddPart(buf, wa->wa_Name, len);
}

/* Icons dropped from Workbench; TRUE = the dock must be rebuilt (after the
   message is replied). */
BOOL drag_drop_wb(struct Dock *d, struct AppMessage *am)
{
    WORD x = am->am_MouseX, y = am->am_MouseY;
    struct Item *on, *before = NULL;
    char path[SD_LINEMAX];
    LONG i;

    /* Drops on the hidden dock's edge strip are ignored for now: Workbench
       locks the screen during an icon drag, so the dock can't slide out to
       show where the icon would land. If Workbench ever allows it, the drop
       could be placed by its position along the strip. */
#ifdef SD_STRIP_DROP
    if (d->vis != d->full) {
        if (d->horiz) y = d->pad + d->ch / 2;
        else x = d->pad + d->cw / 2;
    }
#else
    if (d->vis != d->full) return FALSE;
#endif
    if (am->am_UserData != d->gen) return FALSE; /* queued for a window that is gone: its zones differ */
    if (am->am_NumArgs <= 0) { dz_reset(d); dock_show(d); return FALSE; }
    /* onto the middle of an icon: open them with it */
    if (am->am_Type == AMTYPE_APPWINDOWZONE && dz_icon(am->am_ID) >= 0) {
        struct Item *t = dock_item(d, dz_icon(am->am_ID));
        if (t) launch_with(d, t, am);
        dz_reset(d);
        dock_show(d);                             /* the pressed look drawn by the zone hook */
        return FALSE;
    }
    if (d->ncells + am->am_NumArgs > d->fit) {
        DisplayBeep(d->scr);                    /* full: as long as the screen (or LIMIT) allows */
        dz_reset(d);
        dock_show(d);                           /* what the zone hook drew */
        return FALSE;
    }
    on = dock_item(d, dock_cell(d, x, y));
    if (dz_gap(am->am_ID) >= 0) {
        before = dock_item(d, dz_gap(am->am_ID));  /* the free place shown while hovering */
    } else if (on) {                            /* between icons: before or after this one */
        BOOL after = d->horiz ? x >= on->x + on->w / 2 : y >= on->y + on->h / 2;
        before = after ? dock_item(d, index_of(d, on) + 1) : on;
    } else if (dock_cell(d, d->pad, d->pad) >= 0 && (d->horiz ? x < d->pad : y < d->pad)) {
        before = dock_item(d, 0);               /* in front of the first icon */
    }
    dz_suspend();                                /* the list changes: not for the zone hook now */
    for (i = 0; i < am->am_NumArgs; i++) {
        struct Item *it;
        LONG n;
        if (!wbarg_path(&am->am_ArgList[i], path, sizeof(path)) || !(it = config_add(d, path, before))) continue;
        if (dz_gap(am->am_ID) >= 0 && before && before->keep) {
            it->sx = before->sx - (d->horiz ? d->cw : 0);   /* the free place it was dropped on */
            it->sy = before->sy - (d->horiz ? 0 : d->ch);
        } else {
            it->sx = d->win->LeftEdge + am->am_MouseX - d->cw / 2;   /* from the drop point */
            it->sy = d->win->TopEdge + am->am_MouseY - d->ch / 2;
        }
        it->keep = TRUE;
        if ((n = strlen(path)) > 1 && path[n - 1] == ':') {     /* a disk: its name as the label */
            path[n - 1] = 0;
            it->label = sd_strdup(path);
        }
    }
    return TRUE;
}
