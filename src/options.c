/* SimpleDock - settings: command line, icon tooltypes, config file lines
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <intuition/intuition.h>
#include <proto/utility.h>
#include "sd.h"

extern struct WBStartup *WBenchMsg;

/* The keywords of the command line, the icon's tooltypes and the settings
   lines of the config file. */
static const char TEMPLATE[] =
    "CONFIG,POS=POSITION,DISTANCE/K/N,OFFSET/K/N,FONT/K,PUBSCREEN/K,NOTEXT/S,HOVERTEXT/S,"
    "NOICON/S,NOBORDER/S,ICONDIST/K/N,SCALEICONW/K/N,SCALEICONH/K/N,LIMIT/K/N,AUTOHIDE/K/N,"
    "ONTOP/S,TRANSPARENT/S,TINT/K,OPACITY/K/N,RADIUS/K/N,DELAY/K/N,QUIET/S,VERBOSE/S,HOTKEY/K,CUSTOMTINT/K,CRASHLOG/S,"
    "RUNNING/K/N,CLICKNEW/S,WBSTART/S,DROPMOVE/S,HOLDTIME/K/N,RUNHIDE/S,IGNORE/K,WBHIDE/S,"
    "MINKEY/K,MINALLKEY/K,AHKEY/K,SHELLASK/S";

enum { A_CONFIG, A_POS, A_DISTANCE, A_OFFSET, A_FONT, A_PUBSCREEN, A_NOTEXT, A_HOVERTEXT,
       A_NOICON, A_NOBORDER, A_ICONDIST, A_SCALEW, A_SCALEH, A_LIMIT, A_AUTOHIDE,
       A_ONTOP, A_TRANSPARENT, A_TINT, A_OPACITY, A_RADIUS, A_DELAY, A_QUIET, A_VERBOSE, A_HOTKEY, A_CUSTOM,
       A_CRASHLOG, A_RUNNING, A_CLICKNEW, A_WBSTART, A_DROPMOVE, A_HOLDTIME, A_RUNHIDE, A_IGNORE, A_WBHIDE,
       A_MINKEY, A_MINALLKEY, A_AHKEY, A_SHELLASK, A_COUNT };

#define TTLINE 2048

static char iconname[512];   /* the dock's icon: its tooltypes (a drawer dock also saves into it) */

static LONG num(LONG *a, LONG def) { return a ? *a : def; }
static LONG range(LONG v, LONG lo, LONG hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Append tooltype "KEY=VALUE" / "KEY" to line as a ReadArgs argument, if KEY
   is in the template. Disabled ("(KEY=..)") and unknown tooltypes are skipped. */
static void add_tooltype(char *line, LONG *len, CONST_STRPTR tt)
{
    char key[32];
    LONG k = 0, n;
    const char *v;

    while (tt[k] && tt[k] != '=' && k < (LONG)sizeof(key) - 1) { key[k] = tt[k]; k++; }
    key[k] = 0;
    if (k == 0 || tt[k] && tt[k] != '=' || FindArg((STRPTR)TEMPLATE, key) < 0) return;
    v = tt[k] == '=' ? tt + k + 1 : NULL;
    n = k + (v ? 2 * strlen(v) + 4 : 1) + 1;
    if (*len + n >= TTLINE - 2) return;
    strcpy(line + *len, key); *len += k;
    if (v) {                                     /* quoted; '*' is ReadArgs' escape inside quotes */
        char *p = line + *len;
        *p++ = '='; *p++ = '"';
        for (; *v; v++) {
            if (*v == '\n') continue;
            if (*v == '*' || *v == '"') *p++ = '*';
            *p++ = *v;
        }
        *p++ = '"';
        *len = p - line;
    }
    line[(*len)++] = ' ';
    line[*len] = 0;
}

static struct RDArgs *rd_icon;          /* tooltypes parsed after the start (Shell start, reload) */
static struct RDArgs *rd_file;          /* settings lines of the config file */
static LONG cliargs[A_COUNT];           /* Shell start: the command line (wins over tooltypes) */
static BOOL fromshell;
static STRPTR projcfg_kept;             /* Workbench start with a project icon: its path */
static LONG ah_disabled;                /* "(AUTOHIDE=n)": speed kept while auto-hide is off */
static BOOL nomem;                      /* the last parse failed for want of memory, not a bad line */

/* Parse settings lines ("KEY=VALUE", "KEY", or "(KEY)" for off), from
   tooltypes or the settings part of the config file, with the command-line
   template. */
static struct RDArgs *parse_list(STRPTR *tt, LONG *args)
{
    static char line[TTLINE];
    struct RDArgs *rd;
    LONG len = 0;

    line[0] = 0;
    for (; tt && *tt; tt++) {
        add_tooltype(line, &len, *tt);
        if (!Strnicmp(*tt, "(AUTOHIDE=", 10)) StrToLong(*tt + 10, &ah_disabled);
    }
    line[len++] = '\n';
    line[len] = 0;
    nomem = TRUE;
    if (!(rd = AllocDosObject(DOS_RDARGS, NULL))) return NULL;
    rd->RDA_Source.CS_Buffer = (UBYTE *)line;
    rd->RDA_Source.CS_Length = len;
    rd->RDA_Flags = RDAF_NOPROMPT;
    nomem = FALSE;
    if (!ReadArgs((STRPTR)TEMPLATE, args, rd)) {
        nomem = IoErr() == ERROR_NO_FREE_STORE;
        FreeArgs(rd);                            /* (dos.doc: still due after a failed ReadArgs) */
        FreeDosObject(DOS_RDARGS, rd);
        rd = NULL;
    }
    return rd;
}

static struct RDArgs *parse_icon(struct DiskObject *dobj, LONG *args)
{
    return parse_list((STRPTR *)dobj->do_ToolTypes, args);
}

/* The keyword of a settings line ("KEY", "KEY=...", "(KEY...)") as its place
   in the template (POS and POSITION count as the same). -1 if it isn't a
   setting, an icon path for example. */
LONG opts_key(CONST_STRPTR line)
{
    char key[32];
    LONG k = 0;
    if (*line == '(') line++;
    while (line[k] && line[k] != '=' && line[k] != ')' && k < (LONG)sizeof(key) - 1) {
        if (line[k] == ':' || line[k] == '/' || line[k] == ' ') return -1;
        key[k] = line[k]; k++;
    }
    key[k] = 0;
    if (!k || (line[k] && line[k] != '=' && line[k] != ')')) return -1;
    return FindArg((STRPTR)TEMPLATE, key);
}

/* Is template keyword k a switch (/S)? */
BOOL opts_switch(LONG k)
{
    CONST_STRPTR t = TEMPLATE;
    while (k-- > 0 && (t = strchr(t, ','))) t++;
    if (!t) return FALSE;
    while (*t && *t != ',') { if (t[0] == '/' && t[1] == 'S') return TRUE; t++; }
    return FALSE;
}

/* A line of the config file that is a setting, not an icon path. */
BOOL opts_is_setting(CONST_STRPTR line)
{
    return opts_key(line) >= 0;
}

/* Did the last check or parse fail for want of memory (not a bad line)? */
BOOL opts_nomem(void) { return nomem; }

/* Would these settings lines be accepted (values of the right kind)? Nothing
   is changed. */
BOOL opts_check(STRPTR *lines)
{
    LONG a[A_COUNT], keep = ah_disabled;
    struct RDArgs *rd;
    memset(a, 0, sizeof(a));
    rd = parse_list(lines, a);
    ah_disabled = keep;
    if (!rd) return FALSE;
    FreeArgs(rd);
    FreeDosObject(DOS_RDARGS, rd);
    return TRUE;
}

/* Workbench start: read the tooltypes of the program's icon, or of a config
   file's project icon (default tool SimpleDock). In that case the file is the
   config and its name the ARexx port, so several docks can run. */
static struct RDArgs *read_tooltypes(LONG *args, char *portname, LONG portlen, STRPTR *cfg)
{
    struct WBArg *wa = &WBenchMsg->sm_ArgList[WBenchMsg->sm_NumArgs > 1 ? 1 : 0];
    struct DiskObject *dobj;
    struct RDArgs *rd = NULL;
    BPTR old = CurrentDir(wa->wa_Lock);

    if ((dobj = GetDiskObject(wa->wa_Name))) {
        rd = parse_icon(dobj, args);
        FreeDiskObject(dobj);
        if (!rd) {                                               /* a bad tooltype: say so, use defaults */
            struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME,
                                     "A tooltype of %s is not valid.\nStarting with default settings.", "Ok" };
            EasyRequest(NULL, &es, NULL, (LONG)wa->wa_Name);
            memset(args, 0, A_COUNT * sizeof(LONG));
        }
    }
    if (!rd && (rd = AllocDosObject(DOS_RDARGS, NULL))) {       /* no icon (or a bad one): defaults */
        rd->RDA_Source.CS_Buffer = (UBYTE *)"\n";
        rd->RDA_Source.CS_Length = 1;
        if (!ReadArgs((STRPTR)TEMPLATE, args, rd)) { FreeArgs(rd); FreeDosObject(DOS_RDARGS, rd); rd = NULL; }
    }
    if (!NameFromLock(wa->wa_Lock, iconname, sizeof(iconname)) || !AddPart(iconname, wa->wa_Name, sizeof(iconname)))
        iconname[0] = 0;
    if (WBenchMsg->sm_NumArgs > 1) {
        if (iconname[0]) *cfg = iconname;
        strncpy(portname, wa->wa_Name, portlen - 1);
        portname[portlen - 1] = 0;
    }
    CurrentDir(old);
    return rd;
}

/* Shell start: the command line, on top of the tooltypes of the program's
   own icon. */
static struct RDArgs *read_shell(LONG *args)
{
    LONG t[A_COUNT], i;
    char prog[108], name[160];
    struct DiskObject *dobj;
    struct RDArgs *rd;

    memset(t, 0, sizeof(t));
    strcpy(name, "PROGDIR:");
    if (GetProgramName(prog, sizeof(prog)) && AddPart(name, FilePart(prog), sizeof(name)) &&
        (dobj = GetDiskObject(name))) {
        rd_icon = parse_icon(dobj, t);
        FreeDiskObject(dobj);
    }
    if (!(rd = ReadArgs((STRPTR)TEMPLATE, args, NULL))) return NULL;
    memcpy(cliargs, args, sizeof(cliargs));
    fromshell = TRUE;
    for (i = 0; i < A_COUNT; i++)
        if (!args[i] && rd_icon) args[i] = t[i];
    return rd;
}

static void fill(struct Opts *o, LONG *a, STRPTR projcfg);

struct RDArgs *opts_read(struct Opts *o, char *portname, LONG portlen)
{
    LONG a[A_COUNT];
    struct RDArgs *rd;
    STRPTR projcfg = NULL;

    memset(a, 0, sizeof(a));
    if (WBenchMsg) rd = read_tooltypes(a, portname, portlen, &projcfg);
    else rd = read_shell(a);
    if (!rd) return NULL;
    projcfg_kept = projcfg;
    fill(o, a, projcfg);
    return rd;
}

/* The dock's icon: its tooltypes are read at every load; a drawer dock saves into it. */
static BOOL icon_path(char *name, LONG size)
{
    char prog[108];
    if (iconname[0]) { strncpy(name, iconname, size - 1); name[size - 1] = 0; return TRUE; }
    strcpy(name, "PROGDIR:");
    return GetProgramName(prog, sizeof(prog)) && AddPart(name, FilePart(prog), size);
}

/* Work out the settings in use: the icon's tooltypes, then the config file's
   settings lines on top (a bracketed switch such as "(NOTEXT)" turns it off
   even if the icon has it), then the Shell command line. lines may be NULL (no
   settings in the file, or a drawer). If a setting isn't valid, nothing
   changes and FALSE is returned. */
BOOL opts_apply(struct Dock *d, STRPTR *lines, CONST_STRPTR from)
{
    struct Opts *o = d->o;
    STRPTR home = o->config, *l;
    BOOL def = o->defconfig, off[A_COUNT];
    LONG a[A_COUNT], t[A_COUNT], i;
    char name[512];
    struct DiskObject *dobj;
    struct RDArgs *rt = NULL, *rf = NULL;

    memset(a, 0, sizeof(a));
    memset(t, 0, sizeof(t));
    memset(off, 0, sizeof(off));
    ah_disabled = 0;
    if (icon_path(name, sizeof(name)) && (dobj = GetDiskObject(name))) {
        rt = parse_icon(dobj, t);
        FreeDiskObject(dobj);
        if (!rt) {                               /* a bad tooltype: the file's settings still count */
            static BOOL told;
            memset(t, 0, sizeof(t));
            if (!told) sd_msg(d, "A tooltype of %s is not valid; it is ignored.", (LONG)name);
            told = TRUE;
        }
    }
    if (lines && *lines) {
        if (!(rf = parse_list(lines, a))) {
            if (rt) { FreeArgs(rt); FreeDosObject(DOS_RDARGS, rt); }
            sd_msg(d, nomem ? "Not enough memory for the settings of %s.\nThey stay as they are."
                            : "A setting in %s is not valid.\nThe settings stay as they are.", (LONG)from);
            return FALSE;
        }
        for (l = lines; *l; l++)                 /* "(KEY...)": off */
            if ((*l)[0] == '(') {
                char key[32];
                LONG k = 0;
                while ((*l)[k + 1] && (*l)[k + 1] != '=' && (*l)[k + 1] != ')' && k < 31) { key[k] = (*l)[k + 1]; k++; }
                key[k] = 0;
                if ((i = FindArg((STRPTR)TEMPLATE, key)) >= 0 && i < A_COUNT) off[i] = TRUE;
            }
    }
    for (i = 0; i < A_COUNT; i++)
        if (!a[i] && !off[i]) a[i] = t[i];
    if (fromshell)
        for (i = 0; i < A_COUNT; i++) if (cliargs[i]) a[i] = cliargs[i];
    fill(o, a, projcfg_kept);
    o->config = home;
    o->defconfig = def;
    if (rd_icon) { FreeArgs(rd_icon); FreeDosObject(DOS_RDARGS, rd_icon); }
    if (rd_file) { FreeArgs(rd_file); FreeDosObject(DOS_RDARGS, rd_file); }
    rd_icon = rt;                               /* the strings in o point into these */
    rd_file = rf;
    return TRUE;
}

static void fill(struct Opts *o, LONG *a, STRPTR projcfg)
{
    memset(o, 0, sizeof(*o));
    o->config     = a[A_CONFIG] ? (STRPTR)a[A_CONFIG] : projcfg ? projcfg : (STRPTR)SD_CONFIG;
    o->defconfig  = !a[A_CONFIG] && !projcfg;
    o->pos        = (STRPTR)a[A_POS];
    o->font       = (STRPTR)a[A_FONT];
    o->pubscreen  = (STRPTR)a[A_PUBSCREEN];
    o->tint       = (STRPTR)a[A_TINT];
    o->customtint = (STRPTR)a[A_CUSTOM];         /* the one custom colour of the settings window */
    o->hotkey     = a[A_HOTKEY] ? (STRPTR)a[A_HOTKEY] : (STRPTR)"ctrl alt shift";   /* reveal the dock (tapped) */
    o->minkey     = a[A_MINKEY] ? (STRPTR)a[A_MINKEY] : (STRPTR)"ctrl alt m";           /* minimise the active window */
    o->minallkey  = a[A_MINALLKEY] ? (STRPTR)a[A_MINALLKEY] : (STRPTR)"ctrl alt shift m";   /* ... all */
    o->ahkey      = a[A_AHKEY] ? (STRPTR)a[A_AHKEY] : (STRPTR)"ctrl alt h";            /* auto-hide on / off */
    o->distance   = num((LONG *)a[A_DISTANCE], 4);       /* gap to the screen edge */
    o->offset     = num((LONG *)a[A_OFFSET], 0);
    o->icondist   = num((LONG *)a[A_ICONDIST], 2);
    o->delay      = range(num((LONG *)a[A_DELAY], 0), 0, 500);             /* at most 10 s */
    o->limit      = range(num((LONG *)a[A_LIMIT], 0), 0, 1000);
    o->scalew     = range(num((LONG *)a[A_SCALEW], 0), 0, 256);
    o->scaleh     = range(num((LONG *)a[A_SCALEH], 0), 0, 256);
    o->offset     = range(o->offset, -4096, 4096);
    o->autohide   = a[A_AUTOHIDE] ? range(num((LONG *)a[A_AUTOHIDE], 3), 0, 100) : 0;
    o->ahspeed    = o->autohide > 0 ? o->autohide : ah_disabled > 0 ? ah_disabled : 3;   /* the speed when auto-hide is turned on */
    o->radius     = num((LONG *)a[A_RADIUS], 12);
    o->opacity    = num((LONG *)a[A_OPACITY], 55);
    o->notext     = a[A_NOTEXT] != 0;
    o->hovertext  = a[A_HOVERTEXT] != 0;
    o->noicon     = a[A_NOICON] != 0;
    o->noborder   = a[A_NOBORDER] != 0;
    o->ontop      = a[A_ONTOP] != 0;
    o->transparent = a[A_TRANSPARENT] != 0;
    o->quiet      = a[A_QUIET] != 0;
    o->verbose    = a[A_VERBOSE] != 0;
    o->crashlog   = a[A_CRASHLOG] != 0;          /* crash reports (crash.c) */
    o->running    = range(num((LONG *)a[A_RUNNING], 2), 0, 2);   /* running programs: off, dot, dot + number */
    o->clicknew   = a[A_CLICKNEW] != 0;          /* a click starts a new copy (Shift+click: to the front) */
    o->wbstart    = a[A_WBSTART] != 0;           /* programs started by Workbench, not the dock */
    o->dropmove   = a[A_DROPMOVE] != 0;          /* drops onto a drawer move on the same disk */
    o->holdtime   = range(num((LONG *)a[A_HOLDTIME], 3), 1, 50);  /* long press, tenths of a second */
    o->runhide    = a[A_RUNHIDE] != 0;           /* programs not on the dock are not shown */
    o->ignore     = (STRPTR)a[A_IGNORE];         /* ... nor these ("Name,Name") */
    o->wbhide     = a[A_WBHIDE] != 0;            /* no Workbench icon for the drawer windows */
    o->shellask   = a[A_SHELLASK] != 0;          /* Shell icons: "Execute a file" first (ask.c) */
    if (o->distance < 0) o->distance = 0;
    if (o->distance > 64) o->distance = 64;
    if (o->icondist < 0) o->icondist = 0;
    if (o->icondist > 64) o->icondist = 64;
    if (o->radius < 0) o->radius = 0;
    if (o->radius > 32) o->radius = 32;
    if (o->opacity < 0) o->opacity = 0;
    if (o->opacity > 100) o->opacity = 100;
}

static BOOL is_key(CONST_STRPTR tt, CONST_STRPTR key)
{
    LONG n = strlen(key);
    return !Strnicmp((STRPTR)tt, (STRPTR)key, n) && (tt[n] == 0 || tt[n] == '=');
}

/* Tooltype tt is KEY, KEY=..., (KEY) or (KEY=...) */
static BOOL is_key_any(CONST_STRPTR tt, CONST_STRPTR key)
{
    return is_key(tt, key) || (tt[0] == '(' && is_key(tt + 1, key)) ||
           (tt[0] == '(' && !Strnicmp((STRPTR)tt + 1, (STRPTR)key, strlen(key)) && tt[1 + strlen(key)] == ')');
}

static const char *const SAVED[] = { "POS", "POSITION", "AUTOHIDE", "NOTEXT", "HOVERTEXT", "NOICON", "ICONDIST",
    "SCALEICONW", "SCALEICONH", "DISTANCE", "TRANSPARENT", "TINT", "OPACITY", "RADIUS", "ONTOP", "HOTKEY",
    "CUSTOMTINT", "MINKEY", "MINALLKEY", "AHKEY", NULL };

/* A settings line that saving writes itself (the rest are kept as they are). */
BOOL opts_saved(CONST_STRPTR line)
{
    LONG i;
    if (*line == '(') line++;
    for (i = 0; SAVED[i]; i++) {
        LONG n = strlen(SAVED[i]);
        if (!Strnicmp((STRPTR)line, (STRPTR)SAVED[i], n) && (!line[n] || line[n] == '=' || line[n] == ')'))
            return TRUE;
    }
    return FALSE;
}

/* The settings as lines (static buffers, valid until the next call);
   returns how many (at most 19; callers allow 20). */
LONG opts_lines(struct Dock *d, STRPTR *nt)
{
    static char buf[15][72];
    struct Opts *o = d->o;
    LONG k = 0, b = 0, v[1];

    buf[b][0] = 'P'; buf[b][1] = 'O'; buf[b][2] = 'S'; buf[b][3] = '=';
    if (o->pos && o->pos[0] && strlen(o->pos) < 8) strcpy(buf[b] + 4, o->pos);   /* (also while closed) */
    else { buf[b][4] = d->edge; buf[b][5] = d->align != 'C' ? d->align : 0; buf[b][6] = 0; }
    nt[k++] = buf[b++];
    v[0] = o->autohide > 0 ? o->autohide : o->ahspeed;
    SD_FMT(buf[b], o->autohide > 0 ? "AUTOHIDE=%ld" : "(AUTOHIDE=%ld)", v); nt[k++] = buf[b++];
    nt[k++] = o->hovertext ? "HOVERTEXT" : "(HOVERTEXT)";
    nt[k++] = o->notext && !o->hovertext ? "NOTEXT" : "(NOTEXT)";
    nt[k++] = o->noicon ? "NOICON" : "(NOICON)";
    SD_FMT(buf[b], "ICONDIST=%ld", &o->icondist); nt[k++] = buf[b++];
    SD_FMT(buf[b], "SCALEICONW=%ld", &o->scalew); nt[k++] = buf[b++];      /* 0: icons at their own size */
    SD_FMT(buf[b], "SCALEICONH=%ld", &o->scaleh); nt[k++] = buf[b++];
    SD_FMT(buf[b], "DISTANCE=%ld", &o->distance); nt[k++] = buf[b++];
    nt[k++] = o->ontop ? "ONTOP" : "(ONTOP)";
    nt[k++] = o->transparent ? "TRANSPARENT" : "(TRANSPARENT)";
    v[0] = d->tint;
    if (o->tint && o->tint[0] == '#' && strlen(o->tint) == 7) { strcpy(buf[b], "TINT="); strcat(buf[b], o->tint); }
    else SD_FMT(buf[b], "TINT=#%06lx", v);
    nt[k++] = buf[b++];
    SD_FMT(buf[b], "OPACITY=%ld", &o->opacity); nt[k++] = buf[b++];
    SD_FMT(buf[b], "RADIUS=%ld", &o->radius); nt[k++] = buf[b++];
    if (strlen(o->hotkey) < sizeof(buf[0]) - 8) {
        strcpy(buf[b], "HOTKEY="); strcat(buf[b], o->hotkey); nt[k++] = buf[b++];
    }
    if (o->customtint && strlen(o->customtint) < sizeof(buf[0]) - 12) {
        strcpy(buf[b], "CUSTOMTINT="); strcat(buf[b], o->customtint); nt[k++] = buf[b++];
    }
    {                                            /* the other keys */
        static const char *const kn[3] = { "MINKEY=", "MINALLKEY=", "AHKEY=" };
        STRPTR kv[3];
        LONG i;
        kv[0] = o->minkey; kv[1] = o->minallkey; kv[2] = o->ahkey;
        for (i = 0; i < 3; i++)
            if (kv[i] && strlen(kv[i]) < sizeof(buf[0]) - 12) { strcpy(buf[b], kn[i]); strcat(buf[b], kv[i]); nt[k++] = buf[b++]; }
    }
    return k;
}

/* Write the settings into the tooltypes of the dock's own icon. Only used for
   a dock made from a drawer of icons, which has no config file. Switches that
   are off are written in brackets; other tooltypes are kept. */
BOOL opts_save(struct Dock *d)
{
    char name[512];
    struct DiskObject *dobj;
    STRPTR *old, *nt;
    LONG n = 0, k = 0, i;
    BOOL ok, skip;

    if (!icon_path(name, sizeof(name))) return FALSE;
    if (!(dobj = GetDiskObject(name)) && !(dobj = GetDefDiskObject(WBTOOL))) {
        sd_msg(d, "Cannot read the icon %s", (LONG)name);
        return FALSE;
    }
    old = (STRPTR *)dobj->do_ToolTypes;
    while (old && old[n]) n++;
    if (!(nt = AllocVec((n + 21) * sizeof(STRPTR), MEMF_CLEAR))) { FreeDiskObject(dobj); return FALSE; }
    for (n = 0; old && old[n]; n++) {
        skip = FALSE;
        for (i = 0; SAVED[i] && !skip; i++) skip = is_key_any(old[n], SAVED[i]);
        if (!skip) nt[k++] = old[n];
    }
    k += opts_lines(d, nt + k);
    dobj->do_ToolTypes = (STRPTR *)nt;
    ok = PutDiskObject(name, dobj);
    dobj->do_ToolTypes = (STRPTR *)old;
    FreeDiskObject(dobj);
    FreeVec(nt);
    if (!ok) sd_msg(d, "Cannot write to\n%s", (LONG)name);
    return ok;
}

void opts_free(struct RDArgs *rd)
{
    if (rd_icon) { FreeArgs(rd_icon); FreeDosObject(DOS_RDARGS, rd_icon); rd_icon = NULL; }
    if (rd_file) { FreeArgs(rd_file); FreeDosObject(DOS_RDARGS, rd_file); rd_file = NULL; }
    if (!rd) return;
    FreeArgs(rd);
    if (WBenchMsg) FreeDosObject(DOS_RDARGS, rd);
}
