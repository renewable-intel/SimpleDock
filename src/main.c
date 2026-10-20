/* SimpleDock - a small application dock for AmigaOS 3.x
   Copyright (C) 2026 renewable-intel

   This program is free software: you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free Software
   Foundation, either version 3 of the License, or (at your option) any later
   version. It is distributed WITHOUT ANY WARRANTY; see the LICENSE file.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>
#include "sd.h"

extern struct WBStartup *WBenchMsg;

static const char version[] = "$VER: " SD_NAME " " SD_VERSION SD_BUILD " (" SD_DATE ")";

/* vbcc startup: switch to a stack of at least this size (a Shell gives 4 KB). */
unsigned long __stack = 32768;

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LayersBase, *IconBase, *WorkbenchBase, *UtilityBase;
struct RxsLib *RexxSysBase;
struct Library *DiskfontBase, *P96Base, *AslBase, *CxBase, *GadToolsBase, *ChooserBase, *StringBase;

static struct Dock dock;

#ifdef SD_DEBUG
/* trace line with a time stamp (1/50 s since midnight), flushed */
void sd_trace(CONST_STRPTR s)
{
    struct DateStamp ds;
    LONG v[2];
    DateStamp(&ds);
    v[0] = ds.ds_Minute * 3000 + ds.ds_Tick; v[1] = (LONG)s;
    VPrintf("trace: %ld %s\n", v);
    Flush(Output());
}

ULONG sd_prof_us[12], sd_prof_n[12];

/* slot -1: start (t0 = now); else add the time since t0 to the slot */
void sd_prof(LONG slot, struct timeval *t0)
{
    extern struct Device *TimerBase;
    struct timeval now;
    if (!TimerBase) { t0->tv_secs = t0->tv_micro = 0; return; }
    GetSysTime(&now);
    if (slot < 0) { *t0 = now; return; }
    if (!t0->tv_secs && !t0->tv_micro) return;
    SubTime(&now, t0);
    sd_prof_us[slot] += now.tv_secs * 1000000 + now.tv_micro;
    sd_prof_n[slot]++;
}
#endif

char *sd_strdup(CONST_STRPTR s)
{
    LONG n = strlen(s) + 1;
    char *p = AllocVec(n, MEMF_ANY);
    if (p) memcpy(p, s, n);
    return p;
}

static void vmsg(struct Dock *d, CONST_STRPTR fmt, APTR args)
{
    if (!WBenchMsg) {
        PutStr(SD_NAME ": ");
        VPrintf((STRPTR)fmt, args);
        PutStr("\n");
    } else if (IntuitionBase) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME, NULL, "Ok" };
        es.es_TextFormat = (STRPTR)fmt;
        EasyRequestArgs(d && d->win ? d->win : NULL, &es, NULL, args);
    }
}

void sd_msg(struct Dock *d, CONST_STRPTR fmt, ...)
{
    if (d && d->o && d->o->quiet) return;
    vmsg(d, fmt, (APTR)(&fmt + 1));
}

void sd_verbose(struct Dock *d, CONST_STRPTR fmt, ...)
{
    if (!d || !d->o || !d->o->verbose || d->o->quiet) return;
    vmsg(d, fmt, (APTR)(&fmt + 1));
}

/* Minimum versions are those of the calls used (icon/workbench V44 calls). */
static CONST_STRPTR open_libs(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase       = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    LayersBase    = OpenLibrary("layers.library", 39);
    IconBase      = OpenLibrary("icon.library", 44);
    WorkbenchBase = OpenLibrary("workbench.library", 44);
    UtilityBase   = OpenLibrary("utility.library", 39);
    RexxSysBase   = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 36);
    DiskfontBase  = OpenLibrary("diskfont.library", 39);
    P96Base       = OpenLibrary("Picasso96API.library", 2);
    AslBase       = OpenLibrary("asl.library", 38);
    CxBase        = OpenLibrary("commodities.library", 38);   /* the hotkey's description (ParseIX) */
    GadToolsBase  = OpenLibrary("gadtools.library", 39);      /* the settings window */
    ChooserBase   = OpenLibrary("gadgets/chooser.gadget", 44);   /* its lists (else cycle gadgets) */
    StringBase    = OpenLibrary("gadgets/string.gadget", 47);    /* its text fields (double-click marks all; */
                                                                 /* OS 3.2's: older ones draw no text) */
    return !IntuitionBase ? "intuition.library 39" : !GfxBase ? "graphics.library 39" :
           !LayersBase ? "layers.library 39" : !IconBase ? "icon.library 44" :
           !WorkbenchBase ? "workbench.library 44" : !UtilityBase ? "utility.library 39" : NULL;
}

void sd_close_libs(void)
{
    CloseLibrary(ChooserBase);
    CloseLibrary(StringBase);
    CloseLibrary(GadToolsBase);
    CloseLibrary(CxBase);
    CloseLibrary(AslBase);
    CloseLibrary(P96Base);
    CloseLibrary(DiskfontBase);
    CloseLibrary((struct Library *)RexxSysBase);
    CloseLibrary(UtilityBase);
    CloseLibrary(WorkbenchBase);
    CloseLibrary(IconBase);
    CloseLibrary(LayersBase);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
}

int main(void)
{
    struct Opts o;
    struct RDArgs *rd = NULL;
    struct MsgPort *rexx = NULL;
    struct Dock *d = &dock;
    int rc = RETURN_FAIL;
    BOOL run = TRUE;
    CONST_STRPTR missing;
    ULONG ptrsig = 0, timersig = 0, menusig = 0, animsig = 0, runsig = 0;

    if (!(SysBase->AttnFlags & AFF_68020)) {     /* (built for the 68020: a 68000/010 would crash) */
        if (!WBenchMsg) PutStr(SD_NAME " needs a 68020 or better.\n");
        return RETURN_FAIL;
    }
    strcpy(d->portname, SD_PORTNAME);
    NewList((struct List *)&d->items);
    NewList((struct List *)&d->extra);
    NewList((struct List *)&d->runs);
    TRACE("start");
    if ((missing = open_libs())) {
        if (!WBenchMsg) Printf(SD_NAME ": cannot open %s\n", (LONG)missing);
        goto out;
    }
    if (!(rd = opts_read(&o, d->portname, sizeof(d->portname)))) {
        PrintFault(IoErr(), SD_NAME);
        goto out;
    }
    d->o = &o;
    if (strlen(o.config) >= sizeof(d->cfgbuf)) { sd_msg(d, "The CONFIG path is too long"); goto out; }
    strcpy(d->cfgbuf, o.config);                 /* o's strings change on reload */
    o.config = d->cfgbuf;
    crash_context(d, &rexx, &rd);              /* (CRASHLOG: what a crash must close) */
    if (!(rexx = rexx_open(d))) {
        sd_msg(d, "A dock named %s is already running", (LONG)d->portname);
        rc = RETURN_WARN;
        goto out;
    }
    if (o.delay > 0) Delay(o.delay);
    TRACE("config");
    if (!config_load(d, NULL)) config_free(d);   /* unreadable config: start empty, never overwrite it */
    menusig = am_open();                       /* before the dock: its drop target */
    TRACE("open");
    timersig = ah_open(d);                     /* timers first: the dock's drawing is timed (debug) */
    animsig = an_open();
    runsig = run_open();
    if (!dock_open(d)) goto out;
    ptrsig = in_open(d);
    am_build(d);
    ah_start(d);
    run_check(d);                              /* running programs: dots on the dock */
    config_created(d);                         /* a missing config file is written now */
    TRACE("loop");
    rc = RETURN_OK;

    while (run) {
        ULONG wsig = d->win ? 1UL << d->win->UserPort->mp_SigBit : 0;
        ULONG psig = am_prefs_sig();             /* the settings window, while open */
        ULONG sigs = Wait(wsig | 1UL << rexx->mp_SigBit | ptrsig | timersig | menusig | animsig | runsig | psig |
                          SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) { TRACE("quit: ctrl-c"); run = FALSE; }
        if (d->win && (sigs & wsig)) {
            struct IntuiMessage *im;
            while (d->win && (im = (struct IntuiMessage *)GetMsg(d->win->UserPort))) {
                struct IntuiMessage copy = *im;
                ReplyMsg((struct Message *)im);
                if (!dock_event(d, &copy)) run = FALSE;
            }
        }
        if (sigs & 1UL << rexx->mp_SigBit && !rexx_handle(d, rexx)) run = FALSE;
        if (sigs & ptrsig) {
            if (d->raisecheck) { d->raisecheck = FALSE; dock_settle(d); }
            if (d->hotkey) { d->hotkey = FALSE; ah_hotkey(d); }
            if (d->keyact) {                     /* the other keys (input.c) */
                UBYTE k;
                Forbid();                        /* (the input handler sets bits meanwhile) */
                k = d->keyact;
                d->keyact = 0;
                Permit();
                if (k & KEY_MIN) run_key_min(d);
                if (k & KEY_MINALL) run_key_minall(d);
                if (k & KEY_AH) ah_key(d);
            }
            if (d->listev) run_list_input(d);
            ah_pointer(d);
            tip_update(d);
            run_check(d);
            if (d->winchange) { d->winchange = FALSE; run_wake(d); run_soon(d); }
            if (d->delquit) {                    /* (only if that cell still holds the icon it held at */
                LONG c = d->delquit - 1;         /*  the key: an ARexx MOVE or a scan in this pass may */
                d->delquit = 0;                  /*  have changed the icons) */
                if (d->win && dock_item(d, c) == d->delitem) run_quit(d, c);
            }
        }
        if (sigs & timersig) { ah_timer(d); run_check(d); }   /* (a slide shows or hides the dock) */
        if (sigs & animsig) an_timer(d);
        if (sigs & runsig) run_timer(d);
        if ((sigs & menusig) && !am_handle(d)) run = FALSE;
        if (psig && (sigs & psig)) am_prefs_handle(d);
    }
    drag_cancel(d, FALSE);                     /* (quit during our drag: its layer locks go first) */
    am_prefs_store(d);                         /* an open settings window keeps its changes */
out:
    drop_copy_wait();                          /* a copy into a dock drawer finishes first */
    crash_set(FALSE);
    dock_close(d);                             /* drop target and window before the port */
    am_close();
    an_close();
    ah_close();
    run_close();
    in_close();
    config_free(d);
    rexx_close(rexx);
    opts_free(rd);
    sd_close_libs();
    return rc;
}
