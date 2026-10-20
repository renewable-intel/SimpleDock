/* SimpleDock - icon images, drawn once (and smaller: SCALEICONW / SCALEICONH)
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   icon.library is slow to draw an icon (it decodes and colour-maps it every
   time), and the dock redraws all icons in every animation frame. So each
   icon is drawn once, at full size, over black and over white: where both
   agree the icon is opaque, the difference is its transparency (also the
   soft edges of PNG icons). From that, once, at the dock's icon size (bigger
   icons are shrunk to SCALEICONW/H, smaller ones are not enlarged):
     - more than 8 bits: colour + transparency (box filter when shrinking),
       blended onto what is there when drawn;
     - up to 8 bits: pens, with a cut-out mask for the blitter;
     - solid dock: also the icon already blended onto the dock colour - a
       plain rectangle, one blit per frame (icon_dock). */
#include <string.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <workbench/icon.h>
#include "sd.h"
#include "p96.h"

extern struct Library *P96Base;

static void draw_full(struct RastPort *rp, struct Item *it, BOOL selected)
{
    DrawIconState(rp, it->dobj, NULL, 0, 0, selected ? IDS_SELECTED : IDS_NORMAL,
                  ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE,
                  ICONDRAWA_EraseBackground, FALSE, TAG_END);
}

/* The icon drawn at full size over 'pen' into a new bitmap (NULL: no memory). */
static struct BitMap *render_on(struct Dock *d, struct Item *it, WORD w, WORD h, LONG depth, LONG pen)
{
    struct RastPort rp;
    struct BitMap *bm = AllocBitMap(w, h, depth, BMF_MINPLANES | BMF_CLEAR,
                                    depth > 8 ? d->scr->RastPort.BitMap : NULL);
    if (!bm) return NULL;
    InitRastPort(&rp);
    rp.BitMap = bm;
    SetRast(&rp, pen);
    draw_full(&rp, it, FALSE);
    WaitBlit();
    return bm;
}

static UBYTE pen_at(struct BitMap *bm, LONG depth, LONG x, LONG y)
{
    LONG off = y * bm->BytesPerRow + (x >> 3), p;
    UBYTE pen = 0, bit = 0x80 >> (x & 7);
    for (p = 0; p < depth; p++) if (bm->Planes[p][off] & bit) pen |= 1 << p;
    return pen;
}

/* More than 8 bits: premultiplied colour + alpha, shrunk with a box filter. */
static BOOL scale_true(struct Dock *d, struct Item *it, WORD w, WORD h, WORD sw, WORD sh, LONG depth)
{
    struct BitMap *bk = render_on(d, it, w + 1, h, depth, d->shadowpen);   /* over black (a spare column) */
    struct BitMap *wh = render_on(d, it, w + 1, h, depth, d->textpen);     /* over white */
    UBYTE *a = AllocVec(w * h * 3 * 2, MEMF_ANY), *b, *out = AllocVec(sw * sh * 4, MEMF_ANY);
    struct RastPort rp;
    LONG x, y, tx, ty, full = 255;
    BOOL ok = bk && wh && a && out;

    if (!ok) scale_why |= (bk ? 0 : 8) | (wh ? 0 : 16) | (a && out ? 0 : 32);

    if (ok) {
        b = a + w * h * 3;
        InitRastPort(&rp);
        rp.BitMap = bk; pix_read(&rp, 0, 0, w, h, a);
        rp.BitMap = wh; pix_read(&rp, 0, 0, w, h, b);
        {                                        /* black to white as this screen shows them (16 bits: */
            UBYTE pk[3], pw[3];                  /* 248..): the spare column, background only */
            rp.BitMap = bk; pix_read(&rp, w, 0, 1, 1, pk);
            rp.BitMap = wh; pix_read(&rp, w, 0, 1, 1, pw);
            x = ((LONG)pw[0] - pk[0] + (LONG)pw[1] - pk[1] + (LONG)pw[2] - pk[2]) / 3;
            if (x > 128) full = x;               /* (else the pens are too alike: as before) */
        }
        for (ty = 0; ty < sh; ty++)
            for (tx = 0; tx < sw; tx++) {
                LONG x0 = tx * w / sw, x1 = (tx + 1) * w / sw, y0 = ty * h / sh, y1 = (ty + 1) * h / sh;
                ULONG sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                UBYTE *o = out + (ty * sw + tx) * 4;
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                for (y = y0; y < y1; y++)
                    for (x = x0; x < x1; x++) {
                        UBYTE *pa = a + (y * w + x) * 3, *pb = b + (y * w + x) * 3;
                        LONG diff = ((LONG)pb[0] - pa[0] + (LONG)pb[1] - pa[1] + (LONG)pb[2] - pa[2]) * 255 / (3 * full);
                        sa += diff < 0 ? 255 : diff > 255 ? 0 : 255 - diff;
                        sr += pa[0]; sg += pa[1]; sb += pa[2];   /* over black = premultiplied */
                        n++;
                    }
                o[0] = sa / n; o[1] = sr / n; o[2] = sg / n; o[3] = sb / n;
            }
        it->argb = out;
        out = NULL;
    }
    FreeVec(a);
    FreeVec(out);
    WaitBlit();
    if (bk) FreeBitMap(bk);
    if (wh) FreeBitMap(wh);
    return ok;
}

/* Up to 8 bits: pens picked, plus a mask (opaque where both renders agree). */
static BOOL scale_clut(struct Dock *d, struct Item *it, WORD w, WORD h, WORD sw, WORD sh, LONG depth)
{
    LONG p0 = 0, p1 = 1, x, y, p;
    struct BitMap *b0, *b1, *sbm = NULL;
    PLANEPTR mask = NULL;
    BOOL ok;

    if (d->textpen != d->shadowpen) { p0 = d->shadowpen; p1 = d->textpen; }
    b0 = render_on(d, it, w, h, depth, p0);
    b1 = render_on(d, it, w, h, depth, p1);
    ok = b0 && b1 && (sbm = AllocBitMap(sw, sh, depth, BMF_CLEAR, NULL)) &&
         (mask = AllocVec(sbm->BytesPerRow * sh, MEMF_CHIP | MEMF_CLEAR));
    WaitBlit();                                  /* BMF_CLEAR may clear with the blitter */
    if (ok) {
        for (y = 0; y < sh; y++)
            for (x = 0; x < sw; x++) {
                LONG fx = (2 * x + 1) * w / (2 * sw), fy = (2 * y + 1) * h / (2 * sh);   /* source centre */
                UBYTE c0 = pen_at(b0, depth, fx, fy), c1 = pen_at(b1, depth, fx, fy);
                LONG off = y * sbm->BytesPerRow + (x >> 3);
                UBYTE bit = 0x80 >> (x & 7);
                if (c0 != c1) continue;                  /* the background showed: transparent */
                mask[off] |= bit;
                for (p = 0; p < depth; p++) if (c0 & (1 << p)) sbm->Planes[p][off] |= bit;
            }
        it->sbm = sbm;
        it->smask = mask;
    } else {
        if (sbm) FreeBitMap(sbm);
        FreeVec(mask);
    }
    WaitBlit();
    if (b0) FreeBitMap(b0);
    if (b1) FreeBitMap(b1);
    return ok;
}

/* Up to 8 bits, own size: the render over black is the image; the mask is
   where it equals the render over white - word compares, plane by plane. */
static BOOL clut_own(struct Dock *d, struct Item *it, WORD w, WORD h, LONG depth)
{
    LONG p0 = 0, p1 = 1, p, i, n;
    struct BitMap *b0, *b1;
    PLANEPTR mask = NULL;

    if (d->textpen != d->shadowpen) { p0 = d->shadowpen; p1 = d->textpen; }
    b0 = render_on(d, it, w, h, depth, p0);
    b1 = render_on(d, it, w, h, depth, p1);
    if (b0 && b1 && b0->BytesPerRow == b1->BytesPerRow &&
        (mask = AllocVec(b0->BytesPerRow * h, MEMF_CHIP | MEMF_CLEAR))) {
        UWORD *m = (UWORD *)mask;                            /* rows are whole words */
        n = b0->BytesPerRow * h / 2;
        for (p = 0; p < depth; p++) {
            const UWORD *a = (const UWORD *)b0->Planes[p], *b = (const UWORD *)b1->Planes[p];
            for (i = 0; i < n; i++) m[i] |= a[i] ^ b[i];          /* differs: background showed */
        }
        for (i = 0; i < n; i++) m[i] = ~m[i];
        it->sbm = b0;
        it->smask = mask;
        b0 = NULL;
    }
    WaitBlit();
    if (b0) FreeBitMap(b0);
    if (b1) FreeBitMap(b1);
    return mask != NULL;
}

/* Why icons were not shrunk since dock_open cleared it (VERBOSE tells): 1 no
   black and white pens, 2 no Picasso96, 8/16 no bitmap over black/white, 32
   no memory. */
LONG scale_why;

/* The icon's images, made once (iw, ih: its size, then the size shown). */
void scale_icon(struct Dock *d, struct Item *it)
{
    LONG lw = d->o->scalew > 0 ? d->o->scalew : d->o->scaleh, lh = d->o->scaleh > 0 ? d->o->scaleh : lw;
    LONG depth = GetBitMapAttr(d->scr->RastPort.BitMap, BMA_DEPTH);
    WORD w = it->iw, h = it->ih, sw = w, sh = h;

    if (!it->dobj || w <= 0 || h <= 0) return;
    if (lw > 0 && (w > lw || h > lh)) {                  /* shrink, keeping the proportions */
        if (w * lh >= h * lw) { sw = lw; sh = h * lw / w; }
        else { sh = lh; sw = w * lh / h; }
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
    }
    if (d->textpen >= 0 && d->shadowpen >= 0 && d->textpen != d->shadowpen) {   /* needs black and white */
        BOOL ok;
        if (depth <= 8)                          /* solid, own size: the rectangle below is enough */
            ok = sw != w || sh != h ? scale_clut(d, it, w, h, sw, sh, depth) :
                 d->o->transparent || d->fillpen < 0 ? clut_own(d, it, w, h, depth) : FALSE;
        else ok = P96Base && (sw != w || sh != h || d->o->transparent) &&   /* solid, own size: cbm only */
                  scale_true(d, it, w, h, sw, sh, depth);
        if (ok) { it->iw = sw; it->ih = sh; }
        else if (sw != w || sh != h) scale_why |= depth > 8 && !P96Base ? 2 : depth <= 8 ? 32 : 0;
    } else if (sw != w || sh != h) scale_why |= 1;
    if (!d->o->transparent && d->fillpen >= 0) {         /* solid: blended onto the dock colour */
        struct RastPort rp;
        if ((it->cbm = AllocBitMap(it->iw, it->ih, depth, BMF_MINPLANES, d->scr->RastPort.BitMap))) {
            InitRastPort(&rp);
            rp.BitMap = it->cbm;
            SetRast(&rp, d->fillpen);
            icon_image(d, &rp, it, 0, 0, FALSE);
            WaitBlit();
        }
    }
}

/* Is the box inside the solid part of the dock (inside the rounded border)?
   The shape is convex: its four corners are enough. */
BOOL in_solid(struct Dock *d, WORD x, WORD y, WORD w, WORD h)
{
    LONG r = d->radius, i;
    for (i = 0; i < 4; i++) {
        LONG px = i & 1 ? x + w - 1 : x, py = i & 2 ? y + h - 1 : y, cx, cy;
        if (px < 1 || py < 1 || px > d->w - 2 || py > d->h - 2) return FALSE;   /* the border */
        cx = px < r ? r : px > d->w - 1 - r ? d->w - 1 - r : px;
        cy = py < r ? r : py > d->h - 1 - r ? d->h - 1 - r : py;
        if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > (r - 1) * (r - 1)) return FALSE;
    }
    return TRUE;
}

/* The icon at x, y in a dock-sized bitmap: the ready-made rectangle where the
   dock is solid, else the icon image. */
void icon_dock(struct Dock *d, struct RastPort *rp, struct Item *it, WORD x, WORD y, BOOL selected)
{
    if (it->cbm && !selected && in_solid(d, x, y, it->iw, it->ih))
        BltBitMapRastPort(it->cbm, 0, 0, rp, x, y, it->iw, it->ih, 0xC0);
    else icon_image(d, rp, it, x, y, selected);
}

void scale_free(struct Item *it)
{
    FreeVec(it->argb); it->argb = NULL;
    if (it->sbm) { WaitBlit(); FreeBitMap(it->sbm); it->sbm = NULL; }
    FreeVec(it->smask); it->smask = NULL;
    if (it->cbm) { WaitBlit(); FreeBitMap(it->cbm); it->cbm = NULL; }
    if (it->lbm) { WaitBlit(); FreeBitMap(it->lbm); it->lbm = NULL; }
}

/* Draw the icon image at x, y: the shrunk copy if there is one, else icon.library. */
void icon_image(struct Dock *d, struct RastPort *rp, struct Item *it, WORD x, WORD y, BOOL selected)
{
    if (it->argb && P96Base) {                   /* blend onto what is there (pixel.s) */
        LONG n = it->iw * it->ih;
        UBYTE *dst = render_rowbuf(n * 3);
        if (!dst) return;
        pix_read(rp, x, y, it->iw, it->ih, dst);
        SD_BLEND(dst, it->argb, n, selected ? 160 : 256);   /* pressed: darker */
        pix_write(rp, x, y, it->iw, it->ih, dst);
    } else if (it->sbm) {
        BltMaskBitMapRastPort(it->sbm, 0, 0, rp, x, y, it->iw, it->ih, 0xE0, it->smask);
    } else if (it->dobj) {
        DrawIconState(rp, it->dobj, NULL, x, y, selected ? IDS_SELECTED : IDS_NORMAL,
                      ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE,
                      ICONDRAWA_EraseBackground, FALSE, TAG_END);
    }
}
