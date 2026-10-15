; SimpleDock - pixel loops in assembly (true-colour screens)
; Copyright (C) 2026 renewable-intel
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Same results as the C versions in render.c / scale.c (built with
; -DSD_NOASM to compare). Scratch: d0/d1/a0/a1; others are saved.

        section code,code

; void sd_lut3(a0 = RGB bytes, d0 = pixels, a1 = 768-byte table: R, G, B)
; Each byte through its channel's table (tinting the background).
        xdef    _sd_lut3
_sd_lut3:
        subq.l  #1,d0
        bmi.s   .done
        moveq   #0,d1
.px:    move.b  (a0),d1
        move.b  (a1,d1.w),(a0)+
        move.b  (a0),d1
        move.b  256(a1,d1.w),(a0)+
        move.b  (a0),d1
        move.b  512(a1,d1.w),(a0)+
        subq.l  #1,d0
        bpl.s   .px
.done:  rts

; void sd_blend(a0 = RGB bytes (in and out), a1 = alpha + premultiplied RGB,
;               d0 = pixels, d1 = 256 (normal) or less (pressed: darker))
; out = (src * d1 >> 8) + (dst * k' >> 8), k' = k + k / 128 (k = 255 - alpha),
; at most 255. Fully transparent pixels are skipped, opaque ones copied
; (the same result: k' = 0, src * 256 >> 8 = src).
        xdef    _sd_blend
_sd_blend:
        movem.l d2-d5,-(sp)
        move.l  d1,d5
        subq.l  #1,d0
        bmi.s   .out
.bpx:   moveq   #0,d2
        move.b  (a1)+,d2                ; alpha
        beq.s   .skip
        cmp.b   #255,d2                 ; opaque and not pressed: a copy
        bne.s   .mix
        cmp.l   #256,d5
        bne.s   .mix
        move.b  (a1)+,(a0)+
        move.b  (a1)+,(a0)+
        move.b  (a1)+,(a0)+
        bra.s   .next
.mix:   move.l  #255,d3
        sub.l   d2,d3                   ; k
        move.l  d3,d4
        lsr.l   #7,d4
        add.l   d4,d3                   ; k' (0..256)
        moveq   #2,d1                   ; three channels
.ch:    moveq   #0,d2
        move.b  (a1)+,d2
        mulu.w  d5,d2
        lsr.l   #8,d2
        moveq   #0,d4
        move.b  (a0),d4
        mulu.w  d3,d4
        lsr.l   #8,d4
        add.l   d4,d2
        cmp.l   #255,d2
        bls.s   .ok
        move.l  #255,d2
.ok:    move.b  d2,(a0)+
        dbra    d1,.ch
        bra.s   .next
.skip:  addq.l  #3,a1
        addq.l  #3,a0
.next:  subq.l  #1,d0
        bpl.s   .bpx
.out:   movem.l (sp)+,d2-d5
        rts
