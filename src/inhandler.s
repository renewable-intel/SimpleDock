; SimpleDock - input handler entry
; Copyright (C) 2026 renewable-intel
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Called by input.device with A0 = event list (A1 = is_Data), returns the list
; in D0, unchanged. _in_watch notes the left button (and which window was
; active when it went down); while auto-hide or hover labels are on (_in_on)
; _in_check also looks at the pointer, once per batch of events.

        section code,code
        xdef    _in_handler
        xref    _in_on
        xref    _in_check
        xref    _in_watch

_in_handler:
        move.l  a0,-(sp)                ; kept: the result (input.device's list)
        move.l  a0,-(sp)                ; argument (C may change its own copy)
        jsr     _in_watch
        tst.w   _in_on
        beq.s   .out
        move.l  4(sp),(sp)              ; a fresh argument for in_check
        jsr     _in_check
.out:   addq.l  #4,sp
        move.l  (sp)+,d0
        rts
