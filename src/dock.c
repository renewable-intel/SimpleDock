/* SimpleDock - dock window: icons, layout, position, input
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/timer.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <proto/icon.h>
#include <proto/diskfont.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include "sd.h"

extern struct GfxBase *GfxBase;

extern struct Library *DiskfontBase;

#define LABELMAX 96          /* widest label, pixels */

static ULONG hexcolour(CONST_STRPTR s, ULONG def)
{
    ULONG v = 0;
    LONG n = 0;
    if (!s) return def;
    if (*s == '#') s++;
    for (; *s; s++, n++) {
        char c = *s;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return def;
    }
    return n == 6 ? v : def;
}

static struct TextFont *open_font(struct Dock *d)
{
    struct TextFont *f = NULL;
    if (d->o->font && DiskfontBase) {          /* FONT=name/size */
        char name[64];
        struct TextAttr ta;
        CONST_STRPTR sl = strchr(d->o->font, '/');
        LONG n = sl ? sl - d->o->font : strlen(d->o->font), size = 8;
        if (n > (LONG)sizeof(name) - 6) n = sizeof(name) - 6;
        memcpy(name, d->o->font, n);
        strcpy(name + n, ".font");
        if (sl) StrToLong((STRPTR)sl + 1, &size);
        ta.ta_Name = (STRPTR)name; ta.ta_YSize = size; ta.ta_Style = 0; ta.ta_Flags = 0;
        f = OpenDiskFont(&ta);
        if (!f) sd_verbose(d, "Cannot open font %s", (LONG)d->o->font);
    }
    if (!f) f = OpenFont(d->scr->Font);
    if (!f) {                                    /* e.g. an outline screen font */
        sd_verbose(d, "Cannot open the screen font, using the default font");
        struct TextFont *df = GfxBase->DefaultFont;
        struct TextAttr ta;
        ta.ta_Name = (STRPTR)df->tf_Message.mn_Node.ln_Name;
        ta.ta_YSize = df->tf_YSize; ta.ta_Style = 0; ta.ta_Flags = 0;
        f = OpenFont(&ta);
    }
    return f;
}

/* Is the file or disk there, or at least its icon (SYS:System/Shell is only
   an icon)? Checked without "Please insert volume" requesters: a disk that
   isn't in the drive counts as missing. */
BOOL dock_exists(CONST_STRPTR path)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    char info[SD_LINEMAX + 8];
    BPTR lock;
    me->pr_WindowPtr = (APTR)-1;
    if (!(lock = Lock((STRPTR)path, ACCESS_READ)) && strlen(path) < SD_LINEMAX) {
        strcpy(info, path);
        strcat(info, ".info");
        lock = Lock(info, ACCESS_READ);
    }
    if (lock) UnLock(lock);
    me->pr_WindowPtr = old;
    return lock != 0;
}

/* Icons for this screen (those not loaded yet) and the uniform cell size. */
static void load_icons(struct Dock *d, WORD *cw, WORD *ch)
{
    struct RastPort rp;
    struct Item *it;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    WORD maxw = 0;

    InitRastPort(&rp);
    SetFont(&rp, d->font);
    d->maxih = 0;
    me->pr_WindowPtr = (APTR)-1;                 /* an icon on a disk not inserted: no requester */
    FOR_ITEMS(d, it) {
        struct Rectangle r;
        CONST_STRPTR name = it->image ? it->image : it->path, label;
        char buf[SD_LINEMAX];
        LONG n = strlen(name);

        if (!it->dobj && it->temp) {             /* a program running, not kept: its icon, or the standard one */
            it->missing = FALSE;
            if (!it->nofile)
                it->dobj = GetIconTags(it->path, ICONGETA_Screen, d->scr, ICONGETA_FailIfUnavailable, FALSE, TAG_END);
            if (!it->dobj)
                it->dobj = GetIconTags(NULL, ICONGETA_GetDefaultType, it->temp == RUN_WB ? WBDRAWER : WBTOOL,
                                       ICONGETA_Screen, d->scr, TAG_END);
            it->iw = it->ih = 0;
            if (it->dobj && GetIconRectangle(&rp, it->dobj, NULL, &r, ICONDRAWA_Frameless, TRUE,
                                             ICONDRAWA_Borderless, TRUE, TAG_END)) {
                it->iw = r.MaxX - r.MinX + 1;
                it->ih = r.MaxY - r.MinY + 1;
                scale_icon(d, it);
            }
        } else if (!it->dobj) {                  /* (loaded: dock_relayout) */
            it->missing = !dock_exists(it->path);
            if (n > 5 && !Stricmp((STRPTR)name + n - 5, ".info") && n < (LONG)sizeof(buf)) {
                strcpy(buf, name); buf[n - 5] = 0; name = buf;
            } else if (n && name[n - 1] == ':' && n + 4 < (LONG)sizeof(buf)) {
                strcpy(buf, name); strcat(buf, "Disk"); name = buf;   /* a volume: its Disk.info */
            }
            it->dobj = GetIconTags(name, ICONGETA_Screen, d->scr, ICONGETA_FailIfUnavailable, FALSE, TAG_END);
            if (!it->dobj && it->image)
                it->dobj = GetIconTags(it->path, ICONGETA_Screen, d->scr, ICONGETA_FailIfUnavailable, FALSE, TAG_END);
            if (!it->dobj && it->missing)        /* not there: a standard icon, to show the "?" on */
                it->dobj = GetIconTags(NULL, ICONGETA_GetDefaultType, it->path[0] && it->path[strlen(it->path) - 1] == ':'
                                       ? WBDISK : WBTOOL, ICONGETA_Screen, d->scr, TAG_END);
            it->iw = it->ih = 0;
            if (it->dobj && GetIconRectangle(&rp, it->dobj, NULL, &r, ICONDRAWA_Frameless, TRUE,
                                             ICONDRAWA_Borderless, TRUE, TAG_END)) {
                it->iw = r.MaxX - r.MinX + 1;
                it->ih = r.MaxY - r.MinY + 1;
                scale_icon(d, it);               /* drawn once (and shrunk to SCALEICONW/H) */
            }
        }
        label = it->label ? it->label : FilePart(it->path);
        it->lw = TextLength(&rp, (STRPTR)label, strlen(label));
        if (it->lw > LABELMAX) it->lw = LABELMAX;
        if (!d->o->noicon && it->iw > maxw) maxw = it->iw;
        if (LABELS(d) && it->lw > maxw) maxw = it->lw;
        if (it->ih > d->maxih) d->maxih = it->ih;
    }
    me->pr_WindowPtr = oldwin;
    if (!d->maxih && !maxw) {                    /* empty dock: room for icons of the usual size */
        LONG s = d->o->scalew > 0 ? d->o->scalew : 46;
        maxw = s; d->maxih = s;
    }
    if (d->o->noicon) d->maxih = 0;
    /* ICONDIST widens the dock along its edge only; across it the icons sit
       close to the border, like the macOS dock (ICON_DY counts the 2 above
       the icon). */
    d->spx = d->horiz ? d->o->icondist : 0;
    d->spy = d->horiz ? 0 : d->o->icondist;
    *cw = maxw + 2 * d->spx + 4;
    *ch = 2 + d->spy + (d->o->noicon ? 0 : d->maxih)            /* same space above and below */
            + (LABELS(d) ? (d->o->noicon ? 0 : 2) + d->font->tf_YSize + 1 : 0) + d->spy + 2 + RUNBAND;
    if (*cw < 8) *cw = 8;                        /* never 0: the grid divides by it */
    if (*ch < 8) *ch = 8;
}

/* Grid of uniform cells; returns FALSE if nothing to show. */
static BOOL layout(struct Dock *d)
{
    WORD cw, ch, per, lines, n = 0, gap, keep, sw = d->scr->Width, sh = d->scr->Height;
    BOOL horiz;
    WORD gaps;                                   /* the dividers: 0, 1 or 2 */
    struct Item *it;
    STRPTR pos = d->o->pos;

    d->edge = 'B'; d->align = 'C';
    if (pos && *pos) {
        d->edge = ToUpper(pos[0]);
        if (pos[1]) d->align = ToUpper(pos[1]);
        if (!strchr("TBLR", d->edge)) d->edge = 'B';
    }
    horiz = d->edge == 'T' || d->edge == 'B';
    d->horiz = horiz;                            /* load_icons needs it */

    run_layout_out(d);                           /* the running others are placed again below */
    run_free_hidden(d);                          /* (those switched off: RUNNING=0, RUNHIDE, WBHIDE) */
    dock_unhide(d);                              /* which icons fit is decided again below */
    TRACE("icons");
    run_layout_all(d);                           /* (their icons count for the cell size too) */
    load_icons(d, &cw, &ch);
    run_layout_out(d);
    TRACE("icons loaded");
    FOR_ITEMS(d, it) n++;
    d->radius = d->o->radius < 0 ? 0 : d->o->radius;
    d->pad = 2 + d->radius / 6;                  /* icons 6 px from the border (RADIUS 12) */
    /* one row: as many icons as fit along the edge, keeping the gap to the
       edge (DISTANCE) at both ends too; LIMIT allows fewer */
    {                                            /* (with running others: the dividers' space) */
        struct Item *r;
        BOOL wb = FALSE, prog = FALSE;
        for (r = (struct Item *)d->runs.mlh_Head; r->node.mln_Succ; r = (struct Item *)r->node.mln_Succ)
            if (r->temp == RUN_WB) wb = TRUE; else prog = TRUE;
        gaps = (wb || prog) + (wb && prog);
    }
    d->fit = ((horiz ? sw : sh) - 2 * d->pad - 2 * d->o->distance - gaps * DIVGAP) / (horiz ? cw : ch);
    if (d->fit < 1) d->fit = 1;
    if (d->o->limit > 0 && d->o->limit < d->fit) d->fit = d->o->limit;
    FOR_ITEMS(d, it) it->over = FALSE;
    keep = d->fit;                               /* a full dock with programs running: the last place */
    d->pushed = 0;                               /* is the "more" cell */
    if (d->runs.mlh_Head->mln_Succ && n >= d->fit && d->fit >= 2) { keep = d->fit - 1; d->pushed = 1; }
    while (n > keep) {                           /* the rest wait in d->extra, in order */
        it = (struct Item *)RemTail((struct List *)&d->items);
        it->over = TRUE;
        AddHead((struct List *)&d->extra, (struct Node *)it);
        n--;
    }
    run_layout_in(d, d->fit - n);                /* the running others after them */
    n = 0;
    {
        BOOL wb = FALSE, prog = FALSE;
        FOR_ITEMS(d, it) { n++; if (it->temp == RUN_WB) wb = TRUE; else if (it->temp) prog = TRUE; }
        gaps = (wb || prog) + (wb && prog);
    }
    per = n ? n : 3;                             /* empty: three free places to drop on */
    if (!n) n = per;
    lines = 1;
    d->w = (horiz ? per * cw + gaps * DIVGAP : lines * cw) + 2 * d->pad;
    d->h = (horiz ? lines * ch : per * ch + gaps * DIVGAP) + 2 * d->pad;
    if (d->radius * 2 > d->w) d->radius = d->w / 2;
    if (d->radius * 2 > d->h) d->radius = d->h / 2;
    if (d->w > sw) d->w = sw;
    if (d->h > sh) d->h = sh;

    d->cw = cw; d->ch = ch; d->per = per; d->horiz = horiz;
    dock_place(d);

    gap = d->o->distance;                        /* to the screen edge (DISTANCE, default 4) */
    switch (d->edge) {
    case 'T': d->top = gap; break;
    case 'B': d->top = sh - d->h - gap; break;
    case 'L': d->left = gap; break;
    default:  d->left = sw - d->w - gap; break;
    }
    if (horiz) {
        d->left = d->align == 'L' ? d->o->offset : d->align == 'R' ? sw - d->w - d->o->offset
                                                  : (sw - d->w) / 2 + d->o->offset;
    } else {
        d->top = d->align == 'T' ? d->o->offset : d->align == 'B' ? sh - d->h - d->o->offset
                                                 : (sh - d->h) / 2 + d->o->offset;
    }
    if (d->left < 0) d->left = 0;
    if (d->top < 0) d->top = 0;
    if (d->left + d->w > sw) d->left = sw - d->w;
    if (d->top + d->h > sh) d->top = sh - d->h;
    switch (d->edge) {                           /* from the edge to the far side of the dock */
    case 'T': d->full = d->top + d->h; break;
    case 'B': d->full = sh - d->top; break;
    case 'L': d->full = d->left + d->w; break;
    default:  d->full = sw - d->left; break;
    }
    return TRUE;
}

/* Targets in list order, without 'skip' and leaving cell 'gap' free (-1: none).
   The running others sit behind the first divider's space: the Workbench
   icon, then (behind a second divider) the programs. While one is dragged
   to be kept (dragkeep), the first divider is after its place. */
void dock_targets(struct Dock *d, struct Item *skip, LONG gap)
{
    struct Item *it;
    WORD n = 0, k = 0, kept = 0, wb = 0, temps = 0, size = d->horiz ? d->cw : d->ch, in, i;
    FOR_ITEMS(d, it) {
        n++;
        if (!it->temp) kept++;
        else if (it->temp == RUN_WB) wb++;
        else temps++;
    }
    in = skip && skip->temp && d->dragkeep ? 1 : 0;   /* (the one being kept: in front of the divider; */
    if (skip && !skip->temp && gap < 0) in = -1;      /*  a kept one dragged off: the others close up) */
    d->divb[0] = wb + temps ? kept + in : -1;
    d->divb[1] = wb && temps - (in > 0) > 0 ? d->divb[0] + wb : -1;
    d->gapc[0] = wb + temps ? kept : -1;         /* (at rest, for dock_along) */
    d->gapc[1] = wb && temps ? kept + wb : -1;
    FOR_ITEMS(d, it) {
        WORD c, a, b, along;
        if (it == skip) continue;
        c = gap >= 0 && k >= gap ? k + 1 : k;
        k++;
        a = c % d->per; b = c / d->per;
        along = d->pad + a * size + (d->divb[0] >= 0 && c >= d->divb[0] ? DIVGAP : 0) +
                (d->divb[1] >= 0 && c >= d->divb[1] ? DIVGAP : 0);
        it->tx = d->horiz ? along : d->pad + b * d->cw;
        it->ty = d->horiz ? d->pad + b * d->ch : along;
        it->w = d->cw; it->h = d->ch;
    }
    for (i = 0; i < 2; i++) d->divtx[i] = d->divb[i] >= 0 ? d->pad + d->divb[i] * size + i * DIVGAP + DIVGAP / 2 : -1;
    d->ncells = n;
    d->nkept = kept;
    d->ndiv = kept;
    dock_runcells(d);
}

/* Which cells hold a running program (Del over one closes it: input.c). */
void dock_runcells(struct Dock *d)
{
    struct Item *it;
    LONG k = 0;
    FOR_ITEMS(d, it) {
        if (k >= (LONG)sizeof(d->runcell)) break;
        d->runcell[k++] = it->running > 0 && it->temp != RUN_MORE && run_trackable(it);
    }
    while (k < (LONG)sizeof(d->runcell)) d->runcell[k++] = 0;
}

/* Is window position a (along the dock) in a divider's space? Only
   arithmetic: the input handler asks too. */
BOOL dock_in_gap(struct Dock *d, WORD a)
{
    WORD size = d->horiz ? d->cw : d->ch, i, b;
    for (i = 0; i < 2 && d->gapc[i] >= 0; i++) {
        b = d->pad + d->gapc[i] * size + i * DIVGAP;
        if (a >= b && a < b + DIVGAP) return TRUE;
    }
    return FALSE;
}

/* Along the dock (window position a): where it would be without the
   dividers' space; in such a space, the icon in front of it. */
WORD dock_along(struct Dock *d, WORD a)
{
    WORD size = d->horiz ? d->cw : d->ch, i, b;
    for (i = 0; i < 2 && d->gapc[i] >= 0; i++) {
        b = d->pad + d->gapc[i] * size + i * DIVGAP;   /* (where divider i's space starts) */
        if (a < b) break;
        if (a < b + DIVGAP) return d->gapc[i] ? b - i * DIVGAP - 1 : b - i * DIVGAP;
    }
    return a - i * DIVGAP;
}

/* Cell positions in list order, no glide. */
void dock_place(struct Dock *d)
{
    struct Item *it;
    dock_targets(d, NULL, -1);
    FOR_ITEMS(d, it) {
        it->x = it->tx; it->y = it->ty;
    }
    d->divx[0] = d->divtx[0];
    d->divx[1] = d->divtx[1];
}

/* Put the icons that didn't fit back at the end of the list, so all are in
   order again (for a new layout, saving or closing). */
void dock_unhide(struct Dock *d)
{
    struct Node *n;
    while ((n = RemHead((struct List *)&d->extra))) AddTail((struct List *)&d->items, n);
}

LONG dock_extra(struct Dock *d)
{
    struct Node *n;
    LONG k = 0;
    for (n = (struct Node *)d->extra.mlh_Head; n->ln_Succ; n = n->ln_Succ) k++;
    return k;
}

struct Item *dock_item(struct Dock *d, LONG cell)
{
    struct Item *it;
    if (cell < 0) return NULL;
    FOR_ITEMS(d, it)
        if (cell-- == 0) return it;
    return NULL;
}

/* Cell under window coordinates x, y (-1 = none). */
LONG dock_cell(struct Dock *d, WORD x, WORD y)
{
    LONG c, r, i;
    if (d->horiz) x = dock_along(d, x);
    else y = dock_along(d, y);
    x -= d->pad; y -= d->pad;
    if (x < 0 || y < 0) return -1;
    c = x / d->cw; r = y / d->ch;
    if (d->horiz ? c >= d->per : r >= d->per) return -1;
    i = d->horiz ? r * d->per + c : c * d->per + r;
    return i < d->ncells ? i : -1;
}

static void pens(struct Dock *d, BOOL get)
{
    struct ColorMap *cm = d->scr->ViewPort.ColorMap;
    if (get) {
        ULONG bc = render_border_colour(d->tint);
        d->textpen = ObtainBestPen(cm, 0xffffffff, 0xffffffff, 0xffffffff, TAG_END);
        d->shadowpen = ObtainBestPen(cm, 0, 0, 0, TAG_END);
        d->fillpen = ObtainBestPen(cm, (d->tint >> 16 & 255) * 0x01010101UL, (d->tint >> 8 & 255) * 0x01010101UL,
                                   (d->tint & 255) * 0x01010101UL, OBP_Precision, PRECISION_EXACT, TAG_END);
        d->borderpen = ObtainBestPen(cm, (bc >> 16 & 255) * 0x01010101UL, (bc >> 8 & 255) * 0x01010101UL,
                                     (bc & 255) * 0x01010101UL, OBP_Precision, PRECISION_EXACT, TAG_END);
        {                                          /* the divider: light grey, dark on a light dock */
            ULONG g = (d->tint >> 16 & 255) * 3 + (d->tint >> 8 & 255) * 6 + (d->tint & 255) > 1280 ? 0x50 : 0xB8;
            d->divpen = ObtainBestPen(cm, g * 0x01010101UL, g * 0x01010101UL, g * 0x01010101UL, TAG_END);
        }
        {                                          /* the pulsing dot: white down to grey */
            static const UBYTE lv[4] = { 0xFF, 0xD0, 0xA0, 0x70 };
            LONG i;
            for (i = 0; i < 4; i++)
                d->pulsepen[i] = ObtainBestPen(cm, lv[i] * 0x01010101UL, lv[i] * 0x01010101UL, lv[i] * 0x01010101UL,
                                               TAG_END);
        }
    } else {
        if (d->textpen >= 0) ReleasePen(cm, d->textpen);
        if (d->shadowpen >= 0) ReleasePen(cm, d->shadowpen);
        if (d->fillpen >= 0) ReleasePen(cm, d->fillpen);
        if (d->borderpen >= 0) ReleasePen(cm, d->borderpen);
        if (d->divpen >= 0) ReleasePen(cm, d->divpen);
        {
            LONG i;
            for (i = 0; i < 4; i++) { if (d->pulsepen[i] >= 0) ReleasePen(cm, d->pulsepen[i]); d->pulsepen[i] = -1; }
        }
        d->textpen = d->shadowpen = d->fillpen = d->borderpen = d->divpen = -1;
    }
}

BOOL dock_open(struct Dock *d)
{
    extern struct Device *TimerBase;
    struct timeval t0;
    if (TimerBase) GetSysTime(&t0);
    d->textpen = d->shadowpen = d->fillpen = d->borderpen = d->divpen = -1;
    d->pulsepen[0] = d->pulsepen[1] = d->pulsepen[2] = d->pulsepen[3] = -1;
    if (!(d->scr = LockPubScreen(d->o->pubscreen))) {
        sd_msg(d, "Cannot find screen %s", (LONG)(d->o->pubscreen ? d->o->pubscreen : (STRPTR)"Workbench"));
        return FALSE;
    }
    TRACE("font");
    if (!(d->font = open_font(d))) { sd_msg(d, "Cannot open a font"); goto fail; }
    d->tint = hexcolour(d->o->tint, d->o->transparent ? 0x202020 : 0x383838);   /* graphite / dark grey */
    d->opacity = d->o->transparent ? d->o->opacity : 100;
    pens(d, TRUE);                             /* before the icons: scale.c uses them */
    TRACE("layout");
    layout(d);
    d->vis = d->full;
    d->hover = d->tipcell = -1;
    TRACE("background");
    if (!render_background(d) || !render_image(d)) { sd_msg(d, "Not enough memory for the dock"); goto fail; }
    TRACE("window");
    d->win = OpenWindowTags(NULL,
        WA_PubScreen, d->scr, WA_Left, d->left, WA_Top, d->top, WA_Width, d->w, WA_Height, d->h,
        WA_MinWidth, 1, WA_MinHeight, 1, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_Borderless, TRUE, WA_Activate, FALSE, WA_ReportMouse, TRUE,
        WA_SmartRefresh, TRUE,             /* keeps its contents while auto-hide slides it */
        WA_BackFill, LAYERS_NOBACKFILL,    /* a part uncovered while it grows is not cleared to grey first */
        WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_REFRESHWINDOW | IDCMP_INACTIVEWINDOW |
                  IDCMP_CHANGEWINDOW,
        TAG_END);
    if (!d->win) { sd_msg(d, "Cannot open the dock window"); goto fail; }
    d->gen++;                                  /* drops still queued for the old window are ignored */
    if (am_port())                             /* icons dropped from Workbench */
        d->appwin = AddAppWindow(0, d->gen, d->win, am_port(), TAG_END);
    dz_add(d);                                 /* make way while Workbench icons hover */
    dock_show(d);
    d->behind = dock_behind(d);                /* the snapshot was taken with these behind it */
    TRACE("drawn");
    if (dock_extra(d) > d->pushed && !d->overtold) {   /* (not the one the "more" cell took the place of) */
        d->overtold = TRUE;
        am_note_overflow(d, dock_extra(d) - d->pushed);
    }
    d->hidden = FALSE;
    if (TimerBase) {
        struct timeval now;
        GetSysTime(&now);
        SubTime(&now, &t0);
        d->openms = now.tv_secs * 1000 + now.tv_micro / 1000;
    }
    return TRUE;
fail:
    dock_close(d);
    return FALSE;
}

void dock_close(struct Dock *d)
{
    struct Item *it;
    drag_cancel(d, FALSE);
    an_stop(d);
    tip_hide(d);
    run_list_close(d);
    if (d->appwin) { dz_remove(d); RemoveAppWindow(d->appwin); d->appwin = NULL; }
    if (d->win) { struct Window *w = d->win; d->win = NULL; CloseWindow(w); }   /* (the input handler looks: NULL first) */
    dock_unhide(d);                              /* every icon's image is freed below */
    run_images_free(d);
    render_free(d);
    render_tables_free(d);
    if (d->scr) pens(d, FALSE);
    FOR_ITEMS(d, it)
        { scale_free(it); if (it->dobj) { FreeDiskObject(it->dobj); it->dobj = NULL; } }
    if (d->font) { CloseFont(d->font); d->font = NULL; }
    if (d->scr) { UnlockPubScreen(NULL, d->scr); d->scr = NULL; }
    d->pressed = NULL;
    d->hidden = TRUE;
    d->vis = 0;
}

/* Source offset in img of what the window shows now (auto-hide slides it). */
static void src_offset(struct Dock *d, WORD *sx, WORD *sy)
{
    *sx = d->edge == 'L' ? d->w - d->win->Width : 0;
    *sy = d->edge == 'T' ? d->h - d->win->Height : 0;
}

void dock_show(struct Dock *d)
{
    WORD sx, sy;
    PROF_START(t0);
    if (!d->win || !d->img) return;
    if (d->vis == 0 && d->strip) {               /* hidden: the strip shows the screen under it */
        WORD sw = GetBitMapAttr(d->strip, BMA_WIDTH), sh = GetBitMapAttr(d->strip, BMA_HEIGHT);
        BltBitMapRastPort(d->strip, 0, 0, d->win->RPort, 0, 0, d->win->Width < sw ? d->win->Width : sw,
                          d->win->Height < sh ? d->win->Height : sh, 0xC0);
        d->stripshown = TRUE;
        return;
    }
    d->stripshown = FALSE;
    src_offset(d, &sx, &sy);
    {                                            /* (just after a shrink the window may still be bigger) */
        WORD w = d->win->Width, h = d->win->Height;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (w > d->w - sx) w = d->w - sx;
        if (h > d->h - sy) h = d->h - sy;
        if (w > 0 && h > 0) BltBitMapRastPort(d->img, sx, sy, d->win->RPort, 0, 0, w, h, 0xC0);
    }
    PROF_STOP(5, t0);
}

void box_add(struct Box *b, WORD x, WORD y, WORD w, WORD h)
{
    if (w <= 0 || h <= 0) return;
    if (b->x1 <= b->x0) { b->x0 = x; b->y0 = y; b->x1 = x + w; b->y1 = y + h; return; }
    if (x < b->x0) b->x0 = x;
    if (y < b->y0) b->y0 = y;
    if (x + w > b->x1) b->x1 = x + w;
    if (y + h > b->y1) b->y1 = y + h;
}

void box_clip(struct Dock *d, struct Box *b)
{
    if (b->x0 < 0) b->x0 = 0;
    if (b->y0 < 0) b->y0 = 0;
    if (b->x1 > d->w) b->x1 = d->w;
    if (b->y1 > d->h) b->y1 = d->h;
}

/* Add the cell where 'it' is drawn now - kept inside the image, as render_item does. */
void item_box(struct Dock *d, struct Item *it, struct Box *b)
{
    WORD x = it->x, y = it->y;
    if (x > d->w - it->w) x = d->w - it->w;
    if (y > d->h - it->h) y = d->h - it->h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    box_add(b, x, y, it->w, it->h);
}

/* Grow box b to whole cells, so each icon is redrawn in full. A half-restored
   icon would get its soft edges blended twice. */
void box_cells(struct Dock *d, struct Box *b)
{
    struct Item *it;
    BOOL grew = TRUE;
    LONG guard = 8;
    while (grew && guard--) {
        grew = FALSE;
        FOR_ITEMS(d, it) {
            struct Box c = { 0, 0, 0, 0 };
            if (d->dragging && it == d->drag) continue;
            item_box(d, it, &c);
            if (c.x0 < b->x1 && c.x1 > b->x0 && c.y0 < b->y1 && c.y1 > b->y0 &&
                (c.x0 < b->x0 || c.y0 < b->y0 || c.x1 > b->x1 || c.y1 > b->y1)) {
                box_add(b, c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0);
                grew = TRUE;
            }
        }
    }
    box_clip(d, b);
}

/* Part of img into the window (auto-hide may show only part of the dock). */
void dock_show_box(struct Dock *d, struct Box *b)
{
    WORD sx, sy, x0, y0, x1, y1;
    PROF_START(t0);
    if (!d->win || !d->img) return;
    src_offset(d, &sx, &sy);
    x0 = b->x0 - sx; y0 = b->y0 - sy; x1 = b->x1 - sx; y1 = b->y1 - sy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d->win->Width) x1 = d->win->Width;
    if (y1 > d->win->Height) y1 = d->win->Height;
    if (x1 > x0 && y1 > y0)
        BltBitMapRastPort(d->img, x0 + sx, y0 + sy, d->win->RPort, x0, y0, x1 - x0, y1 - y0, 0xC0);
    PROF_STOP(5, t0);
}

/* Only box b of the dock image again (a glide frame), shown. */
void dock_compose_box(struct Dock *d, struct Box *b)
{
    struct RastPort rp;
    PROF_START(t0);
    box_clip(d, b);
    if (!d->img || b->x1 <= b->x0 || b->y1 <= b->y0) return;
    box_cells(d, b);                             /* whole cells: a blend is never drawn twice */
    dz_lock();
    BltBitMap(d->bg, b->x0, b->y0, d->img, b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = d->img;
    render_items_in(d, &rp, b);
    WaitBlit();
    dz_unlock();
    dock_show_box(d, b);
    PROF_STOP(1, t0);
}

/* The dock image from the background and the icons where they are now. */
void dock_compose(struct Dock *d)
{
    struct RastPort rp;
    PROF_START(t0);
    if (!d->img) return;
    dz_lock();
    BltBitMap(d->bg, 0, 0, d->img, 0, 0, d->w, d->h, 0xC0, 0xFF, NULL);
    InitRastPort(&rp);
    rp.BitMap = d->img;
    render_items(d, &rp, NULL, FALSE);
    WaitBlit();
    dz_unlock();
    dock_show(d);
    PROF_STOP(1, t0);
}

/* An icon was added or removed: resize the window in place instead of closing
   and reopening it, which would flash the screen behind and reload every icon.
   The background is reused from the copy under the old dock and read from the
   screen for the new parts. Returns FALSE when that isn't possible (hidden,
   sliding, no memory); the caller then reopens. */
static BOOL dock_relayout(struct Dock *d)
{
    WORD ol = d->left, ot = d->top, ow = d->w, oh = d->h, x0, y0, x1, y1;
    struct BitMap *sbm, *oraw = d->raw, *nraw;
    run_list_close(d);                           /* (its place changes with the icons) */

    if (!d->win || !oraw || d->hidden || d->vis != d->full || d->dragging || d->stripshown) return FALSE;
    sbm = d->scr->RastPort.BitMap;
    tip_hide(d);
    an_stop(d);
    if (d->appwin) { dz_remove(d); RemoveAppWindow(d->appwin); d->appwin = NULL; }
    dz_lock();                                   /* the zone hook shares the icon caches */
    layout(d);                                   /* loads only the new icons */
    d->vis = d->full;
    d->hover = d->tipcell = -1;
    d->pressed = NULL;
    if (!(nraw = AllocBitMap(d->w, d->h, GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES, sbm))) {
        dz_unlock();
        return FALSE;
    }
    LockLayerInfo(&d->scr->LayerInfo);          /* around the old dock: the screen */
    BltBitMap(sbm, d->left, d->top, nraw, 0, 0, d->w, d->h, 0xC0, 0xFF, NULL);
    UnlockLayerInfo(&d->scr->LayerInfo);
    x0 = ol > d->left ? ol : d->left;            /* under it: the copy taken before */
    y0 = ot > d->top ? ot : d->top;
    x1 = ol + ow < d->left + d->w ? ol + ow : d->left + d->w;
    y1 = ot + oh < d->top + d->h ? ot + oh : d->top + d->h;
    if (x1 > x0 && y1 > y0)
        BltBitMap(oraw, x0 - ol, y0 - ot, nraw, x0 - d->left, y0 - d->top, x1 - x0, y1 - y0, 0xC0, 0xFF, NULL);
    WaitBlit();
    render_free(d);                              /* old raw, background, image, strip */
    d->raw = nraw;
    if (!render_background(d) || !render_image(d)) { dz_unlock(); return FALSE; }
    dz_unlock();
    if (d->left == ol && d->top == ot && d->w == ow && d->h == oh) dock_show(d);   /* same box: no IDCMP_CHANGEWINDOW comes */
    else {
        d->relayout = TRUE;                      /* IDCMP_CHANGEWINDOW shows it all */
        ChangeWindowBox(d->win, d->left, d->top, d->w, d->h);
        in_poke();
    }
    d->gen++;                                    /* drops queued for the old layout are ignored */
    if (am_port()) d->appwin = AddAppWindow(0, d->gen, d->win, am_port(), TAG_END);
    dz_add(d);
    return TRUE;
}

/* The icons changed while auto-hide keeps the dock away (a program started or ended):
   laid out again out of sight, with a new snapshot of the screen under it,
   so it comes out finished. The strip at the edge moves with it. */
BOOL dock_relayout_hidden(struct Dock *d)
{
    struct Item *it;
    if (!d->win || d->vis != 0 || d->dragging || d->lmb) return FALSE;
    tip_hide(d);
    an_stop(d);
    run_list_close(d);
    dz_remove(d);                                /* (none while hidden; to be sure) */
    dz_lock();
    layout(d);                                   /* loads only the new icons */
    d->vis = 0;
    d->hover = d->tipcell = -1;
    d->pressed = NULL;
    FOR_ITEMS(d, it) it->keep = FALSE;           /* (no glide: nobody sees it) */
    if (d->strip) { WaitBlit(); FreeBitMap(d->strip); d->strip = NULL; }   /* (made again for the new size) */
    if (!render_resnap(d)) {                     /* no memory: the old images don't fit any more */
        dz_unlock();
        dock_reopen(d);
        return TRUE;
    }
    dz_unlock();
    ah_hidden_moved(d);                          /* the strip window to its new place */
    return TRUE;
}

/* Reopen with the new size and background. The icons glide from where they
   were on screen (new icons from their sx, sy if keep is set). */
void dock_reopen_glide(struct Dock *d)
{
    struct Item *it;
    dz_suspend();                                /* the zone hook writes sx / keep too */
    FOR_ITEMS(d, it)
        if (!it->keep && it->dobj && !it->over) { it->sx = d->left + it->x; it->sy = d->top + it->y; it->keep = TRUE; }
    if (!dock_relayout(d)) dock_reopen(d);
    if (!d->win) return;
    FOR_ITEMS(d, it) {
        if (it->keep) {                          /* from where it was, but inside the new dock */
            it->x = it->sx - d->left; it->y = it->sy - d->top;
#ifdef SD_DEBUG
            { extern LONG render_clamps;
              if (it->x < 0 || it->y < 0 || it->x > d->w - it->w || it->y > d->h - it->h) render_clamps += 1000; }
#endif
            if (it->x < 0) it->x = 0;
            if (it->y < 0) it->y = 0;
            if (it->x > d->w - it->w) it->x = d->w - it->w;
            if (it->y > d->h - it->h) it->y = d->h - it->h;
        }
        it->keep = FALSE;
    }
    dock_compose(d);                             /* (drawn where they start: not at both places) */
    an_start(d, GLIDE_MS);
}

void dock_show_item(struct Dock *d, struct Item *it, BOOL selected)
{
    struct RastPort rp;
    WORD sx, sy;
    InitRastPort(&rp);
    rp.BitMap = d->img;
    dz_lock();
    render_items(d, &rp, it, selected);
    dz_unlock();
    src_offset(d, &sx, &sy);
    {
        struct Box b = { 0, 0, 0, 0 };
        item_box(d, it, &b);                     /* (kept inside the image, as drawn) */
        box_clip(d, &b);
        if (b.x1 > b.x0 && b.y1 > b.y0)
            BltBitMapRastPort(d->img, b.x0, b.y0, d->win->RPort, b.x0 - sx, b.y0 - sy, b.x1 - b.x0, b.y1 - b.y0, 0xC0);
    }
}

static struct Item *hit(struct Dock *d, WORD x, WORD y)
{
    struct Item *it;
    FOR_ITEMS(d, it)
        if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return it;
    return NULL;
}

/* Reopen with new settings / layout (also takes a fresh background snapshot). */
void dock_reopen(struct Dock *d)
{
    dock_close(d);
    if (!dock_open(d)) sd_msg(d, "Cannot reopen the dock");
    else { ah_start(d); run_check(d); }
}


void dock_set_pos(struct Dock *d, char edge)
{
    char al = d->o->pos && d->o->pos[0] ? ToUpper(d->o->pos[1]) : 0;
    BOOL horiz = edge == 'T' || edge == 'B';
    d->posbuf[0] = edge;               /* keep an alignment that fits the new edge */
    d->posbuf[1] = (horiz ? al == 'L' || al == 'R' : al == 'T' || al == 'B') ? al : 0;
    d->posbuf[2] = 0;
    d->o->pos = d->posbuf;
}

/* The dock never keeps the focus. A click makes it the active window, leaving
   an empty menu bar, so on release the window that was active before gets the
   focus back (or Workbench's, if that window is gone). */
static void unfocus(struct Dock *d)
{
    extern struct Window *volatile in_prevwin;
    struct Window *want = in_prevwin, *found = NULL, *wb = NULL, *w;
    struct Screen *s;

    if (!d->win || !(d->win->Flags & WFLG_WINDOWACTIVE)) return;
    Forbid();                                    /* (its program can't close it between the look and the call) */
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        for (w = s->FirstWindow; w; w = w->NextWindow) {
            if (w == want) found = w;
            if (!wb && s == d->scr && (w->Flags & WFLG_WBENCHWINDOW) && (w->Flags & WFLG_BACKDROP)) wb = w;
        }
    if (found && LayersBase->lib_Version >= 45 && found->WLayer && (found->WLayer->Flags & LAYERHIDDEN))
        found = NULL;                            /* (minimised: never made active by the dock - that shows it) */
    if (!found) found = wb;
    if (found == d->win || found == d->tip) found = NULL;
    if (found) ActivateWindow(found);
    Permit();
}

/* Is window w covered by a window in front of it (not ours, not a hidden
   one)? Under the screen's layer lock. */
static BOOL covered(struct Dock *d, struct Window *w, struct Window *pw)
{
    struct Layer *me = w->WLayer, *l;
    struct Task *self = FindTask(NULL);
    for (l = me->front; l; l = l->front) {
        struct Window *x = (struct Window *)l->Window;
        if ((d->tip && l == d->tip->WLayer) || (d->list && l == d->list->WLayer) || l == d->win->WLayer) continue;
        if (pw && l == pw->WLayer) continue;     /* the settings window stays in front */
        if (x && x->UserPort && x->UserPort->mp_SigTask &&   /* (ours: About, colour picker, the copy's */
            (x->UserPort->mp_Flags & PF_ACTION) != PA_SOFTINT &&   /*  requesters; a soft interrupt port */
            (x->UserPort->mp_SigTask == (APTR)self ||               /*  has no task) */
             (((struct Task *)x->UserPort->mp_SigTask)->tc_Node.ln_Name &&
              !strcmp(((struct Task *)x->UserPort->mp_SigTask)->tc_Node.ln_Name, "SimpleDock copy")))) continue;
        if (LayersBase->lib_Version >= 45 && (l->Flags & LAYERHIDDEN)) continue;   /* (a minimised window) */
        if (l->bounds.MinX <= me->bounds.MaxX && l->bounds.MaxX >= me->bounds.MinX &&
            l->bounds.MinY <= me->bounds.MaxY && l->bounds.MaxY >= me->bounds.MinY) return TRUE;
    }
    return FALSE;
}

/* ONTOP: if a window covers part of the dock (or its held list), bring the
   dock, its label and the list to the front. Never while the button is
   held (that would cancel a Workbench icon drag), while the settings window
   is open (its lists drop down over the dock), or for the hidden dock's
   edge strip. At most 3 times without a key or button used in between:
   another window that keeps itself in front (a second dock) would make
   both go on forever. */
static BOOL forced;                     /* dock_raise_now: also with the settings window open */

BOOL dock_raise(struct Dock *d)
{
    static UBYTE raised;
    BOOL cov;
    struct Window *pw = am_prefs_win();

    if (d->userin) { d->userin = FALSE; raised = 0; }
    if (!d->o->ontop || !d->win || d->lmb || d->dragging || (pw && !forced) || raised >= 3 ||
        (d->vis == 0 && !d->want))
        return FALSE;
    LockLayerInfo(&d->scr->LayerInfo);
    cov = covered(d, d->win, pw) || (d->list && covered(d, d->list, pw));
    UnlockLayerInfo(&d->scr->LayerInfo);
    if (!cov) return FALSE;
    raised++;
    WindowToFront(d->win);
    if (d->tip) WindowToFront(d->tip);
    if (d->list) WindowToFront(d->list);
    if (pw) WindowToFront(pw);                   /* (the settings window stays in front) */
    return TRUE;
}

/* In front was just switched on in the settings window: now. */
void dock_raise_now(struct Dock *d)
{
    forced = TRUE;
    dock_raise(d);
    forced = FALSE;
}

/* Which windows lie behind the dock, and where. The snapshot behind the dock
   (corners, edge, transparent background) is right as long as this stays the
   same. */
ULONG dock_behind(struct Dock *d)
{
    struct Layer *l;
    ULONG sig = 0;
    WORD x0 = d->left, y0 = d->top, x1 = d->left + d->w - 1, y1 = d->top + d->h - 1;   /* the whole dock */
    if (!d->win) return 0;
    LockLayerInfo(&d->scr->LayerInfo);
    for (l = d->win->WLayer->back; l; l = l->back)
        if (l->bounds.MinX <= x1 && l->bounds.MaxX >= x0 && l->bounds.MinY <= y1 && l->bounds.MaxY >= y0)
            sig = sig * 31 + (ULONG)l + ((ULONG)l->bounds.MinX << 16) + l->bounds.MinY +
                  ((ULONG)l->bounds.MaxX << 8) + ((ULONG)l->bounds.MaxY << 20);
    UnlockLayerInfo(&d->scr->LayerInfo);
    return sig;
}

/* Called shortly after a button release: bring the dock back to the front if
   wanted, and take a new snapshot if the windows behind it changed. Right
   after raising, Intuition hasn't moved the window yet, so the check waits for
   the next round. */
void dock_settle(struct Dock *d)
{
    if (d->peekwait) {                           /* (back after a snapshot: and in front if wanted) */
        if (!d->lmb) { ah_peek_end(d); if (dock_raise(d)) in_settle_again(); }
        return;
    }
    if (dock_raise(d)) { in_settle_again(); return; }
    if (!d->win || d->lmb || d->dragging || an_running() || d->vis != d->full) return;
    if (dock_behind(d) != d->behind) ah_peek(d);
}

/* While the button is held on an icon the dock gets timer ticks (only then:
   they would wake it up for nothing otherwise); held for HOLDTIME tenths of
   a second (ticks come ten a second): the list of its windows. */
static void hold_ticks(struct Dock *d, BOOL on)
{
    ULONG f = d->win->IDCMPFlags;
    d->holdticks = 0;
    if (on != !!(f & IDCMP_INTUITICKS)) ModifyIDCMP(d->win, on ? f | IDCMP_INTUITICKS : f & ~IDCMP_INTUITICKS);
}

BOOL dock_event(struct Dock *d, struct IntuiMessage *im)
{
    struct Item *it;
    BOOL up = im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTUP;
    WORD sx = d->win->LeftEdge + im->MouseX, sy = d->win->TopEdge + im->MouseY;   /* on the screen */
    switch (im->Class) {
    case IDCMP_REFRESHWINDOW:
        BeginRefresh(d->win);
        dock_show(d);
        EndRefresh(d->win, TRUE);
        break;
    case IDCMP_CHANGEWINDOW:                  /* auto-hide or dock_relayout moved/resized the window: */
        /* all of it again (a newly uncovered part would show the empty
           window for a frame; dock_show also draws the hidden strip) */
        dock_show(d);
        if (d->relayout) { d->relayout = FALSE; d->behind = dock_behind(d); }
        break;
    case IDCMP_INTUITICKS:
        if (d->pressed && !d->dragging && !d->list && ++d->holdticks == d->o->holdtime) run_hold(d, d->pressed);
        break;
    case IDCMP_MOUSEMOVE:
        if (d->list && !d->liststicky && d->pressed && d->pressed->temp != RUN_MORE && d->pressed->temp != RUN_WB &&
            !d->pressed->mini) {
            WORD along = d->horiz ? im->MouseX - d->downx : im->MouseY - d->downy;
            WORD across = d->horiz ? im->MouseY - d->downy : im->MouseX - d->downx;
            if (along < 0) along = -along;
            if (across < 0) across = -across;
            if (along > across && along > (d->horiz ? d->cw : d->ch) / 2 &&   /* (towards the list: no) */
                im->MouseX >= 0 && im->MouseY >= 0 && im->MouseX < d->w && im->MouseY < d->h) {
                run_list_close(d);                /* held, then moved along the dock: a drag after all */
                d->fromlist = TRUE;
            }
        }
        if (d->list) run_list_move(d, sx, sy);    /* the held list: no drag */
        else drag_event(d, im);
        break;
    case IDCMP_MOUSEBUTTONS:
        if (up) hold_ticks(d, FALSE);
        if (up && d->list) {                  /* the held list: the window picked comes to the front */
            if ((it = d->pressed)) { d->pressed = NULL; dock_show_item(d, it, FALSE); }
            unfocus(d);
            up = FALSE;
            run_list_pick(d, sx, sy);
            break;
        }
        if (drag_event(d, im)) break;         /* a drag ended (layers unlocked again) - also mid-glide */
        if (d->vis != d->full || an_running()) {      /* sliding or gliding: no clicks */
            if (up && d->pressed) { dock_show_item(d, d->pressed, FALSE); d->pressed = NULL; }
            break;
        }
        if (im->Code == SELECTDOWN && d->list && d->liststicky) {   /* a list left open: this click only closes */
            run_list_close(d);                   /* it (an icon under it is not clicked: after picking */
            break;                               /* windows to minimise, it would show them all again) */
        }
        if (im->Code == SELECTDOWN && (it = hit(d, im->MouseX, im->MouseY))) {
            d->pressed = it;
            d->fromlist = FALSE;
            d->downx = im->MouseX; d->downy = im->MouseY;
            tip_hide(d);
            dock_show_item(d, it, TRUE);
            if (it->temp == RUN_MORE) run_hold(d, it);   /* the "more" cell: its list at once */
            else if (run_trackable(it)) hold_ticks(d, TRUE);
        } else if (im->Code == SELECTUP && d->pressed) {
            it = d->pressed;
            d->pressed = NULL;
            dock_show_item(d, it, FALSE);
            if (hit(d, im->MouseX, im->MouseY) == it) {
                d->last = it;
                unfocus(d);                   /* before the program opens its window */
                up = FALSE;
                /* running: to the front, unless a new copy is asked for (Shift; with
                   CLICKNEW the other way round) */
                if (it->temp == RUN_MORE) ;
                else if (it->temp == RUN_WB) run_front(d, it);   /* (no new copy of Workbench) */
                else if (run_is_drawer(it)) { if (!run_front(d, it)) launch_item(d, it); }   /* (open: to the front) */
                else if (!!(im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != d->o->clicknew ||
                         !run_front(d, it))
                    launch_item(d, it);
                run_clicked(d);
            }
            if (d->o->noicon) tip_update(d);  /* the label under the pointer: highlighted again */
        }
        break;
    case IDCMP_INACTIVEWINDOW:
        hold_ticks(d, FALSE);
        if (!d->liststicky) run_list_close(d);   /* (one that stays open: closed by its own input) */
        drag_cancel(d, TRUE);
        if (d->pressed) { dock_show_item(d, d->pressed, FALSE); d->pressed = NULL; }
        break;
    }
    if (up) unfocus(d);
    return TRUE;
}
