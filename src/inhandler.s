; SimpleDock - input handler entry
; Copyright (C) 2026 renewable-intel
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Called by input.device with A0 = event list (A1 = is_Data), returns the list
; in D0. _in_watch notes the left button and the active window (and takes
; the hotkey and the held list's keys); while the pointer matters (_in_on:
; auto-hide, hover labels, labels only, ONTOP) _in_check also looks at it,
; once per batch of events. The C code is small-data (globals relative to
; A4): A4 is set to the dock's data here (input.device's own kept).

        section code,code
        xdef    _in_handler
        xref    _in_on
        xref    _in_check
        xref    _in_watch
        xref    _LinkerDB

_in_handler:
        move.l  a4,-(sp)
        lea     _LinkerDB,a4
        move.l  a0,-(sp)                ; kept: the result (input.device's list)
        move.l  a0,-(sp)                ; argument (C may change its own copy)
        jsr     _in_watch
        tst.w   _in_on
        beq.s   .out
        move.l  4(sp),(sp)              ; a fresh argument for in_check
        jsr     _in_check
.out:   addq.l  #4,sp
        move.l  (sp)+,d0
        move.l  (sp)+,a4
        rts
