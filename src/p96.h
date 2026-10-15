/* SimpleDock - the two Picasso96API calls used (pixel rows in and out)
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SD_P96_H
#define SD_P96_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* RGBFTYPE order: NONE, CLUT, R8G8B8, ... */
#define SD_RGBFB_CLUT   1
#define SD_RGBFB_R8G8B8 2

struct RenderInfo {
    APTR  Memory;
    WORD  BytesPerRow;
    WORD  pad;
    ULONG RGBFormat;
};

extern struct Library *P96Base;

void __p96WritePixelArray(__reg("a6") void *, __reg("a0") struct RenderInfo *ri, __reg("d0") UWORD sx,
    __reg("d1") UWORD sy, __reg("a1") struct RastPort *rp, __reg("d2") UWORD dx, __reg("d3") UWORD dy,
    __reg("d4") UWORD w, __reg("d5") UWORD h) = "\tjsr\t-102(a6)";
void __p96ReadPixelArray(__reg("a6") void *, __reg("a0") struct RenderInfo *ri, __reg("d0") UWORD dx,
    __reg("d1") UWORD dy, __reg("a1") struct RastPort *rp, __reg("d2") UWORD sx, __reg("d3") UWORD sy,
    __reg("d4") UWORD w, __reg("d5") UWORD h) = "\tjsr\t-108(a6)";
#define p96WritePixelArray(ri, sx, sy, rp, dx, dy, w, h) __p96WritePixelArray(P96Base, ri, sx, sy, rp, dx, dy, w, h)
#define p96ReadPixelArray(ri, dx, dy, rp, sx, sy, w, h)  __p96ReadPixelArray(P96Base, ri, dx, dy, rp, sx, sy, w, h)

#endif
