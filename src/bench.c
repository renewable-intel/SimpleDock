/* SimpleDock - ARexx BENCH: drawing times on this machine
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   rx "options results; address SIMPLEDOCK 'BENCH'; say result"
   Times, with the dock as it is now: opening it (icons and background, at
   the last open), a full redraw, a redraw of one icon's place (an animation
   frame), the background - plus the CPU and the screen, so results from
   different machines can be compared. The dock flickers while it runs. */
#include <string.h>
#include <devices/timer.h>
#include <exec/execbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include "sd.h"

extern struct Device *TimerBase;
extern struct ExecBase *SysBase;
extern struct Library *P96Base;

static LONG us_since(struct timeval *t0)
{
    struct timeval now;
    GetSysTime(&now);
    SubTime(&now, t0);
    return now.tv_secs * 1000000 + now.tv_micro;
}

void dock_bench(struct Dock *d, char *res)
{
    struct timeval t0;
    struct BitMap *sbm, *bm;
    struct Item *it;
    LONG v[12], i, n = 0, cpu, flags;

    if (!TimerBase || !d->win || !d->img) { strcpy(res, "no dock or no timer"); return; }
    GetSysTime(&t0);
    for (i = 0; i < 10; i++) dock_compose(d);
    v[1] = us_since(&t0) / 1000;                 /* 10 redraws: 1/10 ms each */
    GetSysTime(&t0);
    for (i = 0; i < 20; i++) {
        struct Box b = { 0, 0, 0, 0 };
        if ((it = dock_item(d, 0))) item_box(d, it, &b);
        dock_compose_box(d, &b);
    }
    v[2] = us_since(&t0) / 2000;                 /* 20 frames: 1/10 ms each */
    GetSysTime(&t0);
    if ((bm = render_glass(d, NULL, d->left, d->top, d->w, d->h, d->radius, d->opacity))) {
        WaitBlit();
        FreeBitMap(bm);
    }
    v[3] = us_since(&t0) / 1000;
    v[0] = d->openms;
    flags = SysBase->AttnFlags;
    cpu = flags & (1 << 7) ? 6 : flags & AFF_68040 ? 4 : flags & AFF_68030 ? 3 : flags & AFF_68020 ? 2 : 0;
    sbm = d->scr->RastPort.BitMap;
    FOR_ITEMS(d, it) n++;
    v[4] = v[1] / 10; v[5] = v[1] % 10; v[6] = v[2] / 10; v[7] = v[2] % 10;
    v[1] = v[0]; v[0] = cpu;
    v[2] = (LONG)(P96Base && !(GetBitMapAttr(sbm, BMA_FLAGS) & BMF_STANDARD) ? "graphics card" : "native");
    v[8] = d->scr->Width; v[9] = d->scr->Height; v[10] = GetBitMapAttr(sbm, BMA_DEPTH); v[11] = n;
    {
        LONG o[12];
        o[0] = v[0]; o[1] = v[1]; o[2] = v[4]; o[3] = v[5]; o[4] = v[6]; o[5] = v[7]; o[6] = v[3];
        o[7] = v[2]; o[8] = v[8]; o[9] = v[9]; o[10] = v[10]; o[11] = v[11];
        SD_FMT(res, "680%ld0: open %ld ms, redraw %ld.%ld ms, frame %ld.%ld ms, background %ld ms | "
                    "%s %ldx%ldx%ld, %ld icons", o);
    }
}
