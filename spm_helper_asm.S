.nolist
#include <avr/io.h>
.list
.section .init0
; init0 happens before anything else
; we require that the stack pointer remain untouched
; and the magic word r16-19 remain untouched
check_magic_1:
	cpi r16, MAGIC_1
	brne no_magic
	cpi r17, MAGIC_2
	brne no_magic
	cpi r18, MAGIC_3
	brne no_magic
	cpi r19, MAGIC_4
	brne no_magic

; at this point, the magic word matches
; remember how we did a rcall to no where just to save the PC to stack?
; now we can use the stack to get back to where we were
	ret

; no matching magic word means that this is a "normal" reset
no_magic:
; from here, __ctors_end continues

.text
; void call_spm(uint8_t csr, uint32_t seq_adr); seq_adr is optional
.global call_spm
.func   call_spm
call_spm:
; r16-19 are used for the magic
	push	r16
	push	r17
	push	r18
	push	r19

; r24 is what is going to be put into SPMCSR
; r24 is also the first parameter of this function
; no move required

; force erase magic word
	ldi r16, 0
	ldi r17, 0
	ldi r18, 0
	ldi r19, 0

; this stores the PC to the stack
	rcall	check_magic_2

check_magic_2:
	cpi r16, MAGIC_1
	brne write_magic
	cpi r17, MAGIC_2
	brne write_magic
	cpi r18, MAGIC_3
	brne write_magic
	cpi r19, MAGIC_4
	brne write_magic

; if the magic word matches, then we wait for SPM to finish
wait_spm_fin:
	lds	r24, _SFR_MEM_ADDR(SPMCSR)
	sbrc	r24, 0
	rjmp	wait_spm_fin
	rjmp	exit_call_spm

write_magic:
	ldi r16, MAGIC_1
	ldi r17, MAGIC_2
	ldi r18, MAGIC_3
	ldi r19, MAGIC_4

	; jump to where the sts and spm and ijmp are located
	#ifndef AVR_SIMULATION
	#ifdef __AVR_HAVE_JMP_CALL__
	jmp  SPM_SEQ_ADR
	#else
	rjmp SPM_SEQ_ADR
	#endif
	#else
	#ifdef __AVR_HAVE_JMP_CALL__
	jmp  fake_spm_seq_func
	#else
	rjmp fake_spm_seq_func
	#endif
	#endif

exit_call_spm:
	pop		r19
	pop		r18
	pop		r17
	pop		r16
	ret
.endfunc

#ifdef AVR_SIMULATION
.global fake_spm_seq_func
.func fake_spm_seq_func
fake_spm_seq_func:
	sts _SFR_MEM_ADDR(SPMCSR), r24
	spm
	eor	r30, r30
	eor	r31, r31
	ijmp
.endfunc
#endif