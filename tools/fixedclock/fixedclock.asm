; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 velteyn
;
; fixedclock.com - DOS TSR that pins the clock for reproducible captures.
;
; EF2000 seeds its campaign RNG with the DOS date/time read at generation
; time, and writes that Unix timestamp into the CAMPAIGN.CG header. Pinning
; the clock makes two runs with the same seed comparable, which turns a
; capture into a controlled A/B experiment.
;
; It intercepts DOS INT 21h:
;   AH = 2Ah (get date) -> CX=1996, DH=4, DL=19, AL=5 (Friday)
;   AH = 2Ch (get time) -> CH=12, CL=0, DH=0, DL=0
; Everything else is passed to the previous INT 21h handler.
;
; Build:
;   nasm -f bin -o fixedclock.com fixedclock.asm
;
; Run it before the game in the same DOS session (see README.md).

BITS 16
CPU 386
ORG 0x100

FIXED_YEAR  equ 1996
FIXED_MONTH equ 4
FIXED_DAY   equ 19
FIXED_WDAY  equ 5          ; 1996-04-19 was a Friday
FIXED_HOUR  equ 12
FIXED_MIN   equ 0
FIXED_SEC   equ 0

start:
    jmp init

old_int21:
    dd 0

handler:
    cmp ah, 0x2A
    je .date
    cmp ah, 0x2C
    je .time
    jmp far [cs:old_int21]

.date:
    mov cx, FIXED_YEAR
    mov dh, FIXED_MONTH
    mov dl, FIXED_DAY
    mov al, FIXED_WDAY
    push bp
    mov bp, sp
    and byte [bp+6], 0xFE  ; clear CF in the interrupted FLAGS image
    pop bp
    iret

.time:
    mov ch, FIXED_HOUR
    mov cl, FIXED_MIN
    mov dh, FIXED_SEC
    xor dl, dl
    push bp
    mov bp, sp
    and byte [bp+6], 0xFE
    pop bp
    iret

init:
    mov ax, 0x3521
    int 0x21
    mov [old_int21], bx
    mov [old_int21 + 2], es

    mov ax, 0x2521
    mov dx, handler
    int 0x21

    mov dx, message
    mov ah, 0x09
    int 0x21

    ; Keep everything from the PSP through end_of_resident.
    mov dx, end_of_resident + 15
    shr dx, 4
    mov ax, 0x3100
    int 0x21

message:
    db 'fixedclock: DOS clock pinned to 1996-04-19 12:00:00', 13, 10, '$'

end_of_resident:
