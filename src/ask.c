/* SimpleDock - "Execute a file": the command of a Shell icon, to edit first
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   Workbench asks this before it starts an icon set to start from a Shell
   (unless the icon has DONOTPROMPT); Workbench's own requester can't be
   called from outside, so with SHELLASK the dock asks with this one. A
   text line, Execute and Cancel. Modal: the dock waits for the answer, as
   for its other requesters. */
#include <string.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <intuition/sghooks.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include "sd.h"

enum { A_TEXT, A_EXEC, A_CANCEL };

/* Edit hook of the text line: Esc ends it as Cancel (code 27); keys typed
   in it never reach the window. Runs in input.device's task: no library
   calls. */
static ULONG esc_hook(__reg("a0") struct Hook *h, __reg("a2") struct SGWork *sgw, __reg("a1") ULONG *msg)
{
    if (*msg == SGH_KEY && sgw->IEvent && sgw->IEvent->ie_Code == 0x45) {   /* Esc */
        sgw->Actions = (sgw->Actions | SGA_END) & ~SGA_USE;
        sgw->Code = 27;
    }
    return ~0UL;
}
static struct Hook eschook = { { NULL, NULL }, (ULONG (*)())esc_hook, NULL, NULL };

/* Ask for the command in buf (shown, then edited there). TRUE: Execute. */
BOOL ask_command(struct Dock *d, char *buf, LONG size)
{
    static const char prompt[] = "Enter command and its arguments:";
    struct Screen *s, *own;
    struct Window *w = NULL;
    struct Gadget *list = NULL, *g, *str = NULL;
    struct NewGadget ng;
    APTR vi = NULL;
    LONG fh, iw, ih, bw, tw;
    BOOL done = FALSE, ok = FALSE;

    if (!(s = d->scr) || (own = LockPubScreen(d->o->pubscreen)) != s) {   /* (its own lock on the dock's screen) */
        if (s && own) UnlockPubScreen(NULL, own);
        return FALSE;
    }
    if (!(vi = GetVisualInfo(s, TAG_END))) goto out;
    fh = s->Font->ta_YSize;
    tw = TextLength(&s->RastPort, (STRPTR)prompt, sizeof(prompt) - 1);
    bw = TextLength(&s->RastPort, "Execute", 7) + 24;
    iw = tw + 20 > 360 ? tw + 20 : 360;
    if (iw > s->Width - s->WBorLeft - s->WBorRight) iw = s->Width - s->WBorLeft - s->WBorRight;
    ih = 8 + fh + 6 + fh + 6 + 8 + fh + 6 + 6;
    if (!(g = CreateContext(&list))) goto out;
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = s->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = s->WBorLeft + 10;
    ng.ng_TopEdge = s->WBorTop + fh + 1 + 8 + fh + 6;
    ng.ng_Width = iw - 20;
    ng.ng_Height = fh + 6;
    ng.ng_GadgetID = A_TEXT;
    g = str = CreateGadget(STRING_KIND, g, &ng, GTST_String, buf, GTST_MaxChars, size - 1, GTST_EditHook, &eschook,
                           TAG_END);
    ng.ng_TopEdge += fh + 6 + 8;
    ng.ng_Width = bw;
    ng.ng_GadgetText = (STRPTR)"Execute";
    ng.ng_GadgetID = A_EXEC;
    ng.ng_Flags = PLACETEXT_IN;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    ng.ng_LeftEdge = s->WBorLeft + iw - 10 - bw;
    ng.ng_GadgetText = (STRPTR)"Cancel";
    ng.ng_GadgetID = A_CANCEL;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    if (!g) goto out;
    w = OpenWindowTags(NULL, WA_PubScreen, s, WA_Title, "Execute a file",
                       WA_Left, (s->Width - iw - s->WBorLeft - s->WBorRight) / 2,
                       WA_Top, (s->Height - ih - s->WBorTop - fh - 1 - s->WBorBottom) / 2,
                       WA_InnerWidth, iw, WA_InnerHeight, ih, WA_Gadgets, list,
                       WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                       WA_RMBTrap, TRUE, WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY |
                       IDCMP_REFRESHWINDOW, TAG_END);
    if (!w) goto out;
    GT_RefreshWindow(w, NULL);
    SetAPen(w->RPort, 1);                        /* (the screen's text pen) */
    SetFont(w->RPort, w->WScreen->RastPort.Font);
    Move(w->RPort, s->WBorLeft + 10, s->WBorTop + fh + 1 + 8 + w->RPort->TxBaseline);
    Text(w->RPort, (STRPTR)prompt, sizeof(prompt) - 1);
    ((struct StringInfo *)str->SpecialInfo)->BufferPos = strlen(buf);   /* (the cursor at the end) */
    ActivateGadget(str, w, NULL);
    while (!done) {
        struct IntuiMessage *im;
        WaitPort(w->UserPort);
        while ((im = GT_GetIMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *gad = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(w); GT_EndRefresh(w, TRUE); }
            else if (class == IDCMP_CLOSEWINDOW) done = TRUE;
            else if (class == IDCMP_VANILLAKEY && code == 27) done = TRUE;   /* Esc */
            else if (class == IDCMP_VANILLAKEY && code == 13) done = ok = TRUE;   /* Return */
            else if (class == IDCMP_GADGETUP) {
                if (gad->GadgetID == A_TEXT && code == 27) done = TRUE;   /* Esc (esc_hook) */
                else if (gad->GadgetID == A_EXEC || (gad->GadgetID == A_TEXT && code == 0)) done = ok = TRUE;
                else if (gad->GadgetID == A_CANCEL) done = TRUE;
            }
        }
    }
    if (ok) {
        strncpy(buf, (char *)((struct StringInfo *)str->SpecialInfo)->Buffer, size - 1);
        buf[size - 1] = 0;
    }
out:
    if (w) CloseWindow(w);
    if (list) FreeGadgets(list);
    if (vi) FreeVisualInfo(vi);
    UnlockPubScreen(NULL, own);                  /* (after FreeVisualInfo) */
    return ok && buf[0];
}
