.nolist
#include <avr/io.h>
.list

.org SPMFUNC_ADR

; this exists in the bootloader region
; app region code cannot call the spm instruction
; but by placing the spm instruction in bootloader region
; the app region code can use spm by calling this function
flash_page:
	push	r0
	push	r1
	push	ZH
	push	ZL
	push	XH
	push	XL
	push	r25

	; setup address for erase using Z
	movw	ZL, r22

	#if FLASHEND > UINT16_MAX
	; save RAMPZ so it can be restored later
	in		r16, _SFR_IO_ADDR(RAMPZ)
	push	r16
	; setup address for erase using RAMPZ
	out		_SFR_IO_ADDR(RAMPZ), r24
	#endif

	; we are about to mess up r24, save it to stack
	push	r24

	; initiate erase operation
	ldi		r24, (_BV(SPMEN) | _BV(PGERS))
	sts		_SFR_MEM_ADDR(SPMCSR), r24
	spm

	; wait for erase to complete
wait_spm_1:
	lds		r24, _SFR_MEM_ADDR(SPMCSR)
	sbrc	r24, SPMEN
	rjmp	wait_spm_1

	; erase completed

	; fill page buffer, one word at a time
	movw	ZL, r22
	movw	XL, r20
	ldi		r25, 128
load_word:
	ld		r0, X+
	ld		r1, X+

	; do the fill
	ldi		r24, _BV(SPMEN)
	sts		_SFR_MEM_ADDR(SPMCSR), r24
	spm

	; wait for fill
wait_spm_2:
	lds		r24, _SFR_MEM_ADDR(SPMCSR)
	sbrc	r24, SPMEN
	rjmp	wait_spm_2

	; go to next word
	adiw	ZL, 2
	dec		r25
	brne	load_word

	; set the target page address for write
	movw	ZL, r22
	; initiate write operation
	ldi		r24, (_BV(SPMEN) | _BV(PGWRT))
	sts		_SFR_MEM_ADDR(SPMCSR), r24
	spm

	; wait for write to complete
wait_spm_3:
	lds		r24, _SFR_MEM_ADDR(SPMCSR)
	sbrc	r24, SPMEN
	rjmp	wait_spm_3

	; re-enable RWW region
reen_rww:
	ldi		r24, (_BV(SPMEN) | _BV(RWWSRE))
	sts		_SFR_MEM_ADDR(SPMCSR), r24
	spm

	; wait for re-enable to complete
wait_spm_4:
	lds		r24, _SFR_MEM_ADDR(SPMCSR)
	sbrc	r24, SPMEN
	rjmp	wait_spm_4

	; check that re-enable has succeeded
	sbrc	r24, RWWSB
	rjmp	reen_rww

	; we stored r24
	pop		r24

#if FLASHEND > UINT16_MAX
	; r16 was used to store RAMPZ
	pop		r16
	out		_SFR_IO_ADDR(RAMPZ), r16
#endif

	; restore the rest
	pop		r25
	pop		XL
	pop		XH
	pop		ZL
	pop		ZH
	pop		r1
	pop		r0

	; make sure the stack pointer has the correct return address
	ret
