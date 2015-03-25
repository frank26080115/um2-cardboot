.ifndef AS_SECONDARY_BOOTLOADER
.text
.section .fini1
.endif
.org SPMFUNC_ADR
.ifndef AS_SECONDARY_BOOTLOADER
.global	call_spm
.func	call_spm
.endif

; this only exists as a backup mechanism, in case the app region needs to utilize SPM
; make sure r24 contains whatever you want for SPMCSR (0x0057) first
; and make sure the stack pointer has the correct return address

call_spm:	sts		0x0057, r24
; remember that spm must be called within 4 cycles of writing SPMCSR
			spm
wait_spm_1:	lds		r24, 0x0057
			sbrc	r24, 0
			rjmp	wait_spm_1
			; Re-enable read access to the flash
reen_rww:	ldi	r24, 0b00010001
			sts		0x0057, r24
			spm
wait_spm_2:	lds		r24, 0x0057
			sbrc	r24, 0
			rjmp	wait_spm_2
			ret
.ifndef AS_SECONDARY_BOOTLOADER
.endfunc
.endif