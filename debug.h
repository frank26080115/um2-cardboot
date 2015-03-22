#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>
#include <avr/pgmspace.h>

#ifdef ENABLE_DEBUG

	void dbg_init(void);
	void dbg_deinit(void);
	int ser_putch(uint8_t, FILE*);

	extern FILE ser_stdout;

	#define dbg_printf(fmt, args...) fprintf_P(&ser_stdout, PSTR(fmt), ##args)
	// this doesn't work, but this is the same way I've debugged everywhere else on a ton of other platforms
	// ser_putch is now public, and can be used for a more primitive debug

	//#define dbg_printf(fmt, args...)

#else
	#define dbg_init()
	#define dbg_deinit()
	#define dbg_printf(fmt, args...)
	#define ser_putch(a, b)
#endif

#endif