#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "main.h"
#include "debug.h"

#ifdef ENABLE_DEBUG

int dbg_putch(uint8_t c, FILE* s) {
	#ifndef AVR_SIMULATION
	if (c == '\n') dbg_putch('\r', s);
	#ifdef AS_SECONDARY_BOOTLOADER
	while (bit_is_clear(UCSRnA, UDREn)) ; // wait for TX to finish
	UDRn = c;
	#else
	ser_putch(c);
	#endif
	#endif
	return 1;
}

FILE ser_stdout = FDEV_SETUP_STREAM(dbg_putch, NULL, _FDEV_SETUP_WRITE);

#ifdef AS_SECONDARY_BOOTLOADER
static char     irq_enabled;
static uint8_t  ucsrna_cache;
static uint8_t  ucsrnb_cache;
static uint8_t  ucsrnc_cache;
static uint16_t ubrrn_cache;
#endif

#endif

#ifdef ENABLE_DEBUG
void dbg_init(void)
{
	#ifdef AS_SECONDARY_BOOTLOADER
	/*
	Ultimaker2's primary bootloader uses weird settings, and the app FW depends on the same settings already being set
	this means the settings must be saved, and then restored later
	*/

	ucsrna_cache = UCSRnA;
	ucsrnb_cache = UCSRnB;
	ucsrnc_cache = UCSRnC;
	ubrrn_cache = UBRRn;
	UCSRnB = 0;
	UCSRnA = 0;
	UCSRnC = 0;

	irq_enabled = bit_is_set(SREG, 7);

	SER_PIN_DDRx |= _BV(SER_PIN_NUM); // TX pin as output
	UBRRn = 16; // 115200 baud
	UCSRnA = (1 << U2Xn);
	UCSRnC = (1 << USBSn) | (1 << UCSZn1) | (1 << UCSZn0);
	UCSRnB = _BV(TXENn) | _BV(RXENn);
	#else
	// assume STK500v2 code already initialized UART
	#endif
}

void dbg_deinit(void)
{
	#ifdef AS_SECONDARY_BOOTLOADER
	loop_until_bit_is_set(UCSRnA, UDREn);

	UCSRnB = 0;
	UCSRnA = 0;
	UCSRnC = 0;
	UBRRn  = ubrrn_cache;
	UCSRnA = ucsrna_cache;
	UCSRnC = ucsrnc_cache;
	UCSRnB = ucsrnb_cache;

	if (irq_enabled) {
		sei();
	}
	#endif
}

#ifdef EXTEND_PSTR
/*
The problem is pointers are 16 bit on AVR platform
But we placed these strings beyond that
Since we know the starting address of our bootloader
We can simply cast the 16 bit pointer to a 32 bit integer
and add an offset to it, according to the starting address
then use pgm_read_byte_far to copy the characters
*/
int dbg_printf_P(const PROGMEM char* __pfmt, ...)
{
	va_list args;
	va_start (args, __pfmt);

	char     fmtbuf[128];
	uint8_t  i;
	uint32_t j;
	char     c;

	j = (uint32_t)__pfmt;
	if (j < BOOT_ADR) {
		uint32_t far_base = BOOT_ADR & 0xFFFF0000;
		j += far_base;
	}

	for (i = 0; i < 128; i++, j++) {
		fmtbuf[i] = c = pgm_read_byte_far(j);
		if (c == '\0' || c > 0x7F) break;
	}

	int r = vfprintf (&ser_stdout, fmtbuf, args);

	va_end (args);

	return r;
}
#endif
#endif