; SimpleDock - character output for exec RawDoFmt()
; Copyright (C) 2026 renewable-intel
; SPDX-License-Identifier: GPL-3.0-or-later
;
; RawDoFmt() takes a NULL PutChProc only from exec V45.1 on; older systems
; jump to address 0. This is the classic stuffChar routine: D0 = character,
; A3 = buffer pointer.

        section code,code
        xdef    _sd_putch
_sd_putch:
        move.b  d0,(a3)+
        rts
