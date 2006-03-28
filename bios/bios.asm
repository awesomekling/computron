; VOMIT ROM-BIOS
; (C) Andreas Kling 2003-2006
;
;

[org 0]
[bits 16]

    jmp     0xF000:_bios_post

reboot_on_any_key:
	mov     si, szAnyKeyPlease
	call    safe_putString
.loop:
	mov     ax, 0x1600
	out     0xE6, ax
	or      ax, 0
	jz      .loop
	jmp     0xF000:0

_cpux_dividebyzero:                 ; Interrupt 0x00 - Divide by zero
    push    si
    mov     si, szDivideByZero
    call    safe_putString
    pop     si
	jmp     reboot_on_any_key

_cpux_singlestep:                   ; Interrupt 0x01 - Single step
    iret                            ; IRET. Write your own handler.

; == NOT IMPLEMENTED ==
;_cpux_nmi:							; Interrupt 0x02 - NMI
;	push	ax						; Preserve AX
;	mov		ax, 1					; AX = 1
;	out		0xE8, al				; Set cpu_state=CPU_ALIVE (exit halt)
;	pop		ax						; Restore AX

_cpux_breakpoint:					; Interrupt 0x03 - Breakpoint
	push	ax						; Preserve AX
	mov		ax, 0x0300				; VM call 0x0300: Show debug prompt
	out		0xE6, ax
	pop		ax						; Restore AX

_cpux_overflow:						; Interrupt 0x04 - Overflow
	push    si
	mov     si, szOverflow
	call    safe_putString
	pop     si
	jmp     reboot_on_any_key

_cpux_invalidop:
	push    si
	mov     si, szInvalidOpcode
	call    safe_putString
	pop     si
	jmp     reboot_on_any_key

; INT 08 - PIT interrupt
;
; Increments DWORD at BDA:6C, which should be 0 at midnight (a TODO.)

_cpu_timer:
	push    ds
	push    bx
	xor     bx, bx
	mov     ds, bx
	inc     word [0x46c]
	jnz     .skipHighWord
	inc     word [0x46e]
.skipHighWord:
	pop     bx
	pop     ds
	int     0x1C
	iret

_cpu_default_softtimer:
	iret

_kbd_interrupt:
	int		0x1B
	iret

_bios_ctrl_break:
	iret

_unimplemented_isr:
	push    si
	mov     si, szUnimplemented
	call    safe_putString
	pop     si
	iret

_bios_post:							; Power On Self-Test ;-)
	cli
	mov		ax, 0x0030				; actual default
	mov		ss, ax					; POST stack location
	mov		sp, 0x00FF				; (whee)
	
	push	cs
	pop		ds

	mov		ax, 0x9000
	mov		ss, ax

	call	vga_clear				; Clear screen and move cursor to
	xor		ax, ax					; upper left corner.
	call	vga_store_cursor

	mov     si, szVersion
	call    safe_putString			; Print BIOS Version string

	call    _bios_setup_ints		; Install BIOS Interrupts
	
	call    _bios_init_data			; Initialize BIOS data area (0040:0000)

	mov		si, szCrLf
	call	safe_putString

	call    _bios_find_bootdrv      ; Find a boot drive 
	jc      .nobootdrv

	call    _bios_load_bootsector   ; Load boot sector from boot drive
	jc      .bootloadfail

	sti

	jmp     0x0000:0x7C00           ; JMP to software entry

.nobootdrv:    
	mov		si, szNoBootDrive
	call	safe_putString
	hlt

.bootloadfail:
	mov		si, szBootLoadFail
	call	safe_putString
	mov		ax, 0x0000
	out		0xE6, ax
	hlt

safe_putString:
	push    ds
	push    cs
	pop     ds
	push    ax
	push	bx
.nextchar:
	lodsb
	or      al, 0x00
	jz      .end
	pushf
	push	cs
	call	vga_ttyecho
	jmp     .nextchar
.end:
	pop		bx
	pop     ax
	pop     ds
	ret

; print_integer
;
;    Prints a 16-bit unsigned integer in decimal.
;
; Parameters:
;
;    AX = Integer to print
;

print_integer:
	push    ax
	push    bx

	or      ax, 0               ; If AX == 0, no need to loop through the digits
	jnz     .nonZero

	mov     al, '0'
	pushf
	push    cs
	call    vga_ttyecho         ; vga_ttyecho returns via IRET

	pop     bx
	pop     ax
	ret

.nonZero:
	push    cx
	push    dx
	push    bp

	xor     bp, bp              ; BP keeps track of whether we've started output
	mov     cx, 10000           ; Start with 10K digit

.loop:
	xor     dx, dx
	div     cx

	or      bp, ax              ; OR result into BP
	jz      .nextDigit          ; If BP == 0, skip printing this digit
	
	add     al, '0'

	pushf
	push    cs
	call    vga_ttyecho         ; vga_ttyecho returns via IRET

.nextDigit:
	push    dx                  ; Store away remainder
	xor     dx, dx
	mov     ax, cx
	mov     cx, 10
	div     cx
	mov     cx, ax              ; CX /= 10
	pop     ax                  ; AX = remainder
	
	jcxz    .end
	jmp     .loop

.end:
	pop     bp
	pop     dx
	pop     cx
	pop     bx
	pop     ax
	ret

_bios_setup_ints:
	push    ds

	mov     dx, _unimplemented_isr
	xor     al, al
.loop:
	call    .install
	inc     al
	jnz     .loop

	mov     al, 0x00
	mov     dx, _cpux_dividebyzero
	call    .install

	mov     al, 0x01
	mov     dx, _cpux_singlestep
	call    .install

;   == NOT IMPLEMENTED ==
;	mov		al, 0x02
;	mov		dx, _cpux_nmi
;	call	.install

	mov     al, 0x03
	mov     dx, _cpux_breakpoint
	call    .install

	mov     al, 0x04
	mov     dx, _cpux_overflow
	call    .install

	mov		al, 0x06
	mov		dx, _cpux_invalidop
	call	.install

	mov		al, 0x08
	mov		dx, _cpu_timer
	call	.install

	mov		al, 0x09
	mov		dx, _kbd_interrupt
	call	.install

	mov     al, 0x10
	mov     dx, _bios_interrupt10
	call    .install

	mov     al, 0x11
	mov     dx, _bios_interrupt11
	call    .install

	mov     al, 0x12
	mov     dx, _bios_interrupt12
	call    .install

	mov     al, 0x13
	mov     dx, _bios_interrupt13
	call    .install

	mov     al, 0x14
	mov     dx, _bios_interrupt14
	call    .install

	mov     al, 0x15
	mov     dx, _bios_interrupt15
	call    .install

	mov     al, 0x16
	mov     dx, _bios_interrupt16
	call    .install

	mov     al, 0x17
	mov     dx, _bios_interrupt17
	call    .install

	mov		al, 0x19
	mov		dx, 0x0000
	call	.install

	mov     al, 0x1a
	mov     dx, _bios_interrupt1a
	call    .install

	mov		al, 0x1b
	mov		dx, _bios_ctrl_break
	call	.install

	mov		al, 0x1c
	mov		dx, _cpu_default_softtimer
	call	.install

	pop     ds

	ret

.install:
	push    ax
	mov		cl, 4
	mul		byte cl
	xor		bx, bx
	mov		ds, bx
	mov		bx, ax
	mov		ax, cs
	mov		word [bx], dx
	mov		word [bx+2], ax
	pop     ax
	ret

check_for_8086:
	mov     al, 0xff
	mov     cl, 0x80
	shr     al, cl
	ret

check_for_80186:
	pushf
	pop    ax
	and    ah, 0x0f
	push   ax
	popf
	pushf
	pop    ax
	not    ah
	test   ah, 0xf0
	ret
	
	

; INITALIZE BIOS DATA AREA -----------------------------
;
;    This section sets various values in the BDA,
;    depending on the current configuration.
;    Most importantly, it probes the floppy drives.
;

_bios_init_data:
	push    ds
	xor     ax, ax
	mov     ds, ax

	call    check_for_8086
	je      .print8086

	call    check_for_80186
	je      .print80186

	mov     si, szUnknownCPU
.cCend:
	call    safe_putString
	mov     si, szCPU
	call    safe_putString
	jmp     .checkMem

.print8086:
	mov     si, sz8086
	jmp     .cCend
.print80186:
	mov     si, sz80186
	jmp     .cCend

.checkMem:
	mov     ax, 0x0001          ; Get the amount of memory in kB
	out     0xE6, ax

	call    print_integer

	mov     si, szBaseMemory
	call    safe_putString
		
	mov     word [0x0413], ax   ; Store it.

	mov     byte [0x0496], 0x0E ; 0x0E = CTRL & ALT depressed;
	                            ; 101/102 ext. kbd.
	mov     byte [0x0417], 0x8F

; EQUIPMENT LIST ---------------------------------------
;			                      v DMA (1 if not)
;	mov		word [0x0410], 0000000100100000b
;                                  xx     x   floppies
	mov		cx, 0000000100100000b
	; No DMA
	; 80x25 color
; ------------------------------------------------------

	xor		bp, bp
	mov		dl, 0x00
	mov		ax, 0x1300
	out		0xE6, ax
	cmp		ah, 0x00
	jz		.setDisk00
.check01:	
	mov		dl, 0x01
	mov		ax, 0x1300
	out		0xE6, ax
	cmp		ah, 0x00
	jz		.setDisk01
.check80:										; We'll skip this for now.
	cmp		bp, 0x00
	je		.noFloppies
	mov		word [0x410], cx
.end:
	pop		ds
	ret
	
.setDisk00:
	or		cx, 0x01
	mov		si, szDisk00
	call	safe_putString
	inc		bp
	jmp		.check01
.setDisk01:
	or		cx,	0x40
	mov		si, szDisk01
	call	safe_putString
	inc		bp
	jmp		.check80
.noFloppies:
	mov		si, szNoFloppies
	call	safe_putString
	jmp		.end

_bios_find_bootdrv:
	mov		dl, 0x00
	mov		ax, 0x1301
	out		0xE6, ax
	jnc		.end
	mov		dl, 0x01
	mov		ax, 0x1301
	out		0xE6, ax
	jnc		.end
	mov		dl, 0x80
	mov		ax, 0x1301
	out		0xE6, ax
	jnc		.end
	mov		dl, 0x81
	mov		ax, 0x1301
	out		0xE6, ax
	jnc		.end
.error:
	ret
.end:
	ret

_bios_load_bootsector:
	mov		ax, 0x0201
	mov		cx, 0x0001
	xor		dh, dh
	xor		bx, bx
	mov		es, bx
	mov		bx, 0x7C00
	out		0xE2, al
	jc		.end
	cmp		word [es:0x7dfe], 0xaa55
	jne		.noboot
.end:
	ret
.noboot:
	mov		si, szNotBootable
	call	safe_putString
	jmp		.end

_bios_interrupt10:					; BIOS Video Interrupt
	cmp		ah, 0x0e
	je		.outChar
	cmp		ah, 0x09
	je		.outCharStill
	cmp		ah, 0x08
	je		.readChar
	cmp		ah, 0x13
	je		.putString
	cmp		ah, 0x02
	je		.setCursor
	cmp		ah, 0x03
	je		.getCursor
	cmp		ah, 0x00
	je		.setVideoMode
	cmp		ah, 0x06
	je		.scrollWindow
	cmp		ah, 0x0f
	je		.getVideoState
	cmp		ah, 0x12
	je		.vgaConf
	cmp		ah, 0x05				; 5 - Select active page
	je		.selectPage				; Nah. Paging? Pahh.
	push	ax
	mov		al, 0x10
	out		0xE0, ax				; VM call 0xA0 - What the fuck is up?
	pop		ax						; AL = INT, AH = function
	jmp		.end
.putString:
	push	ax
	mov		ax, 0x1013
	out		0xE6, ax
	pop		ax
	jmp		.end
.readChar:
	jmp		vga_readchr
.outChar:
	jmp		vga_ttyecho
.outCharStill:
	jmp		vga_putc
.setVideoMode:
	jmp		vga_set_mode
.setCursor:
	call	vga_setcur
	jmp		.end
.getCursor:
	call	vga_getcur
	jmp		.end
.getVideoState:
	mov		ah, 80					; 80 columns. No argument.
	mov		al, 3					; 80x25*16colors
	mov		bh, 0					; display page 0
	jmp		.end
.vgaConf:
	cmp		bl, 0x10
	jne		.end
.getconf:
	mov		bh, 0					; color mode
	mov		bl, 0					; 64k EGA mem
	add		sp, 2
	iret	; we want to return in AX
.scrollWindow
	out		0xE7, al
	jmp		.end
.selectPage:
	mov		ax, 0x1005
	out		0xE6, ax
.end:
    iret

; vga_set_mode
;
;    Sets the VGA mode
;    Called by INT 10,00
;
; Parameters:
;
;    AL = Video mode
;

vga_set_mode:

	push	ds
	push	bx
	push	ax

	mov		ah, al
	and		al, 0x80				; AL bit 7=1 prevents EGA,MCGA & VGA
									; from clearing display.
	jnz		.noClear
	call	vga_clear
.noClear:

	xor		bx, bx
	mov		ds, bx
	mov		[0x449], ah				; Store new video mode in BDA

	and		byte [0x487], 0x7f		; Update the video mode options
	or		[0x487], al				; (AND to clear, OR to set)

	pop		ax
	pop		bx
	pop		ds
	iret

vga_clear:
	push	es
	push	di
	push	ax
	push	cx
	mov		ax, 0xb800
	mov		es, ax
	xor		di, di
	mov		ax, 0x0720
	mov		cx, (80*25)
	rep		stosw
	pop		cx
	pop		ax
	pop		di
	pop		es
	ret

vga_putc:
	push	es
	push	dx
	push	di
	push	ax

	mov		dx, 0x3d4
	mov		al, 0x0e
	out		dx, al
	inc		dx
	in		al, dx
	xchg	al, ah
	dec		dx
	mov		al, 0x0f
	out		dx, al
	inc		dx
	in		al, dx
	
	shl		ax, 1
	mov		di, ax

	mov		ax, 0xb800
	mov		es, ax

	pop		ax
	push	ax

	mov		ah, bl

	stosw
	
	pop		ax
	pop		di
	pop		dx
	pop		es
	iret

; vga_ttyecho
;
;    Prints a character to screen and advances cursor.
;    Handles CR, NL, TAB and backspace.
;    Scrolls video viewport if cursor position overflows.
;
;    Called by POST routines and INT 10,0E
;
; Parameters:
;
;    AL = Character to write
;    BH = Page number ( not implemented )
;
; Overrides:
;    BL = Foreground pixel color, always 2(green) to indicate BIOS output

vga_ttyecho:
	push	dx
	push	es
	push	di
	push	ax

	mov     bl, 0x02

	mov		ax, 0xb800				; Point ES to video memory for STOS access.
	mov		es, ax

	call	vga_load_cursor
	
	shl		ax, 1
	mov		di, ax					; DI = offset*2
	
	pop		ax						; original AX, outchar in AL
	push	ax

	mov		ah, bl					; attribute in BL
	and		bl, 0x0f				; AND off intensity + rgb

	cmp		al, 0x0d
	je		.cr
	cmp		al, 0x0a
	je		.lf
	cmp		al, 0x08
	je		.bs
	cmp		al, 0x09
	je		.tab
.regular:
	stosw
	call	vga_load_cursor
	inc		ax						; advance
	call	.locate
	jmp		.end

.cr:
	mov		ax, di
	shr		ax, 1
	jz		.zero				; XXX: Necessary? Why not straight to .end?
	xor		dx, dx
	mov		di, 80
	div		word di
	mov		di, 80
	mul		word di
.zero:
	call	.locate
	jmp		.end
.lf:
	mov		ax, di
	shr		ax, 1				; AX = Normal offset
	add		ax, 80				; One row down
	call	.locate
	jmp		.end
.bs:
	mov		ax, di
	shr		ax, 1
	dec		ax
	push	.end				; Backspace isn't going to overflow the cursor,
	jmp		vga_store_cursor	; so we walk around .locate (v_s_c RETs for us)
.tab:
	mov		al, 0x20
	stosw
	stosw
	stosw
	stosw
	mov		ax, di
	shr		ax, 1
	add		ax, 4
	call 	.locate
	jmp		.end

.locate:						; Move cursor to AX
	cmp		ax, 2000
	jl		vga_store_cursor	; If cursor doesn't overflow, no need to scroll

	push	ds
	push	si
	push	cx

	push	es
	pop		ds

	xor		di, di				; Scroll the contents of videomemory one row up
	mov		si, 160
	mov		cx, (80*24)
	rep		movsw
	
	mov		ax, 0x0720
	mov		cx, 80
	mov		di, (4000-160)
	rep		stosw
	
	pop		cx
	pop		si
	pop		ds

	mov		ax, (80*24)			; Go to last row, first column.
	jmp		vga_store_cursor	; vga_store_cursor will RET for us.

.end:
	pop		ax
	pop		di
	pop		es
	pop		dx
	
	iret

; vga_load_cursor
;
;    Reads the current VGA cursor location from 6845.
;    Breaks DX.
;
;    6845 registers are read in reverse order and XCHG:ed to save
;    space and time.
;
; Returns:
;
;    AX = Cursor location
;

vga_load_cursor:
	mov		dx, 0x3d4			; Select register
	mov		al, 0x0e			; (Cursor MSB)
	out		dx, al
	inc		dx					; Read register
	in		al, dx
	xchg	al, ah
	dec		dx					; Select register
	mov		al, 0x0f			; (Cursor LSB)
	out		dx, al
	inc		dx
	in		al, dx
	ret

; vga_store_cursor
;
;    Writes AL/AH to the VGA cursor location registers.
;    Breaks DX.
;

vga_store_cursor:
	push	ax
	mov		dx, 0x3d4			; Select register
	mov		al, 0x0f			; (Cursor LSB)
	out		dx, al
	inc		dx					; Write register
	pop		ax
	out		dx, al
	dec		dx					; Select register
	mov		al, 0x0e			; (Cursor MSB)
	out		dx, al
	inc		dx					; Write register
	xchg	al, ah
	out		dx, al
	ret

vga_setcur:
	push	dx
	push	ax

	xor		ah, ah
	mov		al, dh
	mov		dh, 80
	mul		byte dh				; AX = row offset
	xor		dh, dh
	add		ax, dx

	call	vga_store_cursor
	
	pop		ax
	pop		dx
	ret
	
vga_getcur:
	push	ax
	call	vga_load_cursor
	
	mov		cl, 80
	div		cl
	mov		dh, al
	mov		dl, ah

	; CH/CL is supposed to hold cursor scanlines.
	; We return 0 for now.
	xor		cx, cx

	pop		ax
	
	ret

vga_readchr:
	push	ds
	push	dx
	push	si

	call	vga_load_cursor

	shl		ax, 1
	mov		si, ax
	
	mov		ax, 0xb800
	mov		ds, ax
	lodsw

	pop		si
	pop		dx
	pop		ds

	iret

_bios_interrupt11:
    push    ds
    xor     ax, ax
    mov     ds, ax
    mov     ax, [0x0410]
    pop     ds
    iret

_bios_interrupt12:
	push	ds
	xor		ax, ax
	mov		ds, ax
	mov		ax, [0x413]
	pop		ds
    iret

_bios_interrupt13:
    cmp     ah, 0x00
    je      .resetDisk
    cmp     ah, 0x01
    je      .getDiskStatus
    cmp     ah, 0x02
    je      .readSectors
    cmp		ah, 0x03
	je		.writeSectors
	cmp		ah, 0x04
    je		.verifySectors
	cmp		ah, 0x05
	je		.formatTrack
    cmp		ah, 0x08
    je		.getDriveParams
    cmp		ah, 0x15
    je		.readDASDType
	cmp		ah, 0x18
	je		.setMediaType
    push	ax
    mov		al, 0x13
    out		0xE0, ax				; VM call 0x00 - What the fuck is up?
    pop		ax						; AL = INT, AH = function
    jmp		.end
.resetDisk:
    mov		ax, 0x1300
	out		0xE6, ax
	jmp     .end
.getDiskStatus:
    push	ds
	mov     ax, 0x0040
	mov		ds, ax
	mov		al, [0x0041]
	pop		ds
    jmp     .end
.readSectors:
	out     0xE2, al                ; VM call 0xE2 - int13h clone (al is dummy)
	jmp     .end
.verifySectors:
	out		0xE4, al
	jmp		.end
.writeSectors:
	out		0xE3, al
	jmp		.end
.getDriveParams:
	mov		byte [cs:temp], al
    mov     ax, 0x1308
    out     0xE6, ax
	mov		al, byte [cs:temp]
    jmp     .end
.readDASDType:
	mov		byte [cs:temp], al
    mov     ax, 0x1315
    out     0xE6, ax
	mov		al, byte [cs:temp]
    jmp     .end
.setMediaType:
	mov		byte [cs:temp], al
	mov		ax, 0x1318
	out		0xE6, ax
	mov		al, byte [cs:temp]
	jmp		.end
.formatTrack:
	mov		byte [cs:temp], al
	mov		ax, 0x1305
	out		0xE6, ax
	mov		al, byte [cs:temp]
	jmp		.end
	; no jmp .end here ;-)
.end:
    push    bp
    mov     bp, sp
    jc      .carry
    and     byte [bp+6], 0xfe       ; No carry
    pop     bp
    iret
.carry:
	or      byte [bp+6], 0x01       ; Carry
	pop     bp
    iret

_bios_interrupt14:
    cmp     ah, 0x00
    je      .fn0x00
	cmp		ah, 0x03
	je		.fn0x03
    push    ax
    mov     al, 0x14
    out     0xE0, ax                ; VM call 0x00 - What the fuck is up?
    pop     ax                      ; AL = INT, AH = function
    jmp     .end
.fn0x00:
    mov     ax, 0x0000              ; That's right. I'm not about to emulate a fucking RS232 port.
    jmp     .end
.fn0x03:
	mov		ax, 0x0000				; Status is 0. No way.
	jmp		.end
.end:
    iret

; INT 15 - System BIOS Services
;
; 24	A20 gate control		(not supported)

_bios_interrupt15:
	cmp		ah, 0x24
	je		.controlA20
    cmp     ah, 0xc0
    je      .fn0xc0
    cmp     ah, 0x41
    je      .fn0x41
	cmp		ah, 0x88
	je		.fn0x88
	cmp		ah, 0xc1
	je		.fn0xc1
    push    ax
    mov     al, 0x15
    out     0xE0, ax				; VM call 0x00 - What the fuck is up?
    pop     ax						; AL = INT, AH = function
    jmp     .end
.controlA20:
	stc
	mov		ah, 0x86
	jmp		.end
.fn0x88:
	stc								; This call is only valid on 286/386 machines
	xor		ax, ax
	jmp		.end
.fn0xc0:
    stc								; This ain't no fucking PS/2 system, dickweed.
    mov		ah, 0x86				; 80 for PC, 86 for XT/AT
	jmp		.end
.fn0xc1:
	stc								; Same as C0
	mov		ah, 0x86
	jmp		.end
.fn0x41:
    stc								; Unsupported
.end:
    push    bp
    mov     bp, sp
    jc      .carry
    and     byte [bp+6], 0xfe       ; No carry
    pop     bp
    iret
.carry:
    or      byte [bp+6], 0x01       ; Carry
	pop     bp
    iret

_bios_interrupt16:
    cmp     ah, 0x00
    je      .waitKey
	cmp		ah, 0x01
	je		.kbhit
	cmp		ah, 0x10
	je		.waitKey
	cmp		ah, 0x11
	je		.kbhit
	cmp		ah, 0x02
	je		.getFlags
	cmp		ah, 0x22
	je		.getEFlags
	cmp		ah, 0x92
	je		.checkCapa10
	cmp		ah, 0xA2
	je		.checkCapa20
    push    ax
    mov     al, 0x16
    out     0xE0, ax                ; VM call 0x00 - What the fuck is up?
    pop     ax                      ; AL = INT, AH = function
    jmp     .end
.getFlags:
	push	ds
	xor		ax, ax
	mov		ds, ax
	mov		al, [0x417]
	pop		ds
    jmp		.end
.getEFlags:
	push	ds
	xor		ax, ax
	mov		ds, ax
	mov		al, [0x417]
	mov     ah, [0x418]
	pop		ds
    jmp		.end
.checkCapa10:
	mov     ah, 0x80                ; Functions 0x10, 0x11 and 0x12 supported
	jmp     .end
.checkCapa20:
	jmp		.end					; Functions 0x20, 0x21 and 0x22 NOT supported
.waitKey:
	sti								; Allow clock fucking during this.
    mov     ax, 0x1600
    out     0xE6, ax                ; Send 0x1600 to port 0xE6, VM handler of assorted crap.
	cmp		ax, 0
	jz		.waitKey
	jmp		.end
.kbhit:
	mov		ax, 0x1601
	out		0xE6, ax
	push	bp
	mov		bp, sp					; This whole mekk can be optimized
	jz		.zero					; to make DOS run speedier.
    and     byte [bp+6], 0xbf       ; No ZF
	jmp		.ZFend
.zero:
	or		byte [bp+6], 0x40		; ZF!
.ZFend:
	pop		bp
.end:
	iret

_bios_interrupt17:
	cmp		ah, 0x00
	je		.printChar				; echo $c > prn (!)
    cmp     ah, 0x01
    je      .fn0x01
    cmp     ah, 0x02
    je      .fn0x02
    push    ax
    mov     al, 0x17
    out     0xE0, ax                ; VM call 0x00 - What the fuck is up?
    pop     ax                      ; AL = INT, AH = function
    jmp     .end
.printChar:
	push	cx
	mov		cl, al
	mov		ax, 0x1700
	out		0xE6, ax
	pop		cx
	jmp		.end
.fn0x01:
    mov     ah, 0x00
    jmp     .end
.fn0x02:
    mov     ah, 0x00    		    ; Zeroes. No fucking printer here, dude.
.end:
    iret

_bios_interrupt1a:
    cmp     ah, 0x00
    je      .getticks
	cmp		ah, 0x01
	je		.vmcall
    cmp     ah, 0x02
    je      .vmcall
	cmp		ah, 0x03
	je		.vmcall
    cmp     ah, 0x04
    je      .vmcall
	cmp		ah, 0x05
	je		.vmcall
    push    ax
    mov     al, 0x1a
    out     0xE0, ax                ; VM call 0x00 - What the fuck is up?
    pop     ax                      ; AL = INT, AH = function
    jmp     .end
.vmcall:
	push	ax
	xchg	al, ah
	mov		ah, 0x1A
	out		0xE6, ax
	pop		ax						; there used to be a 'jmp .end' here. phwa.
.end:
    push	bp
    mov		bp, sp
    jc		.carry
    and		byte [bp+6], 0xfe       ; No carry
    pop		bp
    iret
.carry:
    or		byte [bp+6], 0x01       ; Carry
    pop		bp
    iret
.getticks:							; This thingie is placed separately for
	mov		ax, 0x1A00				; optimization. DOS calls it all the time
	out		0xE6, ax				; and a speed boost can't hurt :-)
	iret
	
; DATA

    szVersion       db  "VOMIT Virtual Machine", 0x0d, 0x0a
					db	"(C) Copyright Andreas Kling 2003-2006", 0x0d, 0x0a, 0x0d, 0x0a, 0

	sz8086			db	"8086", 0
	sz80186			db	"80186", 0
	szUnknownCPU	db	"Unknown", 0
	szCPU			db	" CPU", 0x0d, 0x0a, 0

	szBaseMemory	db	" kB memory", 0x0d, 0x0a, 0

	szDisk00		db	"Floppy A: present.", 0x0d, 0x0a, 0
	szDisk01		db	"Floppy B: present.", 0x0d, 0x0a, 0
	szNoFloppies	db	"No floppy drives present.", 0x0d, 0x0a, 0

    szDivideByZero  db  "Divide by zero.", 0x0d, 0x0a, 0
    szOutOfBounds   db  "Array index out of bounds.", 0x0d, 0x0a, 0
    szOverflow      db  "Overflow.", 0x0d, 0x0a, 0
	szInvalidOpcode	db	"Invalid opcode.", 0x0d, 0x0a, 0

	szUnimplemented db  "Unimplemented ISR.", 0x0d, 0x0a, 0

    szNoBootDrive   db  "No bootable media found.", 0x0d, 0x0a, 0
    szNotBootable   db  "Warning: Boot signature missing.", 0x0d, 0x0a, 0
    szBootLoadFail  db  "Could not load boot sector.", 0x0d, 0x0a, 0

	szAnyKeyPlease  db  "Hit any key to reboot.", 0x0d, 0x0a, 0

	szCrLf          db  0x0d, 0x0a, 0

	temp			dw	0x0000
