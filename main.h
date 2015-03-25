#ifndef _MAIN_H_
#define _MAIN_H_

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <stdint.h>

#include "debug.h"

#if defined(RAMPZ)
#define addr_t int32_t
#else
#define addr_t int16_t
#endif

#if (FLASHEND > UINT16_MAX)
#define pgm_read_byte_at(x)  pgm_read_byte_far(x)
#define pgm_read_word_at(x)  pgm_read_word_far(x)
#define pgm_read_dword_at(x) pgm_read_dword_far(x)
#else
#define pgm_read_byte_at(x)  pgm_read_byte(x)
#define pgm_read_word_at(x)  pgm_read_word(x)
#define pgm_read_dword_at(x) pgm_read_dword(x)
#endif

#ifdef __AVR_MEGA__
#define VECTORS_USE_JMP
#define xjmp_t uint32_t
#define pgm_read_xjmp(x) pgm_read_dword_far(x)
#else
#define VECTORS_USE_RJMP
#define xjmp_t uint16_t
#define pgm_read_xjmp(x) pgm_read_word(x)
#endif

// AVR's printf cannot format 32 bit properly, so split it into two 16 bit, use %04X%04X as formatting
#define DBG32(x) (((uint16_t*)(x))[1]),(((uint16_t*)(x))[0])
#if (FLASHEND > UINT16_MAX)
#define DBG32A(x) (((uint16_t*)(x))[1]),(((uint16_t*)(x))[0])
#else
#define DBG32A(x) 0,(x)
#endif

#define BOOTSIZE (0x400 * 8)
#define APP_END  (FLASHEND - (2*BOOTSIZE) + 1)

#define CARDDETECT_DDRx  DDRG
#define CARDDETECT_PORTx PORTG
#define CARDDETECT_PINx  PING
#define CARDDETECT_BIT   2
#define CARD_DETECTED()  bit_is_clear(CARDDETECT_PINx, CARDDETECT_BIT)

#define BUTTON_DDRx  DDRD
#define BUTTON_PORTx PORTD
#define BUTTON_PINx  PIND
#define BUTTON_BIT   2
#define BUTTON_PRESSED() bit_is_clear(BUTTON_PINx, BUTTON_BIT)

#define LED_DDRx  DDRH
#define LED_PORTx PORTH
#define LED_BIT   5
#define LED_ON()  PORTH |= _BV(LED_BIT)
#define LED_OFF() PORTH &= ~_BV(LED_BIT)
#define LED_TOG() PORTH ^= _BV(LED_BIT)

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
#define U2Xn   U2X0
#define RXCn   RXC0
#define USBSn  USBS0

extern void sd_card_boot(void);
extern void app_start(void);
extern void LED_blink_pattern(uint32_t x);
extern void dly_100us(void); // from asmfunc.S
extern char can_jump(void);
extern void ser_putch(unsigned char);
extern uint8_t ser_readch(void);
extern uint8_t ser_readch_timeout(void);
#define ser_avail() bit_is_set(UCSRnA, RXCn)
extern void flash_write_page(addr_t adr, const uint8_t* dat);
extern void call_spm(uint8_t)
#ifndef AS_2NDARY_BOOTLOADER
	__attribute__ ((section (".fini1")))
#endif
;
extern uint8_t master_buffer[512];

#endif