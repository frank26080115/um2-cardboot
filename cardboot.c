/*-------------------------------------------------------------------------/
/ MMC/SD card boot loader
/--------------------------------------------------------------------------/
/
/  Original copyright statement from ChaN
/
/  Copyright (C) 2010, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/--------------------------------------------------------------------------/
/ March 18, 2015
/--------------------------------------------------------------------------/
/ Frank26080115 modified this bootloader for use as the Ultimaker2's bootloader
/ See https://github.com/frank26080115/um2-cardboot/ for all information
/
/--------------------------------------------------------------------------/
/ Dec 6, 2010  R0.01  First release
/--------------------------------------------------------------------------/
/ http://elm-chan.org/fsw/ff/00index_p.html
/-------------------------------------------------------------------------*/

#include "main.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <string.h>
#include "pff.h"
#include "debug.h"

static FATFS CardFs;
uint8_t master_buffer[SPM_PAGESIZE * 2];
static BYTE* Buff;

void flash_write_page(addr_t adr, const uint8_t* dat)
{
	addr_t i;
	uint16_t j;
	dbg_printf("fwp 0x%04X%04X\n", DBG32A(adr));
	#ifdef AS_SECONDARY_BOOTLOADER
	flash_page_wrapper(adr, dat);
	#else
	boot_spm_busy_wait();
	boot_page_erase(adr);
	boot_spm_busy_wait();
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += sizeof(uint16_t), j += sizeof(uint16_t))
	{
		boot_page_fill(i, *((uint16_t*)(&dat[j])));
		boot_spm_busy_wait();
	}
	boot_page_write(adr);
	boot_spm_busy_wait();
	do
	{
		boot_rww_enable();
		boot_spm_busy_wait();
	}
	while (boot_rww_busy());
	#endif

	// validate every byte
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += sizeof(uint16_t), j += sizeof(uint16_t)) {
		uint16_t r, m;
		r = pgm_read_word_at(i);
		m = *((uint16_t*)(&dat[j]));
		if (r != m)
		{
			dbg_printf("veri fail @ 0x%04X%04X r 0x%04X != 0x%04X\n", DBG32A(i), r, m);
			// erasing page 0 will invalidate the app, so it cannot be launched, preventing rogue code from causing damage
			#ifdef AS_SECONDARY_BOOTLOADER
			memset(Buff, 0xFF, SPM_PAGESIZE);
			flash_page_wrapper(0, dat);
			#else
			boot_page_erase(0);
			boot_spm_busy_wait();
			do
			{
				boot_rww_enable();
				boot_spm_busy_wait();
			}
			while (boot_rww_busy());
			#endif
			while (1) {
				LED_blink_pattern(0x83E1E39A);
			}
		}
	}
}

#ifdef AS_SECONDARY_BOOTLOADER

xjmp_t app_reset_vector;

int main(void)
{
	#ifdef _FIX_STACK_POINTER_2_
	//	some chips dont set the stack properly
	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_HI_ADDR) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_LO_ADDR) );
	#endif

	#ifdef _FIX_ISSUE_181_
	//*	Dec 29,	2011	<MLS> Issue #181, added watch dog timer support
	//*	handle the watch dog timer
	uint8_t	mcuStatusReg;
	mcuStatusReg	=	MCUSR;

	cli();
	MCUSR	=	0;
	wdt_disable();
	// check if WDT generated the reset, if so, go straight to app
	if (mcuStatusReg & _BV(WDRF)) {
		app_start();
	}
	#endif

	dbg_init();
	dbg_printf("\nBootloader ");

	#ifdef TEST_FLASH
	dbg_printf("TEST FLASH\n");
	memset(Buff, 0xAA, SPM_PAGESIZE); // Clear buffer
	flash_page_wrapper(0, Buff);
	dbg_printf("DONE\n");
	while (1) ;
	#endif

	sd_card_boot();
	app_start();
	while (1);
	return 0;
}

#ifdef VECTORS_USE_JMP
xjmp_t make_jmp(addr_t x)
{
	x >>= 1;
	addr_t y = x & 0x0001FFFF;
	x &= 0xFFFE0000;
	x <<= 3;
	y |= x;
	y |= 0x940C0000;
	y &= 0x95FDFFFF;
	return ((y & 0x0000FFFF) << 16 | (y & 0xFFFF0000) >> 16);
	// AVR 32 bit instruction ordering isn't straight forward little-endian
}
#endif
#ifdef VECTORS_USE_RJMP
xjmp_t make_rjmp(addr_t src, addr_t dst)
{
	addr_t delta = dst - src;
	uint16_t delta16 = (uint16_t)delta;
	delta16 >>= 1;
	delta16 &= 0x0FFF;
	xjmp_t res = 0xCFFF & ((delta16) | 0xC000));
}
#endif

void check_reset_vector(void)
{
	xjmp_t tmp1, tmp2;
	#ifdef VECTORS_USE_JMP
	tmp1 = make_jmp(BOOT_ADR);
	#elif defined(VECTORS_USE_RJMP)
	tmp1 = make_rjmp(0, BOOT_ADR);
	#endif
	tmp2 = pgm_read_xjmp(0);
	if (tmp2 != tmp1)
	{
		dbg_printf("rst vec need OW\n");
		//dbg_printf("read 0x%04X%04X ", DBGXJMP(tmp2));
		//dbg_printf("!= 0x%04X%04X\n", DBGXJMP(tmp1));
		// this means existing flash will not activate the bootloader
		// so we force a rewrite of this vector
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		(*((xjmp_t*)Buff)) = tmp1;
		flash_write_page(0, Buff);
	}
}

#endif

#ifdef _FIX_STACK_POINTER_1_
/*
 * since this bootloader is not linked against the avr-gcc crt1 functions,
 * to reduce the code size, we need to provide our own initialization
 */

//#define	SPH_REG	0x3E
//#define	SPL_REG	0x3D

void __jumpMain(void)
{
//	July 17, 2010	<MLS> Added stack pointer initialzation
//	the first line did not do the job on the ATmega128

	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );

//	set stack pointer to top of RAM

	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_HI_ADDR) );

	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_LO_ADDR) );

	asm volatile ( "clr __zero_reg__" );									// GCC depends on register r1 set to 0
	asm volatile ( "out %0, __zero_reg__" :: "I" (_SFR_IO_ADDR(SREG)) );	// set SREG to 0
	asm volatile ( "jmp main");												// jump to main()
}
#endif

// quickly exit SD card stuff if STK500v2 needs to respond to a command
#if defined(AS_SECONDARY_BOOTLOADER) || defined(ENABLE_DEBUG) || defined(DISABLE_STK500V2)
#define CHECK_UART_FOR_STK500V2()
#else
#define CHECK_UART_FOR_STK500V2() do { if (ser_avail() != 0) return; } while (0)
#endif

void sd_card_boot(void)
{
	dbg_printf("SD Card\n");

	#ifdef DISABLE_CARDBOOT
	return;
	#endif

	DWORD fa; // Flash address
	WORD br;  // Bytes read
	DWORD bw; // Bytes written
	WORD i;   // Index for page difference check
	char canjump, canwrite;

	CHECK_UART_FOR_STK500V2();

	CARDDETECT_DDRx &= ~_BV(CARDDETECT_BIT); // pin as input
	CARDDETECT_PORTx |= _BV(CARDDETECT_BIT); // enable internal pull-up resistor
	BUTTON_DDRx &= ~_BV(BUTTON_BIT); // pin as input
	BUTTON_PORTx |= _BV(BUTTON_BIT); // enable internal pull-up resistor

	CHECK_UART_FOR_STK500V2();
	canwrite = can_write();
	CHECK_UART_FOR_STK500V2();

	#ifdef AS_SECONDARY_BOOTLOADER
	char end_of_file = 0;
	if (canwrite) {
		check_reset_vector();
	}
	#else
	#ifndef FORCE_CARD
	if (!BUTTON_PRESSED()) {
		dbg_printf("no btn, stk time\n");
		return;
	}
	#endif
	#endif

	#ifndef FORCE_CARD

	CHECK_UART_FOR_STK500V2();
	canjump = can_jump();
	CHECK_UART_FOR_STK500V2();

	if (!canwrite && BUTTON_PRESSED())
	{
		for (uint8_t blinks = 0; blinks < 4 || !canjump; blinks++) {
			LED_blink_pattern(0x1138A);
		}
		app_start();
	}

	CHECK_UART_FOR_STK500V2();

	if (canjump)
	{
		#ifndef ENABLE_DEBUG
		dly_100us(); // only done to wait for signals to rise
		#endif
		CHECK_UART_FOR_STK500V2();
		if (!CARD_DETECTED()) {
			dbg_printf("no card\n");
			return;
		}

		if (!BUTTON_PRESSED()) {
			dbg_printf("no btn\n");
			return;
		}
	#ifdef ENABLE_DEBUG
		dbg_printf("can jump, almost primed\n");
	}
	else
	{
		dbg_printf("forced to boot from card\n");
	#endif
	}

	CHECK_UART_FOR_STK500V2();
	#endif

	char canfile = try_open_file("APP.BIN", 3);

	if (canfile == 0)
	{
		char was_btn = BUTTON_PRESSED();
		for (uint8_t blinks = 0; blinks < 4 || was_btn; blinks++) {
			LED_blink_pattern(0xB38F0F82);
		}
		return;
	}

	LED_ON();

	// wait for button release
	#ifdef ENABLE_DEBUG
	if (canjump) {
		dbg_printf("wait btn rel...");
	}
	#endif
	while (BUTTON_PRESSED() && canjump) {
		// blink the LED while waiting
		LED_blink_pattern(0x10C);
	}
	#ifdef ENABLE_DEBUG
	if (canjump) {
		dbg_printf(" RELEASED!!\n");
	}
	#endif

	dbg_printf("start card read\n");

	Buff = (BYTE*)master_buffer;
	for (fa = 0, bw = 0; fa < BOOT_ADR; fa += SPM_PAGESIZE) // Update all application pages
	{
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		#ifdef AS_SECONDARY_BOOTLOADER
		if (!end_of_file)
		#endif
		pf_read(Buff, SPM_PAGESIZE, &br); // Load a page data

		char to_write = 0;

		if (br > 0 // If data is available
		#ifdef AS_SECONDARY_BOOTLOADER
		|| fa == (BOOT_ADR - SPM_PAGESIZE) // If is last page
		#endif
		)
		{
			#ifdef AS_SECONDARY_BOOTLOADER
			if (fa < SPM_PAGESIZE) // If is very first page
			{
				// the old reset vector will point inside the application
				// but we need it to point into the bootloader, or else the bootloader will never launch
				// we need to save the old reset vector, then replace it
				app_reset_vector = (*((xjmp_t*)Buff));
				(*((xjmp_t*)Buff)) = 
				#ifdef VECTORS_USE_JMP
					make_jmp(BOOT_ADR);
				#elif defined(VECTORS_USE_RJMP)
					make_rjmp(0, BOOT_ADR);
				#endif
				dbg_printf("rst vect old 0x%04X%04X new 0x%04X%04X\n", DBGXJMP(app_reset_vector), DBGXJMP((*((xjmp_t*)Buff))));
				if (br <= 0) br += sizeof(xjmp_t);
			}
			else if (fa == (BOOT_ADR - SPM_PAGESIZE)) // If is trampoline
			{
				// we need a way to launch the application
				// that's why we saved the old reset vector
				// we check where it points to, and write it as a trampoline
				// use the trampoline to launch the real app from the SD card bootloader

				xjmp_t* inst_ptr = ((xjmp_t*)(&Buff[SPM_PAGESIZE-sizeof(xjmp_t)]));
				#ifdef VECTORS_USE_JMP
				if ((app_reset_vector & 0xFE0E0000) == 0x940C0000) {
					// this is a JMP instruction, we can put it here without changing it
					(*inst_ptr) = app_reset_vector;
					dbg_printf("tramp use JMP addr 0x%04X%04X insn 0x%04X%04X\n", DBG32(fa), DBG32(app_reset_vector));
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0x0000F000) == 0x0000C000) {
					// this is a RJMP instruction
					(*inst_ptr) = make_jmp((app_reset_vector & 0x0FFF) << 1);
					dbg_printf("tramp RJMP conv JMP addr 0x%04X%04X RJMP 0x%04X JMP 0x%04X%04X\n", DBG32(fa), app_reset_vector & 0xFFFF, DBG32(*inst_ptr));
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0xFFFF) == 0xFFFF || (app_reset_vector & 0xFFFF) == 0x0000) {
					(*inst_ptr) = make_jmp(BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
					dbg_printf("tramp no app addr 0x%04X%04X JMP to boot 0x%04X%04X\n", DBG32A(fa), DBG32(*inst_ptr));
				}
				#elif defined(VECTORS_USE_RJMP)
				if ((app_reset_vector & 0xF000) == 0xC000) {
					// this is a RJMP instruction
					addt_t dst = (app_reset_vector & 0x0FFF) << 1;
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), dst);
					dbg_printf("tramp addr 0x%04X%04X RJMP 0x%04X\n", DBG32A(fa), (*inst_ptr));
				}
				else if (app_reset_vector == 0xFFFF || app_reset_vector == 0x0000) {
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
					dbg_printf("tramp no app addr 0x%04X%04X RJMP 0x%04X\n", DBG32A(fa), (*inst_ptr));
				}
				#endif
				else {
					// hmm... it wasn't a JMP or RJMP but it wasn't blank, we put it here and hope for the best
					(*inst_ptr) = app_reset_vector;
					dbg_printf("tramp unknown addr 0x%04X%04X RJMP 0x%04X\n", DBG32A(fa), (*inst_ptr));
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
			}
			#endif

			for (i = 0; i < SPM_PAGESIZE && to_write == 0; i++)
			{ // check if the page has differences
				if (pgm_read_byte_at(fa + i) != Buff[i])
				{
					to_write = 1;
				}
			}
		}
		#ifdef AS_SECONDARY_BOOTLOADER
		else if (br <= 0)
		{
			end_of_file = 1;
		}
		#endif

		if (to_write) // write only if required
		{
			#ifndef DISABLE_BLINK
			LED_TOG(); // blink the LED while writing
			#endif
			flash_write_page(fa, Buff);
			bw += br;
			dbg_printf("bytes written: %d\n", bw);
		}
	}

	if (bw > 0)
	{
		dbg_printf("all done\n");
		// triple blink the LED to indicate that new firmware written
		LED_blink_pattern(0x40000); // used as a delay
		LED_blink_pattern(0x402A02A);
		app_start();
	}
	else
	{
		dbg_printf("all done no W\n");
		// single blink the LED to indicate that nothing was actually written
		LED_blink_pattern(0x40002);
		LED_blink_pattern(0x40002);
		app_start();
	}
}

char try_open_file(const char* fname, uint8_t retries)
{
	if (retries <= 0) retries = 1;

	uint8_t attempts;
	FRESULT fres;

	for (attempts = 0; attempts < retries; attempts++)
	{
		fres = pf_mount(&CardFs); // Initialize file system
		if (fres == FR_OK)
		{
			fres = pf_open(fname);
			if (fres == FR_OK) // Open application file
			{
				dbg_printf("file openned\n");
				return 1;
			}
			else
			{
				dbg_printf("file open ERR 0x%02X\n", fres);
			}
		}
		else
		{
			dbg_printf("fs mnt ERR 0x%02X\n", fres);
		}
	}
	return 0;
}

void app_start(void)
{
	char canjump = can_jump();

	if (canjump) {
		dbg_printf("jmp2app\n");
	}
	else {
		dbg_printf("no app\n");
		// long blink to indicate blank app
		while (1) {
			LED_blink_pattern(0x87FF);
		}
	}

	dbg_deinit();

	#ifdef USE_BUFFERED_SERIAL
	MCUCR = (1 << IVCE);	// enable change of interrupt vectors
	MCUCR = (0 << IVSEL);	// move interrupts to app flash section
	cli();
	#endif

	#ifdef AS_SECONDARY_BOOTLOADER
		// there is an instruction stored here (trampoline), jump here and execute it
		#ifdef VECTORS_USE_JMP
			#ifdef __AVR_HAVE_JMP_CALL__
				asm volatile("jmp (__vectors - 4)");
			#else
				asm volatile("rjmp (__vectors - 4)");
			#endif
		#elif defined(VECTORS_USE_RJMP)
			#ifdef __AVR_HAVE_JMP_CALL__
				asm volatile("jmp (__vectors - 2)");
			#else
				asm volatile("rjmp (__vectors - 2)");
			#endif
		#endif
	#else
		// this "xjmp to 0" approach is better than the "function pointer to 0" approach when dealing with a larger chip
		#ifdef __AVR_HAVE_JMP_CALL__
			asm volatile("jmp 0000");
		#else
			asm volatile("rjmp 0000");
		#endif
	#endif
}

void LED_blink_pattern(uint32_t x)
{
	// each _delay_ms call is inlined so this function exists so we don't put _delay_ms everywhere
	// thus saving memory

	// the highest bit is a boundary bit, used to dictate the length of the pattern
	// bit 0 doesn't actually do anything
	// all the bits in between dictate the pattern itself
	// all 1s would mean "keep LED on"
	// all 0s would mean "keep LED off"
	// 101010 would mean "blink the LED twice fast"
	// 10001110001110 would mean "blink the LED twice slowly"
	// 0x83E1E39A is fast to slow, 0xB38F0F82 is slow to fast

 	while (x)
	{
		x >>= 1;
		if ((x & 1) != 0) {
			LED_ON();
		}
		else {
			LED_OFF();
		}
		_delay_ms(100);
	}
}

char can_jump(void)
{
	#ifdef AS_SECONDARY_BOOTLOADER
	check_reset_vector();
	xjmp_t tmpx = pgm_read_xjmp(BOOT_ADR - sizeof(xjmp_t));
	// check if trampoline exists
	#ifdef VECTORS_USE_JMP
	if ((tmpx & 0xFFFF) == 0xFFFF || (tmpx & 0xFFFF) == 0x0000 || tmpx == make_jmp(BOOT_ADR)) {
		dbg_printf("tramp missing read 0x%04X%04X\n", DBGXJMP(tmpx));
	#elif defined(VECTORS_USE_RJMP)
	if (tmpx == 0xFFFF || tmpx == 0x0000 || tmpx == make_rjmp(0, BOOT_ADR)) {
		dbg_printf("tramp missing read 0x%04X\n", tmpx);
	#endif
		return 0;
	}
	return 1;
	#else
	// check if app exists by seeing if there's a valid instruction
	uint16_t x = pgm_read_word_at(0);
	if (x == 0x0000 || x == 0xFFFF) {
		dbg_printf("app missing\n");
		return 0;
	}
	else {
		return 1;
	}
	#endif
}

char can_write(void)
{
	// if we are in app region, check if the bootloader region has our injected spm function code
	#ifdef AS_SECONDARY_BOOTLOADER
	uint16_t insn;
	insn = pgm_read_word_at(SPMFUNC_ADR);
	if (insn == 0xFFFF || insn == 0x0000) {
		dbg_printf("no way to write\n");
		return 0;
	}
	#endif
	return 1;
}