/* SimpleDock - crash reports (CRASHLOG, off by default)
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   With CRASHLOG on, the dock's own task gets a trap handler (crashtrap.s). It
   costs nothing until an error happens in that task: a bad memory access,
   an illegal instruction, a division by zero. Then the dock writes a report
   to ENVARC:SimpleDock-crash.txt, says so, and closes as cleanly as it can.
   The input handler goes first: its code must not run after the program is
   gone. Errors in other tasks (Workbench's drag, the input handler itself)
   are not caught here. */
#include <string.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <intuition/intuitionbase.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdlib.h>
#include "sd.h"

extern struct ExecBase *SysBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *WorkbenchBase;
extern struct WBStartup *WBenchMsg;

ULONG crash_regs[15], crash_usp, crash_no, crash_pc, crash_count, crash_sp;
UWORD crash_sr, crash_fmt;
struct RexxMsg *crash_rexx;             /* the ARexx message being handled (replied on a crash) */
#define SPARE 2048                      /* 8 KB stack for the recovery, only while armed */
static ULONG *spare;
static APTR oldtrap;
static BOOL armed;
static struct Dock *cd;
static struct MsgPort **cport;
static struct RDArgs **crd;

void crash_trap(void);                  /* crashtrap.s */

/* What the recovery needs to close the dock: main's port and arguments. */
void crash_context(struct Dock *d, struct MsgPort **rexx, struct RDArgs **rd)
{
    cd = d; cport = rexx; crd = rd;
}

/* The trap handler on (CRASHLOG) or off. */
void crash_set(BOOL on)
{
    struct Task *me = FindTask(NULL);
    if (on && !armed) {
        if (!spare && !(spare = AllocVec(SPARE * sizeof(ULONG), MEMF_ANY))) return;   /* (no memory: no reports) */
        oldtrap = me->tc_TrapCode;
        crash_sp = (ULONG)&spare[SPARE - 4] & ~3UL;
        me->tc_TrapCode = (APTR)crash_trap;
        armed = TRUE;
    } else if (!on && armed) {
        me->tc_TrapCode = oldtrap;
        armed = FALSE;
        FreeVec(spare);
        spare = NULL;
    }
}

static CONST_STRPTR what(ULONG n)
{
    static const char *const name[] = { NULL, NULL, "Bus error", "Address error", "Illegal instruction",
        "Division by zero", "CHK instruction", "TRAPV instruction", "Privilege violation", "Trace",
        "Line A instruction", "Line F instruction" };
    if (n < 12 && name[n]) return name[n];
    if (n >= 32 && n < 48) return "TRAP instruction";
    return "Exception";
}

/* The section (hunk) of the program and the offset in it for an address; with
   the linker map this names the function. Returns -1 if the address is not in
   the program. */
static LONG where(ULONG pc, ULONG *off)
{
    BPTR seg = WBenchMsg ? WBenchMsg->sm_Segment : Cli() ? Cli()->cli_Module : 0;
    LONG n = 0;
    *off = pc;
    while (seg && n < 32) {
        ULONG *p = (ULONG *)BADDR(seg), start = (ULONG)(p + 1), size = p[-1] - 8;
        if (pc >= start && pc < start + size) { *off = pc - start; return n; }
        seg = (BPTR)p[0];
        n++;
    }
    return -1;
}

static void put(BPTR fh, CONST_STRPTR fmt, LONG *args)
{
    char line[256];
    SD_FMT(line, fmt, args);
    FPuts(fh, line);
}

static void report(struct Dock *d)
{
    static char sets[2048];
    struct Task *me = FindTask(NULL);
    BPTR fh = Open("ENVARC:SimpleDock-crash.txt", MODE_NEWFILE);
    LONG v[8], i, n = 0, m = 0;
    ULONG off;
    struct Item *it;
    if (!fh) return;
    FPuts(fh, "SimpleDock crash report\n\nVersion: " SD_NAME " " SD_VERSION SD_BUILD " (" SD_DATE ")\n");
    v[0] = (LONG)what(crash_no); v[1] = crash_no;
    put(fh, "Error: %s (trap %ld)\n", v);
    v[0] = where(crash_pc, &off); v[1] = off; v[2] = crash_pc;
    put(fh, "At: section %ld, offset $%lx (address $%08lx)\n", v);
    v[0] = crash_sr; v[1] = crash_fmt >> 12;
    put(fh, "SR: $%04lx, frame format %ld\n", v);
    for (i = 0; i < 8; i++) v[i] = crash_regs[i];
    put(fh, "D0-D7: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", v);
    for (i = 0; i < 7; i++) v[i] = crash_regs[8 + i];
    v[7] = crash_usp;
    put(fh, "A0-A6: %08lx %08lx %08lx %08lx %08lx %08lx %08lx  SP: %08lx\n", v);
    if (crash_usp >= (ULONG)me->tc_SPLower && crash_usp + 32 <= (ULONG)me->tc_SPUpper && !(crash_usp & 1)) {
        for (i = 0; i < 8; i++) v[i] = ((ULONG *)crash_usp)[i];   /* (only inside the task's stack) */
        put(fh, "Stack: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n", v);
        for (i = 0; i < 128 && crash_usp + 4 * i + 4 <= (ULONG)me->tc_SPUpper; i++) {
            ULONG x = ((ULONG *)crash_usp)[i];   /* return addresses into SimpleDock's own code */
            if ((v[1] = where(x, &off)) >= 0 && x != crash_pc) {
                v[0] = 4 * i; v[2] = off; v[3] = x;
                put(fh, "  SP+%ld: section %ld, offset $%lx ($%08lx)\n", v);
            }
        }
    }
    {
        BPTR seg = WBenchMsg ? WBenchMsg->sm_Segment : Cli() ? Cli()->cli_Module : 0;
        for (i = 0; seg && i < 8; i++) {          /* where each section was loaded */
            v[0] = i; v[1] = (LONG)((ULONG *)BADDR(seg) + 1);
            put(fh, "Section %ld at $%08lx\n", v);
            seg = *(BPTR *)BADDR(seg);
        }
    }
    v[0] = SysBase->LibNode.lib_Version; v[1] = SysBase->LibNode.lib_Revision;
    v[2] = IntuitionBase ? IntuitionBase->LibNode.lib_Version : 0;
    v[3] = WorkbenchBase ? WorkbenchBase->lib_Version : 0;
    v[4] = SysBase->AttnFlags & AFF_68060 ? 60 : SysBase->AttnFlags & AFF_68040 ? 40 :
           SysBase->AttnFlags & AFF_68030 ? 30 : SysBase->AttnFlags & AFF_68020 ? 20 : 0;
    put(fh, "System: exec %ld.%ld, intuition %ld, workbench %ld, 680%ld\n", v);
    if (d && d->scr) {
        v[0] = d->scr->Width; v[1] = d->scr->Height; v[2] = GetBitMapAttr(d->scr->RastPort.BitMap, BMA_DEPTH);
        put(fh, "Screen: %ld x %ld, %ld bits\n", v);
    }
    if (d && d->o) {
        FOR_ITEMS(d, it) n++;
        for (it = (struct Item *)d->extra.mlh_Head; it->node.mln_Succ; it = (struct Item *)it->node.mln_Succ) m++;
        v[0] = n; v[1] = m;
        put(fh, "Icons: %ld shown, %ld waiting\n", v);
        config_get(d, NULL, sets, sizeof(sets));
        FPuts(fh, "\nSettings:\n");
        FPuts(fh, sets);
        FPuts(fh, "\n");
    }
    Close(fh);
}

/* After a crash (crashtrap.s continues here, on the spare stack): write the
   report, then close everything, the input handler first. */
void crash_recover(void)
{
    struct Dock *d = cd;
    run_crash_unlock();                          /* (Intuition's locks, if it happened while holding them) */
    run_show_all();                              /* (windows the dock hid: back) */
    if (d && d->dragging) drag_cancel(d, FALSE); /* (our drag holds the screen's layers: free them first) */
    in_close();
    drop_copy_wait();                            /* its process runs our code */
    report(d);
    if (crash_rexx) {                            /* the ARexx command that was running */
        crash_rexx->rm_Result1 = RC_FATAL;
        crash_rexx->rm_Result2 = 0;
        ReplyMsg((struct Message *)crash_rexx);
        crash_rexx = NULL;
    }
    if (d) {
        LONG v[1];
        v[0] = (LONG)what(crash_no);
        sd_msg(d, "SimpleDock stopped after an error (%s).\n"
                  "A report was saved as ENVARC:SimpleDock-crash.txt -\n"
                  "please attach it to an issue at\n"
                  "github.com/renewable-intel/SimpleDock", v[0]);
        dock_close(d);
    }
    am_close();
    an_close();
    ah_close();
    run_close();
    if (d) config_free(d);
    if (cport) rexx_close(*cport);
    if (crd) opts_free(*crd);
    crash_set(FALSE);
    sd_close_libs();
    exit(RETURN_FAIL);
}

/* A second crash while recovering: stay here. The program stays in memory,
   so whatever still points into it keeps working. */
void crash_halt(void)
{
    Wait(0);
}
