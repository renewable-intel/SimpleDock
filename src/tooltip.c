/* SimpleDock - macOS-style name label over the icon under the pointer
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   With Text "On hover" (HOVERTEXT) the names are not drawn below the
   icons; the icon under the pointer gets a small rounded label (in the
   dock's colour) just outside the dock, like the macOS dock. The input
   handler reports the icon under the pointer (input.c), so nothing runs
   while the pointer is still. */
#include <string.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/layers.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include "sd.h"

#define PADX 8
#define PADY 3
#define POINT 4                        /* the point towards the icon, pixels */
#define GAP  2                         /* between the point and the dock */

void tip_hide(struct Dock *d)
{
    struct Item *it;
    if (d->tip) { CloseWindow(d->tip); d->tip = NULL; }
    else if (d->o && d->o->noicon && d->tipcell >= 0 && d->win && d->img && !d->dragging &&
             (it = dock_item(d, d->tipcell)) && it != d->pressed)
        dock_show_item(d, it, FALSE);            /* labels only: the highlight off */
    d->tipcell = -1;
}

static void tip_show(struct Dock *d, struct Item *it, LONG cell)
{
    CONST_STRPTR s = it->label ? it->label : FilePart(it->path);
    LONG n = strlen(s), opacity = !d->o->transparent ? 100 : d->opacity + 25 > 90 ? 90 : d->opacity + 25;
    struct RastPort rp;
    struct BitMap *bm;
    WORD tw, th, ww, wh, x, y, cx, cy, tx = PADX, ty = PADY, rad, pos, sw = d->scr->Width, sh = d->scr->Height;

    InitRastPort(&rp);
    SetFont(&rp, d->font);
    tw = TextLength(&rp, (STRPTR)s, n) + 2 * PADX + 1;
    if (tw > sw - POINT) {                       /* a very long name: cut to the screen */
        struct TextExtent te;
        n = TextFit(&rp, (STRPTR)s, n, &te, NULL, 1, sw - POINT - 2 * PADX - 1, 32767);
        tw = sw - POINT;
    }
    th = d->font->tf_YSize + 2 * PADY + 1;
    rad = th / 2 > 6 ? 6 : th / 2;
    ww = tw; wh = th;                            /* the window: the label and its point */
    if (d->horiz) wh += POINT; else ww += POINT;
    cx = d->left + it->x + it->w / 2;
    cy = d->top + it->y + it->h / 2;
    x = cx - ww / 2; y = cy - wh / 2;
    switch (d->edge) {
    case 'B': y = d->top - wh - GAP; break;
    case 'T': y = d->top + d->h + GAP; ty += POINT; break;
    case 'L': x = d->left + d->w + GAP; tx += POINT; break;
    default:  x = d->left - ww - GAP; break;
    }
    if (x + ww > sw) x = sw - ww;
    if (y + wh > sh) y = sh - wh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (d->horiz) {                              /* the point at the icon, clear of the corners */
        pos = cx - x;
        if (pos > tw - rad - POINT - 1) pos = tw - rad - POINT - 1;
        if (pos < rad + POINT + 1) pos = rad + POINT + 1;
    } else pos = th / 2;                         /* beside the dock: in the label's middle */

    render_arrow(d->edge == 'B' ? 'B' : d->edge == 'T' ? 'T' : d->edge == 'L' ? 'L' : 'R', pos, POINT);
    bm = render_glass(d, NULL, x, y, ww, wh, rad, opacity);
    render_arrow(0, 0, 0);
    if (!bm) return;
    rp.BitMap = bm;
    SetDrMd(&rp, JAM1);
    if (d->textpen != d->shadowpen) {
        SetAPen(&rp, d->shadowpen);
        Move(&rp, tx + 1, ty + rp.TxBaseline + 1);
        Text(&rp, (STRPTR)s, n);
    }
    SetAPen(&rp, d->textpen);
    Move(&rp, tx, ty + rp.TxBaseline);
    Text(&rp, (STRPTR)s, n);
    /* No backfill: the window keeps the screen's pixels until the label is
       copied in, instead of flashing as an empty grey box first. */
    d->tip = OpenWindowTags(NULL, WA_PubScreen, d->scr, WA_Left, x, WA_Top, y,
                            WA_Width, ww, WA_Height, wh, WA_Borderless, TRUE, WA_Activate, FALSE,
                            WA_SmartRefresh, TRUE, WA_NoCareRefresh, TRUE,
                            WA_BackFill, LAYERS_NOBACKFILL, TAG_END);
    if (d->tip) BltBitMapRastPort(bm, 0, 0, d->tip->RPort, 0, 0, ww, wh, 0xC0);
    WaitBlit();
    FreeBitMap(bm);
    if (d->tip) d->tipcell = cell;               /* not opened: tried again next time */
}

/* The input handler reported a new icon under the pointer (or none): show its
   label bubble, or on a labels-only dock highlight its label. */
void tip_update(struct Dock *d)
{
    LONG cell = (d->o->hovertext || d->o->noicon) && !d->dragging && d->win && d->vis == d->full ? d->hover : -1;
    struct Item *it;
    if (cell == d->tipcell || d->lmb) return;   /* button held: a window change ends a Workbench drag */
    tip_hide(d);
    if (cell < 0 || !(it = dock_item(d, cell))) return;
    if (!d->o->noicon) tip_show(d, it, cell);
    else if (d->img && it != d->pressed) { dock_show_item(d, it, TRUE); d->tipcell = cell; }
}
