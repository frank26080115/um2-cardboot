#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "main.h"
#include "debug.h"

#ifdef ENABLE_DEBUG

int dbg_putch(uint8_t c, FILE* s) {
	if (c == '\n') ser_putch('\r');
	ser_putch(c);
	return 1;
}

FILE ser_stdout = FDEV_SETUP_STREAM(dbg_putch, NULL, _FDEV_SETUP_WRITE);

#endif

#ifdef ENABLE_DEBUG
void dbg_init(void)
{
	// assume STK500v2 code already initialized UART
}

void dbg_deinit(void)
{
	
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
		j+= far_base;
	}

	for (i = 0; ; i++, j++) {
		fmtbuf[i] = c = pgm_read_byte_far(j);
		if (c == '\0') break;
	}

	int r = vfprintf (&ser_stdout, fmtbuf, args);

	va_end (args);

	return r;
}
#endif
#endif