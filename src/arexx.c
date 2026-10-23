/* SimpleDock - ARexx port
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <graphics/clip.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include "sd.h"

/* Public port with the dock's name; NULL if that name is already in use. */
struct MsgPort *rexx_open(struct Dock *d)
{
    struct MsgPort *port;
    Forbid();
    if (FindPort(d->portname)) { Permit(); return NULL; }
    if ((port = CreateMsgPort())) {
        port->mp_Node.ln_Name = d->portname;
        port->mp_Node.ln_Pri = 0;
        AddPort(port);
    }
    Permit();
    return port;
}

/* Commands to other docks (SETTINGS, QUIT, REVEAL) go out through one port
   and are not waited for: the answers come back as a signal the main loop
   serves (rexx_reap frees them). Nothing blocks, and a dock that is slow to
   answer (in a requester) can never reply into a freed port. */
static struct MsgPort *outbox;
static LONG outstanding;

ULONG rexx_outbox_sig(void)
{
    if (!outbox && !(outbox = CreateMsgPort())) return 0;
    return 1UL << outbox->mp_SigBit;
}

void rexx_reap(void)
{
    struct RexxMsg *rm;
    while (outbox && (rm = (struct RexxMsg *)GetMsg(outbox))) {
        ClearRexxMsg(rm, 1);
        DeleteRexxMsg(rm);
        outstanding--;
    }
}

/* FALSE: no such port (or no memory); the command's own result is not seen. */
BOOL rexx_send(CONST_STRPTR portname, CONST_STRPTR cmd)
{
    struct MsgPort *port;
    struct RexxMsg *rm;
    if (!RexxSysBase || !outbox || !(rm = CreateRexxMsg(outbox, NULL, NULL))) return FALSE;
    rm->rm_Action = RXCOMM;
    if (!(rm->rm_Args[0] = CreateArgstring((STRPTR)cmd, strlen((char *)cmd)))) { DeleteRexxMsg(rm); return FALSE; }
    Forbid();
    if ((port = FindPort((STRPTR)portname))) { PutMsg(port, &rm->rm_Node); outstanding++; }
    Permit();
    if (!port) { ClearRexxMsg(rm, 1); DeleteRexxMsg(rm); }
    return port != NULL;
}

/* At the end: the answers still out are waited for a moment; any that never
   come leave the port behind, made deaf (PA_IGNORE, no task), so a late reply
   can do no harm. */
static void outbox_close(void)
{
    LONG t;
    if (!outbox) return;
    for (t = 0; outstanding > 0 && t < 40; t++) { Delay(5); rexx_reap(); }   /* (up to ~4 s) */
    rexx_reap();
    if (outstanding > 0) {
        Forbid();
        outbox->mp_Flags = PA_IGNORE;
        outbox->mp_SigTask = NULL;
        Permit();
        FreeSignal(outbox->mp_SigBit);
    } else DeleteMsgPort(outbox);
    outbox = NULL;
}

void rexx_close(struct MsgPort *port)
{
    struct RexxMsg *rm;
    outbox_close();
    if (!port) return;
    Forbid();                                   /* nobody can find it and send to it meanwhile */
    RemPort(port);
    while ((rm = (struct RexxMsg *)GetMsg(port))) {
        rm->rm_Result1 = RC_FATAL;
        rm->rm_Result2 = 0;
        ReplyMsg((struct Message *)rm);
    }
    DeleteMsgPort(port);
    Permit();
}

static BOOL word(CONST_STRPTR *s, CONST_STRPTR w)
{
    LONG n = strlen(w);
    CONST_STRPTR p = *s;
    while (*p == ' ') p++;
    if (Strnicmp((STRPTR)p, (STRPTR)w, n) || (p[n] && p[n] != ' ')) return FALSE;
    *s = p + n;
    return TRUE;
}

/* The rest of a command parsed with a DOS template (quotes work: "Ram Disk:"). */
static struct RDArgs *rargs(CONST_STRPTR s, CONST_STRPTR tmpl, LONG *a, LONG n)
{
    static char line[600];
    struct RDArgs *rd;
    LONG len;
    while (*s == ' ') s++;
    if ((len = strlen(s)) > (LONG)sizeof(line) - 2) return NULL;
    strcpy(line, s);
    line[len++] = '\n';
    line[len] = 0;
    memset(a, 0, n * sizeof(LONG));
    if (!(rd = AllocDosObject(DOS_RDARGS, NULL))) return NULL;
    rd->RDA_Source.CS_Buffer = (UBYTE *)line;
    rd->RDA_Source.CS_Length = len;
    rd->RDA_Flags = RDAF_NOPROMPT;
    if (!ReadArgs((STRPTR)tmpl, a, rd)) { FreeArgs(rd); FreeDosObject(DOS_RDARGS, rd); return NULL; }
    return rd;
}

static void rfree(struct RDArgs *rd)
{
    if (rd) { FreeArgs(rd); FreeDosObject(DOS_RDARGS, rd); }
}

/* Kept icon n (0 = first) as LIST counts them: the running others left out. */
static struct Item *kept_item(struct Dock *d, LONG n)
{
    struct Item *it;
    FOR_ITEMS(d, it) if (!it->temp && n-- == 0) return it;
    return NULL;
}

/* An icon by name: "#n" (its place, 1 = first), its label, its file name or
   its path, in any case. */
static struct Item *find(struct Dock *d, CONST_STRPTR name)
{
    struct Item *it;
    LONG n;
    if (name[0] == '#') return StrToLong((STRPTR)name + 1, &n) > 0 && n >= 1 ? kept_item(d, n - 1) : NULL;
    FOR_ITEMS(d, it)
        if (!it->temp && ((it->label && !Stricmp(it->label, (STRPTR)name)) || !Stricmp(FilePart(it->path), (STRPTR)name) ||
                          !Stricmp(it->path, (STRPTR)name)))
            return it;
    return NULL;
}

/* The icon to insert before for AT n, AFTER name or BEFORE name (NULL: at the
   end). FALSE if there is no such icon. */
static BOOL where(struct Dock *d, LONG *at, CONST_STRPTR after, CONST_STRPTR before, struct Item **pb)
{
    struct Item *x;
    *pb = NULL;
    if (before) return (*pb = find(d, before)) != NULL;
    if (after) {
        if (!(x = find(d, after))) return FALSE;
        *pb = x->node.mln_Succ->mln_Succ ? (struct Item *)x->node.mln_Succ : NULL;
        return TRUE;
    }
    if (at) *pb = kept_item(d, *at - 1);         /* past the end: at the end */
    return TRUE;
}

/* A command that only asks (no window or list change: fine during a drag). */
static BOOL asks(CONST_STRPTR cmd)
{
    static const char *const q[] = { "GETPOS", "LIST", "GET", NULL };
    LONG i;
    for (i = 0; q[i]; i++) {
        CONST_STRPTR c = cmd;
        if (word(&c, q[i])) return TRUE;
    }
    return FALSE;
}

/* Before changing the icon list: keep the zone hook out and put the icons that
   didn't fit back in the list (the new layout sorts them again). */
static void changing(struct Dock *d)
{
    dz_suspend();
    dock_unhide(d);
}

/* The icons changed: resize the dock (unless it is HIDE'd). A new icon 'add'
   appears at its place at once while the others make way. */
static void changed(struct Dock *d, struct Item *add)
{
    if (d->hidden) return;
    dock_reopen_glide(d);
    if (add && d->win) { add->x = add->tx; add->y = add->ty; }
}

/* One reply per message: RC, plus RESULT when asked for and RC is 0. */
static void reply(struct RexxMsg *rm, LONG rc, CONST_STRPTR result)
{
    rm->rm_Result1 = rc;
    rm->rm_Result2 = 0;
    if (rc == RC_OK && result && (rm->rm_Action & RXFF_RESULT) && RexxSysBase)
        rm->rm_Result2 = (LONG)CreateArgstring((STRPTR)result, strlen(result));
    ReplyMsg((struct Message *)rm);
}

BOOL rexx_handle(struct Dock *d, struct MsgPort *port)
{
    struct RexxMsg *rm;
    BOOL run = TRUE;

    while ((rm = (struct RexxMsg *)GetMsg(port))) {
        CONST_STRPTR cmd;
        static char res[2048];
        LONG rc = RC_OK, a[5];
        CONST_STRPTR out = NULL;
        if (rm->rm_Node.mn_Length < sizeof(struct RexxMsg) && !(RexxSysBase && IsRexxMsg(rm))) {
            /* (any program can send to the port: a message shorter than a RexxMsg is not ours to read;
                CreateRexxMsg gives the full length but no name, so IsRexxMsg alone refuses those) */
            ReplyMsg((struct Message *)rm);
            continue;
        }
        cmd = (CONST_STRPTR)rm->rm_Args[0];
        struct RDArgs *ra = NULL;
        char *big = NULL;

        crash_rexx = rm;                         /* (a crash replies to it) */
        if (!cmd || (rm->rm_Action & RXCODEMASK) != RXCOMM || !run) rc = RC_ERROR;   /* after QUIT: no more */
        else if (word(&cmd, "QUIT")) { TRACE("quit: arexx"); run = FALSE; }
        else if (d->dragging && !asks(cmd))
            rc = RC_WARN;                        /* our own drag holds the screen's layers: busy */
        else if (word(&cmd, "HIDE")) { if (!d->hidden) dock_close(d); }
        else if (word(&cmd, "SHOW")) { if (d->hidden) { if (dock_open(d)) ah_start(d); else rc = RC_ERROR; } }
        else if (word(&cmd, "REFRESH")) {
            dock_reopen(d);
            if (d->hidden) rc = RC_ERROR;
        }
        else if (word(&cmd, "GETPOS")) {
            LONG v[8], n = 4;
            v[0] = d->left; v[1] = d->top; v[2] = d->w; v[3] = d->h;
            if (d->last) {
                v[4] = d->left + d->last->x; v[5] = d->top + d->last->y;
                v[6] = d->last->w; v[7] = d->last->h; n = 8;
            }
            SD_FMT(res, n == 8 ? "%ld %ld %ld %ld %ld %ld %ld %ld" : "%ld %ld %ld %ld",
                     v);
            out = res;
        }
        else if (word(&cmd, "LIST")) {           /* the icons in dock order, one a line */
            struct Item *it;
            BOOL names = word(&cmd, "NAMES");
            LONG len = 1, n = 0, pass;
            BOOL first = TRUE;
            for (pass = 0; pass < 2; pass++)     /* those shown, then those that do not fit */
                for (it = (struct Item *)(pass ? &d->extra : &d->items)->mlh_Head; it->node.mln_Succ;
                     it = (struct Item *)it->node.mln_Succ)
                    len += strlen(it->path) + (it->label ? strlen(it->label) : 0) + 1;   /* (an upper bound) */
            if ((big = AllocVec(len, MEMF_ANY))) {
                big[0] = 0;
                for (pass = 0; pass < 2; pass++)
                    for (it = (struct Item *)(pass ? &d->extra : &d->items)->mlh_Head; it->node.mln_Succ;
                         it = (struct Item *)it->node.mln_Succ) {
                        CONST_STRPTR s = names ? (it->label ? it->label : FilePart(it->path)) : it->path;
                        if (it->temp) continue;          /* running, not kept */
                        if (names && !*s) s = it->path;   /* a disk without a label */
                        if (!first) big[n++] = '\n';
                        first = FALSE;
                        strcpy(big + n, s); n += strlen(s);
                    }
                out = big;
            } else rc = RC_ERROR;
        }
        else if (word(&cmd, "ADD")) {            /* PATH [AT n | AFTER name | BEFORE name] [LABEL text] */
            struct Item *before, *it;
            BPTR lock = 0;
            if (!(ra = rargs(cmd, "PATH/A,AT/K/N,AFTER/K,BEFORE/K,LABEL/K", a, 5)) ||
                !*(STRPTR)a[0] || !(lock = Lock((STRPTR)a[0], ACCESS_READ)) ||   /* no such file ("" would lock the */
                (!d->hidden && d->nkept + d->pushed + 1 > d->fit)) rc = RC_ERROR;   /* current drawer); the dock is full */
            else if (changing(d), !where(d, (LONG *)a[1], (STRPTR)a[2], (STRPTR)a[3], &before)) {
                rc = RC_ERROR;
                changed(d, NULL);
            } else {
                LONG n = strlen((STRPTR)a[0]);
                if (!(it = config_add(d, (STRPTR)a[0], before))) rc = RC_ERROR;
                else if (a[4]) it->label = sd_strdup((STRPTR)a[4]);
                else if (n > 1 && ((STRPTR)a[0])[n - 1] == ':' && (it->label = sd_strdup((STRPTR)a[0])))
                    it->label[n - 1] = 0;        /* a disk: its name as the label */
                changed(d, it);
            }
            if (lock) UnLock(lock);
        }
        else if (word(&cmd, "REMOVE")) {         /* NAME */
            struct Item *it;
            if (!(ra = rargs(cmd, "NAME/A", a, 1))) rc = RC_ERROR;
            else {
                changing(d);
                if ((it = find(d, (STRPTR)a[0]))) config_remove(d, it);
                else rc = RC_ERROR;
                changed(d, NULL);
            }
        }
        else if (word(&cmd, "MOVE")) {           /* NAME AT n | AFTER name | BEFORE name */
            struct Item *it, *before;
            struct Node *pred;
            if (!(ra = rargs(cmd, "NAME/A,AT/K/N,AFTER/K,BEFORE/K", a, 4)) || !(a[1] || a[2] || a[3])) rc = RC_ERROR;
            else if (changing(d), !(it = find(d, (STRPTR)a[0]))) {
                rc = RC_ERROR;
                changed(d, NULL);
            } else {
                pred = (struct Node *)it->node.mln_Pred;
                Remove((struct Node *)it);
                if (!where(d, (LONG *)a[1], (STRPTR)a[2], (STRPTR)a[3], &before)) {
                    Insert((struct List *)&d->items, (struct Node *)it, pred);   /* back where it was */
                    rc = RC_ERROR;
                } else if (before) Insert((struct List *)&d->items, (struct Node *)it, (struct Node *)before->node.mln_Pred);
                else AddTail((struct List *)&d->items, (struct Node *)it);
                changed(d, NULL);
            }
        }
        else if (word(&cmd, "SET")) {            /* KEYWORD=value ... (as in the settings file) */
            am_prefs_close(d);                    /* its values would be stale */
            if (!config_set(d, cmd)) rc = RC_ERROR;
            else if (!d->hidden) dock_reopen(d);
        }
        else if (word(&cmd, "GET")) {            /* [KEYWORD] */
            if (!(ra = rargs(cmd, "KEY", a, 1)) || !config_get(d, (STRPTR)a[0], res, sizeof(res))) rc = RC_ERROR;
            else out = res;
        }
        else if (word(&cmd, "SAVE")) {           /* [FILE] - the dock's own file if none */
            if (!(ra = rargs(cmd, "FILE", a, 1)) || !config_save(d, (STRPTR)a[0])) rc = RC_ERROR;
        }
        else if (word(&cmd, "RELOAD")) {         /* the dock's own file again */
            am_prefs_close(d);
            if (!config_reload(d, NULL)) rc = RC_ERROR;
        }
        else if (word(&cmd, "LOAD")) {           /* FILE */
            am_prefs_close(d);
            if (!(ra = rargs(cmd, "FILE/A", a, 1)) || !config_reload(d, (STRPTR)a[0])) rc = RC_ERROR;
        }
        else if (word(&cmd, "REVEAL")) {         /* as the hotkey */
            if (d->hidden) rc = RC_ERROR;
            else ah_hotkey(d);
        }
#ifdef SD_DEBUG
        else if (word(&cmd, "CRASH")) {          /* CRASH DIV / ILLEGAL: test the crash report */
            static volatile LONG zero;
            if (word(&cmd, "ILLEGAL")) crash_illegal();
            else rc = 100 / zero;
        }
        else if (word(&cmd, "ITEMS")) {          /* names in dock order */
            struct Item *it;
            static char list[256];
            LONG n = 0;
            list[0] = 0;
            FOR_ITEMS(d, it) {                   /* (running others marked +, the "more" cell ...) */
                CONST_STRPTR s = it->temp == RUN_MORE ? "..." : FilePart(it->path);
                if (n + strlen(s) + 3 >= sizeof(list)) break;
                if (n) list[n++] = ',';
                if (it->temp == RUN_TEMP) list[n++] = it->nofile ? '?' : '+';   /* (? : its file not known) */
                strcpy(list + n, *s ? s : it->path); n += strlen(list + n);
            }
            out = list;
        }
        else if (word(&cmd, "LAYERTRY")) {       /* could the dock draw now? (1/0) */
            LONG v[1];
            v[0] = d->win && AttemptLockLayerRom(d->win->WLayer);
            if (v[0]) UnlockLayerRom(d->win->WLayer);
            SD_FMT(res, "%ld", v);
            out = res;
        }
        else if (word(&cmd, "ANSTATE")) {
            extern LONG an_frames, an_ms, an_glides, render_clamps;
            LONG v[4];
            v[0] = an_glides; v[1] = an_frames; v[2] = an_ms; v[3] = render_clamps;
            SD_FMT(res, "glides %ld last: frames %ld ms %ld clamps %ld", v);
            out = res;
        }
        else if (word(&cmd, "ACTIVE")) {         /* which window has the focus */
            extern struct IntuitionBase *IntuitionBase;
            struct Window *aw = ((struct IntuitionBase *)IntuitionBase)->ActiveWindow;
            LONG v[3];
            v[0] = aw && aw == d->win; v[1] = aw ? (LONG)aw->Flags : 0;
            v[2] = (LONG)(aw && aw->Title ? (char *)aw->Title : "-");
            SD_FMT(res, "dock %ld flags %lx title %.60s", v);
            out = res;
        }
        else if (word(&cmd, "COVERED")) {        /* windows in front overlapping the dock */
            LONG v[1];
            struct Layer *me, *l;
            v[0] = 0;
            if (d->win) {
                me = d->win->WLayer;
                LockLayerInfo(&d->scr->LayerInfo);
                for (l = me->front; l; l = l->front)
                    if ((!d->tip || l != d->tip->WLayer) && l->bounds.MinX <= me->bounds.MaxX &&
                        l->bounds.MaxX >= me->bounds.MinX && l->bounds.MinY <= me->bounds.MaxY &&
                        l->bounds.MaxY >= me->bounds.MinY) v[0]++;
                UnlockLayerInfo(&d->scr->LayerInfo);
            }
            SD_FMT(res, "%ld", v);
            out = res;
        }
        else if (word(&cmd, "PEEKS")) {          /* new snapshots taken after windows changed behind */
            extern LONG ah_peeks;
            LONG v[1];
            v[0] = ah_peeks;
            SD_FMT(res, "%ld", v);
            out = res;
        }
        else if (word(&cmd, "OPENWITH")) {       /* drops onto an icon: how many, last program, arguments */
            extern LONG lw_count, lw_args;
            extern char *lw_path;
            LONG v[3];
            v[0] = lw_count; v[1] = (LONG)lw_path; v[2] = lw_args;
            SD_FMT(res, "%ld %.200s %ld", v);
            out = res;
        }
        else if (word(&cmd, "MENU")) {           /* a menu choice by name (tests) */
            while (*cmd == ' ') cmd++;
            if (!am_choose(d, cmd)) rc = RC_ERROR;
        }
        else if (word(&cmd, "PROF")) {           /* ms and count per timed step; PROF RESET */
            LONG v[24], i;
            if (word(&cmd, "RESET")) for (i = 0; i < 12; i++) sd_prof_us[i] = sd_prof_n[i] = 0;
            for (i = 0; i < 12; i++) { v[2 * i] = sd_prof_us[i] / 1000; v[2 * i + 1] = sd_prof_n[i]; }
            SD_FMT(res, "items %ld/%ld compose %ld/%ld dragglide %ld/%ld glass %ld/%ld zone %ld/%ld "
                        "show %ld/%ld dragmove %ld/%ld image %ld/%ld copy %ld/%ld blend %ld/%ld write %ld/%ld "
                        "read %ld/%ld", v);
            out = res;
        }
        else if (word(&cmd, "GRID")) {
            LONG v[5];
            v[0] = d->cw; v[1] = d->ch; v[2] = d->per; v[3] = d->pad; v[4] = d->ncells;
            SD_FMT(res, "%ld %ld %ld %ld %ld", v);
            out = res;
        }
        else if (word(&cmd, "RUNSTATE")) {
            extern LONG run_scans, run_skips, run_found, run_redraws;
            extern ULONG run_us_sum, run_us_max;
            struct Item *ri;
            LONG v[8];
            char *p;
            v[0] = d->vis; v[1] = run_scans; v[2] = run_skips; v[3] = run_found; v[4] = run_redraws;
            v[5] = run_scans ? run_us_sum / run_scans : 0; v[6] = run_us_max;
            SD_FMT(res, "vis %ld scans %ld skips %ld windows %ld redraws %ld avg %ld us max %ld us |", v);
            p = res + strlen(res);
            FOR_ITEMS(d, ri)
                if (ri->running && p < res + sizeof(res) - 40) {
                    v[0] = (LONG)FilePart(ri->path); v[1] = ri->running;
                    SD_FMT(p, " %s=%ld", v);
                    p += strlen(p);
                }
            out = res;
        }
        else if (word(&cmd, "AHSTATE")) {
            extern LONG ah_frames, ah_ms, ah_calls, in_calls, in_sigs, in_lastx, in_lasty;
            extern volatile WORD in_on;
            LONG v[13];
            v[0] = d->vis; v[1] = d->full; v[2] = d->lmb; v[3] = d->want; v[4] = ah_frames; v[5] = ah_ms;
            v[6] = in_calls; v[7] = in_sigs; v[8] = ah_calls; v[9] = in_lastx; v[10] = in_lasty;
            v[11] = in_on;
            SD_FMT(res, "vis %ld full %ld lmb %ld want %ld frames %ld ms %ld | in %ld sig %ld ah %ld at %ld,%ld on %ld", v);
            out = res;
        }
#endif
        else if (word(&cmd, "SETTINGS")) am_settings(d, word(&cmd, "ADVANCED"));   /* the settings window */
        else if (word(&cmd, "HOTKEYINFO")) {     /* the hotkey's counters and the state it looks at */
            LONG v[HK_N + 8], j;
            for (j = 0; j < HK_N; j++) v[j] = hk_stat[j];
            v[HK_N] = d->lmb; v[HK_N + 1] = d->dragging; v[HK_N + 2] = d->pinned; v[HK_N + 3] = d->vis;
            v[HK_N + 4] = d->full; v[HK_N + 5] = d->o->autohide; v[HK_N + 6] = d->parked; v[HK_N + 7] = dock_covered(d);
            SD_FMT(res, "tap armed %ld, fired %ld, cancelled by a key %ld, by a button %ld, button state healed %ld\n"
                        "handled %ld: busy %ld, reopened %ld, to front %ld, shown %ld, hidden %ld\n"
                        "now: button %ld, dragging %ld, pinned %ld, vis %ld of %ld, autohide %ld, parked %ld, covered %ld", v);
            out = res;
        }
        else if (word(&cmd, "DROPINFO")) {       /* Shell drops: the windows as the dock sees them (running.c) */
            extern void run_dropinfo(struct Dock *, char *, LONG);
            run_dropinfo(d, res, sizeof(res));
            out = res;
        }
#ifdef SD_DEBUG
        else if (word(&cmd, "DROPCOPY")) {       /* DROPCOPY CELL DIR NAME: a drop onto a dock drawer, as from */
            struct AppMessage am;                /*  Workbench (tests the copy process) */
            struct WBArg wa;
            struct Item *di;
            if (!(ra = rargs(cmd, "CELL/A/N,DIR/A,NAME/A", a, 3)) || !(di = dock_item(d, *(LONG *)a[0])) ||
                !(wa.wa_Lock = Lock((STRPTR)a[1], SHARED_LOCK))) rc = RC_ERROR;
            else {
                memset(&am, 0, sizeof(am));
                wa.wa_Name = (BYTE *)a[2];
                am.am_NumArgs = 1;
                am.am_ArgList = &wa;
                if (!drop_copy(d, di, &am)) rc = RC_WARN;
                UnLock(wa.wa_Lock);
            }
        }
        else if (word(&cmd, "SCREENS")) {        /* the screens (run_debug_screens) */
            extern void run_debug_screens(char *, LONG);
            run_debug_screens(res, sizeof(res));
            out = res;
        }
        else if (word(&cmd, "DOACT")) {          /* DOACT NAME ACTION [NOASK] (run_debug_act) */
            extern LONG run_debug_act(struct Dock *, CONST_STRPTR, CONST_STRPTR, BOOL);
            if (!(ra = rargs(cmd, "NAME/A,ACTION/A,NOASK/S", a, 3))) rc = RC_ERROR;
            else rc = run_debug_act(d, (STRPTR)a[0], (STRPTR)a[1], a[2] != 0);
        }
        else if (word(&cmd, "SHELLDROPTEST")) {  /* SHELLDROPTEST DIR NAME (run_debug_shelldrop) */
            extern LONG run_debug_shelldrop(struct Dock *, CONST_STRPTR, CONST_STRPTR);
            if (!(ra = rargs(cmd, "DIR/A,NAME/A", a, 2))) rc = RC_ERROR;
            else rc = run_debug_shelldrop(d, (STRPTR)a[0], (STRPTR)a[1]);
        }
        else if (word(&cmd, "WINS")) {           /* every window: name:hidden, '*' active (run_debug_wins) */
            extern void run_debug_wins(struct Dock *, char *, LONG);
            run_debug_wins(d, res, sizeof(res));
            out = res;
        }
        else if (word(&cmd, "NOWB")) {           /* NOWB [OFF]: Shell starts without Workbench (its fallback) */
            extern BOOL rl_noowo;
            rl_noowo = !word(&cmd, "OFF");
        }
#endif
        else if (word(&cmd, "BENCH")) {          /* drawing times on this machine (bench.c) */
            dock_bench(d, res);
            out = res;
        }
        else rc = RC_ERROR;
        crash_rexx = NULL;
        reply(rm, rc, out);
        rfree(ra);
        FreeVec(big);
    }
    return run;
}
