/* SimpleDock - files dropped onto a drawer or disk icon on the dock
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   They are copied into it with their icons, keeping dates, protection bits
   and comments; with DROPMOVE, moved when on the same disk. A file of the
   same name is replaced only if the user says so; drawers are never merged.
   The copy runs in its own process and Workbench is told about each new
   object. Refused with a beep: a drawer into itself or its own drawers, an
   object into the drawer it is in, whole disks. The dock waits for a copy
   to finish before it quits (the process runs the dock's code). */
#include <string.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/wb.h>
#include "sd.h"

#define MAXCOPY 32                      /* objects in one drop */
#define PATHLEN 512

struct Job {
    struct Message msg;                 /* how the copying process gets it */
    BPTR  dest;                         /* the drawer copied into */
    LONG  n;
    BPTR  dir[MAXCOPY];                 /* each object: the drawer it is in ... */
    char  name[MAXCOPY][108];           /* ... its name ... */
    BOOL  move[MAXCOPY];                /* ... and moved (same disk, DROPMOVE) or copied */
    BOOL  nobeep;                       /* (the setting, for the copying process) */
};

static volatile LONG jobs;              /* copying processes still running */
static struct Task *dock;
static BYTE donesig = -1;

#define BUFSIZE  32768
#define MAXDEPTH 32                     /* drawers in drawers */

/* What is called 'name' in drawer 'dir': 0 nothing, 1 a file, 2 a drawer. */
static LONG there(BPTR dir, CONST_STRPTR name)
{
    struct FileInfoBlock *fib;
    BPTR old = CurrentDir(dir), l = Lock(name, SHARED_LOCK);
    LONG what = 1;
    CurrentDir(old);
    if (!l) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(l, fib) && fib->fib_DirEntryType > 0) what = 2;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    return what;
}

/* Ask before a file is replaced. */
static BOOL replace(struct Job *j, LONG k)
{
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME,
        "\"%s\" is already in\n%s\n\nReplace it?", "Replace|Skip" };
    char dst[256];
    if (!NameFromLock(j->dest, dst, sizeof(dst))) strcpy(dst, "that drawer");
    { LONG a[2]; a[0] = (LONG)j->name[k]; a[1] = (LONG)dst; return sd_request(NULL, &es, a) == 1; }
}

/* Dates, protection bits and comment of the source (fib) onto the copy. */
static void clone(BPTR ddir, CONST_STRPTR name, struct FileInfoBlock *fib)
{
    BPTR old = CurrentDir(ddir);
    SetComment(name, fib->fib_Comment);
    SetProtection(name, fib->fib_Protection);
    SetFileDate(name, &fib->fib_Date);
    CurrentDir(old);
}

/* Copy 'name' from drawer sdir into drawer ddir: a file, or a drawer with
   everything in it. A drawer reached through a link is left out (it could
   lead back up). FALSE: not all of it was copied. */
static BOOL copy_obj(BPTR sdir, CONST_STRPTR name, BPTR ddir, UBYTE *buf, LONG depth, BOOL link)
{
    struct FileInfoBlock *fib;
    BPTR old, l, nd, in, out;
    LONG n;
    BOOL ok = FALSE;
    old = CurrentDir(sdir);
    l = Lock(name, SHARED_LOCK);
    CurrentDir(old);
    if (!l) return FALSE;
    if (!(fib = AllocDosObject(DOS_FIB, NULL)) || !Examine(l, fib)) goto done;
    if (fib->fib_DirEntryType > 0) {             /* a drawer */
        if (link) { ok = TRUE; goto done; }
        if (depth >= MAXDEPTH) goto done;
        old = CurrentDir(ddir);
        if ((nd = CreateDir(name))) { UnLock(nd); nd = Lock(name, SHARED_LOCK); }
        CurrentDir(old);
        if (!nd) goto done;
        ok = TRUE;
        {
            struct FileInfoBlock *e = AllocDosObject(DOS_FIB, NULL);
            if (!e || !Examine(l, e)) ok = FALSE;
            else while (ExNext(l, e))
                if (!copy_obj(l, e->fib_FileName, nd, buf, depth + 1,
                              e->fib_DirEntryType == ST_SOFTLINK || e->fib_DirEntryType == ST_LINKDIR)) ok = FALSE;
            if (e) FreeDosObject(DOS_FIB, e);
        }
        UnLock(nd);
    } else {                                     /* a file */
        old = CurrentDir(sdir);
        in = Open(name, MODE_OLDFILE);
        CurrentDir(ddir);
        out = in ? Open(name, MODE_NEWFILE) : 0;
        CurrentDir(old);
        if (out) {
            ok = TRUE;
            while ((n = Read(in, buf, BUFSIZE)) > 0)
                if (Write(out, buf, n) != n) { ok = FALSE; break; }
            if (n < 0) ok = FALSE;
            if (!Close(out)) ok = FALSE;
            if (!ok) { old = CurrentDir(ddir); DeleteFile(name); CurrentDir(old); }   /* no half copies */
        }
        if (in) Close(in);
    }
    if (ok) clone(ddir, name, fib);
done:
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(l);
    return ok;
}

/* Move one object (and its icon) into the job's drawer: a rename. */
static BOOL move_one(struct Job *j, LONG k)
{
    char src[PATHLEN], to[PATHLEN], si[PATHLEN + 8], ti[PATHLEN + 8];
    BPTR l;
    if (!NameFromLock(j->dir[k], src, PATHLEN) || !AddPart(src, j->name[k], PATHLEN) ||
        !NameFromLock(j->dest, to, PATHLEN) || !AddPart(to, j->name[k], PATHLEN) || !Rename(src, to))
        return FALSE;
    strcpy(si, src); strcat(si, ".info");
    strcpy(ti, to); strcat(ti, ".info");
    if ((l = Lock(si, SHARED_LOCK))) { UnLock(l); DeleteFile(ti); Rename(si, ti); }
    UpdateWorkbench(j->name[k], j->dir[k], UPDATEWB_ObjectRemoved);
    UpdateWorkbench(j->name[k], j->dest, UPDATEWB_ObjectAdded);
    return TRUE;
}

/* One object (and its icon) into the job's drawer. FALSE: beep. */
static BOOL copy_one(struct Job *j, LONG k, UBYTE *buf)
{
    char icon[120];
    LONG src = there(j->dir[k], j->name[k]), dst = there(j->dest, j->name[k]);
    BOOL ok;
    if (!src) return FALSE;
    if (dst) {                                   /* the same name there already */
        if (src == 2 || dst == 2) return FALSE;  /* drawers are not merged */
        if (!replace(j, k)) return TRUE;         /* (skipped: no beep) */
        UpdateWorkbench(j->name[k], j->dest, UPDATEWB_ObjectRemoved);
        if (j->move[k]) {
            BPTR old = CurrentDir(j->dest);
            DeleteFile(j->name[k]);
            CurrentDir(old);
        }
    }
    if (j->move[k]) {
        if (!(ok = move_one(j, k)) && dst) UpdateWorkbench(j->name[k], j->dest, UPDATEWB_ObjectAdded);
        return ok;
    }
    ok = copy_obj(j->dir[k], j->name[k], j->dest, buf, 0, FALSE);
    strcpy(icon, j->name[k]);
    strcat(icon, ".info");
    if (ok && there(j->dir[k], icon) == 1) copy_obj(j->dir[k], icon, j->dest, buf, 0, FALSE);   /* its icon */
    if (ok || dst) UpdateWorkbench(j->name[k], j->dest, UPDATEWB_ObjectAdded);
    return ok;
}

static void free_job(struct Job *j)
{
    LONG k;
    for (k = 0; k < j->n; k++) UnLock(j->dir[k]);
    UnLock(j->dest);
    FreeVec(j);
}

/* The copying process. */
static __saveds LONG copier(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct Job *j;
    LONG k;
    UBYTE *buf;
    BOOL ok = TRUE;
    WaitPort(&me->pr_MsgPort);
    j = (struct Job *)GetMsg(&me->pr_MsgPort);
    if (!(buf = AllocVec(BUFSIZE, MEMF_ANY))) ok = FALSE;
    else {
        for (k = 0; k < j->n; k++) if (!copy_one(j, k, buf)) ok = FALSE;
        FreeVec(buf);
    }
    if (!ok && !j->nobeep) DisplayBeep(NULL);    /* something was not copied */
    free_job(j);
    Forbid();                                    /* the dock may quit once this returns */
    jobs--;
    Signal(dock, 1UL << donesig);
    return 0;
}

/* Is 'target' the object itself, or inside it? */
static BOOL inside(BPTR target, BPTR obj)
{
    BPTR p = DupLock(target), up;
    if (!p) return TRUE;                         /* (can't tell: refused) */
    while (p) {
        if (SameLock(p, obj) == LOCK_SAME) { UnLock(p); return TRUE; }
        up = ParentDir(p);
        UnLock(p);
        p = up;
    }
    return FALSE;
}

/* Icons dropped onto the drawer or disk icon 'it': copied into it (or moved, DROPMOVE). */
BOOL drop_copy(struct Dock *d, struct Item *it, struct AppMessage *am)
{
    struct Job *j;
    struct Process *p;
    BPTR target, obj, old;
    LONG i;
    if (donesig < 0) { dock = FindTask(NULL); donesig = AllocSignal(-1); }
    if (donesig < 0 || !(j = AllocVec(sizeof(*j), MEMF_PUBLIC | MEMF_CLEAR))) { sd_beep(d); return FALSE; }
    if (!(target = Lock(it->path, SHARED_LOCK))) { FreeVec(j); sd_beep(d); return FALSE; }
    j->dest = target;
    for (i = 0; i < am->am_NumArgs && j->n < MAXCOPY; i++) {
        struct WBArg *a = &am->am_ArgList[i];
        if (!a->wa_Lock || !a->wa_Name || !a->wa_Name[0] || strlen(a->wa_Name) > 107) continue;   /* a whole disk, or a name too long */
        if (SameLock(a->wa_Lock, target) == LOCK_SAME) continue;    /* in that drawer already */
        old = CurrentDir(a->wa_Lock);
        obj = Lock(a->wa_Name, SHARED_LOCK);
        CurrentDir(old);
        if (!obj) continue;
        if (inside(target, obj)) { UnLock(obj); continue; }        /* itself, or one of its own drawers */
        UnLock(obj);
        if (!(j->dir[j->n] = DupLock(a->wa_Lock))) continue;
        strcpy(j->name[j->n], a->wa_Name);
        j->move[j->n] = d->o->dropmove && SameDevice(a->wa_Lock, target);   /* like Workbench */
        j->nobeep = d->o->nobeep;
        j->n++;
    }
    if (j->n < am->am_NumArgs) sd_beep(d);   /* some (or all) refused */
    if (!j->n) { free_job(j); return FALSE; }
    p = CreateNewProcTags(NP_Entry, copier, NP_Name, "SimpleDock copy", NP_StackSize, 16384,
                          NP_WindowPtr, NULL, TAG_END);
    if (!p) { free_job(j); sd_beep(d); return FALSE; }
    Forbid();
    jobs++;
    Permit();
    j->msg.mn_Node.ln_Type = NT_MESSAGE;
    j->msg.mn_Length = sizeof(*j);
    PutMsg(&p->pr_MsgPort, &j->msg);
    return TRUE;
}

/* Quitting: a copy still running finishes first. */
void drop_copy_wait(void)
{
    while (jobs > 0) Wait(1UL << donesig);
    if (donesig >= 0) { FreeSignal(donesig); donesig = -1; }
}
