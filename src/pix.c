/* SimpleDock - pixels in and out of graphics-card bitmaps as R8G8B8
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   Picasso96 converts between the screen's pixel format and R8G8B8 (measured:
   faster than doing it here). One place for it, so the callers stay short. */
#include <proto/exec.h>
#include <proto/graphics.h>
#include "sd.h"
#include "p96.h"

/* w x h pixels at x, y of rp into rgb (R8G8B8, w * 3 bytes a row). */
void pix_read(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, UBYTE *rgb)
{
    struct RenderInfo ri;
    ri.Memory = rgb; ri.BytesPerRow = w * 3; ri.pad = 0; ri.RGBFormat = SD_RGBFB_R8G8B8;
    p96ReadPixelArray(&ri, 0, 0, rp, x, y, w, h);
}

/* rgb (R8G8B8) into w x h pixels at x, y of rp. */
void pix_write(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, UBYTE *rgb)
{
    struct RenderInfo ri;
    ri.Memory = rgb; ri.BytesPerRow = w * 3; ri.pad = 0; ri.RGBFormat = SD_RGBFB_R8G8B8;
    p96WritePixelArray(&ri, 0, 0, rp, x, y, w, h);
}
