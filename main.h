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

void sd_card_boot(void);
void app_start(void);
void LED_blink_pattern(uint32_t x);
void dly_100us(void); // from asmfunc.S
char can_jump(void);

#endif