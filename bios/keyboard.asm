; Computron x86 PC Emulator
; Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions
; are met:
; 1. Redistributions of source code must retain the above copyright
;    notice, this list of conditions and the following disclaimer.
; 2. Redistributions in binary form must reproduce the above copyright
;    notice, this list of conditions and the following disclaimer in the
;    documentation and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
; EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
; PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
; CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
; EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
; PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
; PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
; OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;

; vim:set syntax=nasm et sts=4 sw=4:

[org 0]
[bits 16]

struc scancode_to_ascii_entry
    .normal:    resw 1
    .shift:     resw 1
    .ctrl:      resw 1
    .alt:       resw 1
    .lockflag:  resw 1
    .size:
endstruc

keyboard_interrupt_handler:
    ;int     0x1B
    push    ax
    mov     al, 0x20
    out     0x20, al
    pop     ax
    iret

%define none 0

scancode_to_ascii_table:
    dw   none,   none,   none,   none, none
    dw 0x011b, 0x011b, 0x011b, 0x0100, none ; escape 
    dw 0x0231, 0x0221,   none, 0x7800, none ; 1! 
    dw 0x0332, 0x0340, 0x0300, 0x7900, none ; 2@ 
    dw 0x0433, 0x0423,   none, 0x7a00, none ; 3# 
    dw 0x0534, 0x0524,   none, 0x7b00, none ; 4$ 
    dw 0x0635, 0x0625,   none, 0x7c00, none ; 5% 
    dw 0x0736, 0x075e, 0x071e, 0x7d00, none ; 6^ 
    dw 0x0837, 0x0826,   none, 0x7e00, none ; 7& 
    dw 0x0938, 0x092a,   none, 0x7f00, none ; 8* 
    dw 0x0a39, 0x0a28,   none, 0x8000, none ; 9( 
    dw 0x0b30, 0x0b29,   none, 0x8100, none ; 0) 
    dw 0x0c2d, 0x0c5f, 0x0c1f, 0x8200, none ; -_ 
    dw 0x0d3d, 0x0d2b,   none, 0x8300, none ; =+ 
    dw 0x0e08, 0x0e08, 0x0e7f,   none, none ; backspace 
    dw 0x0f09, 0x0f00,   none,   none, none ; tab 
    dw 0x1071, 0x1051, 0x1011, 0x1000, 0x40 ; Q 
    dw 0x1177, 0x1157, 0x1117, 0x1100, 0x40 ; W 
    dw 0x1265, 0x1245, 0x1205, 0x1200, 0x40 ; E 
    dw 0x1372, 0x1352, 0x1312, 0x1300, 0x40 ; R 
    dw 0x1474, 0x1454, 0x1414, 0x1400, 0x40 ; T 
    dw 0x1579, 0x1559, 0x1519, 0x1500, 0x40 ; Y 
    dw 0x1675, 0x1655, 0x1615, 0x1600, 0x40 ; U 
    dw 0x1769, 0x1749, 0x1709, 0x1700, 0x40 ; I 
    dw 0x186f, 0x184f, 0x180f, 0x1800, 0x40 ; O 
    dw 0x1970, 0x1950, 0x1910, 0x1900, 0x40 ; P 
    dw 0x1a5b, 0x1a7b, 0x1a1b,   none, none ; [{ 
    dw 0x1b5d, 0x1b7d, 0x1b1d,   none, none ; ]} 
    dw 0x1c0d, 0x1c0d, 0x1c0a,   none, none ; Enter 
    dw   none,   none,   none,   none, none ; L Ctrl 
    dw 0x1e61, 0x1e41, 0x1e01, 0x1e00, 0x40 ; A 
    dw 0x1f73, 0x1f53, 0x1f13, 0x1f00, 0x40 ; S 
    dw 0x2064, 0x2044, 0x2004, 0x2000, 0x40 ; D 
    dw 0x2166, 0x2146, 0x2106, 0x2100, 0x40 ; F 
    dw 0x2267, 0x2247, 0x2207, 0x2200, 0x40 ; G 
    dw 0x2368, 0x2348, 0x2308, 0x2300, 0x40 ; H 
    dw 0x246a, 0x244a, 0x240a, 0x2400, 0x40 ; J 
    dw 0x256b, 0x254b, 0x250b, 0x2500, 0x40 ; K 
    dw 0x266c, 0x264c, 0x260c, 0x2600, 0x40 ; L 
    dw 0x273b, 0x273a,   none,   none, none ; ;: 
    dw 0x2827, 0x2822,   none,   none, none ; '" 
    dw 0x2960, 0x297e,   none,   none, none ; `~ 
    dw   none,   none,   none,   none, none ; L shift 
    dw 0x2b5c, 0x2b7c, 0x2b1c,   none, none ; |\ 
    dw 0x2c7a, 0x2c5a, 0x2c1a, 0x2c00, 0x40 ; Z 
    dw 0x2d78, 0x2d58, 0x2d18, 0x2d00, 0x40 ; X 
    dw 0x2e63, 0x2e43, 0x2e03, 0x2e00, 0x40 ; C 
    dw 0x2f76, 0x2f56, 0x2f16, 0x2f00, 0x40 ; V 
    dw 0x3062, 0x3042, 0x3002, 0x3000, 0x40 ; B 
    dw 0x316e, 0x314e, 0x310e, 0x3100, 0x40 ; N 
    dw 0x326d, 0x324d, 0x320d, 0x3200, 0x40 ; M 
    dw 0x332c, 0x333c,   none,   none, none ; ,< 
    dw 0x342e, 0x343e,   none,   none, none ; .> 
    dw 0x352f, 0x353f,   none,   none, none ; /? 
    dw   none,   none,   none,   none, none ; R Shift 
    dw 0x372a, 0x372a,   none,   none, none ; * 
    dw   none,   none,   none,   none, none ; L Alt 
    dw 0x3920, 0x3920, 0x3920, 0x3920, none ; space 
    dw   none,   none,   none,   none, none ; caps lock 
    dw 0x3b00, 0x5400, 0x5e00, 0x6800, none ; F1 
    dw 0x3c00, 0x5500, 0x5f00, 0x6900, none ; F2 
    dw 0x3d00, 0x5600, 0x6000, 0x6a00, none ; F3 
    dw 0x3e00, 0x5700, 0x6100, 0x6b00, none ; F4 
    dw 0x3f00, 0x5800, 0x6200, 0x6c00, none ; F5 
    dw 0x4000, 0x5900, 0x6300, 0x6d00, none ; F6 
    dw 0x4100, 0x5a00, 0x6400, 0x6e00, none ; F7 
    dw 0x4200, 0x5b00, 0x6500, 0x6f00, none ; F8 
    dw 0x4300, 0x5c00, 0x6600, 0x7000, none ; F9 
    dw 0x4400, 0x5d00, 0x6700, 0x7100, none ; F10 
    dw   none,   none,   none,   none, none ; Num Lock 
    dw   none,   none,   none,   none, none ; Scroll Lock 
    dw 0x4700, 0x4737, 0x7700,   none, 0x20 ; 7 Home 
    dw 0x4800, 0x4838,   none,   none, 0x20 ; 8 UP 
    dw 0x4900, 0x4939, 0x8400,   none, 0x20 ; 9 PgUp 
    dw 0x4a2d, 0x4a2d,   none,   none, none ; - 
    dw 0x4b00, 0x4b34, 0x7300,   none, 0x20 ; 4 Left 
    dw 0x4c00, 0x4c35,   none,   none, 0x20 ; 5 
    dw 0x4d00, 0x4d36, 0x7400,   none, 0x20 ; 6 Right 
    dw 0x4e2b, 0x4e2b,   none,   none, none ; + 
    dw 0x4f00, 0x4f31, 0x7500,   none, 0x20 ; 1 End 
    dw 0x5000, 0x5032,   none,   none, 0x20 ; 2 Down 
    dw 0x5100, 0x5133, 0x7600,   none, 0x20 ; 3 PgDn 
    dw 0x5200, 0x5230,   none,   none, 0x20 ; 0 Ins 
    dw 0x5300, 0x532e,   none,   none, 0x20 ; Del 
    dw   none,   none,   none,   none, none
    dw   none,   none,   none,   none, none
    dw 0x565c, 0x567c,   none,   none, none ; \| 
    dw 0x8500, 0x8700, 0x8900, 0x8b00, none ; F11 
    dw 0x8600, 0x8800, 0x8a00, 0x8c00, none ; F12 
