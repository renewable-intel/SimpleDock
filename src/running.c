/* SimpleDock - programs on the dock that are running
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   A running program gets a dot under its icon, and with two copies or more
   their number. A click brings all its windows to the front (again: the
   next one), Shift+click starts a new copy, Del over its icon closes it, a
   held button lists its windows. The list stays open after the button goes
   up: click a window to bring it forward, Del over one closes it (over
   Close / Close All: all), Esc or a click elsewhere closes the list.

   The dock starts program icons itself, as Workbench does, so it hears at
   once when one ends (its start-up message comes back). Programs started
   elsewhere are found by their windows: a window belongs to the task its
   IDCMP port signals, and that task runs under the program's name. Shells
   are counted from AmigaDOS's list; their windows are their console
   handlers'. AmigaOS sends no notice for those, so the windows are looked at
   when another window becomes active (the input handler sees that), every
   2 s while the dock is on screen, and every 0.5 s for 3 s after a click.
   Programs not on the dock get icons of their own at its end (see below). */
#include <string.h>
#include <devices/timer.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/intuitionbase.h>
#include <graphics/clip.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/layers.h>
#include <graphics/layers.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/keymap.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include "sd.h"

extern struct IntuitionBase *IntuitionBase;
extern struct Library *LayersBase;
extern struct Window *volatile in_prevwin;

#define SCAN_US   2000000               /* while the dock is on screen */
#define SOON_US   150000                /* after another window became active */
#define PULSE_US  125000                /* a minimised program's dot: a step */
#define FAST_US   500000                /* after a click, ... */
#define FAST_N    6                     /* ... this many times */
#define RETRY_US  300000                /* a look put off (sliding, button held) */
#define MAXWIN    64                    /* windows looked at */
#define MAXLIST   12                    /* windows in the held list */
#define NAMELEN   32
#define TITLELEN  48
#define PADX      8
#define PADY      2
#define GAP       2                     /* between the list and the dock */
#define MAXLAUNCH 64                    /* programs started by the dock at once */
#define MAXARGS   32                    /* files dropped onto an icon */
#define CMDLEN    128                   /* a Shell command with its path */

struct Own {                            /* a window and the program it belongs to */
    struct Window *win;
    struct Task   *task;
    ULONG          seq;                 /* when the dock first saw it (the list's order) */
    char           name[NAMELEN];
    char           title[TITLELEN];
    char           cmd[CMDLEN];         /* Shell command with a full path, else empty */
    BOOL           con;                 /* a Shell's console window */
    BOOL           stdcon;              /* ... a Shell window icons can be dropped into (any console) */
    BOOL           widget;              /* a small window without border or gadgets (a title-bar clock) */
    struct Screen *scr;                 /* its screen */
    BOOL           hid;                 /* hidden (minimised: its layer is) */
    BOOL           wb;                  /* Workbench's (a drawer window; no name) */
    struct Item   *item;                /* the "more" list: the program the row stands for */
};

static struct Own own[MAXWIN];

/* The windows seen so far, numbered in the order they first appeared: the
   held list keeps that order (AmigaOS keeps no opening time). A window that
   is gone is forgotten; its task tells a new window at the same address. */
static struct { struct Window *win; struct Task *task; ULONG seq; } seen[MAXWIN];
static LONG nseen;
static ULONG seqno;

struct Launch {                         /* a program the dock started */
    struct WBStartup wbs;               /* first: it comes back when the program ends */
    struct Item     *it;                /* its dock icon (NULL: removed meanwhile) */
    struct Task     *task;
    BPTR             seg;               /* its code, as loaded */
    UBYTE            missed;            /* looks that did not find its task (ended) */
    struct WBArg     args[MAXARGS + 1];
    char             names[1];          /* the arguments' names, one after another */
};
static struct Launch *lt[MAXLAUNCH];
static struct MsgPort *rport;           /* where the start-up messages come back */
static LONG nshells;                    /* interactive Shells (collect) */
static struct Task *contask[MAXWIN];    /* their console handlers */
static LONG ncon;
static ULONG lastsig;                   /* what was open at the last look */
#include <proto/timer.h>
extern struct Device *TimerBase;                     /* (autohide.c) */
#ifdef SD_DEBUG
LONG run_scans, run_skips, run_found, run_redraws;   /* debug ARexx RUNSTATE */
ULONG run_us_sum, run_us_max;                        /* time spent looking */
#endif
static struct MsgPort *tport;
static struct timerequest *treq;
static BOOL tbusy;
static LONG fast;                       /* quick looks left (after a click) */
static BOOL soon;                       /* a look after another window became active is waiting */

static struct Own list[MAXLIST];        /* the held list: its windows ... */
static LONG nlist, lsel = -1;           /* ... and the one under the pointer */
static struct Item *litem;              /* whose windows they are ... */
static struct Dock *ldock;              /* ... on which dock */
#define MAXCLOSE 16
static struct IntuiMessage *closing[MAXCLOSE];   /* close requests sent, not answered yet ... */
static struct Task *closetask[MAXCLOSE];         /* ... to these programs */
static struct IntuiMessage *orphan[MAXCLOSE];    /* ... and to programs that ended without answering */
#define MAXCQ 64
static struct { struct Window *win; struct Task *task; struct timeval since; BOOL wb; } cq[MAXCQ];
static LONG ncq;                                 /* Close All: windows waiting for their request */
static struct timeval lastreply;                 /* when a close request was last answered */

static void snap_tasks(void);
static BOOL alive(struct Task *t);
static BOOL win_hide_ok(void);

/* Close requests to programs that have ended meanwhile without answering:
   their slots are free again. The message is still out (in a window port
   nobody reads; whoever closes that window answers it): kept in orphan[],
   so quitting can tell it not to come back (a late answer is still freed
   by ended()). With orphan[] full, the slot stays taken. */
static void close_orphan(LONG k)
{
    LONG j;
    for (j = 0; j < MAXCLOSE && orphan[j]; j++) ;
    if (j < MAXCLOSE) { orphan[j] = closing[k]; closing[k] = NULL; }
}

static void close_prune(void)
{
    LONG k;
    Forbid();
    snap_tasks();
    for (k = 0; k < MAXCLOSE; k++) if (closing[k] && !alive(closetask[k])) close_orphan(k);
    Permit();
}

/* Task t has ended (its start-up message came back), or a new process
   has just been made at its address: its close requests are no longer
   waited for. (Else a new program at the same address could never be
   closed from the dock: 3.1's Clock quits without answering.) */
static void close_forget(struct Task *t)
{
    LONG k;
    Forbid();
    for (k = 0; k < MAXCLOSE; k++) if (closing[k] && closetask[k] == t) close_orphan(k);
    Permit();
}

/* Is a close request to task t not answered yet? Then it is not asked
   again: a program that doesn't answer won't answer a second one either
   (and each one would take a slot). */
static BOOL pending_to(struct Task *t)
{
    LONG k;
    for (k = 0; k < MAXCLOSE; k++) if (closing[k] && closetask[k] == t) return TRUE;
    return FALSE;
}

/* Is any close request not answered yet? */
static BOOL pending_any(void)
{
    LONG k;
    for (k = 0; k < MAXCLOSE; k++) if (closing[k]) return TRUE;
    return FALSE;
}

/* Is there room for another close request? */
static BOOL close_room(void)
{
    LONG k;
    for (k = 0; k < MAXCLOSE && closing[k]; k++) ;
    return k < MAXCLOSE;
}
static struct BitMap *listbg, *listwk;   /* the list's glass, and the image it is drawn in (one blit a redraw) */
static WORD lx, ly, lw, lh, rowh;
/* After the windows (and a divider) the list ends with what can be done
   with the program: rows nlist .. nlist + nact - 1. */
enum { ACT_KEEP = 1, ACT_REMOVE, ACT_SHOWWB, ACT_MINI, ACT_SHOW, ACT_CLOSE, ACT_ALL, ACT_FORCE };
static UBYTE act[6];
static LONG nact;
static UBYTE lmode;                     /* the list asks which window: LM_MINI / LM_SHOW / LM_CLOSE (0: no) */
static LONG lhid, lshow;                /* the listed program's windows minimised / showing (all of */
                                        /*  them, also beyond the rows shown) */
#define LM_MINI 1
#define LM_SHOW 2
#define LM_CLOSE 3

static LONG rows_hidden(BOOL hid);

static CONST_STRPTR act_text(LONG a)
{
    switch (a) {
    case ACT_KEEP:   return "Keep in Dock";
    case ACT_REMOVE: return "Remove from Dock";
    case ACT_SHOWWB: return "Show in Workbench";
    case ACT_MINI:   return rows_hidden(FALSE) > 1 ? "Minimise..." : "Minimise";   /* (...: which one?) */
    case ACT_SHOW: {                             /* with how many are minimised: "Show (3)..." */
        static char t[16];
        LONG v[1];
        v[0] = lhid;
        SD_FMT(t, rows_hidden(TRUE) > 1 ? "Show (%ld)..." : "Show (%ld)", v);
        return t;
    }
    case ACT_ALL:    return lmode == LM_CLOSE ? "Close All" : lmode == LM_MINI ? "Minimise All" : "Show All";
    case ACT_FORCE:  return "Force Quit...";
    default:         return nlist > 1 ? "Close..." : "Close";
    }
}
#define SEPH 5                          /* the divider's height above it */

static void do_act(struct Dock *d, LONG a);
static void pick_row(struct Dock *d, LONG r);
static void reveal(struct Window *w, struct Task *t);
static BOOL close_row(struct Dock *d, LONG r);
static void list_drop(struct Dock *d, LONG r);
static BOOL unminimise(struct Item *it);
static void show_one(struct Window *w, struct Task *t, BOOL hid);
static BOOL same_win(struct Window *x, struct Window *w, struct Task *t);
static void list_actions(struct Item *it);
static void close_program(struct Dock *d, struct Item *it);
static void end_program(struct Dock *d, struct Item *it);
static LONG list_row(WORD sx, WORD sy);
static BOOL is_mini(struct Window *w);
static void mini_drop(LONG k);
static LONG mini_at(struct Window *w);
#define MAXMINI 64
static struct { struct Window *win; struct Task *task; UBYTE shown, seen; } mini[MAXMINI];   /* see "minimised" below */
static LONG nmini;
static void close_next(void);
static void shell_sync(struct Dock *d, LONG n);
static void list_window_close(struct Dock *d);

/* The list window closed (the input handler looks at d->list: NULL first). */
static void list_window_close(struct Dock *d)
{
    struct Window *w = d->list;
    if (!w) return;
    d->list = NULL;
    CloseWindow(w);
}

/* Where row r of the list starts (r >= nlist: the actions, after a divider). */
static WORD row_y(LONG r) { return PADY + r * rowh + (r >= nlist && nlist ? SEPH : 0); }

/* Program names, any case (ASCII is enough: the system's Stricmp goes
   through the locale, too slow to do this often). */
static BOOL same_name(CONST_STRPTR a, CONST_STRPTR b)
{
    for (; *a && *b; a++, b++)
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b)) return FALSE;
    return *a == *b;
}

/* A program icon that can be found running (not drawers, disks or projects). */
BOOL run_is_tool(struct Item *it)
{
    return it->temp == RUN_TEMP || (it->dobj && it->dobj->do_Type == WBTOOL && !it->missing);
}

/* The last part of a path, copied (no library calls: runs under Forbid). */
static void last_part(char *out, CONST_STRPTR s, LONG len)
{
    LONG i, start = 0, n;
    for (i = 0; i < len && s[i]; i++) if (s[i] == ':' || s[i] == '/') start = i + 1;
    n = i - start < NAMELEN - 1 ? i - start : NAMELEN - 1;
    memcpy(out, s + start, n);
    out[n] = 0;
}

/* The name the task's program runs under: a Shell command's name while one
   runs, else the task's own name. */
static void task_name(struct Task *t, char *out, char *cmd)
{
    struct CommandLineInterface *cli;
    out[0] = 0;
    if (cmd) cmd[0] = 0;
    if (t->tc_Node.ln_Type == NT_PROCESS && (cli = BADDR(((struct Process *)t)->pr_CLI)) &&
        cli->cli_Module && cli->cli_CommandName) {
        UBYTE *b = BADDR(cli->cli_CommandName);
        LONG i, n = b[0] < CMDLEN - 1 ? b[0] : CMDLEN - 1;
        last_part(out, (CONST_STRPTR)b + 1, b[0]);
        for (i = 0; cmd && i < n && b[1 + i] != ':'; i++) ;
        if (cmd && i < n) { memcpy(cmd, b + 1, n); cmd[n] = 0; }   /* (only with a full path) */
    } else if (t->tc_Node.ln_Name) last_part(out, t->tc_Node.ln_Name, 255);
}

#define MAXTASKS 256
static struct Task *tasks[MAXTASKS];    /* the tasks there at the last snap_tasks() */
static LONG ntasks;
static BOOL tasksmore;                  /* more than fit: anything counts as there */

/* Copy the system's task lists. With interrupts off: under Forbid alone a
   Signal() from an interrupt can move a task from one list to the other
   during the walk, and it would be missed. */
static void snap_tasks(void)
{
    struct Node *n;
    LONG k = 0;
    Disable();
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ && k < MAXTASKS; n = n->ln_Succ) tasks[k++] = (struct Task *)n;
    tasksmore = n->ln_Succ != NULL;
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ && k < MAXTASKS; n = n->ln_Succ) tasks[k++] = (struct Task *)n;
    if (n->ln_Succ) tasksmore = TRUE;
    Enable();
    ntasks = k;
}

/* Was task t there at the last snap_tasks()? (The running task, the dock
   itself, is in neither list: never asked about.) */
static BOOL alive(struct Task *t)
{
    LONG k;
    for (k = 0; k < ntasks; k++) if (tasks[k] == t) return TRUE;
    return tasksmore;
}

/* Shell windows of the standard console handler have no IDCMP port, so they
   can't be found like other windows. Each Shell's console handler is asked
   for its window instead (ACTION_DISK_INFO: a console answers with its
   window in id_VolumeNode), without waiting: the answers come to cport and
   are taken at the next look. A console that never answers costs only its
   request (and at quit a little memory, left so its late answer has
   somewhere to go). Asked again when the number of Shells changes. */
#define MAXCON 32
#define CA_ASKED 1                      /* no answer yet */
#define CA_KNOWN 2                      /* win is its window */
#define CA_NONE  3                      /* answered without a window */
struct ConAsk {
    struct InfoData       id;           /* first: on a longword (a BPTR points to it) */
    struct StandardPacket sp;
    struct MsgPort       *con;          /* the console handler asked (NULL: its Shell is gone) */
    struct Task          *task;         /* ... its task */
    struct Window        *win;
    UBYTE                 state;
};
static struct ConAsk *ca[MAXCON];
static LONG nca, lastshells = -1;
static struct MsgPort *cport;

static void con_free(LONG k)
{
    FreeVec(ca[k]);
    ca[k] = ca[--nca];
}

/* The answers that came. */
static void con_reap(void)
{
    struct Message *m;
    LONG k;
    if (!cport) return;
    while ((m = GetMsg(cport))) {
        for (k = 0; k < nca && &ca[k]->sp.sp_Msg != m; k++) ;
        if (k == nca) continue;                  /* (not ours: can't happen) */
        if (!ca[k]->con) { con_free(k); continue; }   /* (its Shell went meanwhile) */
        ca[k]->win = ca[k]->sp.sp_Pkt.dp_Res1 ? (struct Window *)ca[k]->id.id_VolumeNode : NULL;
        ca[k]->state = ca[k]->win ? CA_KNOWN : CA_NONE;
    }
}

/* Ask console handler port con for its window. Under Forbid (the port
   can't go meanwhile): only memory and PutMsg. */
static void con_ask(struct MsgPort *con)
{
    struct ConAsk *a;
    if (!cport || nca == MAXCON || !(a = AllocVec(sizeof(*a), MEMF_PUBLIC | MEMF_CLEAR))) return;
    a->con = con;
    a->task = (struct Task *)con->mp_SigTask;
    a->state = CA_ASKED;
    a->sp.sp_Msg.mn_Node.ln_Name = (char *)&a->sp.sp_Pkt;
    a->sp.sp_Msg.mn_ReplyPort = cport;
    a->sp.sp_Pkt.dp_Link = &a->sp.sp_Msg;
    a->sp.sp_Pkt.dp_Port = cport;
    a->sp.sp_Pkt.dp_Type = ACTION_DISK_INFO;
    a->sp.sp_Pkt.dp_Arg1 = MKBADDR(&a->id);
    ca[nca++] = a;
    PutMsg(con, &a->sp.sp_Msg);
}

/* The console task whose Shell window w is (NULL: none known). Only reads. */
static struct Task *con_task(struct Window *w)
{
    LONG k;
    for (k = 0; k < nca; k++) if (ca[k]->state == CA_KNOWN && ca[k]->win == w && ca[k]->con) return ca[k]->task;
    return NULL;
}

/* A little while for answers still out (a long press on the Shell icon:
   its list should have them), at most n ticks. */
static void con_wait(LONG n)
{
    LONG k;
    for (; n > 0; n--) {
        con_reap();
        for (k = 0; k < nca && ca[k]->state != CA_ASKED; k++) ;
        if (k == nca) return;
        Delay(1);
    }
    con_reap();
}

/* The Shells' console ports cons[0..nc-1] now (under Forbid): new ones are
   asked; entries for gone ones are dropped (one with its question still
   out stays until the answer comes); with a new number of Shells, the
   known ones are asked again (a console may have opened a new window). */
static void con_update(struct MsgPort **cons, LONG nc, LONG shells)
{
    LONG k, j;
    for (k = 0; k < nca; k++) {
        for (j = 0; j < nc && cons[j] != ca[k]->con; j++) ;
        if ((j == nc || shells != lastshells) && ca[k]->con) {
            if (ca[k]->state == CA_ASKED) ca[k]->con = NULL;   /* (freed when it answers) */
            else { con_free(k--); continue; }
        }
    }
    lastshells = shells;
    for (j = 0; j < nc; j++) {                   /* (not twice to one that hasn't answered yet) */
        for (k = 0; k < nca && ca[k]->con != cons[j] &&
                    !(ca[k]->state == CA_ASKED && ca[k]->task == (struct Task *)cons[j]->mp_SigTask); k++) ;
        if (k == nca) con_ask(cons[j]);
    }
}

static BOOL busy;                       /* collect: a screen's layers were locked, not all seen */
static BOOL held_ibase;                 /* collect holds the IBase lock (and Forbid) ... */
static struct SignalSemaphore *held_li; /* ... and this screen's layer lock */
static ULONG held_lock;

/* Crash recovery: a crash inside collect() left these held. */
void run_crash_unlock(void)
{
    if (cport) cport->mp_Flags = (cport->mp_Flags & ~PF_ACTION) | PA_IGNORE;   /* (answers still out: no signal) */
    if (held_li) { ReleaseSemaphore(held_li); held_li = NULL; }
    if (held_ibase) { held_ibase = FALSE; Permit(); UnlockIBase(held_lock); }
}
static BOOL pending;                    /* the running others changed, the dock is not laid out yet */

/* Every window on every screen, front to back (screens front to back, then
   by depth), with its program's name and title. Our own windows and
   Workbench's backdrop are left out; Workbench's other windows (its drawers)
   are marked wb and get no name. */
static LONG collect(BOOL titles)
{
    static struct Task *wb;                      /* Workbench's task (looked up again when gone) */
    struct Task *me = FindTask(NULL);
    struct Screen *s;
    struct Layer *l;
    LONG n, i, k, try, ns, nl;
    struct MsgPort *cons[MAXWIN];
    con_reap();
    Forbid();
    nshells = ncon = 0;                          /* the Shells AmigaDOS knows, not run in the background */
    for (i = MaxCli(); i > 0; i--) {
        struct Process *p = FindCliProc(i);
        struct CommandLineInterface *c;
        struct MsgPort *con;
        if (p && (c = BADDR(p->pr_CLI)) && c->cli_Interactive && !c->cli_Background) {
            nshells++;
            if ((con = (struct MsgPort *)p->pr_ConsoleTask) && con->mp_SigTask && ncon < MAXWIN) {
                cons[ncon] = con;
                contask[ncon++] = (struct Task *)con->mp_SigTask;   /* its windows belong to this */
            }
        }
    }
    con_update(cons, ncon, nshells);             /* (their windows, if the console has no port) */
    Permit();
    /* The screens and windows: with the IBase lock (no screen opens or
       closes) and each screen's layer lock (no window closes meanwhile; it
       is only tried - if a program holds it, it may be in the middle of
       changing the list - and then looked at again a moment later). */
    for (try = 0; ; try++) {
    n = 0;
    busy = FALSE;
    held_lock = LockIBase(0);
    Forbid();                                    /* (the windows' tasks can't end meanwhile) */
    held_ibase = TRUE;                           /* (crash recovery: undone there) */
    snap_tasks();                                /* the tasks there now */
    if (!wb || tasksmore || !alive(wb) || !wb->tc_Node.ln_Name || strcmp(wb->tc_Node.ln_Name, "Workbench"))
        wb = FindTask("Workbench");
    for (s = IntuitionBase->FirstScreen, ns = 0; s && n < MAXWIN && ns < 32; s = s->NextScreen, ns++) {
        if (!AttemptSemaphore(&s->LayerInfo.Lock)) { busy = TRUE; continue; }
        held_li = &s->LayerInfo.Lock;
        for (l = s->LayerInfo.top_layer, nl = 0; l && n < MAXWIN && nl < 1024; l = l->back, nl++) {
            struct Window *w = (struct Window *)l->Window;
            struct Task *t;
            if (!w) continue;
            if (!w->UserPort) {                  /* a standard console's Shell window (con_ask), or none */
                if (!(t = con_task(w)) || !alive(t)) continue;
            } else if ((w->UserPort->mp_Flags & PF_ACTION) == PA_SOFTINT ||
                       !(t = (struct Task *)w->UserPort->mp_SigTask) || t == me || !alive(t) ||
                       (t == wb && (w->Flags & WFLG_BACKDROP)))
                continue;                        /* (a soft interrupt port has no task) */
            for (i = 0; i < n && own[i].win != w; i++) ;
            if (i < n) continue;                 /* a window's second layer (GZZ border) */
            own[n].win = w;
            own[n].task = t;
            own[n].scr = s;
            own[n].hid = LayersBase->lib_Version >= 45 && (l->Flags & LAYERHIDDEN);   /* (the flag is V45) */
            own[n].wb = t == wb;
            if (t == wb) own[n].name[0] = own[n].cmd[0] = 0;
            else task_name(t, own[n].name, own[n].cmd);
            for (k = 0; k < ncon && contask[k] != t; k++) ;
            own[n].con = k < ncon;
            own[n].stdcon = own[n].con;          /* (the standard console's, KingCON's ...) */
            own[n].widget = (w->Flags & WFLG_BORDERLESS) &&
                            !(w->Flags & (WFLG_CLOSEGADGET | WFLG_DEPTHGADGET | WFLG_DRAGBAR | WFLG_SIZEGADGET)) &&
                            (w->Width < s->Width / 2 || w->Height < s->Height / 2);
            own[n].title[0] = 0;
            if ((titles || t == wb) && w->Title) {   /* (the held list shows them; Workbench's: whose drawer) */
                for (i = 0; i < TITLELEN - 1 && w->Title[i]; i++) own[n].title[i] = w->Title[i];
                own[n].title[i] = 0;
            }
            n++;
        }
        held_li = NULL;
        ReleaseSemaphore(&s->LayerInfo.Lock);
    }
    for (k = 0; k < nmini; k++) {                /* windows the dock hid: still there? (not in the layers) */
        struct Screen *ms;
        struct Window *x;
        BOOL found = FALSE;
        for (i = 0; i < n && own[i].win != mini[k].win; i++) ;
        if (i < n) {                             /* in the layers: still hidden? A new window at the */
            if (own[i].task != mini[k].task) mini_drop(k--);   /* same place is not ours to show */
            else if (own[i].hid) mini[k].seen = 0;
            else if (mini[k].shown || ++mini[k].seen >= 2) mini_drop(k--);   /* (shown by us, or by */
            continue;                            /*  someone else on two looks: HideWindow may lag) */
        }
        for (ms = IntuitionBase->FirstScreen; ms && !found; ms = ms->NextScreen)
            for (x = ms->FirstWindow; x && !found; x = x->NextWindow)
                if (same_win(x, mini[k].win, mini[k].task)) found = TRUE;
        if (!found || !alive(mini[k].task)) { mini_drop(k--); continue; }
        if (n == MAXWIN) continue;
        own[n].win = mini[k].win;
        own[n].task = mini[k].task;
        own[n].scr = NULL;
        own[n].hid = TRUE;
        own[n].wb = mini[k].task == wb;
        if (own[n].wb) own[n].name[0] = own[n].cmd[0] = 0;
        else task_name(mini[k].task, own[n].name, own[n].cmd);
        for (i = 0; i < ncon && contask[i] != mini[k].task; i++) ;
        own[n].con = i < ncon;
        own[n].stdcon = FALSE;
        own[n].widget = FALSE;
        own[n].title[0] = 0;
        if ((titles || own[n].wb) && mini[k].win->Title) {
            for (i = 0; i < TITLELEN - 1 && mini[k].win->Title[i]; i++) own[n].title[i] = mini[k].win->Title[i];
            own[n].title[i] = 0;
        }
        n++;
    }
    held_ibase = FALSE;
    Permit();
    UnlockIBase(held_lock);
    if (!busy || try == 3) break;
    Delay(1);                                    /* a program is changing its windows: again */
    }
    {                                             /* their first-seen numbers; the gone ones dropped */
        static struct { struct Window *win; struct Task *task; ULONG seq; } now[MAXWIN];
        for (i = 0; i < n; i++) {
            for (k = 0; k < nseen && !(seen[k].win == own[i].win && seen[k].task == own[i].task); k++) ;
            own[i].seq = k < nseen ? seen[k].seq : ++seqno;
            now[i].win = own[i].win; now[i].task = own[i].task; now[i].seq = own[i].seq;
        }
        memcpy(seen, now, n * sizeof(now[0]));
        nseen = n;
    }
    return n;
}

/* The Shell icon (its windows are the console handler's, see above). */
static BOOL is_shell(struct Item *it)
{
    return it->dobj && !it->missing && same_name(FilePart(it->path), "Shell");
}

/* Did the dock start this task for 'it'? */
static BOOL launched(struct Item *it, struct Task *t)
{
    LONG k;
    for (k = 0; k < MAXLAUNCH; k++)
        if (lt[k] && lt[k]->it == it && lt[k]->task == t) return TRUE;
    return FALSE;
}

/* A docked drawer, disk or Trash (its windows are Workbench's). */
BOOL run_is_drawer(struct Item *it)
{
    return !it->temp && it->dobj && !it->missing &&
           (it->dobj->do_Type == WBDRAWER || it->dobj->do_Type == WBDISK || it->dobj->do_Type == WBGARBAGE);
}

/* Does t start with 'name' (any case), followed by its end, a space, or a
   ':' that ends it ("Work:" - not "Work:Music", a drawer on that disk)? */
static BOOL name_at(CONST_STRPTR t, CONST_STRPTR name)
{
    for (; *name; t++, name++)
        if ((*t >= 'A' && *t <= 'Z' ? *t + 32 : *t) != (*name >= 'A' && *name <= 'Z' ? *name + 32 : *name)) return FALSE;
    return !*t || *t == ' ' || (*t == ':' && (!t[1] || t[1] == ' '));
}

/* Window title t is that of drawer 'name': "Prefs", a disk's "Work" with its
   free space after it, or a full path ("Work:Music") when Workbench shows
   those. */
static BOOL title_is(CONST_STRPTR t, CONST_STRPTR name)
{
    CONST_STRPTR s = t, p;
    for (p = t; *p && *p != ' '; p++) if ((*p == ':' || *p == '/') && p[1] && p[1] != ' ') s = p + 1;
    return name_at(t, name) || (s != t && name_at(s, name));
}

/* A window of the program behind 'it': started by the dock, or by name; for
   the Shell icon a window of a Shell's console handler; for a docked drawer
   a Workbench window with its title (wbname, see link_drawers); for the
   Workbench icon any Workbench window. */
static BOOL is_it(struct Own *o, struct Item *it)
{
    LONG k;
    if (it->temp == RUN_WB) return o->wb;
    if (run_is_drawer(it)) return o->wb && it->wbname[0] && title_is(o->title, it->wbname);
    if (o->wb) return FALSE;
    if (is_shell(it)) {
        for (k = 0; k < ncon; k++) if (contask[k] == o->task) return TRUE;
        return FALSE;
    }
    return launched(it, o->task) || same_name(o->name, FilePart(it->path));
}

/* An icon the dock follows: a program, the Shell, a docked drawer, or
   Workbench's drawers. */
BOOL run_trackable(struct Item *it)
{
    return run_is_tool(it) || is_shell(it) || it->temp == RUN_WB || run_is_drawer(it);
}

/* The title a drawer's window has: the last part of its path, or a disk's
   name. */
static void title_of(CONST_STRPTR path, char *out)
{
    CONST_STRPTR s = path, e;
    LONG n;
    for (e = path; *e; e++) if (*e == ':' || *e == '/') { if (e[1]) s = e + 1; }
    for (e = s; *e && *e != ':' && *e != '/'; e++) ;
    n = e - s < 31 ? e - s : 31;
    memcpy(out, s, n);
    out[n] = 0;
}

#define MAXDRAW 16                      /* open drawers looked at */

/* A hash of the docked drawers' names: changed when the icons changed. */
static ULONG wbnames(struct Dock *d)
{
    struct Item *it;
    ULONG h = 0;
    CONST_STRPTR c;
    FOR_ITEMS(d, it) for (c = it->wbname; *c; c++) h = h * 31 + *c;
    return h;
}

/* Docked drawers and disks open in Workbench: their windows' title (wbname),
   from Workbench's list of open drawers (V44+) compared by lock. Looked up
   again only when Workbench's windows or the icons changed. TRUE: a drawer
   was linked or unlinked. */
static BOOL link_drawers(struct Dock *d, LONG n)
{
    static ULONG last;
    static UBYTE again;                          /* (Workbench's list may lag behind a new window: once more) */
    struct List *dl = NULL;
    struct Node *dn;
    struct Item *it;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin;
    BPTR locks[MAXDRAW], l;
    CONST_STRPTR names[MAXDRAW];
    ULONG sig = 1, was;
    LONG i, nd = 0;
    BOOL any = FALSE;
    for (i = 0; i < n; i++) if (own[i].wb) { sig = sig * 31 + (ULONG)own[i].win; any = TRUE; }
    FOR_ITEMS(d, it) if (run_is_drawer(it)) sig = sig * 31 + (ULONG)it;
    if (sig == last && !again) return FALSE;
    again = sig != last;
    last = sig;
    was = wbnames(d);
    FOR_ITEMS(d, it) it->wbname[0] = 0;
    if (!any ||
        !WorkbenchControl(NULL, WBCTRLA_GetOpenDrawerList, &dl, TAG_END) || !dl) return was != 0;
    oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;                 /* (a disk not inserted: no requester) */
    for (dn = dl->lh_Head; dn->ln_Succ && nd < MAXDRAW; dn = dn->ln_Succ)
        if (dn->ln_Name && (locks[nd] = Lock(dn->ln_Name, SHARED_LOCK))) names[nd++] = dn->ln_Name;
    FOR_ITEMS(d, it) {
        if (!run_is_drawer(it) || !nd || !(l = Lock(it->path, SHARED_LOCK))) continue;
        for (i = 0; i < nd && SameLock(l, locks[i]) != LOCK_SAME; i++) ;
        if (i < nd) title_of(names[i], it->wbname);
        UnLock(l);
    }
    while (nd > 0) UnLock(locks[--nd]);
    me->pr_WindowPtr = oldwin;
    WorkbenchControl(NULL, WBCTRLA_FreeOpenDrawerList, dl, TAG_END);
    return wbnames(d) != was;
}

/* How many copies of it run: the tasks the dock started for it, and those
   owning its windows. */
static LONG copies_of(struct Item *it, LONG n)
{
    struct Task *seen[MAXWIN + MAXLAUNCH];
    LONG ns = 0, i, k, j;
    BOOL sh = is_shell(it);
    CONST_STRPTR fp = FilePart(it->path);
    for (k = 0; k < MAXLAUNCH; k++)              /* (the tasks the dock started: counted here already, */
        if (lt[k] && lt[k]->it == it) seen[ns++] = lt[k]->task;   /*  so below only by name) */
    for (i = 0; i < n; i++) {
        if (sh ? !own[i].con : !same_name(own[i].name, fp)) continue;
        for (j = 0; j < ns && seen[j] != own[i].task; j++) ;
        if (j == ns) seen[ns++] = own[i].task;
    }
    return ns;
}

/* ------------------------------------- programs running, not on the dock */

/* They come after the kept icons, behind a divider: a click brings one to
   the front, a held button lists its windows, dragging it in front of the
   divider keeps it. When the dock is full, the last place is the "more"
   cell, which lists the rest. The items not shown wait in d->runs. */
static char morename[] = "More";
static struct Item more = { { NULL, NULL }, morename, morename };
static BOOL morewas;                    /* it was shown before this layout */

/* Console handlers' windows that are not a Shell's (a program's output). */
static const char *const conname[] = { "CON", "RAW", "KingCON", "KCON", "ViNCEd", NULL };

/* Does window o belong to an icon kept on the dock (shown or not)? */
static BOOL kept_owner(struct Dock *d, struct Own *o)
{
    struct Item *it;
    LONG pass;
    for (pass = 0; pass < 2; pass++)
        for (it = (struct Item *)(pass ? &d->extra : &d->items)->mlh_Head; it->node.mln_Succ;
             it = (struct Item *)it->node.mln_Succ)
            if (!it->temp) {
                if (is_shell(it)) { if (o->con) return TRUE; }
                else if (run_is_tool(it) && (same_name(o->name, FilePart(it->path)) || launched(it, o->task)))
                    return TRUE;
            }
    return FALSE;
}

static BOOL kept_shell(struct Dock *d)
{
    struct Item *it;
    LONG pass;
    for (pass = 0; pass < 2; pass++)
        for (it = (struct Item *)(pass ? &d->extra : &d->items)->mlh_Head; it->node.mln_Succ;
             it = (struct Item *)it->node.mln_Succ)
            if (!it->temp && is_shell(it)) return TRUE;
    return FALSE;
}

/* The program file for 'name': from Workbench's list of the programs it
   started (fetched once, *wbl), the Shell command's own path, or the usual
   drawers. FALSE: not known. */
static BOOL find_path(CONST_STRPTR name, CONST_STRPTR cmd, char *out, struct List **wbl, BOOL *wbtried)
{
    static const char *const dirs[] = { "SYS:Utilities/", "SYS:Tools/", "SYS:System/", "SYS:Prefs/",
                                        "SYS:Tools/Commodities/", NULL };
    struct Node *n;
    LONG i;
    if (!*wbtried) {
        *wbtried = TRUE;
        if (!WorkbenchControl(NULL, WBCTRLA_GetProgramList, wbl, TAG_END)) *wbl = NULL;
    }
    if (*wbl)
        for (n = (*wbl)->lh_Head; n->ln_Succ; n = n->ln_Succ)
            if (n->ln_Name && same_name(FilePart(n->ln_Name), name) && strlen(n->ln_Name) < SD_LINEMAX) {
                strcpy(out, n->ln_Name);
                return TRUE;
            }
    if (cmd[0] && same_name(FilePart(cmd), name) && dock_exists(cmd)) { strcpy(out, cmd); return TRUE; }
    for (i = 0; dirs[i]; i++) {
        strcpy(out, dirs[i]);
        strcat(out, name);
        if (dock_exists(out)) return TRUE;
    }
    {                                            /* the dock's own Shell path (its own locks) */
        struct Process *me = (struct Process *)FindTask(NULL);
        struct CommandLineInterface *cli = BADDR(me->pr_CLI);
        APTR oldwin = me->pr_WindowPtr;
        BPTR *p, old, l;
        BOOL found = FALSE;
        me->pr_WindowPtr = (APTR)-1;             /* (a disk not inserted: no requester) */
        for (p = cli ? BADDR(cli->cli_CommandDir) : NULL; p && !found; p = BADDR(p[0])) {
            old = CurrentDir(p[1]);              /* (each entry: next, drawer lock) */
            l = Lock(name, SHARED_LOCK);
            CurrentDir(old);
            if (l) {
                UnLock(l);
                found = NameFromLock(p[1], out, SD_LINEMAX) && AddPart(out, name, SD_LINEMAX);
            }
        }
        me->pr_WindowPtr = oldwin;
        if (found) return TRUE;
    }
    return FALSE;
}

/* Is 'name' in the IGNORE list ("Name,Name", any case)? */
static BOOL ignored(struct Dock *d, CONST_STRPTR name)
{
    CONST_STRPTR p = d->o->ignore, q;
    LONG n = strlen(name), k;
    while (p && *p) {
        while (*p == ' ' || *p == ',' || *p == '"') p++;   /* (quotes too: IGNORE="a,b" in a tooltype) */
        for (q = p; *q && *q != ','; q++) ;
        for (k = q - p; k > 0 && (p[k - 1] == ' ' || p[k - 1] == '"'); k--) ;   /* (spaces after a name) */
        if (k == n) {
            LONG i;
            for (i = 0; i < n && ((p[i] | 32) == (name[i] | 32)); i++) ;
            if (i == n) return TRUE;
        }
        p = q;
    }
    return FALSE;
}

static void free_temp(struct Dock *d, struct Item *t)
{
    Remove((struct Node *)t);
    run_forget(t);
    if (d->last == t) d->last = NULL;
    if (d->pressed == t) d->pressed = NULL;
    scale_free(t);
    if (t->dobj) FreeDiskObject(t->dobj);
    FreeVec(t->path);
    FreeVec(t->label);
    FreeVec(t);
}

/* Out of the icon list, into d->runs (in order); the "more" cell out. */
void run_layout_out(struct Dock *d)
{
    struct Item *it, *p;
    for (it = (struct Item *)d->items.mlh_TailPred; it->node.mln_Pred; it = p) {
        p = (struct Item *)it->node.mln_Pred;
        if (!it->temp) continue;
        Remove((struct Node *)it);
        if (it == &more) { scale_free(it); morewas = TRUE; }
        else AddHead((struct List *)&d->runs, (struct Node *)it);
    }
}

void run_layout_all(struct Dock *d)
{
    struct Node *n;
    while ((n = RemHead((struct List *)&d->runs))) AddTail((struct List *)&d->items, n);
}

/* After the kept icons: as many as there is room for, the rest behind the
   "more" cell. */
void run_layout_in(struct Dock *d, LONG room)
{
    struct Node *n;
    LONG nt = 0, show;
    BOOL was = morewas;
    morewas = FALSE;
    for (n = (struct Node *)d->runs.mlh_Head; n->ln_Succ; n = n->ln_Succ) {
        nt++;
        if (((struct Item *)n)->temp == RUN_WB && n != (struct Node *)d->runs.mlh_Head) {   /* (first: in front */
            struct Node *p = n->ln_Pred;                                                  /*  of the divider) */
            Remove(n);
            AddHead((struct List *)&d->runs, n);
            n = p;
        }
    }
    if (!nt || room <= 0) return;
    show = nt <= room ? nt : room - 1;
    while (show-- > 0 && (n = RemHead((struct List *)&d->runs))) AddTail((struct List *)&d->items, n);
    if (!d->runs.mlh_Head->mln_Succ) return;
    if (!was) {                                  /* new: it glides in from the end */
        more.sx = d->left + (d->horiz ? d->w - d->cw : 0);
        more.sy = d->top + (d->horiz ? 0 : d->h - d->ch);
        more.keep = TRUE;
    }
    more.temp = RUN_MORE;
    more.running = nt - (nt <= room ? nt : room - 1);
    more.iw = more.ih = d->o->noicon ? 0 : d->maxih;
    AddTail((struct List *)&d->items, (struct Node *)&more);
}

/* The dock is closing: the icons of those not shown too. */
void run_images_free(struct Dock *d)
{
    struct Item *it;
    for (it = (struct Item *)d->runs.mlh_Head; it->node.mln_Succ; it = (struct Item *)it->node.mln_Succ) {
        scale_free(it);
        if (it->dobj) { FreeDiskObject(it->dobj); it->dobj = NULL; }
    }
    scale_free(&more);
}

/* The running others not to be shown now are freed (RUNNING=0: all; RUNHIDE:
   the programs; WBHIDE or IGNORE Workbench: the Workbench icon). */
void run_free_hidden(struct Dock *d)
{
    struct Item *it, *nx;
    BOOL nowb = !d->o->running || d->o->wbhide || ignored(d, "Workbench"), noprog = !d->o->running || d->o->runhide;
    for (it = (struct Item *)d->runs.mlh_Head; (nx = (struct Item *)it->node.mln_Succ); it = nx)
        if (it->temp == RUN_WB ? nowb : noprog) free_temp(d, it);
}

void run_free_all(struct Dock *d)
{
    run_layout_out(d);
    while (d->runs.mlh_Head->mln_Succ) free_temp(d, (struct Item *)d->runs.mlh_Head);
}

/* A new running other, at the end: its program file is looked up once. */
static struct Item *new_temp(struct Dock *d, CONST_STRPTR name, CONST_STRPTR cmd, BOOL shell,
                             struct List **wbl, BOOL *wbtried)
{
    char path[SD_LINEMAX];
    struct Item *it;
    BOOL known = shell ? dock_exists(strcpy(path, "SYS:System/Shell")) : find_path(name, cmd, path, wbl, wbtried);
    if (!known) strcpy(path, name);
    if (!(it = AllocVec(sizeof(*it), MEMF_CLEAR))) return NULL;
    if (!(it->path = sd_strdup(path))) { FreeVec(it); return NULL; }
    it->temp = RUN_TEMP;
    it->nofile = !known;
    it->sx = d->left + (d->horiz ? d->w - d->cw : 0);   /* it glides in from the end */
    it->sy = d->top + (d->horiz ? 0 : d->h - d->ch);
    it->keep = TRUE;
    AddTail((struct List *)&d->runs, (struct Node *)it);
    return it;
}

/* The Workbench icon (its drawer windows), first of the running others: in
   front of the divider. Never kept or dragged; the standard drawer icon. */
static struct Item *new_wb(struct Dock *d)
{
    struct Item *it;
    if (!(it = AllocVec(sizeof(*it), MEMF_CLEAR))) return NULL;
    if (!(it->path = sd_strdup("Workbench"))) { FreeVec(it); return NULL; }
    it->temp = RUN_WB;
    it->nofile = TRUE;
    it->sx = d->left + (d->horiz ? d->w - d->cw : 0);
    it->sy = d->top + (d->horiz ? 0 : d->h - d->ch);
    it->keep = TRUE;
    AddHead((struct List *)&d->runs, (struct Node *)it);
    return it;
}

/* A running other dragged off the dock: added to IGNORE (saved), so it is
   not shown again; the settings window's Hidden programs lists them. */
BOOL run_hide(struct Dock *d, struct Item *it)
{
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME,
        "Don't show %s on the dock\nwhen it runs?\n\n(Settings, Advanced: Hidden programs\nshows it again.)",
        "Hide|Cancel" };
    char line[SD_LINEMAX + 16];
    CONST_STRPTR name = FilePart(it->path), old = d->o->ignore ? d->o->ignore : (STRPTR)"";
    LONG n = strlen(old);
    if (!*name || n + strlen(name) + 12 > SD_LINEMAX) { sd_beep(d); return FALSE; }
    if (EasyRequest(d->win, &es, NULL, (LONG)name) != 1) return FALSE;
    strcpy(line, "IGNORE=\"");
    strcat(line, old);
    while (n > 0 && (line[8 + n - 1] == ' ' || line[8 + n - 1] == ',')) line[8 + --n] = 0;
    if (n) strcat(line, ",");
    strcat(line, name);
    strcat(line, "\"");
    if (!config_set(d, line)) { sd_beep(d); return FALSE; }
    am_prefs_sync(d);                            /* (an open settings window: its Hidden programs) */
    config_store(d);
    dz_suspend();
    free_temp(d, it);
    dock_reopen_glide(d);
    return TRUE;
}

/* Dropped in front of the divider, but its program file is not known: ask
   for it. TRUE: found (the icon is loaded again with the next layout). */
BOOL run_locate(struct Dock *d, struct Item *it)
{
    struct FileRequester *fr;
    char path[SD_LINEMAX], title[64];
    LONG v[1];
    BOOL ok = FALSE;
    if (!AslBase || !(fr = AllocAslRequestTags(ASL_FileRequest, TAG_END))) return FALSE;
    v[0] = (LONG)FilePart(it->path);
    if (strlen((char *)v[0]) > 40) v[0] = (LONG)"the program";
    SD_FMT(title, "Where is %s?", v);
    if (AslRequestTags(fr, ASLFR_Screen, d->scr, ASLFR_TitleText, title, ASLFR_InitialDrawer, "SYS:",
                       ASLFR_InitialFile, FilePart(it->path), TAG_END) && fr->fr_File[0]) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        if (AddPart(path, fr->fr_File, sizeof(path)) && dock_exists(path)) {
            char *p = sd_strdup(path);
            if (p) {
                FreeVec(it->path);
                it->path = p;
                it->nofile = FALSE;
                scale_free(it);                  /* its own icon instead of the standard one */
                if (it->dobj) { FreeDiskObject(it->dobj); it->dobj = NULL; }
                ok = TRUE;
            }
        }
    }
    FreeAslRequest(fr);
    return ok;
}

/* The running others as found now (n windows in own[]): new ones added at
   the end, ended ones removed. Shells count as one, when AmigaDOS knows an
   interactive one (their windows can't always be told: the standard console
   handler's have no IDCMP port). TRUE: changed, the dock is laid out again. */
static BOOL sync(struct Dock *d, LONG n, BOOL lmb)
{
    static struct Item *cur[MAXWIN];
    static BOOL seen[MAXWIN], fresh[MAXWIN];
    struct Item *it;
    struct List *wbl = NULL;
    BOOL wbtried = FALSE, changed = FALSE, suspended = FALSE, kshell = kept_shell(d);
    LONG nc = 0, i, j, pass;
    for (pass = 0; pass < 2; pass++)
        for (it = (struct Item *)(pass ? &d->runs : &d->items)->mlh_Head; it->node.mln_Succ;
             it = (struct Item *)it->node.mln_Succ)
            if ((it->temp == RUN_TEMP || it->temp == RUN_WB) && nc < MAXWIN) { seen[nc] = fresh[nc] = FALSE; cur[nc++] = it; }
    for (i = -1; i < n && d->o->running && !d->o->runhide; i++) {
        CONST_STRPTR name, cmd = "";
        struct Own *o = i >= 0 ? &own[i] : NULL;
        if (!o) {                                /* first: the Shells */
            if (!nshells || kshell || ignored(d, "Shell")) continue;
            name = "Shell";
        } else {
            name = o->name;
            if (o->con || o->widget || !name[0]) continue;
            for (j = 0; conname[j] && !same_name(name, conname[j]); j++) ;
            if (conname[j]) continue;
            for (j = 0; j < n && !(own[j].con && same_name(own[j].name, name)); j++) ;
            if (j < n) continue;                 /* a console window that is not a Shell's */
            if (ignored(d, name) || kept_owner(d, o)) continue;
            cmd = o->cmd;
        }
        for (j = 0; j < nc; j++)                 /* known already */
            if (cur[j]->temp == RUN_TEMP &&
                (same_name(FilePart(cur[j]->path), name) || (o && launched(cur[j], o->task)))) break;
        if (j < nc) { seen[j] = TRUE; continue; }
        if (nc < MAXWIN && (it = new_temp(d, name, cmd, !o, &wbl, &wbtried))) {
            seen[nc] = fresh[nc] = TRUE;
            cur[nc++] = it;
            changed = TRUE;
        }
    }
    if (wbl) WorkbenchControl(NULL, WBCTRLA_FreeProgramList, wbl, TAG_END);
    for (i = 0; i < n && !own[i].wb; i++) ;      /* Workbench's drawers open: its icon */
    if (i < n && d->o->running && !d->o->wbhide && !ignored(d, "Workbench")) {
        for (j = 0; j < nc && cur[j]->temp != RUN_WB; j++) ;
        if (j < nc) seen[j] = TRUE;
        else if (nc < MAXWIN && (it = new_wb(d))) {
            seen[nc] = TRUE;
            fresh[nc] = FALSE;
            cur[nc++] = it;
            it->running = 1;
            changed = TRUE;
        }
    }
    for (j = 0; j < nc; j++)                     /* (started by the dock, no window yet: still there) */
        for (i = 0; i < MAXLAUNCH && !seen[j]; i++) if (lt[i] && lt[i]->it == cur[j]) seen[j] = TRUE;
    for (j = 0; j < nc; j++)
        if (!seen[j] && lmb) changed = TRUE;     /* (a click, maybe a drag: freed with the next layout) */
        else if (!seen[j]) {
            if (!suspended) { dz_suspend(); suspended = TRUE; }   /* (the drop-zone hook walks the icon list) */
            free_temp(d, cur[j]);
            changed = TRUE;
        }
    for (j = 0; j < nc; j++)                     /* new ones: their number at once (the others: */
        if (fresh[j]) cur[j]->running = same_name(FilePart(cur[j]->path), "Shell") ? nshells : copies_of(cur[j], n);
    return changed;                              /*  scan counts them and draws the changes) */
}

/* Is x the window w of task t (not a new window at the same address)? */
static BOOL same_win(struct Window *x, struct Window *w, struct Task *t)
{
    return x == w && (x->UserPort ? x->UserPort->mp_SigTask == (APTR)t : con_task(x) == t);   /* (a Shell's: */
}                                                /*  by its console) */

/* The screen of window w of task t, NULL: it is gone. Under Forbid or the
   IBase lock. */
static struct Screen *win_screen(struct Window *w, struct Task *t)
{
    struct Screen *s;
    struct Window *x;
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        for (x = s->FirstWindow; x; x = x->NextWindow)
            if (same_win(x, w, t)) return s;
    return NULL;
}

/* Is the window there, active and in front on its screen? */
static BOOL front_now(struct Window *w, struct Task *t)
{
    struct Screen *s;
    BOOL now;
    Forbid();
    now = (s = win_screen(w, t)) && IntuitionBase->ActiveWindow == w && w->WLayer == s->LayerInfo.top_layer;
    Permit();
    return now;
}

/* The window to the front (and its screen, and active) - if it still exists.
   Under Forbid its program can't close it between the check and the calls. */
static void to_front(struct Window *w, struct Task *t)
{
    struct Screen *s;
    Forbid();
    if ((s = win_screen(w, t)) && s != IntuitionBase->FirstScreen) ScreenToFront(s);
    if (win_screen(w, t)) {                      /* (again: ScreenToFront may have waited) */
        WindowToFront(w);
        ActivateWindow(w);
    }
    Permit();
}

/* All of the program's windows (own[] rows of 'it') to the front, in the
   order they are in, the frontmost one active; minimised ones only if
   'hid' (just shown again). Under Forbid as to_front. */
static void all_front(struct Item *it, LONG n, LONG first, BOOL hid)
{
    struct Screen *s;
    LONG i;
    Forbid();
    if ((s = win_screen(own[first].win, own[first].task)) && s != IntuitionBase->FirstScreen) ScreenToFront(s);
    for (i = n - 1; i >= first; i--)              /* back to front: the frontmost ends on top */
        if (is_it(&own[i], it) && (hid || !own[i].hid) && win_screen(own[i].win, own[i].task))
            WindowToFront(own[i].win);
    if (win_screen(own[first].win, own[first].task)) ActivateWindow(own[first].win);
    Permit();
}

/* A click on a program icon: if it runs, all its windows that show (every
   copy) come to the front, the frontmost active; minimised ones stay so,
   unless all are - then they all come back. If they are all in front
   already and it is the active program, the backmost of them comes
   forward instead, so clicking again goes through its windows. FALSE: not
   running. */
static BOOL front(struct Item *it, BOOL back);

BOOL run_front(struct Dock *d, struct Item *it)
{
    LONG n, i;
    BOOL back;
    (void)d;
    if (!run_trackable(it)) return FALSE;
    n = collect(FALSE);
    for (i = 0; i < n && !(is_it(&own[i], it) && !own[i].hid); i++) ;
    if (i < n) return front(it, FALSE);
    back = unminimise(it);                       /* (all minimised: all back) */
    return front(it, TRUE) || back;
}

/* run_front's windows to the front; with 'back' also those just shown
   again (they may still look hidden for a moment). */
static BOOL front(struct Item *it, BOOL back)
{
    LONG n, i, first = -1, last = -1;
    BOOL infront = TRUE, other = FALSE;
    struct Screen *s = NULL;
    n = collect(FALSE);
    for (i = 0; i < n; i++) {
        if (own[i].hid && !(back && is_it(&own[i], it))) continue;   /* (minimised: left out) */
        if (own[i].scr != s) { s = own[i].scr; other = FALSE; }   /* (each screen: its windows front to back) */
        if (is_it(&own[i], it)) {
            if (first < 0) first = i;
            last = i;
            if (other) infront = FALSE;          /* another program's window is in front of this one */
        } else other = TRUE;
    }
    if (first < 0) return FALSE;
    if (infront && first != last && own[first].win == in_prevwin) to_front(own[last].win, own[last].task);
    else all_front(it, n, first, back);
    return TRUE;
}

/* The running others change the dock at most every 0.4 s (a program that
   opens and closes windows in a burst would otherwise resize it on and on). */
static struct timeval lastrel;

static BOOL relayout_ok(void)
{
    struct timeval now;
    if (!TimerBase) return TRUE;
    GetSysTime(&now);
    SubTime(&now, &lastrel);
    return now.tv_secs > 0 || now.tv_micro >= 400000;
}

static void relayout_done(void)
{
    if (TimerBase) GetSysTime(&lastrel);
}

/* How many copies of each program run; their icons are drawn again when
   that changed. FALSE: not now (the dock is sliding, gliding or held). */
static BOOL scan(struct Dock *d)
{
    static BOOL scanning;
    LONG n, i, copies;
    struct Item *it;
    BOOL later = FALSE, lmb;
    if (!d->o->running) return TRUE;             /* RUNNING=0: nothing to show, nothing to look for */
    if (scanning || !d->win || (d->vis != d->full && d->vis != 0) || d->dragging || d->lmb || an_running()) {
#ifdef SD_DEBUG
        run_skips++;
#endif
        return FALSE;
    }
#ifdef SD_DEBUG
    {
    struct timeval t0, t1;
    ULONG us;
    GetSysTime(&t0);
#endif
    n = collect(FALSE);
#ifdef SD_DEBUG
    run_scans++; run_found = n;
#endif
    if (busy) { later = TRUE; goto done; }       /* (not all windows seen: again in a moment) */
    shell_sync(d, n);                            /* Shell windows: drop targets */
    if (link_drawers(d, n)) lastsig = 0;         /* (docked drawers open in Workbench: counted anew) */
    {                                             /* the same windows and Shells as last time: nothing to count */
        ULONG sig = n * 31 + nshells + d->o->runhide + 2 * d->o->wbhide, wsum = 0;
        CONST_STRPTR c;
        for (c = d->o->ignore; c && *c; c++) sig = sig * 31 + *c;   /* (and the same IGNORE list) */
        for (i = 0; i < n; i++) {                /* (the windows in any order: a click on another */
            ULONG h = (ULONG)own[i].win * 31 + (ULONG)own[i].task;   /*  window changes nothing) */
            h ^= h >> 13;
            wsum += h * 2654435761UL + own[i].name[0];
        }
        sig = sig * 31 + wsum;
        FOR_ITEMS(d, it) sig = sig * 31 + (ULONG)it + it->missing;   /* (and the same icons) */
        if (sig == lastsig) goto done;
        lastsig = sig;
    }
    if (d->list) lastsig = 0;                    /* (the list is open: the others are looked at after it) */
    else if (!relayout_ok()) { lastsig = 0; later = TRUE; }   /* (changed a moment ago: again shortly) */
    else if (sync(d, n, lmb = d->lmb) || pending) {   /* programs not on the dock came or went */
        if (lmb) { pending = TRUE; lastsig = 0; later = TRUE; goto done; }   /* (a click, maybe a drag: the */
                                                 /* button read once, as sync saw it) */
        scanning = TRUE;
        if (d->vis == 0) pending = !dock_relayout_hidden(d);   /* (auto-hide: out of sight, ready when it comes out) */
        else { dock_reopen_glide(d); pending = FALSE; }
        relayout_done();
        scanning = FALSE;
        lastsig = 0;
        if (pending) later = TRUE;
        if (!d->win) goto done;
    }
    FOR_ITEMS(d, it) {
        if (it->temp == RUN_MORE) continue;      /* (its number: how many it holds) */
        copies = 0;
        if (is_shell(it)) copies = nshells;
        else if (it->temp == RUN_WB || run_is_drawer(it))   /* (Workbench's windows: how many) */
            for (i = 0; i < n; i++) copies += is_it(&own[i], it);
        else if (run_is_tool(it)) copies = copies_of(it, n);
        if (copies > 99) copies = 99;
        {                                        /* minimised windows: a pulsing dot */
            BOOL m = FALSE;
            for (i = 0; i < n && !m; i++) m = own[i].hid && is_it(&own[i], it);
            if (m != it->mini) { it->mini = m; it->running = -1; }   /* (redrawn below) */
        }
        if (it->running != copies) {
            it->running = copies;
            if (!copies) it->asked = FALSE;      /* (gone: a next copy starts afresh) */
            if (d->vis != d->full) continue;     /* hidden: drawn when it comes out */
            dock_show_item(d, it, FALSE);
            if (d->tip && d->tipcell >= 0 && dock_item(d, d->tipcell) == it) {
                tip_hide(d);                     /* its hover label shows the count: anew */
                tip_update(d);
            }
#ifdef SD_DEBUG
            run_redraws++;
#endif
        }
    }
    dock_runcells(d);                            /* (for Del over a running icon) */
done:
#ifdef SD_DEBUG
    GetSysTime(&t1);
    us = (t1.tv_secs - t0.tv_secs) * 1000000 + t1.tv_micro - t0.tv_micro;
    run_us_sum += us;
    if (us > run_us_max) run_us_max = us;
    }
#endif
    return !later;                               /* (put off: look again in a moment) */
}

static LONG pulse_left;                 /* us until the next look, pulsing meanwhile */

/* A minimised program's dot pulses while the dock is out and still. */
static BOOL pulsing(struct Dock *d)
{
    struct Item *it;
    if (!d->win || d->vis != d->full || d->dragging || d->list || an_running()) return FALSE;
    FOR_ITEMS(d, it) if (it->mini) return TRUE;
    return FALSE;
}

static void tstart(ULONG us)
{
    if (tbusy) return;                           /* (one waiting already) */
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = us / 1000000;
    treq->tr_time.tv_micro = us % 1000000;
    SendIO((struct IORequest *)treq);
    tbusy = TRUE;
}

static BOOL shown(struct Dock *d) { return d->win && d->vis > 0; }

/* Look now, and again in a while (hidden by auto-hide: only until done). */
static void look(struct Dock *d)
{
    ULONG us = 0;
    if (!d->o->running || !d->win) return;       /* (RUNNING=0, or no dock window: no looking) */
    if (!scan(d)) { us = RETRY_US; if (!shown(d)) soon = TRUE; }
    else if (fast > 0) { fast--; us = FAST_US; }
    else if (shown(d) || ncq) us = ncq ? FAST_US : SCAN_US;   /* (Close All waiting: soon) */
    if (us > PULSE_US && pulsing(d)) { pulse_left = us - PULSE_US; us = PULSE_US; }   /* (the dot steps meanwhile) */
    if (us) tstart(us);
}

static void tstop(void)
{
    pulse_left = 0;
    if (!tbusy) return;
    if (!CheckIO((struct IORequest *)treq)) AbortIO((struct IORequest *)treq);
    WaitIO((struct IORequest *)treq);
    tbusy = FALSE;
}

/* The timer for looking, and the port the start-up messages come back to. */
ULONG run_open(void)
{
    ULONG sigs = 0;
    if ((rport = CreateMsgPort())) sigs = 1UL << rport->mp_SigBit;
    cport = CreateMsgPort();                     /* (the consoles' answers: taken at each look) */
    if (!(tport = CreateMsgPort())) return sigs;
    if (!(treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq))) ||
        OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0)) {
        if (treq) { DeleteIORequest((struct IORequest *)treq); treq = NULL; }
        DeleteMsgPort(tport); tport = NULL;
        return sigs;
    }
    return sigs | 1UL << tport->mp_SigBit;
}

static void free_launch(struct Launch *l)
{
    LONG i;
    for (i = 0; i < l->wbs.sm_NumArgs; i++) if (l->args[i].wa_Lock) UnLock(l->args[i].wa_Lock);
    FreeVec(l);
}

/* Is the program behind l still running the code the dock loaded (not a new
   process at the same address)? Under Forbid. */
static BOOL ours(struct Launch *l)
{
    struct Process *p = (struct Process *)l->task;
    LONG *segs, i;
    if (!alive(l->task) || p->pr_Task.tc_Node.ln_Type != NT_PROCESS || !(segs = BADDR(p->pr_SegList))) return FALSE;
    if (segs[3] == l->seg) return TRUE;          /* (NP_Seglist code is in slot 3; slot 0 says 2 on 3.1 and 3.2) */
    for (i = 1; i <= segs[0] && i < 8; i++) if (segs[i] == l->seg) return TRUE;
    return FALSE;
}

/* Quitting. Programs the dock started may still run: their start-up messages
   now go nowhere when they end (a NULL reply port only marks the message
   free), and stay in memory with their argument locks. Their code is freed
   by AmigaDOS when they end, unless they asked to keep it (sm_Segment 0). */
void run_close(void)
{
    struct Message *m;
    LONG k;
    run_show_all();                              /* hidden windows back first */
    if (cport) {                                 /* the consoles' answers: a second for those still out */
        con_wait(50);
        for (k = 0; k < nca && ca[k]->state != CA_ASKED; k++) ;
        if (k < nca) {                           /* a late one: the port and its question stay, */
            cport->mp_Flags = (cport->mp_Flags & ~PF_ACTION) | PA_IGNORE;   /* nobody is signalled */
            for (k = 0; k < nca; k++) if (ca[k]->state != CA_ASKED) con_free(k--);
        } else {
            while (nca > 0) con_free(nca - 1);
            DeleteMsgPort(cport);
        }
        cport = NULL;
    }
    if (treq) {
        tstop();
        CloseDevice((struct IORequest *)treq);
        DeleteIORequest((struct IORequest *)treq);
        DeleteMsgPort(tport);
        treq = NULL; tport = NULL;
    }
    if (!rport) return;
    snap_tasks();                                /* ended without answering (a Shell-only tool)? Not */
    for (k = 0; k < MAXLAUNCH; k++)              /* there twice, to be sure (as ended() does): freed. */
        if (lt[k] && !alive(lt[k]->task) && lt[k]->wbs.sm_Message.mn_Node.ln_Type != NT_REPLYMSG) {
            Delay(2);                            /* (before the port is emptied for the last time) */
            snap_tasks();
            if (!alive(lt[k]->task)) {
                UnLoadSeg(lt[k]->seg);
                free_launch(lt[k]);
                lt[k] = NULL;
            }
        }
    Forbid();
    while ((m = GetMsg(rport))) {                 /* ended already: freed */
        if (m->mn_Length == sizeof(struct ExtIntuiMessage)) {
            for (k = 0; k < MAXCLOSE; k++) {
                if (closing[k] == (struct IntuiMessage *)m) closing[k] = NULL;
                if (orphan[k] == (struct IntuiMessage *)m) orphan[k] = NULL;
            }
            FreeVec(m);
            continue;
        }
        for (k = 0; k < MAXLAUNCH; k++) if (lt[k] == (struct Launch *)m) lt[k] = NULL;
        if (((struct Launch *)m)->wbs.sm_Segment) UnLoadSeg(((struct Launch *)m)->wbs.sm_Segment);
        free_launch((struct Launch *)m);
    }
    snap_tasks();                                /* (for ours() below) */
    for (k = 0; k < MAXLAUNCH; k++)
        if (lt[k]) {
            struct Launch *l = lt[k];
            l->wbs.sm_Message.mn_ReplyPort = NULL;
            if (l->wbs.sm_Segment && ours(l)) ((struct Process *)l->task)->pr_Flags |= PRF_FREESEGLIST;
            lt[k] = NULL;
        }
    for (k = 0; k < MAXCLOSE; k++) {             /* close requests not answered yet: the same */
        if (closing[k]) { closing[k]->ExecMessage.mn_ReplyPort = NULL; closing[k] = NULL; }
        if (orphan[k]) { orphan[k]->ExecMessage.mn_ReplyPort = NULL; orphan[k] = NULL; }
    }
    Permit();
    DeleteMsgPort(rport);
    rport = NULL;
}

/* A dock icon goes away: its programs run on, but are no longer its. */
void run_forget(struct Item *it)
{
    LONG k;
    if (it == litem && ldock) run_list_close(ldock);   /* its list too */
    for (k = 0; k < MAXLAUNCH; k++) if (lt[k] && lt[k]->it == it) lt[k]->it = NULL;
}

/* Start the program behind 'it' as Workbench does, with the files of a drop
   (am) as further arguments. FALSE: not done (no program file, no memory, too
   many running) - the caller asks Workbench instead. */
/* The drawer of 'path' (also for an icon with no file of its own, as a
   DOSDriver's), locked; 0 if there is none. */
static BPTR path_dir(CONST_STRPTR path)
{
    char buf[SD_LINEMAX + 8];
    LONG k = PathPart(path) - path;
    if (k >= (LONG)sizeof(buf)) return 0;
    memcpy(buf, path, k);
    buf[k] = 0;
    return Lock(buf, SHARED_LOCK);
}

/* Copies of the locks of Workbench's Shell path (the Workbench process's,
   else the dock's own when started from a Shell), at most 'max'. */
#define MAXPATH 24
static LONG wb_path(BPTR *lk, LONG max)
{
    struct Process *p;
    struct CommandLineInterface *cli = NULL;
    BPTR *node;
    LONG n = 0;
    Forbid();
    p = (struct Process *)FindTask("Workbench");
    if (p && p->pr_Task.tc_Node.ln_Type == NT_PROCESS) cli = BADDR(p->pr_CLI);
    if (!cli) cli = Cli();
    for (node = cli ? BADDR(cli->cli_CommandDir) : NULL; node && n < max; node = BADDR(node[0]))
        if (node[1] && (lk[n] = DupLock(node[1]))) n++;   /* (DupLock may break the Forbid: */
    Permit();                                              /*  Workbench's path never changes) */
    return n;
}

/* A lock on 'name' in a drawer of Workbench's path (0: none), with no
   "please insert" requesters. */
static BPTR wb_find(CONST_STRPTR name)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    BPTR lk[MAXPATH], l = 0, old;
    APTR win = me->pr_WindowPtr;
    LONG n = wb_path(lk, MAXPATH), k;
    me->pr_WindowPtr = (APTR)-1;
    for (k = 0; k < n; k++) {
        old = CurrentDir(lk[k]);
        if (!l) l = Lock((STRPTR)name, SHARED_LOCK);
        CurrentDir(old);
        UnLock(lk[k]);
    }
    me->pr_WindowPtr = win;
    return l;
}

/* A default tool as Workbench finds it: a full path as it is, else in the
   project's drawer, then in Workbench's path, then in C:. Its full name into
   'out'. */
static BOOL find_tool(CONST_STRPTR tool, BPTR projdir, char *out, LONG size)
{
    BPTR l, old;
    if (!tool || !*tool || strlen(tool) >= size - 4) return FALSE;
    if (strchr(tool, ':')) { strcpy(out, tool); return TRUE; }
    old = CurrentDir(projdir);
    l = Lock(tool, SHARED_LOCK);
    CurrentDir(old);
    if (!l) l = wb_find(tool);
    if (l) {
        BOOL ok = NameFromLock(l, out, size);
        UnLock(l);
        return ok;
    }
    if (strchr(tool, '/')) return FALSE;
    strcpy(out, "C:");
    strcat(out, tool);
    return TRUE;
}

/* How Workbench starts it from a Shell: 1 an icon with the CLI tooltype
   ("Start from: Shell") or a program with no icon of its own, 2 an icon with
   the REXX tooltype; 0 the Workbench way. The program's own icon counts, not
   a dock image. */
#ifdef SD_DEBUG
BOOL rl_noowo;                                   /* ARexx NOWB: test the fallback */
#define RL_NOOWO rl_noowo
#else
#define RL_NOOWO FALSE
#endif

static LONG shell_kind(struct Item *it)
{
    struct DiskObject *o;
    LONG k = 0;
    if (it->nofile || (it->dobj->do_Type != WBTOOL && it->dobj->do_Type != WBPROJECT)) return 0;
    if (!(o = GetDiskObject(it->path))) return it->dobj->do_Type == WBTOOL && !is_shell(it);
    if (o->do_ToolTypes) {
        k = FindToolType(o->do_ToolTypes, "REXX") ? 2 : FindToolType(o->do_ToolTypes, "CLI") ? 1 : 0;
        if (k && FindToolType(o->do_ToolTypes, "DONOTPROMPT")) k |= 4;   /* (4: never asked first) */
    }
    FreeDiskObject(o);
    return k;
}

/* Name s onto the command line, in quotes if it has a space (as Workbench). */
static void add_name(char *cmd, CONST_STRPTR s)
{
    BOOL q = strchr(s, ' ') != NULL;
    if (q) strcat(cmd, "\"");
    strcat(cmd, s);
    if (q) strcat(cmd, "\"");
}

/* Started from a Shell by the dock: when Workbench refuses it (as Workbench
   does without asking, DONOTPROMPT), or with SHELLASK after the "Execute a
   file" requester (ask; Cancel: TRUE, nothing started). In an output window
   opened on the first output. */
static BOOL shell_run(struct Dock *d, struct Item *it, LONG kind, BOOL ask)
{
    static char cmd[2 * SD_LINEMAX + 40], con[SD_LINEMAX + 80], tool[SD_LINEMAX + 8];
    CONST_STRPTR deft = it->dobj->do_DefaultTool;
    struct Screen *s = d->scr;
    BPTR dir, old, fh, l;
    LONG a[5], stack = 4096;
    BOOL ok = FALSE;
    if (!(dir = path_dir(it->path))) return FALSE;
    cmd[0] = 0;
    if (kind == 2) {                             /* ARexx: the icon's file is the script */
        strcpy(cmd, (l = Lock("SYS:Rexxc/RX", SHARED_LOCK)) ? "SYS:Rexxc/RX " : "RX ");
        if (l) UnLock(l);
    } else if (it->dobj->do_Type == WBPROJECT && deft && *deft) {   /* a project: its tool, then it */
        if (!find_tool(deft, dir, tool, sizeof(tool))) { UnLock(dir); return FALSE; }
        add_name(cmd, tool);
        strcat(cmd, " ");
    }
    add_name(cmd, it->path);
    if (ask && ask_command(d, cmd, sizeof(cmd)) == 0) { UnLock(dir); return TRUE; }   /* (Cancel; could not ask: run) */
    a[1] = s ? s->BarHeight + 1 : 11;
    a[2] = s && s->Width < 640 ? s->Width : 640;
    a[3] = s && s->Height - a[1] < 200 ? s->Height - a[1] : 200;
    a[0] = 0;
    a[4] = (LONG)"Output window";
    SD_FMT(con, "CON:%ld/%ld/%ld/%ld/%s/AUTO/WAIT", a);
    WorkbenchControl(NULL, WBCTRLA_GetDefaultStackSize, &stack, TAG_END);
    if (stack < 4096) stack = 4096;
    if (it->dobj->do_StackSize > stack) stack = it->dobj->do_StackSize;
    old = CurrentDir(dir);                       /* (the Shell starts in the icon's drawer) */
    if ((fh = Open(con, MODE_OLDFILE))) {
        ok = SystemTags(cmd, SYS_Input, fh, SYS_Output, NULL, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                        NP_StackSize, stack, NP_WindowPtr, NULL, TAG_END) != -1;
        if (!ok) Close(fh);
    }
    CurrentDir(old);
    UnLock(dir);
    return ok;
}

BOOL run_launch(struct Dock *d, struct Item *it, struct AppMessage *am)
{
    static char toolpath[SD_LINEMAX + 8];
    struct Launch *l;
    struct Process *p;
    BPTR file, dir, seg, home, pdir = 0;
    LONG i, n = 1, size, slot, stack, pri = 0, first;
    BOOL proj;
    char *nm;
    STRPTR tt;
    CONST_STRPTR prog = it->path;
    if (!rport || !it->dobj || d->o->wbstart) return FALSE;   /* (WBSTART: Workbench starts it) */
    if ((i = shell_kind(it))) {                  /* from a Shell: Workbench starts it (without asking: */
        if (d->o->shellask && !(i & 4)) return shell_run(d, it, i & 3, TRUE);   /*  SHELLASK asks first, */
        return (!RL_NOOWO && OpenWorkbenchObject(it->path, TAG_END)) || shell_run(d, it, i & 3, FALSE);   /* as */
    }                                            /*  a double-click, unless DONOTPROMPT) */
    proj = !run_is_tool(it);                     /* a project: its default tool, the project its argument */
    if (proj) {                                  /* (as a double-click: Workbench's OpenWorkbenchObject */
        if (it->temp || it->dobj->do_Type != WBPROJECT || !it->dobj->do_DefaultTool ||   /*  refuses some, */
            !(pdir = path_dir(it->path))) return FALSE;                                 /*  as a DOSDriver) */
        if (!find_tool(it->dobj->do_DefaultTool, pdir, toolpath, sizeof(toolpath))) { UnLock(pdir); return FALSE; }
        prog = toolpath;
    }
    for (slot = 0; slot < MAXLAUNCH && lt[slot]; slot++) ;
    if (slot == MAXLAUNCH) { if (pdir) UnLock(pdir); return FALSE; }
    first = proj ? 2 : 1;                        /* (the arguments after the program and the project) */
    n = first;
    if (am) n += am->am_NumArgs < MAXARGS - 1 ? am->am_NumArgs : MAXARGS - 1;
    size = strlen(FilePart(prog)) + 1 + (proj ? strlen(FilePart(it->path)) + 1 : 0);
    for (i = first; i < n; i++) size += (am->am_ArgList[i - first].wa_Name ? strlen(am->am_ArgList[i - first].wa_Name) : 0) + 1;
    if (!(l = AllocVec(sizeof(*l) + size, MEMF_PUBLIC | MEMF_CLEAR))) { if (pdir) UnLock(pdir); return FALSE; }
    if (!(file = Lock(prog, SHARED_LOCK))) { if (pdir) UnLock(pdir); FreeVec(l); return FALSE; }   /* (the Shell icon has no file) */
    dir = ParentDir(file);
    UnLock(file);
    if (!dir) { if (pdir) UnLock(pdir); FreeVec(l); return FALSE; }
    if (!(seg = LoadSeg(prog))) { UnLock(dir); if (pdir) UnLock(pdir); FreeVec(l); return FALSE; }
    nm = l->names;                               /* argument 0: the program, in its drawer */
    strcpy(nm, FilePart(prog));
    l->args[0].wa_Lock = dir;
    l->args[0].wa_Name = nm;
    nm += strlen(nm) + 1;
    if (proj) {                                  /* argument 1: the project, in its drawer */
        strcpy(nm, FilePart(it->path));
        l->args[1].wa_Lock = pdir;
        l->args[1].wa_Name = nm;
        nm += strlen(nm) + 1;
    }
    for (i = first; i < n; i++) {                /* the dropped files */
        struct WBArg *a = &am->am_ArgList[i - first];
        strcpy(nm, a->wa_Name ? (char *)a->wa_Name : "");
        l->args[i].wa_Lock = a->wa_Lock ? DupLock(a->wa_Lock) : 0;
        l->args[i].wa_Name = nm;
        nm += strlen(nm) + 1;
    }
    l->wbs.sm_NumArgs = n;
    stack = 4096;                                /* at least Workbench's default */
    WorkbenchControl(NULL, WBCTRLA_GetDefaultStackSize, &stack, TAG_END);
    if (stack < 4096) stack = 4096;
    if (it->dobj->do_StackSize > stack) stack = it->dobj->do_StackSize;
    if (it->dobj->do_ToolTypes && (tt = FindToolType(it->dobj->do_ToolTypes, "TOOLPRI"))) {
        StrToLong(tt, &pri);
        if (pri < -128) pri = -128;
        if (pri > 127) pri = 127;
    }
    home = DupLock(dir);                         /* PROGDIR: */
    p = CreateNewProcTags(NP_Seglist, seg, NP_FreeSeglist, FALSE, NP_StackSize, stack, NP_Name, l->names,
                          NP_Priority, pri, NP_HomeDir, home, NP_WindowPtr, NULL, NP_ConsoleTask, NULL,
                          NP_CurrentDir, 0, NP_Input, 0, NP_Output, 0,   /* as Workbench: no current dir, */
                          NP_CloseInput, FALSE, NP_CloseOutput, FALSE,   /* no input/output handles (a */
                          TAG_END);                                      /* program closing its own made dos close them twice) */
    if (!p) {
        UnLock(home);
        UnLoadSeg(seg);
        free_launch(l);
        return FALSE;
    }
    close_forget(&p->pr_Task);                   /* (a request to the one before at this address: stale) */
    l->wbs.sm_Message.mn_Node.ln_Type = NT_MESSAGE;
    l->wbs.sm_Message.mn_ReplyPort = rport;
    l->wbs.sm_Message.mn_Length = sizeof(struct WBStartup);
    l->wbs.sm_Process = &p->pr_MsgPort;
    l->wbs.sm_Segment = l->seg = seg;            /* freed when it ends, like Workbench does */
    l->wbs.sm_ArgList = l->args;
    l->it = it;
    l->task = &p->pr_Task;
    lt[slot] = l;
    PutMsg(&p->pr_MsgPort, &l->wbs.sm_Message);  /* it waits for this, then runs */
    lastsig = 0;
    if (treq) { tstop(); look(d); }              /* its dot now */
    return TRUE;
}

/* Programs the dock started have ended: their icons now. Their code is
   freed here, unless a program kept it (sm_Segment 0: it left something
   running in it). */
static void ended(struct Dock *d)
{
    struct Message *m;
    struct Launch *gone[MAXLAUNCH];
    LONG k, ng = 0;
    BOOL any = FALSE;
    if (!rport) return;
    while ((m = GetMsg(rport))) {
        if (m->mn_Length == sizeof(struct ExtIntuiMessage)) {   /* a close request answered */
            for (k = 0; k < MAXCLOSE; k++) {
                if (closing[k] == (struct IntuiMessage *)m) closing[k] = NULL;
                if (orphan[k] == (struct IntuiMessage *)m) orphan[k] = NULL;
            }
            FreeVec(m);
            if (TimerBase) GetSysTime(&lastreply);
            continue;
        }
        for (k = 0; k < MAXLAUNCH; k++) if (lt[k] == (struct Launch *)m) lt[k] = NULL;
        close_forget(((struct Launch *)m)->task);
        if (((struct Launch *)m)->wbs.sm_Segment) UnLoadSeg(((struct Launch *)m)->wbs.sm_Segment);
        free_launch((struct Launch *)m);
        any = TRUE;
    }
    for (k = 0; k < MAXLAUNCH && !(lt[k] && lt[k]->wbs.sm_Message.mn_Node.ln_Type != NT_REPLYMSG); k++) ;
    if (k == MAXLAUNCH) goto told;               /* (none still running) */
    Forbid();                                    /* ended without answering (a Shell-only tool): */
    snap_tasks();                                /* not there twice running, to be sure */
    for (k = 0; k < MAXLAUNCH; k++)
        if (lt[k] && lt[k]->wbs.sm_Message.mn_Node.ln_Type != NT_REPLYMSG) {
            if (alive(lt[k]->task)) lt[k]->missed = 0;
            else if (++lt[k]->missed >= 2) {
                gone[ng++] = lt[k];
                lt[k] = NULL;
            }
        }
    Permit();
    for (k = 0; k < ng; k++) {
        close_forget(gone[k]->task);
        UnLoadSeg(gone[k]->seg);
        free_launch(gone[k]);
        any = TRUE;
    }
told:
    if (any && treq) { lastsig = 0; tstop(); look(d); }
}


/* The dock came out (look now, then every 2 s) or went away (stop). */
void run_check(struct Dock *d)
{
    if (!treq) return;
    if (!d->o->running) { if (!ncq) tstop(); fast = 0; return; }
    if (shown(d) && !tbusy) look(d);
    else if (!shown(d) && tbusy && !fast && !soon && !ncq) tstop();   /* (the quick looks after a click go on) */
}

/* Another window became active: a program may have opened or closed one.
   Look in a moment (not at once: it may open more). */
void run_soon(struct Dock *d)
{
    if (!treq || !d->o->running || !d->win) return;   /* (also hidden: ready when it comes out) */
    if (fast > 0) return;                        /* (looking often anyway) */
    tstop();
    tstart(SOON_US);
    soon = TRUE;
}

/* A click on the dock: a program may be starting or closing - look soon,
   a few times. */
void run_clicked(struct Dock *d)
{
    if (!treq) return;
    tstop();
    fast = FAST_N;
    tstart(FAST_US);
    (void)d;
}

void run_timer(struct Dock *d)
{
    ended(d);
    if (pending_any()) close_prune();            /* (requests to programs that ended since) */
    if (!d->dragging && !d->lmb) close_next();   /* (not while our drag holds the layers: later) */
    if (!treq || !tbusy || !CheckIO((struct IORequest *)treq)) return;
    WaitIO((struct IORequest *)treq);
    tbusy = FALSE;
    if (pulse_left > 0) {                        /* a pulse step before the next look */
        struct Item *it;
        d->pulse = (d->pulse + 1) & 7;
        FOR_ITEMS(d, it) if (it->mini) render_dot(d, it);
        if (pulsing(d)) {
            LONG us = pulse_left > PULSE_US ? PULSE_US : pulse_left;
            pulse_left -= us;
            tstart(us);
            if (pulse_left > 0 || us == PULSE_US) return;
        }
        pulse_left = 0;
        if (tbusy) return;                       /* (the rest of the wait runs) */
    }
    if (shown(d) || fast > 0 || soon || ncq) {
        soon = FALSE;
        look(d);
        if (shown(d) && dock_raise(d)) in_settle_again();   /* In front: also a window that grew or moved over it */
    }
    if (ncq && !tbusy) tstart(FAST_US);          /* (Close All waiting: also with RUNNING=0) */
}

/* ------------------------------------------------ the held list of windows */

static void list_bm_free(void)
{
    if (listbg || listwk) WaitBlit();
    if (listbg) FreeBitMap(listbg);
    if (listwk) FreeBitMap(listwk);
    listbg = listwk = NULL;
}

/* Drawn off screen, then one blit into the window: a redraw while the pointer
   moves between rows shows no bare glass in between (it flickered). */
static void list_draw(struct Dock *d)
{
    struct RastPort rpb, *rp = &rpb;
    LONG i, n;
    InitRastPort(rp);
    rp->BitMap = listwk;
    BltBitMap(listbg, 0, 0, listwk, 0, 0, lw, lh, 0xC0, 0xFF, NULL);
    SetFont(rp, d->font);
    SetDrMd(rp, JAM1);
    if (lsel >= 0 && d->borderpen >= 0) {         /* the window under the pointer: a lighter bar */
        WORD y0 = row_y(lsel);
        SetAPen(rp, d->borderpen);
        RectFill(rp, 4, y0 + 1, lw - 5, y0 + rowh - 2);
        RectFill(rp, 3, y0 + 2, lw - 4, y0 + rowh - 3);
    }
    if (nact && nlist && d->divpen >= 0) {        /* the divider above the actions */
        SetAPen(rp, d->divpen);
        RectFill(rp, PADX / 2, row_y(nlist) - SEPH / 2 - 1, lw - 1 - PADX / 2, row_y(nlist) - SEPH / 2 - 1);
    }
    for (i = 0; i < nlist + nact; i++) {
        STRPTR t = i < nlist ? (STRPTR)list[i].title : (STRPTR)act_text(act[i - nlist]);
        struct TextExtent te;
        n = TextFit(rp, t, strlen((char *)t), &te, NULL, 1, lw - 2 * PADX, 32767);
        if (d->textpen != d->shadowpen) {
            SetAPen(rp, d->shadowpen);
            Move(rp, PADX + 1, row_y(i) + 2 + rp->TxBaseline + 1);
            Text(rp, t, n);
        }
        SetAPen(rp, d->textpen);
        Move(rp, PADX, row_y(i) + 2 + rp->TxBaseline);
        Text(rp, t, n);
    }
    BltBitMapRastPort(listwk, 0, 0, d->list->RPort, 0, 0, lw, lh, 0xC0);
}

/* The list window for the windows in list[], next to litem's icon. */
static BOOL list_open(struct Dock *d)
{
    struct RastPort rp;
    LONG i, tw, opacity = !d->o->transparent ? 100 : d->opacity + 25 > 90 ? 90 : d->opacity + 25;
    WORD cx, cy, sw = d->scr->Width, sh = d->scr->Height, rad;
    InitRastPort(&rp);
    SetFont(&rp, d->font);
    rowh = d->font->tf_YSize + 3;                /* (one row: as tall as a hover label) */
    i = (sh - 2 * PADY - (nact ? SEPH + nact * rowh : 0)) / rowh;   /* (no taller than the screen) */
    if (nlist > i) nlist = i > 1 ? i : 1;
    lw = 0;
    for (i = 0; i < nlist; i++)
        if ((tw = TextLength(&rp, (STRPTR)list[i].title, strlen(list[i].title))) > lw) lw = tw;
    for (i = 0; i < nact; i++)
        if ((tw = TextLength(&rp, (STRPTR)act_text(act[i]), strlen(act_text(act[i])))) > lw) lw = tw;
    lw += 2 * PADX + 1;
    if (lw > sw / 2) lw = sw / 2;
    lh = row_y(nlist + nact) + PADY;
    rad = rowh / 2 > 6 ? 6 : rowh / 2;
    cx = d->left + litem->x + litem->w / 2;
    cy = d->top + litem->y + litem->h / 2;
    lx = cx - lw / 2; ly = cy - lh / 2;
    switch (d->edge) {                            /* next to the icon, off the dock */
    case 'B': ly = d->top - lh - GAP; break;
    case 'T': ly = d->top + d->h + GAP; break;
    case 'L': lx = d->left + d->w + GAP; break;
    default:  lx = d->left - lw - GAP; break;
    }
    if (lx + lw > sw) lx = sw - lw;
    if (ly + lh > sh) ly = sh - lh;
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    render_arrow(0, 0, 0);
    if (!(listbg = render_glass(d, NULL, lx, ly, lw, lh, rad, opacity))) return FALSE;
    if (!(listwk = AllocBitMap(lw, lh, GetBitMapAttr(listbg, BMA_DEPTH), BMF_MINPLANES, listbg))) {
        list_bm_free(); return FALSE;
    }
    d->lbx = lx; d->lby = ly; d->lbw = lw; d->lbh = lh;   /* for the input handler (before d->list is set) */
    d->list = OpenWindowTags(NULL, WA_PubScreen, d->scr, WA_Left, lx, WA_Top, ly, WA_Width, lw, WA_Height, lh,
                             WA_Borderless, TRUE, WA_Activate, FALSE, WA_SmartRefresh, TRUE,
                             WA_NoCareRefresh, TRUE, WA_BackFill, LAYERS_NOBACKFILL, TAG_END);
    if (!d->list) { list_bm_free(); return FALSE; }
    list_draw(d);
    return TRUE;
}

/* The button was held on 'it': a list of its windows next to the icon. */
void run_hold(struct Dock *d, struct Item *it)
{
    LONG n, i;
    if (d->list || (!run_trackable(it) && it->temp != RUN_MORE)) return;
    if (is_shell(it)) { collect(FALSE); con_wait(5); }   /* (a console asked just now: its answer) */
    n = collect(it->temp != RUN_MORE);
    nlist = nact = 0;
    if (it->temp == RUN_MORE) {                  /* the "more" cell: the programs it holds */
        struct Item *h;
        for (h = (struct Item *)d->runs.mlh_Head; h->node.mln_Succ && nlist < MAXLIST; h = (struct Item *)h->node.mln_Succ) {
            LONG c = copies_of(h, n), v[2];
            char nm[TITLELEN - 15];
            for (i = 0; i < n && !is_it(&own[i], h); i++) ;
            if (i == n) continue;
            list[nlist] = own[i];                /* its frontmost window */
            list[nlist].item = h;
            strncpy(nm, FilePart(h->path), sizeof(nm) - 1);
            nm[sizeof(nm) - 1] = 0;
            v[0] = (LONG)nm; v[1] = c;
            SD_FMT(list[nlist].title, c > 1 ? "%s (%ld running)" : "%s", v);
            nlist++;
        }
        goto open;
    }
    lhid = lshow = 0;
    for (i = 0; i < n; i++)                       /* its windows in the order they first appeared, */
        if (is_it(&own[i], it)) {                 /* the oldest MAXLIST of them */
            LONG j;
            if (own[i].hid) lhid++; else lshow++;
            if (nlist == MAXLIST && list[MAXLIST - 1].seq < own[i].seq) continue;
            j = nlist < MAXLIST ? nlist++ : MAXLIST - 1;
            while (j > 0 && list[j - 1].seq > own[i].seq) { list[j] = list[j - 1]; j--; }
            list[j] = own[i];
            if (!list[j].title[0]) strcpy(list[j].title, own[i].name);   /* no title: its program */
        }
    for (i = nlist - 1; i > 0; i--) {             /* the same title twice: "Clock", "Clock (2)" */
        LONG j, same = 1, len;
        for (j = 0; j < i; j++) if (!strcmp(list[j].title, list[i].title)) same++;
        if (same > 1 && (len = strlen(list[i].title)) < TITLELEN - 5) {
            list[i].title[len] = ' '; list[i].title[len + 1] = '(';
            list[i].title[len + 2] = '0' + (same > 9 ? 9 : same);
            list[i].title[len + 3] = ')'; list[i].title[len + 4] = 0;
        }
    }
    for (i = 0; i < nlist; i++)                   /* minimised ones marked */
        if (list[i].hid && strlen(list[i].title) < TITLELEN - 4) strcat(list[i].title, " (-)");
    list_actions(it);
open:
    if (!nlist && !nact) return;
    tip_hide(d);
    litem = it;
    ldock = d;
    lsel = -1;
    d->liststicky = FALSE;
    if (!list_open(d)) run_list_close(d);
}

/* Ask the window to close, as its close gadget would: an IDCMP_CLOSEWINDOW
   message the program answers. FALSE: not sent (window gone, no close
   gadget, a request to its task not answered yet, no free slot). */
static BOOL close_win(struct Window *w, struct Task *t)
{
    struct IntuiMessage *im;
    LONG k;
    BOOL sent = FALSE;
    ULONG lock;
    close_prune();
    if (pending_to(t)) return FALSE;             /* (one not answered yet: never two at once) */
    for (k = 0; k < MAXCLOSE && closing[k]; k++) ;
    if (k == MAXCLOSE || !rport) return FALSE;
    {   /* Can it be asked at all? Then: its program may need to be the active
           one to quit cleanly (IBrowse left a small box behind until
           clicked) - shown if minimised, to the front and active first. Not
           Workbench's drawers. */
        BOOL wbw = FALSE, can;
        lock = LockIBase(0);
        can = win_screen(w, t) && (w->IDCMPFlags & IDCMP_CLOSEWINDOW);
        if (can) {
            wbw = t->tc_Node.ln_Name && !strcmp(t->tc_Node.ln_Name, "Workbench");
            can = !wbw || (w->Flags & WFLG_CLOSEGADGET);   /* (Workbench's: with a close gadget) */
        }
        UnlockIBase(lock);
        if (!can) return FALSE;
        if (!wbw) {
            LONG j;
            if (is_mini(w)) {
                reveal(w, t);
                if (!win_hide_ok()) Delay(25);   /* (shown by its layer, behind Intuition's back: */
            }                                    /*  Intuition must catch up before it closes) */
            if (!front_now(w, t)) {              /* Intuition does these at its next input event: */
                to_front(w, t);                  /* wait for that, else a program that closes at once */
                for (j = 0; j < 10 && !front_now(w, t); j++) Delay(1);   /* leaves Intuition working */
            }                                    /* on a freed window (MuForce, 3.1's Clock) */
        }
    }
    if (!(im = AllocVec(sizeof(struct ExtIntuiMessage), MEMF_PUBLIC | MEMF_CLEAR))) return FALSE;
    im->ExecMessage.mn_Node.ln_Type = NT_MESSAGE;
    im->ExecMessage.mn_ReplyPort = rport;
    im->ExecMessage.mn_Length = sizeof(struct ExtIntuiMessage);
    im->Class = IDCMP_CLOSEWINDOW;
    im->IDCMPWindow = w;
    im->IAddress = (APTR)w;
    CurrentTime(&im->Seconds, &im->Micros);
    lock = LockIBase(0);                         /* the window can't close while this is held */
    if (win_screen(w, t) && (w->IDCMPFlags & IDCMP_CLOSEWINDOW)) {   /* (still there) */
        PutMsg(w->UserPort, &im->ExecMessage);
        sent = TRUE;
    }
    UnlockIBase(lock);
    if (!sent) { FreeVec(im); return FALSE; }
    closing[k] = im;
    closetask[k] = t;
    return TRUE;
}

/* Make the window active, if it still exists (see to_front). */
static BOOL activate(struct Window *w, struct Task *t)
{
    BOOL there;
    Forbid();
    if ((there = win_screen(w, t) != NULL)) ActivateWindow(w);
    Permit();
    return there;
}

/* Del over a window in the list: close it, and take it off the list. A Shell's
   console window takes no close request: it gets Ctrl+\ (end-of-file) as
   its close gadget would send, which ends the Shell if no command runs. */
static void list_del(struct Dock *d, LONG r)
{
    if (lmode == LM_CLOSE) { if (r >= 0 && r < nlist) pick_row(d, r); else if (r == nlist) do_act(d, 0); return; }
    if (lmode) return;                           /* (choosing which to hide / show: no Del) */
    if (r >= nlist && r < nlist + nact) { do_act(d, r - nlist); return; }   /* (Del over an action: that) */
    if (r < 0 || r >= nlist) return;
    if (litem && litem->temp == RUN_MORE) {      /* the "more" list: the row's program closes */
        if (list[r].item) close_program(d, list[r].item);
        list_drop(d, r);
        return;
    }
    if (!close_row(d, r)) { sd_beep(d); return; }
    list_drop(d, r);
}

/* Close row r's window (a Shell's: Ctrl+\). FALSE: not sent. */
static BOOL close_row(struct Dock *d, LONG r)
{
    BOOL done = close_win(list[r].win, list[r].task);
    if (!done && list[r].con) {                  /* a Shell: active (shown first if minimised), Ctrl+\ */
        if (is_mini(list[r].win)) reveal(list[r].win, list[r].task);
        if (activate(list[r].win, list[r].task)) done = in_send_eof(list[r].win);
    }
    (void)d;
    return done;
}

/* Row r off the list, which opens again without it (closes when nothing is
   left). */
static void list_drop(struct Dock *d, LONG r)
{
    if (!lmode) { if (list[r].hid) lhid--; else lshow--; }
    nlist--;
    memmove(&list[r], &list[r + 1], (nlist - r) * sizeof(list[0]));
    list_window_close(d);
    list_bm_free();
    lsel = -1;
    if (!nlist && lmode) nact = 0;               /* (asking which one: all done) */
    else if (!lmode) {                           /* (no window left: Close goes too; nothing */
        LONG k, j = 0;                           /*  minimised left: Show; none showing: Minimise) */
        for (k = 0; k < nact; k++)
            if (!(act[k] == ACT_CLOSE && !nlist) && !(act[k] == ACT_MINI && lshow <= 0) &&
                !(act[k] == ACT_SHOW && lhid <= 0)) act[j++] = act[k];
        nact = j;
    }
    if (!(nlist + nact) || !list_open(d)) run_list_close(d);
    else if ((lsel = list_row(d->scr->MouseX, d->scr->MouseY)) >= 0) list_draw(d);   /* (the row now under it) */
    run_clicked(d);                              /* its dot and number soon */
}

/* Which line of the list is at screen position sx, sy (-1: none). */
static LONG list_row(WORD sx, WORD sy)
{
    if (sx < lx || sx >= lx + lw) return -1;
    if (sy >= ly + PADY && sy < ly + PADY + nlist * rowh) return (sy - ly - PADY) / rowh;
    if (nact && sy >= ly + row_y(nlist) && sy < ly + row_y(nlist) + nact * rowh)
        return nlist + (sy - ly - row_y(nlist)) / rowh;   /* an action */
    return -1;
}

/* Close / Close All: every window of the program (also beyond the rows
   shown), oldest first. One request per task at a time (a program that
   quits on the first closes its other windows itself; a second request
   could make it close one twice): the task's other windows wait in cq[]
   (close_next). */
static void close_program(struct Dock *d, struct Item *it)
{
    LONG n, i, k;
    BOOL ok = TRUE;
    it->asked = TRUE;                            /* (Force Quit offered from now on) */
    n = collect(FALSE);
    if (n > 1) {                                 /* oldest first, as the list shows them */
        for (i = 1; i < n; i++) {
            struct Own t = own[i];
            LONG j = i - 1;
            while (j >= 0 && own[j].seq > t.seq) { own[j + 1] = own[j]; j--; }
            own[j + 1] = t;
        }
    }
    for (i = 0; i < n; i++) {
        if (!is_it(&own[i], it)) continue;
        if (own[i].con) {                        /* a Shell window: its Shell ends (each one is a Shell) */
            if (is_mini(own[i].win)) reveal(own[i].win, own[i].task);   /* (never a hidden one active) */
            if (!(activate(own[i].win, own[i].task) && in_send_eof(own[i].win))) ok = FALSE;
            continue;
        }
        for (k = 0; k < i && !(own[k].task == own[i].task && is_it(&own[k], it)); k++) ;
        if (k < i || !close_win(own[i].win, own[i].task)) {
            if (k < i || pending_to(own[i].task) || !close_room()) {   /* (this task got one already: later) */
                LONG q;
                for (q = 0; q < ncq && !(cq[q].win == own[i].win && cq[q].task == own[i].task); q++) ;
                if (q == ncq && ncq < MAXCQ) {   /* (not queued twice: Del, then Close All) */
                    cq[ncq].win = own[i].win; cq[ncq].task = own[i].task; cq[ncq].wb = own[i].wb;
                    if (TimerBase) GetSysTime(&cq[ncq].since);
                    ncq++;
                }
            } else ok = FALSE;                   /* (no close gadget) */
        }
    }
    if (!ok) sd_beep(d);
    run_clicked(d);                              /* the dots and numbers soon (and the queue) */
}

/* The tasks of the program behind 'it': its windows' owners, each once, and
   (all: also the copies with no window - a copy that never got as far as
   opening one, PocketSNES started by an IconX script) every other process
   running the same program by name. Not the Shell icon (every Shell would
   count), not the dock itself. */
static LONG prog_tasks(struct Item *it, struct Task **out, LONG max, BOOL all)
{
    LONG n = collect(FALSE), i, k, m = 0;
    struct Task *me = FindTask(NULL);
    for (i = 0; i < n; i++) {
        if (!is_it(&own[i], it) || own[i].wb || own[i].con) continue;   /* (not Workbench; a Shell's console */
        for (k = 0; k < m && out[k] != own[i].task; k++) ;              /*  handler is not the program) */
        if (k == m && m < max) out[m++] = own[i].task;
    }
    if (all && !is_shell(it) && it->temp != RUN_WB) {
        LONG j;
        Forbid();
        snap_tasks();                            /* (the lists copied with interrupts off: a walk under */
        for (j = 0; j < ntasks && m < max; j++) {   /*  Forbid alone can miss a task that a Signal() moves) */
            struct Task *t = tasks[j];
            char nm[NAMELEN];
            if (t == me || t->tc_Node.ln_Type != NT_PROCESS) continue;
            task_name(t, nm, NULL);
            if (!nm[0] || !same_name(nm, FilePart(it->path))) continue;
            for (k = 0; k < m && out[k] != t; k++) ;
            if (k == m) out[m++] = t;
        }
        Permit();
    }
    return m;
}

/* Force Quit (the list, once Close was tried). First the break signals
   Ctrl+C, D, E and F: Shell programs and ports stop on those and clean up
   after themselves. Still there two seconds later, the user is asked, and
   the program is ended: its tasks are taken out of the system and their
   windows closed (Intuition's way: messages replied, the port detached),
   then a screen of its own left with no windows. AmigaOS tracks nothing a
   program holds, so its memory, open files and locks stay in use until a
   reboot, and a program ended inside a system call can take the system
   down - the requester says so. The same as Scout's Remove. */
#define MAXPT 16
#ifdef SD_DEBUG
BOOL fq_noask;                                   /* tests: the requester skipped (DOACT ... NOASK) */
#endif
static void end_program(struct Dock *d, struct Item *it)
{
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME,
        "%s does not respond.\nEnd it anyway? Unsaved work in it is lost,\n"
        "what it holds stays in use until a reboot,\nand the system may become unstable.", "End it|Cancel" };
    struct Task *t[MAXPT];
    struct Window *wins[MAXWIN], *w;
    struct Screen *scrs[MAXWIN], *s;
    LONG m, k, i, nw = 0;
    CONST_STRPTR name = it->label ? it->label : FilePart(it->path);
    if (!(m = prog_tasks(it, t, MAXPT, TRUE))) { sd_beep(d); return; }
    Forbid();
    snap_tasks();
    for (k = 0; k < m; k++)
        if (alive(t[k])) Signal(t[k], SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
    Permit();
    Delay(100);                                  /* (2 s: most stop by then) */
    if (!prog_tasks(it, t, MAXPT, TRUE)) { lastsig = 0; run_clicked(d); return; }   /* it stopped */
#ifdef SD_DEBUG
    if (!fq_noask)
#endif
    if (EasyRequest(d->win, &es, NULL, (LONG)name) != 1) return;
    m = prog_tasks(it, t, MAXPT, TRUE);
    for (k = 0; k < m; k++) {
        Forbid();
        snap_tasks();
        if (!alive(t[k])) { Permit(); continue; }
        if (t[k]->tc_Node.ln_Type == NT_PROCESS) {   /* a Shell process: its slot in DOS's table of them */
            struct Process *p = (struct Process *)t[k];   /* cleared, as its normal exit would (else Status */
            struct RootNode *rn = (struct RootNode *)DOSBase->dl_Root;   /* lists a ghost, and Break on */
            LONG *ta = rn ? (LONG *)BADDR(rn->rn_TaskArray) : NULL;      /* that number touches freed memory) */
            if (ta && p->pr_TaskNum > 0 && p->pr_TaskNum <= ta[0] && ta[p->pr_TaskNum] == (LONG)&p->pr_MsgPort)
                ta[p->pr_TaskNum] = 0;
        }
        RemTask(t[k]);                           /* (exec frees its stack and process structure) */
        for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)   /* its windows, noted while nothing moves */
            for (w = s->FirstWindow; w && nw < MAXWIN; w = w->NextWindow)
                if (w->UserPort && (w->UserPort->mp_Flags & PF_ACTION) != PA_SOFTINT &&
                    w->UserPort->mp_SigTask == (APTR)t[k]) { wins[nw] = w; scrs[nw++] = s; }
        Permit();
    }
    for (i = 0; i < nw; i++) {                   /* closed as Intuition wants it (CloseWindowSafely) */
        struct Message *msg;
        Forbid();
        while ((msg = GetMsg(wins[i]->UserPort))) ReplyMsg(msg);
        wins[i]->UserPort = NULL;
        ModifyIDCMP(wins[i], 0);
        Permit();
        CloseWindow(wins[i]);
    }
    for (i = 0; i < nw; i++) {                   /* a screen of its own (not Workbench, not public): what */
        struct Window *rest[MAXWIN];             /* is left on it - windows with no port (a display window), */
        LONG nr = 0;                             /* or a port of a task that is gone - closed, then the screen */
        BOOL again = FALSE;                      /* (the user was left on PocketSNES's dead screen) */
        for (k = 0; k < i && !again; k++) again = scrs[k] == scrs[i];
        if (again || scrs[i] == d->scr) continue;
        Forbid();
        for (s = IntuitionBase->FirstScreen; s && s != scrs[i]; s = s->NextScreen) ;
        if (!s || (s->Flags & SCREENTYPE) == WBENCHSCREEN || (s->Flags & SCREENTYPE) == PUBLICSCREEN) {
            Permit();                            /* (the type is a value in those bits: CUSTOMSCREEN has them all */
            continue;                            /*  set, so a bit test took every custom screen for Workbench) */
        }
        snap_tasks();
        for (w = s->FirstWindow; w && nr < MAXWIN; w = w->NextWindow)
            if (!w->UserPort || (w->UserPort->mp_Flags & PF_ACTION) == PA_SOFTINT || !w->UserPort->mp_SigTask ||
                !alive((struct Task *)w->UserPort->mp_SigTask)) rest[nr++] = w;
        Permit();
        for (k = 0; k < nr; k++) {
            struct Message *msg;
            Forbid();
            if (rest[k]->UserPort) {
                while ((msg = GetMsg(rest[k]->UserPort))) ReplyMsg(msg);
                rest[k]->UserPort = NULL;
                ModifyIDCMP(rest[k], 0);
            }
            Permit();
            CloseWindow(rest[k]);
        }
        Forbid();
        for (s = IntuitionBase->FirstScreen; s && s != scrs[i]; s = s->NextScreen) ;
        if (s && !s->FirstWindow) { Permit(); CloseScreen(s); }
        else Permit();
    }
    WBenchToFront();                             /* (never left on a dead screen) */
    it->asked = FALSE;
    lastsig = 0;
    run_clicked(d);                              /* its mark goes */
}

/* ------------------------------------------------------------- minimised */

/* Windows the dock has hidden (HideWindow, intuition V46): they stay open
   and their programs run on. A click on the icon (or Show) brings them back;
   so does quitting the dock. */
/* Windows can be hidden with Intuition V46 (HideWindow, OS 3.2), else with
   layers.library V45 (HideLayer on the window's layers: OS 3.1.4, 3.9). */
static BOOL win_hide_ok(void) { return IntuitionBase->LibNode.lib_Version >= 46; }

static BOOL can_hide(void)
{
    return win_hide_ok() || LayersBase->lib_Version >= 45;
}

/* Hide window w of task t (under Forbid, it exists). Without HideWindow its
   layers are hidden directly, with the screen's layers locked (HideLayer
   may wait, which ends the Forbid: the window could close meanwhile). If it
   is (or is about to be) the active window, another one becomes active:
   Workbench's backdrop, else the first one showing on its screen - a
   minimised window made active later is someone else's doing (run_wake). */
static void hide_win(struct Window *w, struct Task *t)
{
    struct Window *aw = IntuitionBase->ActiveWindow, *x, *to = NULL;
    BOOL active = aw == w || (w == in_prevwin && aw && aw->UserPort &&   /* (a click on the dock gives the */
                              aw->UserPort->mp_SigTask == (APTR)FindTask(NULL));   /*  focus back: queued) */
    if (win_hide_ok()) HideWindow(w);            /* (its result is not reliable: the layer tells) */
    else {
        struct Layer_Info *li = &w->WScreen->LayerInfo;
        struct Layer *l;
        LockLayerInfo(li);
        if (win_screen(w, t)) {
            if (w->BorderRPort && w->BorderRPort->Layer && w->BorderRPort->Layer != w->WLayer)
                HideLayer(w->BorderRPort->Layer);
            if (w->WLayer) HideLayer(w->WLayer);
        }
        UnlockLayerInfo(li);
        /* Intuition knows nothing of HideLayer: the windows uncovered are
           damaged but not told to redraw (a blank box until one is moved,
           CaffeineOS). The front window put to the front - no change - makes
           Intuition look for damage and send the refreshes. */
        if (win_screen(w, t))
            for (l = w->WScreen->LayerInfo.top_layer; l; l = l->back) {
                struct Window *x = (struct Window *)l->Window;
                if (x && x != w && !(l->Flags & LAYERHIDDEN) && !(x->Flags & WFLG_BACKDROP)) {
                    WindowToFront(x);
                    break;
                }
            }
    }
    if (!active || !win_screen(w, t)) return;
    for (x = w->WScreen->FirstWindow; x; x = x->NextWindow)
        if ((x->Flags & WFLG_WBENCHWINDOW) && (x->Flags & WFLG_BACKDROP)) { to = x; break; }
    for (x = w->WScreen->FirstWindow; x && !to; x = x->NextWindow)
        if (x != w && x->WLayer && !(LayersBase->lib_Version >= 45 && (x->WLayer->Flags & LAYERHIDDEN)) &&
            !is_mini(x) && x->UserPort && x->UserPort->mp_SigTask != (APTR)FindTask(NULL))   /* (not ours: our */
            to = x;                              /* list and label have no port, the dock window is ours; not one */
                                                 /* minimised just now - HideWindow may not have taken yet) */
    if (to) ActivateWindow(to);
}

/* A minimised window made active by someone else (Workbench opening a
   drawer that is minimised, or its own program): shown again. The dock
   never makes a minimised window active itself. */
void run_wake(struct Dock *d)
{
    struct Window *w = IntuitionBase->ActiveWindow;
    struct Task *t;
    LONG k;
    if (!w || !is_mini(w)) return;
    k = mini_at(w);
    t = mini[k].task;
    reveal(w, t);
    to_front(w, t);
    run_clicked(d);                              /* its mark soon */
}

/* Window w in mini[] (-1: not). */
static LONG mini_at(struct Window *w)
{
    LONG k;
    for (k = 0; k < nmini && mini[k].win != w; k++) ;
    return k < nmini ? k : -1;
}

/* w of task t is minimised by the dock from now on. FALSE: no room. */
static BOOL mini_add(struct Window *w, struct Task *t)
{
    LONG k = mini_at(w);
    if (k < 0) {
        if (nmini == MAXMINI) return FALSE;
        k = nmini++;
    }
    mini[k].win = w;
    mini[k].task = t;
    mini[k].shown = mini[k].seen = 0;
    return TRUE;
}

/* Minimised by the dock and not shown again since (a Show may still be on
   its way: then it is no longer "minimised", but kept in mini[] so that
   quitting shows it if it did not take). */
static BOOL is_mini(struct Window *w)
{
    LONG k = mini_at(w);
    return k >= 0 && !mini[k].shown;
}

static void mini_drop(LONG k)
{
    nmini--;
    if (k < nmini) memmove(&mini[k], &mini[k + 1], (nmini - k) * sizeof(mini[0]));
}

/* How many of the list's windows are minimised (hid) or not. */
static LONG rows_hidden(BOOL hid)
{
    LONG r, n = 0;
    for (r = 0; r < nlist; r++) if (!list[r].hid == !hid) n++;
    return n;
}

/* Window w hidden and noted in mini[] (hidden already, by someone else or
   a Show that did not take: only noted). FALSE: mini[] is full. */
static BOOL hide_it(struct Window *w, struct Task *t, BOOL hid)
{
    BOOL ok = TRUE;
    Forbid();
    if (win_screen(w, t) && !is_mini(w)) {
        if ((ok = mini_add(w, t)) && !hid) hide_win(w, t);
    }
    Permit();
    return ok;
}

/* One window hidden (picked after Minimise...). FALSE: no room (beep). */
static BOOL hide_one(struct Dock *d, struct Window *w, struct Task *t, BOOL hid)
{
    BOOL ok = hide_it(w, t, hid);
    if (!ok) sd_beep(d);
    lastsig = 0;
    run_clicked(d);                              /* its mark soon */
    return ok;
}

/* All the program's windows hidden. */
static void minimise(struct Dock *d, struct Item *it)
{
    LONG n = collect(FALSE), i;
    BOOL ok = TRUE;
    for (i = 0; i < n && ok; i++)
        if (is_it(&own[i], it)) ok = hide_it(own[i].win, own[i].task, own[i].hid);
    if (!ok) sd_beep(d);                /* (MAXMINI windows minimised already) */
    lastsig = 0;
    run_clicked(d);                              /* its mark soon */
}

/* A hidden window shown again, if it still exists, the way it was hidden:
   ShowWindow (Intuition V46+), else ShowLayer on its layers. Never both
   (ShowWindow may finish a moment later). Intuition before V46 knows
   nothing of ShowLayer, so the window is put to the front through it
   afterwards: only then does its program hear that it has to redraw (else
   it shows stale, broken parts - after quitting the dock, CaffeineOS). The
   layers are locked meanwhile, as in hide_win. If it is still hidden, the
   next look shows it as minimised again, and Show tries again. It stays in
   mini[] (marked shown) until a look sees it showing, so quitting can still
   show it if this did not take. */
static void show_win(struct Window *w, struct Task *t)
{
    Forbid();
    if (win_screen(w, t)) {
        if (win_hide_ok()) ShowWindow(w, WINDOW_FRONTMOST);
        else if (LayersBase->lib_Version >= 45) {   /* (hidden layers: V45) */
            struct Layer_Info *li = &w->WScreen->LayerInfo;
            LockLayerInfo(li);
            if (win_screen(w, t)) {
                if (w->BorderRPort && w->BorderRPort->Layer && w->BorderRPort->Layer != w->WLayer &&
                    (w->BorderRPort->Layer->Flags & LAYERHIDDEN))
                    ShowLayer(w->BorderRPort->Layer, LAYER_FRONTMOST);
                if (w->WLayer && (w->WLayer->Flags & LAYERHIDDEN)) ShowLayer(w->WLayer, LAYER_FRONTMOST);
            }
            UnlockLayerInfo(li);
            if (win_screen(w, t)) WindowToFront(w);   /* (Intuition: its damage, a redraw) */
        }
    }
    Permit();
}

static void reveal(struct Window *w, struct Task *t)
{
    LONG k = mini_at(w);
    show_win(w, t);
    if (k >= 0) mini[k].shown = TRUE;
    lastsig = 0;
}

/* The program's hidden windows shown again. TRUE: there were some. */
static BOOL unminimise(struct Item *it)
{
    LONG n, i;
    BOOL any = FALSE;
    n = collect(FALSE);
    for (i = 0; i < n; i++)
        if (is_it(&own[i], it) && (own[i].hid || is_mini(own[i].win))) {
            reveal(own[i].win, own[i].task);
            any = TRUE;
        }
    return any;
}

/* One window picked in the list (hid: its row said minimised): shown again
   if it is. */
static void show_one(struct Window *w, struct Task *t, BOOL hid)
{
    if (hid || is_mini(w)) reveal(w, t);
}

/* Quitting (or after a crash): every window the dock hid that is still
   hidden shown again. */
void run_show_all(void)
{
    while (nmini > 0) {
        struct Window *w = mini[nmini - 1].win;
        BOOL hid;
        Forbid();
        hid = win_screen(w, mini[nmini - 1].task) && w->WLayer &&
              (LayersBase->lib_Version < 45 || (w->WLayer->Flags & LAYERHIDDEN));
        Permit();
        if (hid) show_win(w, mini[nmini - 1].task);
        nmini--;
    }
}

/* ------------------------------------------------- icons dropped into a Shell */

/* Workbench 3.2's console types the path of an icon dropped onto a Shell
   window itself; older ones (3.1, 3.9, CaffeineOS) do nothing, nor does
   KingCON (1.8, user's CaffeineOS). There the dock makes the Shell windows
   drop targets - any console's - and types the paths, or for one archive
   whose tool is found the command that unpacks it (SHELLDROP, UNPACKTO,
   UNPACKDIR). The text is typed as keys at the cursor, without Return. A
   console that types paths itself would get them twice: SHELLDROP=0. */
#define SHELL_AWID 0x5344                        /* AppWindow id of a Shell window (am_ID; the dock's is 0) */
#define MAXSDW 16
static struct { struct Window *win; struct Task *task; struct AppWindow *aw; } sdw[MAXSDW];
static LONG nsdw;

static BOOL drop_on(struct Dock *d)
{
    return d->o->shelldrop && WorkbenchBase->lib_Version < 47 && am_port();
}

/* Another window became active and the dock does not look for running
   programs (RUNNING=0): the Shell windows looked at here. */
void run_shell_check(struct Dock *d)
{
    if (!d->o->running && (drop_on(d) || nsdw)) shell_sync(d, collect(FALSE));
}

/* No Shell window is a drop target any more (quitting, or SHELLDROP off). */
void run_shell_off(void)
{
    while (nsdw > 0) RemoveAppWindow(sdw[--nsdw].aw);
}

/* After a look (collect: n windows): the Shell windows there now are drop
   targets, those gone are not. */
static void shell_sync(struct Dock *d, LONG n)
{
    LONG i, k;
    if (!drop_on(d)) { run_shell_off(); return; }
    for (k = 0; k < nsdw; k++) {
        for (i = 0; i < n && !(own[i].stdcon && own[i].win == sdw[k].win && own[i].task == sdw[k].task); i++) ;
        if (i == n) {
            RemoveAppWindow(sdw[k].aw);
            sdw[k--] = sdw[--nsdw];
        }
    }
    for (i = 0; i < n && nsdw < MAXSDW; i++) {
        if (!own[i].stdcon || own[i].hid) continue;
        for (k = 0; k < nsdw && sdw[k].win != own[i].win; k++) ;
        if (k < nsdw) continue;
        if ((sdw[nsdw].aw = AddAppWindow(SHELL_AWID, (ULONG)own[i].win, own[i].win, am_port(), TAG_END))) {
            sdw[nsdw].win = own[i].win;
            sdw[nsdw++].task = own[i].task;
        }
    }
}

/* The archive kinds: name ending, tool, its arguments (unpacking in the
   current drawer / into %d); %a the archive, %n its name without the
   ending (one-file packers write it with ">"). */
static const struct { const char *end, *tool, *here, *into; } packer[] = {
    { ".lha", "lha", "x %a", "x %a %d" },       { ".lzh", "lha", "x %a", "x %a %d" },
    { ".lzx", "lzx", "x %a", "x %a %d" },       { ".zip", "unzip", "%a", "%a -d %d" },
    { ".tar.gz", "tar", "xzvf %a", "xzvf %a -C %d" }, { ".tgz", "tar", "xzvf %a", "xzvf %a -C %d" },
    { ".tar", "tar", "xvf %a", "xvf %a -C %d" }, { ".gz", "gzip", ">%n -dc %a", ">%d%n -dc %a" },
    { ".bz2", "bzip2", ">%n -dc %a", ">%d%n -dc %a" }, { ".xz", "xz", ">%n -dc %a", ">%d%n -dc %a" },
    { ".7z", "7z", "x %a", "x %a -o%d" },       { ".rar", "unrar", "x %a", "x %a %d" } };
#define NPACKER ((LONG)(sizeof(packer) / sizeof(packer[0])))

/* Tool 'name' in C: (then out = name) or in Workbench's path (out = its full
   name). */
static BOOL unpack_tool(CONST_STRPTR name, char *out, LONG size)
{
    char c[40];
    BPTR l;
    LONG ok;
    strcpy(c, "C:"); strcat(c, name);
    if ((l = Lock(c, SHARED_LOCK))) { UnLock(l); strcpy(out, name); return TRUE; }
    if (!(l = wb_find(name))) return FALSE;
    ok = NameFromLock(l, out, size);
    UnLock(l);
    return ok;
}

/* The unpack command for archive 'path' (name: its file name) into 'out';
   FALSE: not an archive, or its tool is not there. */
static BOOL unpack_cmd(struct Dock *d, CONST_STRPTR path, CONST_STRPTR name, char *out, LONG size)
{
    char tool[SD_LINEMAX], dest[SD_LINEMAX], base[108], *o;
    CONST_STRPTR t, dir = d->o->unpackto == 1 ? (CONST_STRPTR)"RAM:" : d->o->unpackto == 2 ? d->o->unpackdir : NULL;
    LONG k, nl = strlen(name), el;
    for (k = 0; k < NPACKER; k++) {
        el = strlen(packer[k].end);
        if (nl > el && !Stricmp((STRPTR)name + nl - el, (STRPTR)packer[k].end)) break;
    }
    if (k == NPACKER || nl - el >= (LONG)sizeof(base) || !unpack_tool(packer[k].tool, tool, sizeof(tool))) return FALSE;
    memcpy(base, name, nl - el); base[nl - el] = 0;
    dest[0] = 0;
    if (dir && *dir && strlen(dir) < sizeof(dest) - 2) {   /* (a drawer: ends in ':' or '/') */
        strcpy(dest, dir);
        if (dest[strlen(dest) - 1] != ':' && dest[strlen(dest) - 1] != '/') strcat(dest, "/");
    }
    if (strlen(tool) + strlen(path) + strlen(dest) + nl + 40 > size) return FALSE;
    out[0] = 0;
    add_name(out, tool);
    strcat(out, " ");
    for (t = dest[0] ? packer[k].into : packer[k].here; *t; t++) {
        o = out + strlen(out);
        if (*t != '%') { o[0] = *t; o[1] = 0; continue; }
        t++;
        if (*t == 'a') add_name(out, path);
        else if (*t == 'd' && t[1] == '%' && t[2] == 'n') {   /* (">%d%n": one name, in quotes if needed) */
            char both[2 * SD_LINEMAX];
            strcpy(both, dest); strcat(both, base);
            add_name(out, both);
            t += 2;
        } else if (*t == 'd') add_name(out, dest);
        else if (*t == 'n') add_name(out, base);
    }
    return TRUE;
}

static char droptext[2048];              /* what the last drop types */
static LONG nd_rx, nd_target, nd_text, nd_typed, nd_inactive;   /* drops: received, onto a target, with */
                                                 /* text, typed, window not active (DROPINFO) */
/* The text for the icons of a drop into droptext: their paths, or one
   archive's unpack command. TRUE: there is some. */
static BOOL drop_text(struct Dock *d, struct AppMessage *am)
{
    char path[SD_LINEMAX + 8], cmd[2 * SD_LINEMAX + 64];
    LONG i;
    droptext[0] = 0;
    for (i = 0; i < am->am_NumArgs; i++) {
        struct WBArg *a = &am->am_ArgList[i];
        BOOL file = a->wa_Name && a->wa_Name[0];
        if (!a->wa_Lock || !NameFromLock(a->wa_Lock, path, sizeof(path) - 2)) continue;
        if (file && !AddPart(path, a->wa_Name, sizeof(path) - 2)) continue;
        if (am->am_NumArgs == 1 && file && unpack_cmd(d, path, a->wa_Name, cmd, sizeof(cmd))) {
            strcpy(droptext, cmd);
            break;
        }
        if (strlen(droptext) + strlen(path) + 4 >= sizeof(droptext)) break;
        add_name(droptext, path);
        strcat(droptext, " ");
    }
    if (droptext[0]) nd_text++;
    return droptext[0] != 0;
}

/* Icons dropped onto a Shell window (before the drop is answered: its locks
   are valid until then): the window, if it is still one of ours, with the
   text ready; NULL: nothing to type. */
struct Window *run_shell_drop(struct Dock *d, struct AppMessage *am)
{
    struct Window *w = (struct Window *)am->am_UserData;
    LONG k;
    droptext[0] = 0;
    nd_rx++;
    for (k = 0; k < nsdw && sdw[k].win != w; k++) ;
    if (k == nsdw) return NULL;
    nd_target++;
    return drop_text(d, am) ? w : NULL;
}

/* Icons dropped onto the dock's Shell icon: typed into the Shell window in
   front (the first one showing, front to back) instead of starting a new
   Shell - also on 3.2 and in any console, as the drop is the dock's. FALSE:
   no Shell window (a new Shell starts, as before), or SHELLDROP is off.
   Typed once the drop is answered (run_shell_pending). */
static struct Window *pendw;
static struct Task *pendt;
static char lastfront[80] = "none yet";         /* DROPINFO: what the last drop onto the Shell icon did */
static BOOL frontmiss;                          /* ... it found no Shell: launch.c notes the new one */
static BOOL pendnew;                            /* ... typed into the new Shell once its window is there */
BOOL run_shell_front(struct Dock *d, struct Item *it, struct AppMessage *am)
{
    LONG n, i, v[2];
    if (!d->o->shelldrop || !is_shell(it)) return FALSE;
    n = collect(TRUE);
    for (i = 0; i < n && !(own[i].con && !own[i].hid && !own[i].widget && own[i].scr == d->scr); i++) ;
    frontmiss = TRUE;
    if (i == n) {                                /* a new Shell starts (launch.c); the text goes into it */
        pendnew = drop_text(d, am);
        strcpy(lastfront, "no Shell window: a new Shell");
        return FALSE;
    }
    if (!drop_text(d, am)) { strcpy(lastfront, "no text: a new Shell"); return FALSE; }
    frontmiss = FALSE;
    v[0] = (LONG)own[i].title; v[1] = (LONG)own[i].name;
    SD_FMT(lastfront, "typed into \"%.30s\" (%.20s)", v);
    pendw = own[i].win;
    pendt = own[i].task;
    return TRUE;
}

/* launch.c: a new Shell for a drop onto the Shell icon started (ok) or not. */
void run_note_front(BOOL ok)
{
    if (frontmiss) strcat(lastfront, ok ? " started" : " NOT started");
    if (!frontmiss || !ok) pendnew = FALSE;
    frontmiss = FALSE;
}

static void type_into(struct Dock *d, struct Window *w, struct Task *t);

void run_shell_pending(struct Dock *d)
{
    LONG k, n, i;
    if (pendw) type_into(d, pendw, pendt);
    pendw = NULL;
    if (!pendnew) return;
    pendnew = FALSE;
    for (k = 0; k < 15; k++) {                   /* the new Shell's window: up to 3 s */
        Delay(10);
        n = collect(FALSE);
        for (i = 0; i < n && !(own[i].con && !own[i].hid && !own[i].widget && own[i].scr == d->scr); i++) ;
        if (i < n) {
            Delay(25);                           /* (its prompt first) */
            type_into(d, own[i].win, own[i].task);
            strcat(lastfront, ", typed into it");
            return;
        }
    }
    strcat(lastfront, ", its window not seen");
}

/* The text into Shell window w (after the drop was answered): the window
   made active, then the text typed as keys (keymap.library: this keymap's
   keys for each character). Not typed if the window does not become
   active (it would go to another one). */
void run_shell_type(struct Dock *d, struct Window *w)
{
    LONG k;
    for (k = 0; k < nsdw && sdw[k].win != w; k++) ;
    if (w && k < nsdw) type_into(d, w, sdw[k].task);
}

/* The text into window w of task t, if it still exists (see run_shell_type). */
static void type_into(struct Dock *d, struct Window *w, struct Task *t)
{
    struct Library *KeymapBase;
    struct MsgPort *port;
    struct IOStdReq *io;
    struct InputEvent ie;
    UBYTE keys[16];
    LONG i, k, n, wait;
    if (!w || !droptext[0]) return;
    Forbid();
    if (win_screen(w, t)) {
        if (is_mini(w)) reveal(w, t);            /* (never typed into a hidden window) */
        to_front(w, t);                          /* (in front and active: it may be behind another) */
    } else w = NULL;
    Permit();
    if (!w) return;
    for (wait = 0; wait < 25 && IntuitionBase->ActiveWindow != w; wait++) Delay(1);
    if (IntuitionBase->ActiveWindow != w) { nd_inactive++; sd_beep(d); return; }
    nd_typed++;
    if (!(KeymapBase = OpenLibrary("keymap.library", 37))) return;
    if ((port = CreateMsgPort())) {
        if ((io = (struct IOStdReq *)CreateIORequest(port, sizeof(*io)))) {
            if (!OpenDevice("input.device", 0, (struct IORequest *)io, 0)) {
                for (i = 0; droptext[i]; i++) {
                    n = MapANSI((STRPTR)droptext + i, 1, (STRPTR)keys, sizeof(keys), NULL);
                    for (k = 0; k < n; k++) {
                        memset(&ie, 0, sizeof(ie));
                        ie.ie_Class = IECLASS_RAWKEY;
                        ie.ie_Code = keys[2 * k];
                        ie.ie_Qualifier = keys[2 * k + 1];
                        io->io_Command = IND_WRITEEVENT;
                        io->io_Data = &ie;
                        io->io_Length = sizeof(ie);
                        DoIO((struct IORequest *)io);
                        ie.ie_Code |= IECODE_UP_PREFIX;
                        DoIO((struct IORequest *)io);
                    }
                }
                CloseDevice((struct IORequest *)io);
            }
            DeleteIORequest((struct IORequest *)io);
        }
        DeleteMsgPort(port);
    }
    CloseLibrary(KeymapBase);
}

/* ARexx DROPINFO (diagnosis of Shell drops, while they are new): every
   window on the dock's screen, one per line - its title, its task's name,
   its port (S signal, I soft interrupt, - none), C if its task is a
   Shell's console, D if it is a drop target now - then the counts. */
void run_dropinfo(struct Dock *d, char *out, LONG size)
{
    struct Window *w;
    LONG l = 0, k, i, v[5];
    char line[160];
    ULONG lock;
    out[0] = 0;
    collect(FALSE);                              /* (the Shells' consoles: contask) */
    lock = LockIBase(0);
    for (w = d->scr ? d->scr->FirstWindow : NULL; w && l < size - 400; w = w->NextWindow) {   /* (room for the */
        struct Task *t = w->UserPort ? (struct Task *)w->UserPort->mp_SigTask : con_task(w);   /*  lines below) */
        UBYTE pf = w->UserPort ? (w->UserPort->mp_Flags & PF_ACTION) : 0xFF;
        for (k = 0; k < ncon && contask[k] != t; k++) ;
        for (i = 0; i < nsdw && sdw[i].win != w; i++) ;
        v[0] = (LONG)(w->Title ? (char *)w->Title : "-");
        v[1] = (LONG)(t && t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?");
        v[2] = pf == 0xFF ? '-' : pf == PA_SOFTINT ? 'I' : pf == PA_SIGNAL ? 'S' : 'O';
        v[3] = k < ncon ? 'C' : '.';
        v[4] = i < nsdw ? 'D' : '.';
        SD_FMT(line, "%.40s | %.24s | %lc %lc%lc\n", v);
        strcpy(out + l, line);
        l += strlen(line);
    }
    UnlockIBase(lock);
    v[0] = ncon; v[1] = nsdw; v[2] = WorkbenchBase->lib_Version; v[3] = d->o->shelldrop;
    SD_FMT(line, "consoles %ld, drop targets %ld, workbench %ld, SHELLDROP %ld\n", v);
    strcpy(out + l, line);
    l += strlen(line);
    {
        LONG u[5];
        u[0] = nd_rx; u[1] = nd_target; u[2] = nd_text; u[3] = nd_typed; u[4] = nd_inactive;
        SD_FMT(line, "drops: received %ld, onto a target %ld, with text %ld, typed %ld, not active %ld\n", u);
        strcpy(out + l, line);
        l += strlen(line);
        strcpy(out + l, "last drop onto the Shell icon: ");   /* (lastfront is 80 bytes at most) */
        strcat(out + l, lastfront);
    }
}

#ifdef SD_DEBUG
/* ARexx DOACT NAME ACTION [NOASK] (tests): the long-press list of the icon
   named NAME opened and its action ACTION (as the list shows it) done.
   RC 5: no such icon or action. */
LONG run_debug_act(struct Dock *d, CONST_STRPTR name, CONST_STRPTR action, BOOL noask)
{
    struct Item *it;
    LONG a;
    FOR_ITEMS(d, it) if (same_name(FilePart(it->path), name)) break;
    if (!it->node.mln_Succ) return 5;
    fq_noask = noask;
    run_hold(d, it);
    if (!d->list) return 10;
    for (a = 0; a < nact && Stricmp((STRPTR)act_text(act[a]), (STRPTR)action); a++) ;
    if (a == nact) { run_list_close(d); return 5; }
    do_act(d, a);
    return 0;
}

/* ARexx SCREENS (tests): how many screens, and their titles. */
void run_debug_screens(char *out, LONG size)
{
    struct Screen *s;
    LONG n = 0, l = 0;
    ULONG lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        CONST_STRPTR t = s->Title ? (CONST_STRPTR)s->Title : (CONST_STRPTR)"-";
        n++;
        if (l + (LONG)strlen(t) + 3 < size - 16) { if (l) out[l++] = ','; strcpy(out + l, t); l += strlen(t); }
    }
    UnlockIBase(lock);
    { LONG v[1]; char c[16]; v[0] = n; SD_FMT(c, " (%ld)", v); strcpy(out + l, c); }
}

/* ARexx SHELLDROPTEST DIR NAME (tests): DIR/NAME dropped onto the first
   Shell window that is a drop target, as Workbench sends it. RC 5: none. */
LONG run_debug_shelldrop(struct Dock *d, CONST_STRPTR dir, CONST_STRPTR name)
{
    struct AppMessage am;
    struct WBArg wa;
    struct Window *w;
    if (!nsdw || !(wa.wa_Lock = Lock((STRPTR)dir, SHARED_LOCK))) return 5;
    memset(&am, 0, sizeof(am));
    wa.wa_Name = (BYTE *)name;
    am.am_NumArgs = 1;
    am.am_ArgList = &wa;
    am.am_UserData = (ULONG)sdw[0].win;
    w = run_shell_drop(d, &am);
    UnLock(wa.wa_Lock);
    run_shell_type(d, w);
    return w ? 0 : 5;
}
#endif

/* ------------------------------------------------------- the minimise keys */

/* The active window's place in own[] (after collect), -1: none of a program
   or drawer (Workbench's backdrop, the dock's own, or none active). */
static LONG active_own(struct Dock *d, LONG n)
{
    struct Window *aw = IntuitionBase->ActiveWindow;   /* (only compared) */
    LONG i;
    if (!aw || aw == d->win || aw == d->tip || aw == d->list) return -1;
    for (i = 0; i < n && own[i].win != aw; i++) ;
    return i < n && !own[i].hid ? i : -1;
}

/* The minimise key: the active window, as Minimise in the long-press list. */
void run_key_min(struct Dock *d)
{
    LONG n, a;
    if (!can_hide()) { sd_beep(d); return; }
    n = collect(FALSE);
    if ((a = active_own(d, n)) < 0) return;
    if (!hide_it(own[a].win, own[a].task, FALSE)) sd_beep(d);   /* (MAXMINI minimised already) */
    lastsig = 0;
    run_clicked(d);                              /* its mark soon */
}

/* The minimise-all key, by what is active: a drawer window - all drawer
   windows; a program's window - all windows of that program (every copy;
   for a Shell, all Shells); nothing (or Workbench's backdrop) - every window
   on the front screen. When nothing is showing there any more, the same key
   shows every window the dock has minimised there again, the frontmost last
   (on top). Title-bar strips (widgets) are left alone unless active. */
void run_key_minall(struct Dock *d)
{
    struct Screen *front = IntuitionBase->FirstScreen;
    LONG n, a, i, done = 0;
    BOOL ok = TRUE;
    if (!can_hide()) { sd_beep(d); return; }
    n = collect(FALSE);
    a = active_own(d, n);
    for (i = 0; i <= n && ok; i++) {             /* (the active window last: hiding it makes another */
        struct Own *o = &own[i < n ? i : a];     /*  active, never one of these) */
        BOOL with;
        if (i == n ? a < 0 : i == a) continue;
        if (o->hid || (o->widget && (a < 0 || o != &own[a]))) continue;
        if (a < 0) with = o->scr == front;
        else if (own[a].wb || o->wb) with = own[a].wb && o->wb;
        else if (own[a].con || o->con) with = own[a].con && o->con;
        else with = o->task == own[a].task || (o->name[0] && same_name(o->name, own[a].name));
        if (with) { ok = hide_it(o->win, o->task, FALSE); done++; }
    }
    if (a < 0 && !done)                          /* nothing showing: all back */
        for (i = n - 1; i >= 0; i--)
            if (own[i].hid && is_mini(own[i].win) && (!own[i].scr || own[i].scr == front)) {
                reveal(own[i].win, own[i].task);
                done++;
            }
    if (!ok || !done) sd_beep(d);       /* (no room left, or nothing to do) */
    lastsig = 0;
    run_clicked(d);                              /* the marks soon */
}

#ifdef SD_DEBUG
/* ARexx WINS (tests): every window as name:hidden (drawers: their title, a
   Shell: "Shell"), the active one with a '*'. */
void run_debug_wins(struct Dock *d, char *out, LONG size)
{
    LONG n = collect(TRUE), a = active_own(d, n), i, l = 0;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        CONST_STRPTR nm = own[i].wb ? own[i].title : own[i].con ? "Shell" : own[i].name;
        LONG k = strlen(nm);
        if (l + k + 6 >= size) break;
        if (l) out[l++] = ',';
        if (i == a) out[l++] = '*';
        strcpy(out + l, nm); l += k;
        out[l++] = ':'; out[l++] = own[i].hid ? '1' : '0';
        out[l] = 0;
    }
}
#endif

/* ----------------------------------------------- the list's other actions */

/* Keep in Dock: a running other becomes a kept icon, after the others. */
static void keep(struct Dock *d, struct Item *it)
{
    struct Item *b;
    if (it->temp != RUN_TEMP) return;
    if (DOCKFULL(d)) { sd_beep(d); return; }   /* (the dock is full) */
    if (it->nofile && !run_locate(d, it)) return;
    dz_suspend();
    Remove((struct Node *)it);
    it->temp = FALSE;
    for (b = (struct Item *)d->items.mlh_Head; b->node.mln_Succ && !b->temp; b = (struct Item *)b->node.mln_Succ) ;
    if (b->node.mln_Succ) Insert((struct List *)&d->items, (struct Node *)it, (struct Node *)b->node.mln_Pred);
    else AddTail((struct List *)&d->items, (struct Node *)it);
    config_store(d);
    dock_reopen_glide(d);
}

/* Remove from Dock: if it runs, it comes back behind the divider. */
static void unkeep(struct Dock *d, struct Item *it)
{
    if (it->temp) return;
    dz_suspend();
    config_remove(d, it);
    dock_reopen_glide(d);
    config_store(d);
    lastsig = 0;
    run_clicked(d);
}

/* Show in Workbench: the drawer the program is in. */
static void show_wb(struct Dock *d, struct Item *it)
{
    char p[SD_LINEMAX];
    strncpy(p, it->path, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    *PathPart(p) = 0;
    if (!p[0] || !OpenWorkbenchObject(p, TAG_END)) sd_beep(d);
}

/* What can be done with the program behind 'it' (the list's last rows). */
static void list_actions(struct Item *it)
{
    BOOL hidden, shown;
    nact = 0;
    if (it->temp == RUN_MORE) return;
    if (it->temp != RUN_WB) act[nact++] = it->temp ? ACT_KEEP : ACT_REMOVE;
    if (!it->nofile && PathPart(it->path) != (STRPTR)it->path) act[nact++] = ACT_SHOWWB;   /* (in a drawer) */
    hidden = lhid > 0;                           /* (all its windows, not only the rows) */
    shown = lshow > 0;
    if (shown && can_hide()) act[nact++] = ACT_MINI;   /* (some showing, some minimised: both) */
    if (hidden && can_hide()) act[nact++] = ACT_SHOW;
    if (nlist) act[nact++] = ACT_CLOSE;
    if (nlist && it->asked) act[nact++] = ACT_FORCE;   /* (Close was tried: the next step) */
}

/* Minimise... / Show... / Close... with several windows: the list asks which
   one - those windows, and All. It stays open for more until the last one
   is done (or Esc, or a click elsewhere). */
static void pick_list(struct Dock *d, UBYTE mode)
{
    LONG r, k = 0;
    for (r = 0; r < nlist; r++)
        if (mode == LM_CLOSE || !list[r].hid == (mode == LM_MINI)) list[k++] = list[r];
    nlist = k;
    nact = 1;
    act[0] = ACT_ALL;
    lmode = mode;
    lsel = -1;
    list_window_close(d);
    list_bm_free();
    d->liststicky = TRUE;                        /* (the button is up: a click picks) */
    if (!list_open(d)) run_list_close(d);
    else if ((lsel = list_row(d->scr->MouseX, d->scr->MouseY)) >= 0) list_draw(d);   /* (the row under it) */
}

/* A window row picked while the list asks which one: done, and off the
   list (which stays open while rows are left). A window gone meanwhile
   just goes off it. */
static void pick_row(struct Dock *d, LONG r)
{
    struct Window *w = list[r].win;
    struct Task *t = list[r].task;
    BOOL there;
    Forbid();
    there = win_screen(w, t) != NULL;
    Permit();
    if (there) {
        if (lmode == LM_CLOSE) { if (!close_row(d, r)) { sd_beep(d); return; } }
        else if (lmode == LM_MINI) { if (!hide_one(d, w, t, list[r].hid)) return; }   /* (full: the row stays) */
        else { reveal(w, t); run_clicked(d); }   /* (in front: ShowWindow; the list opens again over it) */
    }
    list_drop(d, r);
}

/* Action a (of the list) for its program. The list closes first (but
   Minimise... / Show... ask which window first). */
static void do_act(struct Dock *d, LONG a)
{
    struct Item *it = litem;
    LONG what = a >= 0 && a < nact ? act[a] : 0;
    UBYTE mode = lmode;
    if (what == ACT_MINI && rows_hidden(FALSE) > 1) { pick_list(d, LM_MINI); return; }
    if (what == ACT_SHOW && rows_hidden(TRUE) > 1) { pick_list(d, LM_SHOW); return; }
    if (what == ACT_CLOSE && nlist > 1) { pick_list(d, LM_CLOSE); return; }
    run_list_close(d);
    if (!it || !what) return;
    switch (what) {
    case ACT_KEEP:   keep(d, it); break;
    case ACT_REMOVE: unkeep(d, it); break;
    case ACT_SHOWWB: show_wb(d, it); break;
    case ACT_MINI:   minimise(d, it); break;
    case ACT_SHOW:   if (unminimise(it)) front(it, TRUE); break;
    case ACT_ALL:    if (mode == LM_MINI) minimise(d, it);
                     else if (mode == LM_CLOSE) close_program(d, it);
                     else if (unminimise(it)) front(it, TRUE);
                     break;
    case ACT_FORCE:  end_program(d, it); break;
    default:         close_program(d, it); break;
    }
}

/* Del with the pointer over a running program's icon: it closes, as with
   Close All. */
void run_quit(struct Dock *d, LONG cell)
{
    struct Item *it = dock_item(d, cell);
    if (!it || d->list || d->dragging || it->temp == RUN_MORE || !run_trackable(it) || !it->running) return;
    close_program(d, it);
}

/* The windows waiting in Close All's queue: each one's close request once
   its program has answered the last one and half a second has passed. Gone
   ones, and those waiting over 10 s (a program that never answers), are
   dropped. */
static void close_next(void)
{
    struct timeval now, t;
    LONG i, k;
    if (!ncq || !TimerBase) return;
    close_prune();
    GetSysTime(&now);
    for (i = 0; i < ncq; ) {
        BOOL drop = FALSE;
        t = now; SubTime(&t, &cq[i].since);
        if (t.tv_secs >= 10) drop = TRUE;
        else if (!pending_to(cq[i].task) && close_room()) {
            t = now; SubTime(&t, &lastreply);
            if (cq[i].wb || t.tv_secs > 0 || t.tv_micro >= 500000) {   /* (Workbench closes only that drawer: at once) */
                close_win(cq[i].win, cq[i].task);   /* (gone meanwhile: nothing sent) */
                drop = TRUE;
            }
        }
        if (drop) { for (k = i; k < ncq - 1; k++) cq[k] = cq[k + 1]; ncq--; }
        else i++;
    }
}

void run_list_move(struct Dock *d, WORD sx, WORD sy)
{
    LONG r;
    if (!d->list) return;
    if ((r = list_row(sx, sy)) != lsel) { lsel = r; list_draw(d); }
}

void run_list_close(struct Dock *d)
{
    d->liststicky = FALSE;
    list_window_close(d);
    list_bm_free();
    nlist = 0; lsel = -1; litem = NULL; nact = 0; lmode = 0;
}

/* The button went up with the list open: over a window, that one to the
   front; over Close / Close All, that; elsewhere the list stays (the input
   handler then reports clicks, Del and Esc: run_list_input). FALSE: no list
   was open. */
BOOL run_list_pick(struct Dock *d, WORD sx, WORD sy)
{
    BOOL hid;
    LONG r;
    struct Window *w;
    struct Task *t;
    if (!d->list) return FALSE;
    if ((r = list_row(sx, sy)) < 0) { d->liststicky = TRUE; return TRUE; }
    if (r >= nlist) { do_act(d, r - nlist); return TRUE; }
    if (lmode) { pick_row(d, r); return TRUE; }
    w = list[r].win;
    t = list[r].task;
    hid = list[r].hid;                           /* (the list goes first) */
    run_list_close(d);
    show_one(w, t, hid);                         /* (if minimised) */
    to_front(w, t);
    return TRUE;
}

/* The input handler saw something while the list stays open. */
void run_list_input(struct Dock *d)
{
    UBYTE e;
    WORD sx, sy;
    LONG r;
    Forbid();                                    /* (the input handler may add to it meanwhile) */
    e = d->listev;
    d->listev = 0;
    Permit();
    if (!d->list || !d->scr) return;
    sx = d->scr->MouseX; sy = d->scr->MouseY;
    if (e & LE_MOVE) run_list_move(d, sx, sy);
    if (e & LE_DEL) {                            /* over a row: that window closes (over Close: all); */
        if ((r = list_row(sx, sy)) >= 0) list_del(d, r);
        else {                                   /* elsewhere the list closes, and over a running */
            run_list_close(d);                   /* icon its program too */
            if (d->win) run_quit(d, dock_cell(d, sx - d->left, sy - d->top));
        }
        e &= LE_ESC;                             /* (one action a time: the list may have changed) */
    }
    if (d->list && (e & LE_PICK) && (r = list_row(sx, sy)) >= 0) {   /* (between rows: nothing) */
        struct Window *w;
        struct Task *t;
        BOOL hid;
        if (r >= nlist) do_act(d, r - nlist);
        else if (lmode) pick_row(d, r);
        else {
            w = list[r].win;
            t = list[r].task;
            hid = list[r].hid;
            run_list_close(d);
            show_one(w, t, hid);
            to_front(w, t);
        }
        e &= LE_ESC;
    }
    if (d->list && (e & (LE_AWAY | LE_ESC))) run_list_close(d);
}
