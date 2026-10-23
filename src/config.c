/* SimpleDock - the config file: settings and icon list in one text file
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later

   ENVARC:SimpleDock.config (or CONFIG=) holds everything: the settings
   lines first, using the command-line keywords ("POS=B", "ONTOP", and
   "(NOTEXT)" for off), then the icons, one path per line, each optionally
   followed by LABEL and IMAGE lines. Lines starting with ';' are comments.
   Closing the settings window writes this file, Load... reads another one
   into the dock and Save as... writes a copy elsewhere. A file without
   settings lines uses the icon's tooltypes. CONFIG can also be a drawer of
   icons; its settings are then kept in the icon. */
#include <string.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <workbench/startup.h>
#include "sd.h"

/* New entry before 'before' (NULL = at the end). */
struct Item *config_add(struct Dock *d, CONST_STRPTR path, struct Item *before)
{
    struct Item *it = AllocVec(sizeof(*it), MEMF_CLEAR);
    if (!it) return NULL;
    if (!(it->path = sd_strdup(path))) { FreeVec(it); return NULL; }
    if (!before || before->temp) {               /* at the end: after the last kept icon (never among */
        struct Item *x, *last = NULL;            /* the running others, which may be in the list now, */
        FOR_ITEMS(d, x) if (!x->temp) last = x;  /* with the icons that did not fit after them) */
        Insert((struct List *)&d->items, (struct Node *)it, last ? (struct Node *)last : NULL);
        return it;
    }
    Insert((struct List *)&d->items, (struct Node *)it, (struct Node *)before->node.mln_Pred);
    return it;
}

static struct Item *add_item(struct Dock *d, CONST_STRPTR path)
{
    return config_add(d, path, NULL);
}

void config_remove(struct Dock *d, struct Item *it)
{
    if (it->temp == RUN_MORE) return;            /* (not an icon of its own) */
    Remove((struct Node *)it);
    run_forget(it);                              /* (its programs run on) */
    if (d->last == it) d->last = NULL;
    if (d->pressed == it) d->pressed = NULL;
    scale_free(it);
    if (it->dobj) FreeDiskObject(it->dobj);
    FreeVec(it->path);
    FreeVec(it->label);
    FreeVec(it->image);
    FreeVec(it);
}

static BOOL keyword(CONST_STRPTR line, CONST_STRPTR kw, CONST_STRPTR *rest)
{
    LONG n = strlen(kw);
    if (Strnicmp((STRPTR)line, (STRPTR)kw, n) || (line[n] != ' ' && line[n])) return FALSE;
    for (line += n; *line == ' '; line++) ;
    *rest = line;
    return TRUE;
}

/* CONFIG is a drawer: every icon in it is an entry (read-only dock). */
static BOOL load_drawer(struct Dock *d, BPTR lock)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    char dir[256], path[SD_LINEMAX];

    if (!fib) return FALSE;
    if (!NameFromLock(lock, dir, sizeof(dir)) || !Examine(lock, fib)) { FreeDosObject(DOS_FIB, fib); return FALSE; }
    while (ExNext(lock, fib)) {
        LONG n = strlen(fib->fib_FileName);
        if (n <= 5 || Stricmp(fib->fib_FileName + n - 5, ".info") || !Stricmp(fib->fib_FileName, "disk.info"))
            continue;
        strcpy(path, dir);
        if (!AddPart(path, fib->fib_FileName, sizeof(path))) continue;
        path[strlen(path) - 5] = 0;
        add_item(d, path);
    }
    FreeDosObject(DOS_FIB, fib);
    return TRUE;
}

/* No config file yet: the standard icons that exist here, labels on hover,
   in front of windows. */
static void config_default(struct Dock *d)
{
    static const char *const std[] = { "SYS:", "SYS:System/Shell", "SYS:Utilities/MultiView",
        "SYS:Utilities/Clock", "SYS:Tools/Calculator", "SYS:Tools/IconEdit", "SYS:Tools/Commodities/Exchange",
        "SYS:Prefs", "SYS:Tools", "SYS:Utilities", "RAM:", NULL };
    char info[SD_LINEMAX];
    LONG i, n;
    BPTR lock;
    struct Item *it;

    for (i = 0; std[i]; i++) {
        n = strlen(std[i]);
        strcpy(info, std[i]);
        if (std[i][n - 1] != ':') strcat(info, ".info");   /* a disk: the disk itself */
        if (!(lock = Lock(info, ACCESS_READ))) continue;
        if (std[i][n - 1] == ':' && (it = add_item(d, std[i])) &&
            NameFromLock(lock, info, sizeof(info)) && (n = strlen(info)) > 1) {
            info[n - 1] = 0;                     /* a disk: its name as the label */
            it->label = sd_strdup(info);
        } else if (std[i][n - 1] != ':') add_item(d, std[i]);
        UnLock(lock);
    }
    d->o->hovertext = TRUE;
    d->o->ontop = TRUE;
    d->overtold = TRUE;                          /* those that do not fit are left out (config_created) */
}

static char setbuf[4096];               /* the settings lines in use (the file's, then ARexx SET's) */
static STRPTR setv[49];

/* Read the icons and settings from 'path', or from the dock's own file if path
   is NULL. If the own file is missing, use a copy left by an interrupted save,
   or the standard icons (the file is then written once the dock is open).
   Returns FALSE if the file exists but can't be read; it is then never saved
   over. */
BOOL config_load(struct Dock *d, CONST_STRPTR path)
{
    static const char *const spare[] = { ".new", ".bak", NULL };
    char line[512], alt[SD_LINEMAX + 8];
    struct Item *last = NULL;
    CONST_STRPTR file = path ? path : (CONST_STRPTR)d->o->config;
    BPTR fh = 0, lock;
    LONG lineno = 0, nset = 0, used = 0, i;

    NewList((struct List *)&d->items);
    NewList((struct List *)&d->extra);
    d->overtold = FALSE;
    setv[0] = NULL;
    if (!path) d->cfgdrawer = FALSE;
    if ((lock = Lock(file, ACCESS_READ))) {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        BOOL dir = fib && Examine(lock, fib) && fib->fib_DirEntryType > 0;
        if (fib) FreeDosObject(DOS_FIB, fib);
        if (dir) {
            BOOL ok = load_drawer(d, lock);
            if (!path) d->cfgdrawer = TRUE;
            UnLock(lock);
            if (!ok) sd_msg(d, "Cannot read drawer %s", (LONG)file);
            else opts_apply(d, NULL, file);
            if (!path) d->cfgfailed = !ok;
            return ok;
        }
        UnLock(lock);
    }
    if (!(fh = Open(file, MODE_OLDFILE))) {
        LONG err = IoErr();
        if (err != ERROR_OBJECT_NOT_FOUND || path) {
            sd_msg(d, "Cannot read %s (error %ld)", (LONG)file, err);
            if (!path) d->cfgfailed = TRUE;                             /* never saved over */
            return FALSE;
        }
        for (i = 0; spare[i] && !fh && strlen(file) + 5 < sizeof(alt); i++) {
            strcpy(alt, file);
            strcat(alt, spare[i]);
            if ((fh = Open(alt, MODE_OLDFILE))) file = alt;            /* left by an interrupted save */
        }
        if (!fh) d->cfgcreate = TRUE;                                   /* missing: made from the defaults */
        if (!fh) {
            opts_apply(d, NULL, file);
            config_default(d);
            d->cfgfailed = FALSE;
            return TRUE;
        }
    }
    while (FGets(fh, line, sizeof(line))) {
        LONG n = strlen(line);
        CONST_STRPTR rest;
        lineno++;
        if (n == sizeof(line) - 1 && line[n - 1] != '\n') {      /* full: too long, unless it ends here */
            LONG c = FGetC(fh);
            if (c >= 0 && c != '\n') {
                while ((c = FGetC(fh)) >= 0 && c != '\n') ;
                sd_msg(d, "%s line %ld is too long, ignored", (LONG)file, lineno);
                last = NULL;
                continue;
            }
        }
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!n || line[0] == ';') continue;
        if (opts_is_setting(line)) {
            {                                    /* the same keyword on an earlier line: this one counts */
                LONG key = opts_key(line);       /* (ReadArgs refuses a keyword twice: all would be lost) */
                for (i = 0; i < nset && opts_key(setv[i]) != key; i++) ;
                if (i < nset) {
                    sd_verbose(d, "%s: %s given again, the later line counts", (LONG)file, (LONG)setv[i]);
                    for (; i < nset - 1; i++) setv[i] = setv[i + 1];
                    nset--;
                }
            }
            if (nset < 48 && used + n + 1 <= (LONG)sizeof(setbuf)) {
                setv[nset++] = strcpy(setbuf + used, line);
                used += n + 1;
            } else sd_verbose(d, "Too many settings lines in %s: %s ignored", (LONG)file, (LONG)line);
        } else if (keyword(line, "LABEL", &rest)) {
            if (last && !last->label) last->label = sd_strdup(rest);
        } else if (keyword(line, "IMAGE", &rest)) {
            if (last && !last->image && *rest) last->image = sd_strdup(rest);
        } else {
            last = add_item(d, line);
        }
    }
    Close(fh);
    setv[nset] = NULL;
    if (nset && !opts_check(setv)) {             /* lines that are not valid are dropped (said once, */
        LONG k = 0;                              /* below); else every change would fail */
        if (opts_nomem()) {                      /* (out of memory, not a bad line: nothing dropped, */
            sd_msg(d, "Not enough memory to read the settings of %s.", (LONG)file);   /* and the file */
            opts_apply(d, NULL, file);           /* is not saved over) */
            if (!path) d->cfgfailed = TRUE;
            return TRUE;
        }
        for (i = 0; i < nset; i++) {
            STRPTR one[2];
            one[0] = setv[i]; one[1] = NULL;
            if (opts_check(one)) setv[k++] = setv[i];
        }
        setv[k] = NULL;
        sd_msg(d, "%ld settings lines of %s are not valid; they are ignored.", nset - k, (LONG)file);
        nset = k;
    }
    opts_apply(d, nset ? setv : NULL, file);
    if (!path) d->cfgfailed = FALSE;
    return TRUE;
}

/* The dock's own file was missing: written now the dock is open (its
   position and colour are known). */
void config_created(struct Dock *d)
{
    if (!d->cfgcreate) return;
    d->cfgcreate = FALSE;
    while (dock_extra(d) > d->pushed)           /* standard icons that do not fit this screen */
        config_remove(d, (struct Item *)d->extra.mlh_TailPred);
    if (config_save(d, NULL)) sd_verbose(d, "Created %s", (LONG)d->o->config);
    else sd_verbose(d, "No config file, and %s cannot be created", (LONG)d->o->config);
}

/* Settings and icons into 'path' (NULL: the dock's own config file), via a
   temporary file. A dock made from a drawer keeps its settings in the icon. */
/* The full path of this program (0: unknown): its home drawer, then its name. */
static BOOL prog_path(char *out, LONG size)
{
    extern struct WBStartup *WBenchMsg;
    struct Process *me = (struct Process *)FindTask(NULL);
    char nm[108];
    out[0] = 0;
    if (me->pr_HomeDir && GetProgramName(nm, sizeof(nm)) && NameFromLock(me->pr_HomeDir, out, size) &&
        AddPart(out, FilePart(nm), size)) return TRUE;
    out[0] = 0;
    if (WBenchMsg && WBenchMsg->sm_NumArgs > 0 && WBenchMsg->sm_ArgList[0].wa_Name &&
        NameFromLock(WBenchMsg->sm_ArgList[0].wa_Lock, out, size) &&
        AddPart(out, WBenchMsg->sm_ArgList[0].wa_Name, size)) return TRUE;
    out[0] = 0;
    return FALSE;
}

/* Save as...: the new file gets a project icon whose default tool is this
   program, so a double-click starts a dock from it (another dock, with its
   own settings). An icon that is already there is left alone. */
static void save_icon(struct Dock *d, CONST_STRPTR path)
{
    struct DiskObject *dobj;
    char prog[512];
    if ((dobj = GetDiskObject(path))) { FreeDiskObject(dobj); return; }
    if (!prog_path(prog, sizeof(prog)) || !(dobj = GetDefDiskObject(WBPROJECT))) return;
    dobj->do_DefaultTool = prog;
    dobj->do_ToolTypes = NULL;
    dobj->do_CurrentX = dobj->do_CurrentY = NO_ICON_POSITION;
    if (PutDiskObject(path, dobj)) sd_verbose(d, "%s has an icon now: double-click it for a dock from this file", (LONG)path);
    FreeDiskObject(dobj);
}

BOOL config_save(struct Dock *d, CONST_STRPTR path)
{
    char tmp[SD_LINEMAX + 8], bak[SD_LINEMAX + 8];
    CONST_STRPTR file = path ? path : (CONST_STRPTR)d->o->config;
    STRPTR set[20];
    struct Item *it;
    BPTR fh;
    BOOL ok = TRUE;
    LONG n, i, pass;

    if (!path && d->cfgdrawer) return opts_save(d);
    if (!path && d->cfgfailed) {
        sd_msg(d, "%s could not be read, so it is not saved over.\n"
                  "Fix or remove it, or use Save as... in the settings window.", (LONG)file);
        return FALSE;
    }
    if (strlen(file) + 5 >= sizeof(tmp)) return FALSE;
    strcpy(tmp, file); strcat(tmp, ".new");
    strcpy(bak, file); strcat(bak, ".bak");
    if (!(fh = Open(tmp, MODE_NEWFILE))) {
        sd_msg(d, "Cannot write %s", (LONG)tmp);
        return FALSE;
    }
    n = opts_lines(d, set);
    if (FPuts(fh, "; SimpleDock settings - a keyword in parentheses is disabled\n")) ok = FALSE;
    for (i = 0; i < n; i++)
        if (FPrintf(fh, "%s\n", (LONG)set[i]) < 0) ok = FALSE;
    for (i = 0; setv[i]; i++)                    /* settings the window does not set: kept */
        if (!opts_saved(setv[i]) && FPrintf(fh, "%s\n", (LONG)setv[i]) < 0) ok = FALSE;
    if (FPuts(fh, "; icons\n")) ok = FALSE;
    for (pass = 0; pass < 2; pass++)             /* the icons shown, then those that do not fit */
        for (it = (struct Item *)(pass ? &d->extra : &d->items)->mlh_Head; it->node.mln_Succ;
             it = (struct Item *)it->node.mln_Succ) {
            if (it->temp) continue;              /* running, not kept */
            if (FPrintf(fh, "%s\n", (LONG)it->path) < 0) ok = FALSE;
            if (it->image && FPrintf(fh, "IMAGE %s\n", (LONG)it->image) < 0) ok = FALSE;
            if (it->label && FPrintf(fh, "LABEL %s\n", (LONG)it->label) < 0) ok = FALSE;
        }
    if (!Close(fh)) ok = FALSE;
    if (ok) {                                    /* the old file becomes .bak until the new one is in place */
        DeleteFile(bak);
        Rename(file, bak);
        if (!(ok = Rename(tmp, file))) Rename(bak, file);
    }
    if (!ok) {
        DeleteFile(tmp);
        sd_msg(d, "Cannot save %s", (LONG)file);
    }
    if (ok && path && Stricmp((STRPTR)path, (STRPTR)d->o->config)) save_icon(d, path);   /* (a copy: its icon) */
    return ok;
}

/* After a change made by hand (icons moved, added or removed with the mouse;
   the settings window closed): settings and icons into the dock's own file.
   Not over a file that could not be read (that was said when it was read). */
void config_store(struct Dock *d)
{
    if (!d->cfgfailed) config_save(d, NULL);
}

/* Read the icons and settings again, from 'path' (Load...) or from the dock's
   own file (NULL). Unsaved changes are dropped. If 'path' can't be read, the
   dock's own file is used. */
BOOL config_reload(struct Dock *d, CONST_STRPTR path)
{
    BOOL ok;
    dock_close(d);
    config_free(d);
    if (!(ok = config_load(d, path))) {
        config_free(d);
        if (path && !config_load(d, NULL)) config_free(d);   /* still a dock: empty, to drop on */
    }
    if (dock_open(d)) { ah_start(d); config_created(d); }
    return ok;
}

/* The file's settings lines the window does not set (FONT, LIMIT, ...),
   for a drawer dock's icon (opts_save). */
LONG config_other_lines(STRPTR *out, LONG max)
{
    LONG i, k = 0;
    for (i = 0; setv[i] && k < max; i++)
        if (!opts_saved(setv[i])) out[k++] = setv[i];
    return k;
}

/* The settings in use as lines: those the settings window sets, from the
   values in use, then the file's others (FONT, LIMIT, ...). */
static LONG merged(struct Dock *d, STRPTR *v)
{
    STRPTR set[20];
    LONG n = opts_lines(d, set), k = 0, i;
    for (i = 0; i < n; i++) v[k++] = set[i];
    for (i = 0; setv[i]; i++)
        if (!opts_saved(setv[i])) v[k++] = setv[i];
    v[k] = NULL;
    return k;
}

/* ARexx SET: apply settings lines such as "AUTOHIDE=3 ONTOP (NOTEXT)" on top
   of the settings in use (quote values with spaces). They are kept for the
   next save. The caller reopens the dock. Returns FALSE, changing nothing, if
   a keyword or value is not valid. */
static BOOL set_lines(struct Dock *d, CONST_STRPTR args, char *tok, char *nbuf);

BOOL config_set(struct Dock *d, CONST_STRPTR args)
{
    char *mem = AllocVec(4096 + sizeof(setbuf), MEMF_ANY);   /* (only while setting: not kept) */
    BOOL ok;
    if (!mem) return FALSE;
    ok = set_lines(d, args, mem, mem + 4096);
    FreeVec(mem);
    return ok;
}

/* config_set's work, with its buffers: tok 4096 bytes, nbuf as setbuf. */
static BOOL set_lines(struct Dock *d, CONST_STRPTR args, char *tok, char *nbuf)
{
    STRPTR v[20 + 48 + 1], nv[20 + 48 + 48 + 1], nt[48];   /* window lines (opts_lines: 19), file lines, words */
    LONG n, k = 0, m = 0, used = 0, i, j, cnt = 0;
    char *p = tok;

    while (*args) {                              /* tokens: spaces between, quotes removed */
        while (*args == ' ') args++;
        if (!*args) break;
        if (cnt == 48) return FALSE;
        nt[cnt++] = p;
        while (*args && *args != ' ') {
            if (p - tok >= 4096 - 2) return FALSE;
            if (*args == '\n') { args++; continue; }   /* (dropped, as the parser does: a save */
            if (*args == '"') {                        /*  would make two lines of it) */
                args++;
                while (*args && *args != '"' && p - tok < 4096 - 2) {
                    if (*args == '\n') args++;
                    else *p++ = *args++;
                }
                if (*args == '"') args++;
            } else *p++ = *args++;
        }
        *p++ = 0;
    }
    if (!cnt) return FALSE;
    for (i = 0; i < cnt; i++)
        if (opts_key(nt[i]) <= 0) return FALSE;  /* not a keyword, or CONFIG (that is LOAD) */
    n = merged(d, v);
    for (i = 0; i < n; i++) {                    /* the settings in use, minus those being set */
        for (j = 0; j < cnt && opts_key(nt[j]) != opts_key(v[i]); j++) ;
        if (j == cnt) nv[k++] = v[i];
    }
    for (i = 0; i < cnt; i++) nv[k++] = nt[i];
    nv[k] = NULL;
    if (!opts_check(nv)) return FALSE;
    for (i = 0; i < k; i++) {                    /* (nv points into setbuf too: a copy first) */
        LONG l = strlen(nv[i]) + 1;
        if (m == 48 || used + l > (LONG)sizeof(setbuf)) return FALSE;
        memcpy(nbuf + used, nv[i], l);
        used += l; m++;
    }
    memcpy(setbuf, nbuf, used);
    for (i = 0, used = 0; i < m; i++) { setv[i] = setbuf + used; used += strlen(setv[i]) + 1; }
    setv[m] = NULL;
    return opts_apply(d, setv, "ARexx SET");
}

/* ARexx GET: the value of one setting ("1" or "0" for a switch, empty if not
   set), or every settings line if key is empty. Returns FALSE for an unknown
   keyword. */
BOOL config_get(struct Dock *d, CONST_STRPTR key, char *out, LONG size)
{
    STRPTR v[96];
    LONG n = merged(d, v), i, want, len = 0;
    out[0] = 0;
    if (!key || !*key) {
        for (i = 0; i < n; i++) {
            LONG l = strlen(v[i]);
            if (len + l + 2 > size) break;
            if (len) out[len++] = '\n';
            strcpy(out + len, v[i]);
            len += l;
        }
        return TRUE;
    }
    if ((want = opts_key(key)) < 0) return FALSE;
    for (i = 0; i < n; i++)
        if (opts_key(v[i]) == want) {
            CONST_STRPTR eq = strchr(v[i], '=');
            if (v[i][0] == '(') strcpy(out, eq || opts_switch(want) ? "0" : "");   /* "(FONT)": not set */
            else if (!eq) strcpy(out, "1");
            else { strncpy(out, eq + 1, size - 1); out[size - 1] = 0; }
            break;
        }
    return TRUE;
}

/* Load... / Save as... (settings window): an ASL file requester on the
   dock's screen. */
BOOL config_choose(struct Dock *d, BOOL save)
{
    struct FileRequester *fr;
    char dir[SD_LINEMAX], path[SD_LINEMAX];
    BOOL done = FALSE;

    if (!AslBase) { sd_msg(d, "asl.library is needed to choose a file"); return FALSE; }
    strncpy(dir, d->o->config, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    *PathPart(dir) = 0;
    if (!(fr = AllocAslRequestTags(ASL_FileRequest, TAG_END))) return FALSE;
    if (AslRequestTags(fr, ASLFR_Screen, d->scr, ASLFR_TitleText, save ? "Save dock settings as" : "Load dock settings",
                       ASLFR_DoSaveMode, save, ASLFR_InitialDrawer, dir,
                       ASLFR_InitialFile, save ? "" : FilePart(d->o->config), TAG_END)) {
        strncpy(path, fr->fr_Drawer, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        if (AddPart(path, fr->fr_File, sizeof(path)) && fr->fr_File[0]) {
            if (save) config_save(d, path);
            else config_reload(d, path);
            done = TRUE;
        }
    }
    FreeAslRequest(fr);
    return done;
}
/* The "..." button of a drawer field: an ASL drawer requester, starting at
   the drawer in 'buf' (or SYS:). TRUE: a drawer chosen, in buf. */
BOOL config_choose_dir(struct Dock *d, char *buf, LONG size)
{
    struct FileRequester *fr;
    BOOL done = FALSE;
    if (!AslBase) { sd_msg(d, "asl.library is needed to choose a drawer"); return FALSE; }
    if (!(fr = AllocAslRequestTags(ASL_FileRequest, TAG_END))) return FALSE;
    if (AslRequestTags(fr, ASLFR_Screen, d->scr, ASLFR_TitleText, "Unpack archives into",
                       ASLFR_DrawersOnly, TRUE, ASLFR_InitialDrawer, buf[0] ? buf : "SYS:", TAG_END) &&
        fr->fr_Drawer && fr->fr_Drawer[0] && (LONG)strlen(fr->fr_Drawer) < size) {
        strcpy(buf, fr->fr_Drawer);
        done = TRUE;
    }
    FreeAslRequest(fr);
    return done;
}

void config_free(struct Dock *d)
{
    run_free_all(d);
    dock_unhide(d);
    while (d->items.mlh_Head->mln_Succ) config_remove(d, (struct Item *)d->items.mlh_Head);
}
