/* SimpleDock - the colour picker of the settings window ("Custom...")
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   Red, green and blue sliders, the colour as #rrggbb and a preview box,
   OK / Cancel. Not modal: the dock keeps working while it is open. The
   preview is drawn with a pen of that colour (on a screen with few free
   pens: the nearest one). */
#include <string.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include "sd.h"

enum { P_RED, P_GREEN, P_BLUE, P_HEX, P_OK = 10, P_CANCEL };
static struct Window *pkw;
static APTR pvi;
static struct Gadget *plist, *slider[3], *hexg;
static struct ColorMap *pcm;
static LONG level[3], pen = -1;
static WORD bx, by, bw, bh;             /* the preview box */
static struct Screen *pscr;             /* the picker's own lock on its screen */
static char hex[8];

void pick_close(void)
{
    if (pen >= 0 && pcm) ReleasePen(pcm, pen);
    pen = -1;
    if (pkw) { CloseWindow(pkw); pkw = NULL; }
    if (plist) { FreeGadgets(plist); plist = NULL; }
    if (pvi) { FreeVisualInfo(pvi); pvi = NULL; }
    if (pscr) { UnlockPubScreen(NULL, pscr); pscr = NULL; }   /* (after FreeVisualInfo) */
}

ULONG pick_sig(void) { return pkw ? 1UL << pkw->UserPort->mp_SigBit : 0; }

/* The preview box and the #rrggbb text for the slider levels. */
static void preview(void)
{
    struct RastPort *rp = pkw->RPort;
    LONG v[3];
    if (pen >= 0) ReleasePen(pcm, pen);
    pen = ObtainBestPen(pcm, (ULONG)level[0] * 0x01010101UL, (ULONG)level[1] * 0x01010101UL,
                        (ULONG)level[2] * 0x01010101UL, OBP_Precision, PRECISION_EXACT, TAG_END);
    if (pen >= 0) {
        SetAPen(rp, pen);
        RectFill(rp, bx + 2, by + 1, bx + bw - 3, by + bh - 2);
    }
    DrawBevelBox(rp, bx, by, bw, bh, GT_VisualInfo, pvi, GTBB_Recessed, TRUE, TAG_END);
    v[0] = level[0]; v[1] = level[1]; v[2] = level[2];
    SD_FMT(hex, "#%02lx%02lx%02lx", v);
    GT_SetGadgetAttrs(hexg, pkw, NULL, GTTX_Text, hex, TAG_END);
}

/* Open it on the dock's screen (or bring it to the front), starting at rgb. */
BOOL pick_open(struct Dock *d, ULONG rgb)
{
    static const char *const name[3] = { "Red", "Green", "Blue" };
    struct Screen *s = d->scr;
    struct NewGadget ng;
    struct Gadget *g;
    LONG i, fh, lw = 0, top, iw, ih, sw, step, btw;

    if (pkw) { WindowToFront(pkw); ActivateWindow(pkw); return TRUE; }
    if (s && (pscr = LockPubScreen(d->o->pubscreen)) != s) {   /* (its own lock on the dock's screen) */
        if (pscr) UnlockPubScreen(NULL, pscr);
        pscr = NULL;
        return FALSE;
    }
    if (!s || !(pvi = GetVisualInfo(s, TAG_END))) { pick_close(); return FALSE; }
    pcm = s->ViewPort.ColorMap;
    level[0] = rgb >> 16 & 255; level[1] = rgb >> 8 & 255; level[2] = rgb & 255;
    fh = s->Font->ta_YSize;
    for (i = 0; i < 3; i++)
        if (TextLength(&s->RastPort, (STRPTR)name[i], strlen(name[i])) > lw)
            lw = TextLength(&s->RastPort, (STRPTR)name[i], strlen(name[i]));
    top = s->WBorTop + fh + 1 + 6;
    step = fh + 6;
    sw = 160;                                    /* slider */
    btw = TextLength(&s->RastPort, "Cancel", 6) + 24;
    bw = 3 * step - 4; bh = 3 * step - 6;
    iw = 10 + lw + 8 + sw + 8 + 4 * fh + 12 + bw + 10;   /* labels, sliders, level text, preview */
    ih = top - s->WBorTop - fh - 1 + 4 * step + 6 + fh + 4 + 6;
    if (!(g = CreateContext(&plist))) { pick_close(); return FALSE; }
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = s->Font;
    ng.ng_VisualInfo = pvi;
    for (i = 0; i < 3 && g; i++) {
        ng.ng_LeftEdge = s->WBorLeft + 10 + lw + 8;
        ng.ng_TopEdge = top + i * step;
        ng.ng_Width = sw; ng.ng_Height = fh + 4;
        ng.ng_GadgetText = (STRPTR)name[i];
        ng.ng_GadgetID = P_RED + i;
        ng.ng_Flags = PLACETEXT_LEFT;
        g = slider[i] = CreateGadget(SLIDER_KIND, g, &ng, GTSL_Min, 0, GTSL_Max, 255, GTSL_Level, level[i],
                                     GTSL_LevelFormat, "%3ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT,
                                     GA_RelVerify, TRUE, TAG_END);
    }
    if (g) {                                     /* the colour as text, under the sliders */
        ng.ng_TopEdge = top + 3 * step;
        ng.ng_GadgetText = NULL;
        ng.ng_GadgetID = P_HEX;
        g = hexg = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, "", GTTX_Border, TRUE, TAG_END);
    }
    for (i = 0; i < 2 && g; i++) {
        ng.ng_LeftEdge = s->WBorLeft + 10 + i * (iw - 20 - btw);
        ng.ng_TopEdge = top + 4 * step + 6;
        ng.ng_Width = btw; ng.ng_Height = fh + 4;
        ng.ng_GadgetText = (STRPTR)(i ? "Cancel" : "OK");
        ng.ng_GadgetID = P_OK + i;
        ng.ng_Flags = PLACETEXT_IN;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    }
    if (!g) { pick_close(); return FALSE; }
    pkw = OpenWindowTags(NULL, WA_PubScreen, s, WA_Title, "Custom colour", WA_InnerWidth, iw, WA_InnerHeight, ih,
                         WA_Left, (s->Width - iw) / 2, WA_Top, (s->Height - ih - fh) / 2 > 0 ? (s->Height - ih - fh) / 2 : 0,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SimpleRefresh, TRUE, WA_Gadgets, plist,
                         WA_IDCMP, SLIDERIDCMP | TEXTIDCMP | BUTTONIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
                         TAG_END);
    if (!pkw) { pick_close(); return FALSE; }
    bx = s->WBorLeft + iw - 10 - bw;
    by = top;
    GT_RefreshWindow(pkw, NULL);
    preview();
    return TRUE;
}

/* Its messages: 0 still open, 1 OK (*rgb: the colour; closed), -1 cancelled (closed). */
LONG pick_handle(ULONG *rgb)
{
    struct IntuiMessage *im;
    LONG r = 0;
    while (pkw && !r && (im = GT_GetIMsg(pkw->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        GT_ReplyIMsg(im);
        if (class == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(pkw);
            GT_EndRefresh(pkw, TRUE);
            preview();
        } else if (class == IDCMP_CLOSEWINDOW) r = -1;
        else if ((class == IDCMP_MOUSEMOVE || class == IDCMP_GADGETUP) &&
                 (g == slider[0] || g == slider[1] || g == slider[2])) {
            level[g == slider[0] ? 0 : g == slider[1] ? 1 : 2] = code;   /* a slider: its new level */
            preview();
        } else if (class == IDCMP_GADGETUP && g->GadgetID == P_OK) {
            *rgb = (ULONG)level[0] << 16 | (ULONG)level[1] << 8 | (ULONG)level[2];
            r = 1;
        } else if (class == IDCMP_GADGETUP && g->GadgetID == P_CANCEL) r = -1;
    }
    if (r) pick_close();
    return r;
}
