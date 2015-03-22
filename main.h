#ifndef _MAIN_H_
#define _MAIN_H_

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <stdint.h>

#include "debug.h"

#if (FLASHEND > USHRT_MAX)
#define addr_t int32_t
#else
#define addr_t int16_t
#endif

#if (BOOT_ADR > USHRT_MAX)
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

#ifndef SPM_SEQ_ADR
#define SCAN_FOR_SPM_SEQUENCE
#endif

#endif