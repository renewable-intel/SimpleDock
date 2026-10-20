; SimpleDock - the crash trap (CRASHLOG, off by default)
; Copyright (C) 2026 renewable-intel
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Exec calls a task's tc_TrapCode in supervisor mode, with the trap number on
; the stack above the exception frame. This notes the registers and the
; frame, then replaces the frame with a plain one that continues in
; _crash_recover (crash.c) in user mode, on a spare stack: the task's own
; stack may be what broke. A second crash while recovering goes to
; _crash_halt, which waits forever instead of looping.

        section code,code
        xdef    _crash_trap
        xdef    _crash_illegal
        xref    _crash_recover
        xref    _crash_halt
        xref    _crash_regs
        xref    _crash_usp
        xref    _crash_no
        xref    _crash_pc
        xref    _crash_sr
        xref    _crash_fmt
        xref    _crash_count
        xref    _crash_sp
        xref    _LinkerDB

_crash_trap:
        movem.l d0-d7/a0-a6,_crash_regs ; the registers at the crash
        move.l  usp,a0
        move.l  a0,_crash_usp
        move.l  (sp)+,_crash_no         ; pushed by exec
        move.w  (sp),_crash_sr
        move.l  2(sp),_crash_pc
        move.w  6(sp),d0                ; format (top 4 bits) and vector offset
        move.w  d0,_crash_fmt
        rol.w   #4,d0
        and.w   #15,d0
        lea     fsize(pc),a0
        moveq   #0,d1
        move.b  (a0,d0.w),d1
        add.l   d1,sp                   ; the whole frame off the stack
        move.l  _crash_sp,a0            ; the spare stack
        move.l  a0,usp
        clr.w   -(sp)                   ; a new format 0 frame
        tst.l   _crash_count
        bne.s   .again
        addq.l  #1,_crash_count
        pea     _crash_recover
        bra.s   .go
.again: pea     _crash_halt
.go:    clr.w   -(sp)                   ; SR: user mode, interrupts on
        lea     _LinkerDB,a4            ; the C code's data (small data: A4 may be anything here)
        rte

; frame size in bytes by format code (68020 to 68060)
fsize:  dc.b    8,8,12,12,16,8,8,60,58,20,32,92,8,8,8,8

; An illegal instruction, for testing (debug ARexx CRASH ILLEGAL).
_crash_illegal:
        illegal
        rts
