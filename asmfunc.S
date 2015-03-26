;---------------------------------------------------------------------------;
; MMC hardware controls and Flash controls      (C)ChaN, 2010
;---------------------------------------------------------------------------;
; Hardware dependent macros to be modified

#define	DDR_CS	_SFR_IO_ADDR(DDRB), 0	// MMC CS pin (DDR, PORT)
#define	PORT_CS	_SFR_IO_ADDR(PORTB), 0

#define	DDR_CK	_SFR_IO_ADDR(DDRB), 1	// MMC SCLK pin (DDR, PORT)
#define	PORT_CK	_SFR_IO_ADDR(PORTB), 1

#define	DDR_DI	_SFR_IO_ADDR(DDRB), 2	// MMC DI pin (DDR, PORT)
#define	PORT_DI	_SFR_IO_ADDR(PORTB), 2

#define	PIN_DO	_SFR_IO_ADDR(PINB), 3	// MMC DO pin (PIN, PORT)
#define	PORT_DO	_SFR_IO_ADDR(PORTB), 3


#if !defined(AVR_SIMULATION) && !defined(TEST_FLASH_CALLS)
#define flash_page SPMFUNC_ADR
#endif

;---------------------------------------------------------------------------;
.nolist
#include <avr/io.h>
.list
.text


;---------------------------------------------------------------------------;
; Initialize MMC port
;
; void init_spi (void);

.global init_spi
.func init_spi
init_spi:
	sbi	DDR_CS		; CS: output
	sbi	DDR_DI		; DI: output
	sbi	DDR_CK		; SCLK: output
	sbi	PORT_DO		; DO: pull-up
	ret
.endfunc



;---------------------------------------------------------------------------;
; Delay 100 microseconds
;
; void dly_us (UINT n);

.global dly_100us
.func dly_100us
dly_100us:
	ldi	r24, lo8(F_CPU / 100000)	/* Loop counter */
1:	sbiw	r30, 1		/* 10 clocks per loop */
	sbiw	r30, 1
	sbiw	r30, 1
	nop
	dec	r24
	brne	1b
	ret
.endfunc



;---------------------------------------------------------------------------;
; Select MMC
;
; void select (void);

.global select
.func select
select:
	rcall	deselect
	cbi	PORT_CS
	rjmp	rcv_spi
.endfunc



;---------------------------------------------------------------------------;
; Deselect MMC
;
; void deselect (void);

.global deselect
.func deselect
deselect:
	sbi	PORT_CS
	; Goto next function
.endfunc



;---------------------------------------------------------------------------;
; Receive a byte
;
; BYTE rcv_spi (void);

.global rcv_spi
.func rcv_spi
rcv_spi:
	ldi	r24, 0xFF	; Send 0xFF to receive data
	; Goto next function
.endfunc



;---------------------------------------------------------------------------;
; Transmit a byte
;
; void xmit_spi (BYTE);

.global xmit_spi
.func xmit_spi
xmit_spi:
	ldi	r25, 8
1:	sbrc	r24, 7		; DI = Bit to sent
	sbi	PORT_DI		; 
	sbrs	r24, 7		; 
	cbi	PORT_DI		; /
	lsl	r24		; Get DO from MMC
	sbic	PIN_DO		; 
	inc	r24		; /
	sbi	PORT_CK		; A positive pulse to SCLK
	cbi	PORT_CK		; /
	dec	r25		; Repeat 8 times
	brne	1b		; /
	ret
.endfunc


;---------------------------------------------------------------------------
; void flash_page_wrapper (uint32_t adr, const uint8_t* data);
.global flash_page_wrapper
.func flash_page_wrapper
flash_page_wrapper:
	call flash_page
	ret
.endfunc


#if defined(AVR_SIMULATION) || defined(TEST_FLASH_CALLS)
flash_page:
	nop
	ret
#endif