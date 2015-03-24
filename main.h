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

void sd_card_boot(void);
void app_start(void);
void LED_blink_pattern(uint32_t x);
void dly_100us(void); // from asmfunc.S
char can_jump(void);
void ser_putch(unsigned char);
uint8_t ser_readch(void);
uint8_t ser_readch_timeout(void);
#define ser_avail() bit_is_set(UCSRnA, RXCn)
void flash_write_page(addr_t adr, const uint8_t* dat);

extern uint8_t master_buffer[512];

#endif