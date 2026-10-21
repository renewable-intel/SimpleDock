/* SimpleDock - rounded background and icon drawing
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   AmigaOS 3.x windows are opaque rectangles, so the background starts as a
   snapshot of the screen under the dock: inside a rounded rectangle it is
   filled with the colour (solid) or tinted (transparent), with a thin lighter
   border; outside the corners it shows unchanged. The snapshot is taken when
   the dock opens, with auto-hide each time it comes out, and after the
   windows behind it changed (dock_settle). */
#include <string.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <intuition/screens.h>
#include <workbench/icon.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include "sd.h"
#include "p96.h"

/* Coverage (0..256) of pixel x of a row whose inside span is [l, r) in 1/16 px. */
static LONG cover(LONG x, LONG l, LONG r)
{
    LONG a = x * 16, b = a + 16;
    if (a < l) a = l;
    if (b > r) b = r;
    return b > a ? (b - a) * 16 : 0;
}

static ULONG isqrt(ULONG v)
{
    ULONG r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* Inside span of row y of a w x h rounded rectangle with radius rad, inset by
   ins pixels; returns FALSE if the row is outside. Units: 1/16 px. */
static BOOL span_rect(LONG y, LONG w, LONG h, LONG rad, LONG ins, LONG *l, LONG *r)
{
    LONG yc = y * 16 + 8, top = ins * 16, bot = (h - ins) * 16, rr = (rad - ins) * 16, dy = 0, dx;
    if (yc < top || yc >= bot) return FALSE;
    if (rr <= 0) { *l = ins * 16; *r = (w - ins) * 16; return TRUE; }
    if (yc < top + rr) dy = top + rr - yc;
    else if (yc > bot - rr) dy = yc - (bot - rr);
    dx = rr - (LONG)isqrt((ULONG)(rr * rr - dy * dy));
    *l = ins * 16 + dx;
    *r = (w - ins) * 16 - dx;
    return *r > *l;
}

/* An optional point on one side of the shape, for a label pointing at its
   icon: edge 'B', 'T', 'L' or 'R' (0 for none), centred 'pos' pixels along
   that side, 'size' pixels long at 45 degrees. The w x h box holds both the
   rounded rectangle and the point. */
static struct { char edge; LONG pos, size; } arrow;

void render_arrow(char edge, LONG pos, LONG size)
{
    arrow.edge = edge; arrow.pos = pos; arrow.size = size;
}

#define INSET45 23                      /* 1 pixel inset across a 45 degree edge, 1/16 px */

/* Point at the bottom (pointing down); the rectangle is the rows above it. */
static BOOL span_down(LONG y, LONG w, LONG h, LONG rad, LONG ins, LONG *l, LONG *r)
{
    LONG bh = h - arrow.size, t, hw;
    if (y < bh && span_rect(y, w, bh, rad, ins, l, r)) return TRUE;
    t = y * 16 + 8 - bh * 16;                    /* below the rectangle's bottom edge */
    if (y < bh && t < -ins * 16) return FALSE;   /* (inside its bottom border: the point opens it) */
    hw = arrow.size * 16 - (t > 0 ? t : 0) - ins * INSET45;
    if (hw <= 0) return FALSE;
    *l = arrow.pos * 16 - hw;
    *r = arrow.pos * 16 + hw;
    return TRUE;
}

/* Point on the right (pointing right); the rectangle is the columns left of it. */
static BOOL span_right(LONG y, LONG w, LONG h, LONG rad, LONG ins, LONG *l, LONG *r)
{
    LONG bw = w - arrow.size, dy = y * 16 + 8 - arrow.pos * 16, ext;
    BOOL in = span_rect(y, bw, h, rad, ins, l, r);
    if (dy < 0) dy = -dy;
    ext = bw * 16 + arrow.size * 16 - dy - ins * INSET45;
    if (in && ext > *r && dy < arrow.size * 16) *r = ext;
    return in;
}

/* The shape's inside span of row y (see span_rect), with the point if set. */
static BOOL span(LONG y, LONG w, LONG h, LONG rad, LONG ins, LONG *l, LONG *r)
{
    LONG a, b;
    BOOL in;
    switch (arrow.edge) {
    case 'B': return span_down(y, w, h, rad, ins, l, r);
    case 'T': return span_down(h - 1 - y, w, h, rad, ins, l, r);      /* mirrored */
    case 'R': return span_right(y, w, h, rad, ins, l, r);
    case 'L':
        in = span_right(y, w, h, rad, ins, &a, &b);                   /* mirrored */
        *l = w * 16 - b; *r = w * 16 - a;
        return in;
    default:  return span_rect(y, w, h, rad, ins, l, r);
    }
}

static UBYTE mix(UBYTE c, UBYTE t, LONG a) { return (UBYTE)(c + (((LONG)t - c) * a >> 8)); }

/* opacity scaled to 0..256 */
static LONG alpha(LONG pct) { return pct <= 0 ? 0 : pct >= 100 ? 256 : pct * 256 / 100; }

ULONG render_border_colour(ULONG tint)
{
    ULONG c = 0, s;
    for (s = 0; s < 24; s += 8) c |= (ULONG)mix((tint >> s) & 255, 255, 80) << s;
    return c;
}

/* The C versions of pixel.s (same results; built with -DSD_NOASM). */
#ifdef SD_NOASM
void sd_lut3_c(UBYTE *p, LONG n, const UBYTE *lut)
{
    for (; n > 0; n--, p += 3) {
        p[0] = lut[p[0]]; p[1] = lut[256 + p[1]]; p[2] = lut[512 + p[2]];
    }
}

void sd_blend_c(UBYTE *p, const UBYTE *s, LONG n, LONG m)
{
    for (; n > 0; n--, s += 4, p += 3) {
        LONG k = 255 - s[0], c, i;
        if (!s[0]) continue;
        k += k >> 7;
        for (i = 0; i < 3; i++) {
            c = (s[1 + i] * m >> 8) + (p[i] * k >> 8);
            p[i] = c > 255 ? 255 : c;
        }
    }
}

#endif

/* A scratch buffer for pixel rows and icons, kept between calls. */
static UBYTE *rowbuf;
static LONG rowsize;

UBYTE *render_rowbuf(LONG bytes)
{
    if (bytes > rowsize) {
        FreeVec(rowbuf);
        rowsize = 0;
        if (!(rowbuf = AllocVec(bytes, MEMF_ANY))) return NULL;
        rowsize = bytes;
    }
    return rowbuf;
}

/* More than 8 bits: rows through Picasso96 as R8G8B8. Pixels fully inside go
   through a table per channel (pixel.s), only the edges are worked out. */
static void shade_truecolour(struct Dock *d, struct RastPort *rp, LONG w, LONG h, LONG rad, LONG opacity,
                             LONG y0, LONG y1, LONG xa, LONG xb)
{
    UBYTE *row = render_rowbuf(w * 3), lut[768];
    ULONG bc = render_border_colour(d->tint);
    LONG a = alpha(opacity), ab = d->o->noborder ? 0 : alpha(opacity + 30), y, x, v;
    UBYTE t[3], b[3];
    BOOL lutmade = FALSE;

    if (!row) return;
    t[0] = d->tint >> 16; t[1] = d->tint >> 8; t[2] = d->tint;
    b[0] = bc >> 16;      b[1] = bc >> 8;      b[2] = bc;
    for (y = y0; y < y1; y++) {
        LONG ol, or, il, ir, ix0 = xb, ix1 = xb;
        BOOL outer = span(y, w, h, rad, 0, &ol, &or);
        BOOL inner = span(y, w, h, rad, 1, &il, &ir);
        if (!outer) continue;
        if (inner) {                             /* fully inside: x0 .. x1 - 1 */
            ix0 = (il + 15) >> 4; ix1 = ir >> 4;
            if (ix0 < xa) ix0 = xa;
            if (ix1 > xb) ix1 = xb;
            if (ix1 < ix0) ix1 = ix0;
        }
        pix_read(rp, xa, y, xb - xa, 1, row);
        for (x = xa; x < xb; x++) {
            LONG co, ci, k;
            UBYTE *p = row + (x - xa) * 3;
            if (x == ix0 && ix1 > ix0) {
                if (!lutmade) {                  /* only if a row has an inside run (not for edges) */
                    for (v = 0; v < 256; v++) {
                        lut[v] = mix(v, t[0], a); lut[256 + v] = mix(v, t[1], a); lut[512 + v] = mix(v, t[2], a);
                    }
                    lutmade = TRUE;
                }
                SD_LUT3(p, ix1 - ix0, lut);
                x = ix1 - 1;
                continue;
            }
            co = cover(x, ol, or); ci = inner ? cover(x, il, ir) : 0;
            if (!co) continue;
            for (k = 0; k < 3; k++) {
                UBYTE c = mix(p[k], t[k], a * ci >> 8);
                p[k] = mix(c, b[k], ab * (co - ci) >> 8);
            }
        }
        pix_write(rp, xa, y, xb - xa, 1, row);
    }
}

/* Pen tables for one opacity: map[p] is the tinted pen and map[256 + p] the
   border pen for screen pen p. Filled lazily by a nearest-colour search over a
   copy of the palette, as a Workbench screen uses few pens (FindColor() per
   entry took seconds on a 68020 with 256 colours). */
struct Tables {
    UBYTE map[512], done[512];
    UBYTE pal[256 * 3];
    UBYTE tint[3], border[3];
    LONG  npens, a;
    BOOL  noborder;
};

static UBYTE nearest(struct Tables *t, LONG r, LONG g, LONG b)
{
    const UBYTE *c = t->pal;
    LONG p, best = 0, bd = 0x7fffffff;
    for (p = 0; p < t->npens; p++, c += 3) {
        LONG dr = c[0] - r, dg = c[1] - g, db = c[2] - b, dist = dr * dr + dg * dg + db * db;
        if (dist < bd) { bd = dist; best = p; if (!dist) break; }
    }
    return (UBYTE)best;
}

static UBYTE fill(struct Tables *t, LONG p, LONG border)
{
    const UBYTE *c = t->pal + p * 3;
    UBYTE r;
    if (border && t->noborder) r = fill(t, p, 0);
    else if (border) r = nearest(t, mix(c[0], t->border[0], 200), mix(c[1], t->border[1], 200), mix(c[2], t->border[2], 200));
    else r = nearest(t, mix(c[0], t->tint[0], t->a), mix(c[1], t->tint[1], t->a), mix(c[2], t->tint[2], t->a));
    t->map[border * 256 + p] = r;
    t->done[border * 256 + p] = 1;
    return r;
}

#define MAP(t, p, b) ((t)->done[(b) * 256 + (p)] ? (t)->map[(b) * 256 + (p)] : fill((t), (p), (b)))

static struct Tables *clut_tables(struct Dock *d, LONG depth, LONG opacity)
{
    LONG p, slot;
    static ULONG rgb[3 * 256];                   /* not on the stack (Shell default: 4 KB) */
    ULONG bc = render_border_colour(d->tint);
    struct Tables *t;

    for (slot = 0; slot < 2; slot++)
        if (d->lut[slot] && d->lutop[slot] == opacity) return (struct Tables *)d->lut[slot];
    slot = d->lut[0] ? 1 : 0;
    if (d->lut[slot]) FreeVec(d->lut[slot]);
    if (!(t = (struct Tables *)(d->lut[slot] = AllocVec(sizeof(struct Tables), MEMF_ANY | MEMF_CLEAR)))) return NULL;
    d->lutop[slot] = opacity;
    t->npens = 1L << depth;
    GetRGB32(d->scr->ViewPort.ColorMap, 0, t->npens, rgb);
    for (p = 0; p < t->npens * 3; p++) t->pal[p] = rgb[p] >> 24;
    t->tint[0] = d->tint >> 16; t->tint[1] = d->tint >> 8; t->tint[2] = d->tint;
    t->border[0] = bc >> 16; t->border[1] = bc >> 8; t->border[2] = bc;
    t->a = alpha(opacity);
    t->noborder = d->o->noborder;
    return t;
}

/* Planar <-> chunky, 8 pixels at a time: spread[v] holds the 8 bits of a
   plane byte as one byte per pixel (pixels 0-3 in [0], 4-7 in [1]). */
static ULONG spread[256][2];

static void spread_init(void)
{
    LONG v, i;
    if (spread[255][0]) return;
    for (v = 0; v < 256; v++) {
        ULONG a = 0, b = 0;
        for (i = 0; i < 4; i++) {
            a |= (ULONG)((v >> (7 - i)) & 1) << (24 - 8 * i);
            b |= (ULONG)((v >> (3 - i)) & 1) << (24 - 8 * i);
        }
        spread[v][0] = a; spread[v][1] = b;
    }
}

/* Bit p of the 4 pens in m as a nibble (pen of the first pixel = top bit):
   one multiply gathers bits 24, 16, 8, 0 into bits 27..24. */
#define GATHER(m, p) ((((m) >> (p)) & 0x01010101UL) * 0x01020408UL >> 24 & 15)

/* Pen of each pixel inside the shape: tinted (inner) or border. */
static ULONG map4(struct Tables *t, ULONG m, LONG x, LONG il, LONG ir, LONG ol, LONG or)
{
    ULONG r = 0;
    LONG i;
    for (i = 0; i < 4; i++, x++) {
        UBYTE p = m >> (24 - 8 * i);
        if (x >= il && x <= ir) p = MAP(t, p, 0);
        else if (x >= ol && x <= or) p = MAP(t, p, 1);
        r |= (ULONG)p << (24 - 8 * i);
    }
    return r;
}

static ULONG map4in(struct Tables *t, ULONG m)
{
    return (ULONG)MAP(t, m >> 24, 0) << 24 | (ULONG)MAP(t, m >> 16 & 255, 0) << 16 |
           (ULONG)MAP(t, m >> 8 & 255, 0) << 8 | MAP(t, m & 255, 0);
}

/* Planar bitmaps up to 8 planes: convert 8 pixels at a time from planes to
   pens and back, mapping the pens inside the shape through the tables. A pixel
   counts as inside if its centre is; whole bytes inside take a fast path. No
   blitter or ReadPixelArray8, and interleaved bitmaps work too. */
static void shade_clut(struct Dock *d, struct BitMap *bm, LONG depth, LONG w, LONG h, LONG rad, LONG opacity,
                       LONG y0, LONG y1, LONG xa, LONG xb)
{
    struct Tables *t = clut_tables(d, depth, opacity);
    LONG y, bx, bpr = bm->BytesPerRow, p;

    if (!t) return;
    spread_init();
    for (y = y0; y < y1; y++) {
        LONG ol, or, il = 1, ir = 0, oxl, oxr, ixl, ixr;
        if (!span(y, w, h, rad, 0, &ol, &or)) continue;
        oxl = (ol + 7) >> 4; oxr = (or - 8) >> 4;          /* pixels whose centre is inside */
        if (span(y, w, h, rad, 1, &il, &ir)) { ixl = (il + 7) >> 4; ixr = (ir - 8) >> 4; }
        else { ixl = 1; ixr = 0; }
        if (oxr >= w) oxr = w - 1;
        for (bx = (oxl > xa ? oxl : xa) >> 3; bx <= (oxr < xb - 1 ? oxr : xb - 1) >> 3; bx++) {
            LONG off = y * bpr + bx, x = bx * 8;
            ULONG m0 = 0, m1 = 0;
            for (p = 0; p < depth; p++) {
                const ULONG *s = spread[bm->Planes[p][off]];
                m0 |= s[0] << p;
                m1 |= s[1] << p;
            }
            if (x >= ixl && x + 7 <= ixr) {                /* all 8 inside: tinted */
                m0 = map4in(t, m0);
                m1 = map4in(t, m1);
            } else {
                m0 = map4(t, m0, x, ixl, ixr, oxl, oxr);
                m1 = map4(t, m1, x + 4, ixl, ixr, oxl, oxr);
            }
            for (p = 0; p < depth; p++)
                bm->Planes[p][off] = (UBYTE)(GATHER(m0, p) << 4 | GATHER(m1, p));
        }
    }
}

/* Up to 8 bits on a graphics card (chunky, not planar): rows of pens through
   Picasso96, same tables. */
static void shade_chunky8(struct Dock *d, struct RastPort *rp, LONG depth, LONG w, LONG h, LONG rad, LONG opacity,
                          LONG y0, LONG y1, LONG xa, LONG xb)
{
    struct Tables *t = clut_tables(d, depth, opacity);
    UBYTE *row = AllocVec(w, MEMF_ANY);
    struct RenderInfo ri;
    LONG y, x;

    if (!t || !row) { FreeVec(row); return; }
    ri.Memory = row; ri.BytesPerRow = w; ri.pad = 0; ri.RGBFormat = SD_RGBFB_CLUT;
    for (y = y0; y < y1; y++) {
        LONG ol, or, il, ir, oxl, oxr, ixl = 1, ixr = 0;
        if (!span(y, w, h, rad, 0, &ol, &or)) continue;
        oxl = (ol + 7) >> 4; oxr = (or - 8) >> 4;
        if (oxr >= w) oxr = w - 1;
        if (span(y, w, h, rad, 1, &il, &ir)) { ixl = (il + 7) >> 4; ixr = (ir - 8) >> 4; }
        if (oxl < xa) oxl = xa;
        if (oxr > xb - 1) oxr = xb - 1;
        if (oxr < oxl) continue;
        p96ReadPixelArray(&ri, 0, 0, rp, oxl, y, oxr - oxl + 1, 1);
        for (x = oxl; x <= oxr; x++) row[x - oxl] = MAP(t, row[x - oxl], !(x >= ixl && x <= ixr));
        p96WritePixelArray(&ri, 0, 0, rp, oxl, y, oxr - oxl + 1, 1);
    }
    FreeVec(row);
}

void render_tables_free(struct Dock *d)
{
    LONG i;
    for (i = 0; i < 2; i++) { FreeVec(d->lut[i]); d->lut[i] = NULL; }
    FreeVec(rowbuf); rowbuf = NULL; rowsize = 0;
}

/* Rows y0..y1 shaded on whatever screen type this is. */
static void shade(struct Dock *d, struct BitMap *bm, struct RastPort *rp, LONG depth, LONG w, LONG h,
                  LONG rad, LONG opacity, LONG y0, LONG y1, LONG xa, LONG xb)
{
    static BOOL told;                            /* (said once: this runs per row, under dz_lock) */
    if (y1 <= y0 || xb <= xa) return;
    if (depth <= 8 && (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD)) shade_clut(d, bm, depth, w, h, rad, opacity, y0, y1, xa, xb);
    else if (depth <= 8 && P96Base) shade_chunky8(d, rp, depth, w, h, rad, opacity, y0, y1, xa, xb);   /* 8-bit card */
    else if (depth > 8 && P96Base) shade_truecolour(d, rp, w, h, rad, opacity, y0, y1, xa, xb);
    else if (!told) {
        told = TRUE;
        sd_verbose(d, depth <= 8 ? "Cannot tint this screen's background"
                                 : "Picasso96API.library not found: background not tinted");
    }
}

/* Copy the screen at left/top (or bitmap src there, if given) and tint it
   inside a rounded rectangle. For a solid dock the rows between the corners
   are filled with the colour's pen and only the corner rows are shaded, so the
   screen shows outside the rounded corners and it stays fast on any screen. */
struct BitMap *render_glass(struct Dock *d, struct BitMap *src, WORD left, WORD top, WORD w, WORD h, WORD rad,
                            LONG opacity)
{
    struct BitMap *sbm = d->scr->RastPort.BitMap, *bm;
    LONG depth = GetBitMapAttr(sbm, BMA_DEPTH), y;
    struct RastPort rp;
    PROF_START(t0);

    if (!(bm = AllocBitMap(w, h, depth, BMF_MINPLANES, sbm))) return NULL;
    dz_lock();                                   /* the scratch buffer is shared with the zone hook */
    if (src) BltBitMap(src, left, top, bm, 0, 0, w, h, 0xC0, 0xFF, NULL);
    else {
        LockLayerInfo(&d->scr->LayerInfo);
        BltBitMap(sbm, left, top, bm, 0, 0, w, h, 0xC0, 0xFF, NULL);
        UnlockLayerInfo(&d->scr->LayerInfo);
    }
    WaitBlit();
    InitRastPort(&rp);
    rp.BitMap = bm;
    if (opacity >= 100 && d->fillpen >= 0 && (d->borderpen >= 0 || d->o->noborder) && w > 2) {
        TRACE("solid");
        for (y = 0; y < h; ) {
            LONG ol, or, il, ir, mx0, mx1, y1;
            BOOL inner;
            for (y1 = y; y1 < h && span(y1, w, h, rad, 0, &ol, &or) && ol == 0 && or == w * 16 &&
                         span(y1, w, h, rad, 1, &il, &ir) && il == 16 && ir == (w - 1) * 16; y1++) ;
            if (y1 > y) {                        /* whole rows: the colour, the border down both sides */
                SetAPen(&rp, d->fillpen);
                RectFill(&rp, 1, y, w - 2, y1 - 1);
                if (!d->o->noborder) {
                    SetAPen(&rp, d->borderpen);
                    RectFill(&rp, 0, y, 0, y1 - 1);
                    RectFill(&rp, w - 1, y, w - 1, y1 - 1);
                }
                y = y1;
                continue;
            }
            if (!span(y, w, h, rad, 0, &ol, &or)) { y++; continue; }   /* the corner (and point) rows: */
            inner = span(y, w, h, rad, 1, &il, &ir);                     /* only their edges are shaded */
            if (inner) { mx0 = (il + 15) >> 4; mx1 = ir >> 4; }       /* fully inside: the colour */
            else { mx0 = (ol + 15) >> 4; mx1 = or >> 4; }             /* a border row */
            if (mx1 < mx0) mx1 = mx0;
            shade(d, bm, &rp, depth, w, h, rad, opacity, y, y + 1, ol >> 4, mx0);
            shade(d, bm, &rp, depth, w, h, rad, opacity, y, y + 1, mx1, (or + 15) >> 4);
            if (mx1 > mx0 && (inner || !d->o->noborder)) {
                SetAPen(&rp, inner ? d->fillpen : d->borderpen);
                RectFill(&rp, mx0, y, mx1 - 1, y);
            }
            y++;
        }
    } else if (opacity > 0 || !d->o->noborder) {
        TRACE("shade");
        shade(d, bm, &rp, depth, w, h, rad, opacity, 0, h, 0, w);
    }
    WaitBlit();
    dz_unlock();
    PROF_STOP(3, t0);
    return bm;
}

/* Build the background from d->raw if dock_relayout made one, otherwise from a
   fresh copy of the screen under the dock, kept as d->raw so the dock can
   resize later without closing. Without memory for the copy, it reads straight
   from the screen. */
BOOL render_background(struct Dock *d)
{
    struct BitMap *sbm = d->scr->RastPort.BitMap;
    BOOL ok;
    if (!d->raw && (d->raw = AllocBitMap(d->w, d->h, GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES, sbm))) {
        LockLayerInfo(&d->scr->LayerInfo);
        BltBitMap(sbm, d->left, d->top, d->raw, 0, 0, d->w, d->h, 0xC0, 0xFF, NULL);
        UnlockLayerInfo(&d->scr->LayerInfo);
        WaitBlit();
    }
    ah_strip_grab(d, FALSE);                     /* the hidden strip's place, as the screen shows it now */
    ok = (d->bg = d->raw ? render_glass(d, d->raw, 0, 0, d->w, d->h, d->radius, d->opacity)
                         : render_glass(d, NULL, d->left, d->top, d->w, d->h, d->radius, d->opacity)) != NULL;
    TRACE("glass done");
    /* transparent on a graphics card: icons are blended in fast RAM (compose_rgb) */
    if (ok && d->o->transparent && P96Base && GetBitMapAttr(d->bg, BMA_DEPTH) > 8 &&
        (d->bgrgb = AllocVec(d->w * d->h * 3, MEMF_ANY))) {
        struct RastPort rp;
        PROF_START(t1);
        InitRastPort(&rp);
        rp.BitMap = d->bg;
        pix_read(&rp, 0, 0, d->w, d->h, d->bgrgb);
        PROF_STOP(11, t1);
    }
    return ok;
}

/* The finished dock (background, icons, labels) in one bitmap: a redraw or an
   auto-hide animation frame is then a single blit. */
BOOL render_image(struct Dock *d)
{
    struct RastPort rp;
    PROF_START(t0);
    if (!(d->img = AllocBitMap(d->w, d->h, GetBitMapAttr(d->bg, BMA_DEPTH), BMF_MINPLANES, d->bg))) return FALSE;
    dz_lock();
    BltBitMap(d->bg, 0, 0, d->img, 0, 0, d->w, d->h, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = d->img;
    {
        struct Box all = { 0, 0, 0, 0 };
        all.x1 = d->w; all.y1 = d->h;
        render_items_in(d, &rp, &all);
    }
    WaitBlit();
    dz_unlock();
    PROF_STOP(7, t0);
    TRACE("image composed");
    return TRUE;
}

/* The label; hi: on a lighter rounded "pill" (labels-only dock: the label
   under the pointer, or pressed). */
static void draw_label(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy, BOOL hi)
{
    CONST_STRPTR s = it->label ? it->label : FilePart(it->path);
    struct TextExtent te;
    LONG n = strlen(s), fit, x, y;

    fit = TextFit(rp, (STRPTR)s, n, &te, NULL, 1, it->w - 4, rp->TxHeight + 1);
    if (!fit) return;
    x = cx + (it->w - te.te_Width) / 2;
    y = cy + 2 + d->spy + (d->o->noicon ? 0 : d->maxih + 2) + rp->TxBaseline;
    if (hi && d->borderpen >= 0) {
        WORD x0 = x - 5, x1 = x + te.te_Width + 4, y0 = y - rp->TxBaseline - 2, y1 = y0 + rp->TxHeight + 3;
        if (x0 < cx) x0 = cx;
        if (x1 > cx + it->w - 1) x1 = cx + it->w - 1;
        SetAPen(rp, d->borderpen);
        RectFill(rp, x0 + 1, y0, x1 - 1, y1);    /* corners cut: rounded at this size */
        RectFill(rp, x0, y0 + 1, x1, y1 - 1);
    }
    if (d->textpen != d->shadowpen) {
        SetAPen(rp, d->shadowpen);
        Move(rp, x + 1, y + 1);
        Text(rp, (STRPTR)s, fit);
    }
    SetAPen(rp, d->textpen);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, fit);
}

#ifdef SD_DEBUG
LONG render_clamps;                      /* draws that would have left the bitmap */
#endif

/* The label. On a solid dock it is drawn once onto the dock colour and then
   copied, as text drawing is slow on planar screens. */
static void label_dock(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy)
{
    WORD ly = 2 + d->spy + (d->o->noicon ? 0 : d->maxih + 2), lh = d->font->tf_YSize + 2;
    if (!d->o->transparent && d->fillpen >= 0 && in_solid(d, cx, cy + ly, it->w, lh)) {
        if (it->lbm && it->lbmw != it->w) { WaitBlit(); FreeBitMap(it->lbm); it->lbm = NULL; }   /* (the cells grew) */
        if (!it->lbm &&
            (it->lbm = AllocBitMap(it->w, lh, GetBitMapAttr(d->bg, BMA_DEPTH), BMF_MINPLANES, d->bg))) {
            struct RastPort lrp;
            InitRastPort(&lrp);
            lrp.BitMap = it->lbm;
            SetRast(&lrp, d->fillpen);
            SetFont(&lrp, d->font);
            SetDrMd(&lrp, JAM1);
            draw_label(d, &lrp, it, 0, -ly, FALSE);
            WaitBlit();
            it->lbmw = it->w;
        }
        if (it->lbm) {
            BltBitMapRastPort(it->lbm, 0, 0, rp, cx, cy + ly, it->w, lh, 0xC0);
            return;
        }
    }
    draw_label(d, rp, it, cx, cy, FALSE);
}

/* The "?" of a missing icon, 5 x 7 cells, for badges bigger than a text line. */
static const UBYTE qmark[7] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };

/* The "?" in the box gx, gy, gw x gh, each cell a filled rectangle. */
static void draw_qmark(struct RastPort *r, LONG gx, LONG gy, LONG gw, LONG gh)
{
    LONG i, j;
    for (j = 0; j < 7; j++)
        for (i = 0; i < 5; i++)
            if (qmark[j] & 0x10 >> i)
                RectFill(r, gx + i * gw / 5, gy + j * gh / 7, gx + (i + 1) * gw / 5 - 1, gy + (j + 1) * gh / 7 - 1);
}

/* An icon whose file was not found (x, y, iw x ih: where it is drawn):
   dimmed with a 50% dither in the dock colour, and a "?" on a light badge in
   its middle, as the macOS dock does. The badge is about 2/5 of the icon, at
   least one text line high; the font's "?" is used at that smallest size.
   Labels only: the label dimmed. */
static void mark_missing(struct Dock *d, struct RastPort *rp, struct Item *it, WORD x, WORD y, WORD w, WORD h)
{
    static UWORD pat[2] = { 0x5555, 0xAAAA };
    struct RastPort r = *rp;
    LONG pen = d->fillpen >= 0 ? d->fillpen : d->shadowpen, bs, bx, by, c, line;
    if (pen < 0 || w <= 0 || h <= 0) return;
    SetAfPt(&r, pat, 1);
    SetDrMd(&r, JAM1);
    SetAPen(&r, pen);
    RectFill(&r, x, y, x + w - 1, y + h - 1);
    SetAfPt(&r, NULL, 0);
    if (d->o->noicon) return;
    line = r.TxHeight + 4;                       /* one text line */
    bs = (w < h ? w : h) * 2 / 5;
    if (bs < line) bs = line;
    if (bs > w || bs > h) return;
    bx = x + (w - bs) / 2; by = y + (h - bs) / 2;
    c = bs > line ? bs / 8 : 1;                  /* the corners cut off */
    SetAPen(&r, d->borderpen >= 0 ? d->borderpen : d->textpen);
    RectFill(&r, bx + c, by, bx + bs - 1 - c, by + bs - 1);
    RectFill(&r, bx, by + c, bx + bs - 1, by + bs - 1 - c);
    if (c >= 2) RectFill(&r, bx + c / 2, by + c / 2, bx + bs - 1 - c / 2, by + bs - 1 - c / 2);
    if (bs == line) {                            /* small: the font's "?" */
        SetAPen(&r, d->shadowpen);
        Move(&r, bx + (bs - TextLength(&r, "?", 1)) / 2 + 1, by + 2 + r.TxBaseline + 1);
        Text(&r, "?", 1);
        SetAPen(&r, d->textpen);
        Move(&r, bx + (bs - TextLength(&r, "?", 1)) / 2, by + 2 + r.TxBaseline);
        Text(&r, "?", 1);
    } else {                                     /* bigger: drawn to the badge's size, with a shadow */
        LONG gw = bs * 5 / 11, gh = bs * 7 / 11, gx = bx + (bs - gw) / 2, gy = by + (bs - gh) / 2;
        LONG sh = bs / 16 > 1 ? bs / 16 : 1;
        if (gw < 5 || gh < 7) return;            /* (a very small font: no empty cells) */
        SetAPen(&r, d->shadowpen);
        draw_qmark(&r, gx + sh, gy + sh, gw, gh);
        SetAPen(&r, d->textpen);
        draw_qmark(&r, gx, gy, gw, gh);
    }
}

/* A running program: a small rounded dot (5 x 3) under its cell (RUNBAND),
   and with two copies or more their number on the icon's top right corner. */
/* The dot's pen: a minimised program's steps bright - dim - bright (d->pulse). */
static LONG dot_pen(struct Dock *d, struct Item *it)
{
    static const UBYTE tri[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    LONG p = it->mini ? d->pulsepen[tri[d->pulse & 7]] : -1;
    return p >= 0 ? p : d->textpen;
}

static void mark_running(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy)
{
    struct RastPort r = *rp;
    WORD x = cx + it->w / 2, y = cy + it->h - RUNBAND;
    char n[3];
    LONG len, bw, bh, bx, by, tw;
    if (!d->o->running) return;                  /* RUNNING=0: none shown */
    SetDrMd(&r, JAM1);
    if (it->temp != RUN_MORE) {                  /* (the "more" cell: only its number) */
        SetAPen(&r, dot_pen(d, it));             /* (minimised: pulsing) */
        RectFill(&r, x - 1, y, x + 1, y + 2);
        RectFill(&r, x - 2, y + 1, x + 2, y + 1);
    }
    SetAPen(&r, d->textpen);
    if (it->running < 2 || d->o->noicon || d->o->running < 2) return;   /* RUNNING=1: the dot only */
    n[0] = it->running > 9 ? '0' + (it->running > 99 ? 99 : it->running) / 10 : '0' + it->running;   /* (up to 99) */
    n[1] = it->running > 9 ? '0' + (it->running > 99 ? 99 : it->running) % 10 : 0;
    n[2] = 0;
    len = strlen(n);
    bh = r.TxHeight + 2;
    tw = TextLength(&r, n, len);
    bw = tw + 6;
    if (bw < bh) bw = bh;
    if (bw > it->iw || bh > it->ih || it->iw > it->w) return;   /* (inside the cell: the image has no layer) */
    bx = cx + (it->w - it->iw) / 2 + it->iw - bw;
    by = cy + ICON_DY(d, it);
    SetAPen(&r, d->borderpen >= 0 ? d->borderpen : d->textpen);
    RectFill(&r, bx + 1, by, bx + bw - 2, by + bh - 1);
    RectFill(&r, bx, by + 1, bx + bw - 1, by + bh - 2);
    SetAPen(&r, d->shadowpen);
    Move(&r, bx + (bw - tw) / 2 + 1, by + 1 + r.TxBaseline + 1);
    Text(&r, n, len);
    SetAPen(&r, d->textpen);
    Move(&r, bx + (bw - tw) / 2, by + 1 + r.TxBaseline);
    Text(&r, n, len);
}

/* A minimised program's dot drawn again at the next pulse step: only its
   own little box (cheap enough for several times a second). */
void render_dot(struct Dock *d, struct Item *it)
{
    struct RastPort rp;
    WORD cx = it->x, cy = it->y, x, y;
    if (!d->img || !d->bg || !d->win || d->vis != d->full || !d->o->running) return;
    if (d->dragging || an_running() || it == d->drag) return;   /* (the cell is moving, or empty) */
    if (cx > d->w - it->w) cx = d->w - it->w;    /* where render_item draws the cell */
    if (cy > d->h - it->h) cy = d->h - it->h;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    x = cx + it->w / 2; y = cy + it->h - RUNBAND;
    if (x < 2 || x + 2 >= d->w || y < 0 || y + 2 >= d->h) return;
    dz_lock();
    BltBitMap(d->bg, x - 2, y, d->img, x - 2, y, 5, 3, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = d->img;
    SetDrMd(&rp, JAM1);
    SetAPen(&rp, dot_pen(d, it));
    RectFill(&rp, x - 1, y, x + 1, y + 2);
    RectFill(&rp, x - 2, y + 1, x + 2, y + 1);
    WaitBlit();
    dz_unlock();
    BltBitMapRastPort(d->img, x - 2, y, d->win->RPort, x - 2, y, 5, 3, 0xC0);
}

/* A divider in front of the running others (and between the Workbench icon
   and the programs): a line across the dock in the middle of its space
   (DIVGAP), at 'at' along the dock (-1: none). This adds its place to box b. */
void divider_box(struct Dock *d, WORD at, struct Box *b)
{
    WORD c = d->horiz ? d->h : d->w;
    if (at < 0) return;
    if (d->horiz) box_add(b, at - DIVGAP / 2, 0, DIVGAP, c);
    else box_add(b, 0, at - DIVGAP / 2, c, DIVGAP);
}

void render_divider_at(struct Dock *d, struct RastPort *rp, struct Box *b, WORD at)
{
    struct RastPort r = *rp;
    WORD size = d->horiz ? d->ch : d->cw, m = size / 8;
    WORD a0 = d->pad + m, a1 = d->pad + size - 1 - m - (d->horiz ? RUNBAND : 0);
    WORD x0, y0, x1, y1;
    if (at < 0 || d->divpen < 0) return;
    if (d->horiz) { x0 = x1 = at; y0 = a0; y1 = a1; }
    else { y0 = y1 = at; x0 = a0; x1 = a1; }
    if (x0 < b->x0) x0 = b->x0;
    if (y0 < b->y0) y0 = b->y0;
    if (x1 > b->x1 - 1) x1 = b->x1 - 1;
    if (y1 > b->y1 - 1) y1 = b->y1 - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d->w - 1) x1 = d->w - 1;            /* (no layer: never outside the image) */
    if (y1 > d->h - 1) y1 = d->h - 1;
    if (x0 > x1 || y0 > y1) return;
    SetDrMd(&r, JAM1);
    SetAPen(&r, d->divpen);
    RectFill(&r, x0, y0, x1, y1);
}

void render_divider(struct Dock *d, struct RastPort *rp, struct Box *b)
{
    render_divider_at(d, rp, b, d->divx[0]);
    render_divider_at(d, rp, b, d->divx[1]);
}

/* The "more" cell's three dots. */
static void mark_section(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy, BOOL selected)
{
    struct RastPort r = *rp;
    SetDrMd(&r, JAM1);
    if (it->temp == RUN_MORE && !d->o->noicon && it->ih > 0) {
        LONG s = it->ih / 8 < 2 ? 2 : it->ih / 8, i, x0, y0;
        if (s > (it->w - 2) / 5) s = (it->w - 2) / 5;   /* (inside the cell: the image has no layer) */
        if (s < 1) return;
        x0 = cx + (it->w - 5 * s) / 2; y0 = cy + ICON_DY(d, it) + (it->ih - s) / 2;
        for (i = 0; i < 3; i++) {
            LONG x = x0 + 2 * s * i + (selected ? 1 : 0), y = y0 + (selected ? 1 : 0);
            SetAPen(&r, d->shadowpen);
            RectFill(&r, x + 1, y + 1, x + s, y + s);
            SetAPen(&r, d->textpen);
            RectFill(&r, x, y, x + s - 1, y + s - 1);
        }
    }
}

/* Draw one icon and its label in the cell at cx, cy of a dock-sized bitmap
   (the dock image, the drag base or the drop-zone buffer). The rastport's font
   and draw mode must be set. These bitmaps have no layer and don't clip, so
   the cell is clamped here: a glide can start outside a dock that just shrank,
   and drawing there would overwrite memory. */
void render_item(struct Dock *d, struct RastPort *rp, struct Item *it, WORD cx, WORD cy, BOOL selected)
{
#ifdef SD_DEBUG
    if (cx < 0 || cy < 0 || cx > d->w - it->w || cy > d->h - it->h) render_clamps++;
#endif
    if (cx > d->w - it->w) cx = d->w - it->w;
    if (cy > d->h - it->h) cy = d->h - it->h;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (it->w > d->w || it->h > d->h) return;
    if (it->dobj && !d->o->noicon)
        icon_dock(d, rp, it, cx + (it->w - it->iw) / 2, cy + ICON_DY(d, it), selected);
    if (LABELS(d) && selected && d->o->noicon) draw_label(d, rp, it, cx, cy, TRUE);
    else if (LABELS(d)) label_dock(d, rp, it, cx, cy);
    if (it->missing) {
        if (d->o->noicon) mark_missing(d, rp, it, cx, cy, it->w, it->h);
        else mark_missing(d, rp, it, cx + (it->w - it->iw) / 2, cy + ICON_DY(d, it), it->iw, it->ih);
    } else if (it->running) mark_running(d, rp, it, cx, cy);
    if (it->temp) mark_section(d, rp, it, cx, cy, selected);
}

/* Draw one item (only != NULL) or all, over the background. */
void render_items(struct Dock *d, struct RastPort *rp, struct Item *only, BOOL selected)
{
    struct Item *it;
    PROF_START(t0);

    SetFont(rp, d->font);
    SetDrMd(rp, JAM1);
    FOR_ITEMS(d, it) {
        if (only && it != only) continue;
        if (d->dragging && it == d->drag) continue;   /* it follows the pointer (drag.c) */
        if (only && it->x >= 0 && it->y >= 0 && it->x + it->w <= d->w && it->y + it->h <= d->h)
            BltBitMapRastPort(d->bg, it->x, it->y, rp, it->x, it->y, it->w, it->h, 0xC0);
        render_item(d, rp, it, it->x, it->y, selected && it == only);
    }
    if (!only) {
        struct Box all;
        all.x0 = all.y0 = 0; all.x1 = d->w; all.y1 = d->h;
        render_divider(d, rp, &all);
    }
    PROF_STOP(0, t0);
}

/* Where render_item draws the cell of 'it' (kept inside the image). */
static void cell_at(struct Dock *d, struct Item *it, WORD *cx, WORD *cy)
{
    WORD x = it->x, y = it->y;
    if (x > d->w - it->w) x = d->w - it->w;
    if (y > d->h - it->h) y = d->h - it->h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *cx = x; *cy = y;
}

/* Transparent on a graphics card: build box b of the image in fast RAM (the
   background with the icons blended in, pixel.s) and write it with one
   Picasso96 call, then draw the labels. Picasso96 converts every pixel it
   touches, so one call per frame beats two per icon. Returns FALSE without
   memory. */
static BOOL compose_rgb(struct Dock *d, struct RastPort *rp, struct Box *b)
{
    LONG bw = b->x1 - b->x0, bh = b->y1 - b->y0, y;
    UBYTE *buf;
    struct Item *it;

    if (bw <= 0 || bh <= 0) return TRUE;
    if (!(buf = render_rowbuf(bw * bh * 3))) return FALSE;
    {
        PROF_START(t1);
        for (y = 0; y < bh; y++)
            memcpy(buf + y * bw * 3, d->bgrgb + ((b->y0 + y) * d->w + b->x0) * 3, bw * 3);
        PROF_STOP(8, t1);
    }
    {
    PROF_START(t2);
    FOR_ITEMS(d, it) {
        WORD cx, cy, ix, iy, x0, x1, y0, y1;
        if ((d->dragging && it == d->drag) || !it->argb || d->o->noicon) continue;
        cell_at(d, it, &cx, &cy);
        ix = cx + (it->w - it->iw) / 2; iy = cy + ICON_DY(d, it);
        x0 = ix > b->x0 ? ix : b->x0; x1 = ix + it->iw < b->x1 ? ix + it->iw : b->x1;
        y0 = iy > b->y0 ? iy : b->y0; y1 = iy + it->ih < b->y1 ? iy + it->ih : b->y1;
        for (y = y0; y < y1; y++)
            if (x1 > x0)
                SD_BLEND(buf + ((y - b->y0) * bw + x0 - b->x0) * 3, it->argb + ((y - iy) * it->iw + x0 - ix) * 4,
                         x1 - x0, 256);
    }
    PROF_STOP(9, t2);
    }
    {
        PROF_START(t3);
        pix_write(rp, b->x0, b->y0, bw, bh, buf);
        PROF_STOP(10, t3);
    }
    SetFont(rp, d->font);
    SetDrMd(rp, JAM1);
    FOR_ITEMS(d, it) {                           /* labels; icons without a blended image */
        struct Box c = { 0, 0, 0, 0 };
        WORD cx, cy;
        if (d->dragging && it == d->drag) continue;
        item_box(d, it, &c);
        if (c.x0 >= b->x1 || c.x1 <= b->x0 || c.y0 >= b->y1 || c.y1 <= b->y0) continue;
        cell_at(d, it, &cx, &cy);
        if (!it->argb && it->dobj && !d->o->noicon)
            icon_image(d, rp, it, cx + (it->w - it->iw) / 2, cy + ICON_DY(d, it), FALSE);
        if (LABELS(d)) draw_label(d, rp, it, cx, cy, FALSE);
        if (it->missing) {
            if (d->o->noicon) mark_missing(d, rp, it, cx, cy, it->w, it->h);
            else mark_missing(d, rp, it, cx + (it->w - it->iw) / 2, cy + ICON_DY(d, it), it->iw, it->ih);
        } else if (it->running) mark_running(d, rp, it, cx, cy);
        if (it->temp) mark_section(d, rp, it, cx, cy, FALSE);
    }
    render_divider(d, rp, b);
    return TRUE;
}

/* The items whose cells touch box b (not the dragged one), over what is there. */
void render_items_in(struct Dock *d, struct RastPort *rp, struct Box *b)
{
    struct Item *it;
    PROF_START(t0);
    if (d->bgrgb) {
        box_clip(d, b);
        if (compose_rgb(d, rp, b)) { PROF_STOP(0, t0); return; }
    }
    SetFont(rp, d->font);
    SetDrMd(rp, JAM1);
    FOR_ITEMS(d, it) {
        struct Box c = { 0, 0, 0, 0 };
        if (d->dragging && it == d->drag) continue;
        item_box(d, it, &c);
        if (c.x0 < b->x1 && c.x1 > b->x0 && c.y0 < b->y1 && c.y1 > b->y0)
            render_item(d, rp, it, it->x, it->y, FALSE);
    }
    render_divider(d, rp, b);
    PROF_STOP(0, t0);
}

/* Take a new snapshot of the screen under the dock. The dock must not be
   covering it (hidden, or stepped aside: ah_peek). FALSE without memory;
   the old one stays. */
BOOL render_resnap(struct Dock *d)
{
    struct BitMap *ob = d->bg, *oi = d->img, *or = d->raw;
    UBYTE *orgb = d->bgrgb;
    d->bg = d->img = d->raw = NULL;
    d->bgrgb = NULL;
    if (render_background(d) && render_image(d)) {
        WaitBlit();
        if (oi) FreeBitMap(oi);
        if (ob) FreeBitMap(ob);
        if (or) FreeBitMap(or);
        FreeVec(orgb);
        return TRUE;
    }
    WaitBlit();
    if (d->img) FreeBitMap(d->img);
    if (d->bg) FreeBitMap(d->bg);
    if (d->raw) FreeBitMap(d->raw);
    FreeVec(d->bgrgb);
    d->bg = ob; d->img = oi; d->raw = or; d->bgrgb = orgb;
    return FALSE;
}

void render_free(struct Dock *d)
{
    FreeVec(d->bgrgb); d->bgrgb = NULL;
    if (d->strip) { WaitBlit(); FreeBitMap(d->strip); d->strip = NULL; }
    if (d->raw) { WaitBlit(); FreeBitMap(d->raw); d->raw = NULL; }
    if (!d->img && !d->bg) return;
    WaitBlit();
    if (d->img) { FreeBitMap(d->img); d->img = NULL; }
    if (d->bg) { FreeBitMap(d->bg); d->bg = NULL; }
}
