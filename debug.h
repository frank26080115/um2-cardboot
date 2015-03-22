#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>
#include <avr/pgmspace.h>

#if (BOOT_ADR > UINT16_MAX)
#define EXTEND_PSTR
/*
PSTR is convenient to place strings in flash
It returns a pointer, but pointers on AVR are 16 bit
Our bootloader is beyond the maximum value of 16 bit
So everything that uses PSTR will fail
See comments inside dbg_printf_P for solution
*/
#endif

#ifdef ENABLE_DEBUG

	void dbg_init(void);
	void dbg_deinit(void);

	extern FILE ser_stdout;

	#ifdef EXTEND_PSTR
		#define dbg_printf(fmt, args...) dbg_printf_P(PSTR(fmt), ##args)
		int dbg_printf_P(const PROGMEM char*, ...);
	#else
		#define dbg_printf(fmt, args...) fprintf_P(&ser_stdout, PSTR(fmt), ##args)
	#endif

#else
	#define dbg_init()
	#define dbg_deinit()
	#define dbg_printf(fmt, args...)
	#define ser_putch(a, b)
#endif

#endif