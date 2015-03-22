#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "debug.h"

#define SER_PIN_DDRx  DDRE
#define SER_PIN_PORTx PORTE
#define SER_PIN_NUM   1 // TX pin

// UM2 main board uses USART0
#define UCSRnA UCSR0A
#define UCSRnB UCSR0B
#define UCSRnC UCSR0C
#define UDREn  UDRE0
#define UDRn   UDR0
#define UBRRn  UBRR0
#define TXENn  TXEN0
#define RXENn  RXEN0
#define UCSZn0 UCSZ00
#define UCSZn1 UCSZ01

#ifdef ENABLE_DEBUG

int ser_putch(uint8_t c, FILE* s) {
	loop_until_bit_is_set(UCSRnA, UDREn);
	UDRn = c;
	return 1;
}

FILE ser_stdout = FDEV_SETUP_STREAM(ser_putch, NULL, _FDEV_SETUP_WRITE);

static char     irq_enabled;
static uint8_t  ucsrna_cache;
static uint8_t  ucsrnb_cache;
static uint8_t  ucsrnc_cache;
static uint16_t ubrrn_cache;

#endif

void dbg_init(void)
{
	#ifdef ENABLE_DEBUG

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

	cli(); // not sure if primary bootloader activated interrupts
	SER_PIN_DDRx |= _BV(SER_PIN_NUM); // TX pin as output
	UBRRn = 207; // 4800 baud at 16MHz
	// if the primary bootloader did set the baud, that baud will be used instead
	UCSRnC = (1 << UCSZn1) | (1 << UCSZn0);
	UCSRnB = _BV(TXENn) | _BV(RXENn);

	#endif
}

void dbg_deinit(void)
{
	#ifdef ENABLE_DEBUG

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

	uint32_t far_base = BOOT_ADR & 0xFFFF0000;
	char     fmtbuf[128];
	uint8_t  i;
	uint32_t j;
	char     c;

	j = (uint32_t)__pfmt;
	j+= far_base;

	for (i = 0; ; i++, j++) {
		fmtbuf[i] = c = pgm_read_byte_far(j);
		if (c == '\0') break;
	}

	int r = vfprintf (&ser_stdout, fmtbuf, args);

	va_end (args);

	return r;
}
#endif