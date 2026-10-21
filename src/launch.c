/* SimpleDock - start the program, project or drawer behind a dock icon
   Copyright (C) 2026 renewable-intel
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/wb.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <utility/tagitem.h>
#include "sd.h"

/* Start the icon's program, or open its drawer, disk or project. If its
   file is gone, the icon is marked (dimmed, with a "?") and the user is
   asked whether to remove it. It is never removed on its own: it may be on
   a disk that isn't inserted. */
BOOL launch_item(struct Dock *d, struct Item *it)
{
    static struct EasyStruct es = { sizeof(struct EasyStruct), 0, SD_NAME,
        "\"%s\" cannot be found.\nRemove it from the dock?", "Remove|Keep" };
    CONST_STRPTR name = it->label ? it->label : FilePart(it->path);
    BOOL was = it->missing;
    if (it->temp) {                              /* running, not kept: a new copy, if its file is known */
        if (it->temp == RUN_TEMP && !it->nofile && dock_exists(it->path) &&
            (run_launch(d, it, NULL) || OpenWorkbenchObject(it->path, TAG_END))) return TRUE;
        DisplayBeep(d->scr);
        return FALSE;
    }
    if ((it->missing = !dock_exists(it->path)) == FALSE) {
        if (was) dock_show_item(d, it, FALSE);   /* back (a disk inserted): unmarked */
        if (run_launch(d, it, NULL) || OpenWorkbenchObject(it->path, TAG_END)) return TRUE;   /* (programs: the dock starts them) */
        DisplayBeep(d->scr);
        sd_msg(d, "Cannot open %s", (LONG)it->path);
        return FALSE;
    }
    dock_show_item(d, it, FALSE);                /* marked as missing */
    if (EasyRequest(d->win, &es, NULL, (LONG)(*name ? name : it->path)) == 1) {
        dz_suspend();                            /* the list changes: not for the zone hook now */
        dock_unhide(d);
        config_remove(d, it);
        dock_reopen_glide(d);
        config_store(d);
    }
    return FALSE;
}

#ifdef SD_DEBUG
LONG lw_count, lw_args;
char *lw_path = "";
#endif

/* Icons dropped onto the middle of a dock icon: arguments for its program
   (the dock starts it, else Workbench); onto a drawer or disk: copied
   (moved with DROPMOVE); onto the Trash: beep. */
BOOL launch_with(struct Dock *d, struct Item *it, struct AppMessage *am)
{
    struct TagItem tags[2 * 32 + 1];
    LONG i, n = 0;
    if (it->temp == RUN_MORE || it->nofile) { DisplayBeep(d->scr); return FALSE; }
    if (it->dobj && (it->dobj->do_Type == WBDRAWER || it->dobj->do_Type == WBDISK) && !it->missing)
        return drop_copy(d, it, am);
    if (it->dobj && it->dobj->do_Type == WBGARBAGE) { DisplayBeep(d->scr); return FALSE; }   /* not the Trash */
    if (run_shell_front(d, it, am)) return TRUE; /* onto the Shell icon: typed into the Shell in front */
    for (i = 0; i < am->am_NumArgs && i < 32; i++) {
        tags[n].ti_Tag = WBOPENA_ArgLock; tags[n++].ti_Data = (ULONG)am->am_ArgList[i].wa_Lock;
        tags[n].ti_Tag = WBOPENA_ArgName; tags[n++].ti_Data = (ULONG)am->am_ArgList[i].wa_Name;
    }
    tags[n].ti_Tag = TAG_END;
#ifdef SD_DEBUG
    { extern LONG lw_count, lw_args; extern char *lw_path; lw_count++; lw_args = i; lw_path = it->path; }
#endif
    if (run_launch(d, it, am) || OpenWorkbenchObjectA(it->path, tags)) return TRUE;
    DisplayBeep(d->scr);
    sd_msg(d, "Cannot open %s", (LONG)it->path);
    return FALSE;
}
