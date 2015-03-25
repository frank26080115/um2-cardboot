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
/ The bootloader must be activated by several conditions
/ * the card must be inserted
/ * the file must exist
/ * the button on the front of the UM2 must be held down at boot
/ Some LED blinking as been added to indicate the bootloader status
/ When the bootloader is ready, it will blink evenly and rapidly
/ The user should then release the button, and the actual flash operations will start
/ A flash write will only be performed if the new page does not match
/ The final LED blink pattern indicates if anything new was written (3 blinks if yes, 1 blink if no)
/ The UM2 must be manually reset to start the application
/
/ There are two ways this can be built
/ 1) the default way simply merges the SD card bootloader with the STK500v2 bootloader
/    and both will be deployed into the bootloader region
/ 2) the original Arduino Mega 2560 STK500v2 bootloader resides in the bootloader region unmodified,
/    some SPM instructions are injected into the end of the bootloader region,
/    the SD card bootloader resides in the user app region just before the bootloader region,
/    a "trampoline" is placed just before the SD card bootloader, used to launch the user app,
/    the user app's reset vector is modified to launch the SD card bootloader
/
/--------------------------------------------------------------------------/
/ Dec 6, 2010  R0.01  First release
/--------------------------------------------------------------------------/
/ This is a stand-alone MMC/SD boot loader for megaAVRs. It requires a 4KB
/ boot section for code. To port the boot loader into your project, follow
/ instructions described below.
/
/ 1. Setup the hardware. Attach a memory card socket to the any GPIO port
/    where you like. Select boot size that is enough for the boot loader with
/    BOOTSZ fuses and enable boot loader with BOOTRST fuse.
/
/ 2. Setup the software. Change the four port definitions in the asmfunc.S.
/    Change MCU_TARGET, BOOT_ADR and MCU_FREQ in the Makefile. The BOOT_ADR
/    is a BYTE address of boot section in the flash. Build the boot loader
/    and write it to the device with a programmer.
/
/ 3. Build the application program and output it in binary form instead of
/    hex format. Rename the file "APP.BIN" (all caps) and put it into the memory card.
/
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

FATFS Fatfs;
uint8_t master_buffer[SPM_PAGESIZE * 2];
BYTE* Buff;

#ifdef AS_SECONDARY_BOOTLOADER
void flash_erase(addr_t); // asmfunc.S
void flash_write(addr_t, const uint8_t*); // asmfunc.S
#endif

void flash_write_page(addr_t adr, const uint8_t* dat)
{
	#ifdef AS_SECONDARY_BOOTLOADER
	flash_erase(adr);
	#else
	boot_spm_busy_wait();
	boot_page_erase(adr);
	boot_spm_busy_wait();
	#endif
	addr_t i;
	uint16_t j;
	#if defined(EANBLE_DEBUG) || !defined(AS_SECONDARY_BOOTLOADER)
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += 2, j += 2)
	{
		#ifdef ENABLE_DEBUG
		// validate that the erase worked
		uint16_t r = pgm_read_word_at(i);
		if (r != 0xFFFF) {
			dbg_printf("erase fail @ 0x%04X%04X r 0x%04X\n", DBG32A(i), r);
		}
		#endif
		#ifndef AS_SECONDARY_BOOTLOADER
		boot_page_fill(i, *((uint16_t*)(&dat[j])));
		#endif
	}
	#endif
	#ifdef AS_SECONDARY_BOOTLOADER
	flash_write(adr, dat);
	#else
	boot_page_write(adr);
	boot_spm_busy_wait();
	boot_rww_enable();
	boot_spm_busy_wait();
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
			flash_erase(0);
			#else
			boot_page_erase(0);
			boot_spm_busy_wait();
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
	sd_card_boot();
	app_start();
	while (1);
	return 0;
}

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
	volatile xjmp_t tmp1, tmp2;
	#ifdef VECTORS_USE_JMP
	tmp1 = make_jmp(BOOT_ADR);
	#elif defined(VECTORS_USE_RJMP)
	tmp1 = make_rjmp(0, BOOT_ADR);
	#endif
	tmp2 = pgm_read_xjmp(0);
	if (tmp2 != tmp1)
	{
		dbg_printf("rst vec need OW ");
		dbg_printf("read 0x%04X%04X ", DBGXJMP(tmp2));
		dbg_printf("!= 0x%04X%04X\n", DBGXJMP(tmp1));
		// this means existing flash will not activate the bootloader
		// so we force a rewrite of this vector
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		(*((xjmp_t*)Buff)) = tmp1;
		flash_write_page(0, Buff);
	}
}

#endif

void sd_card_boot(void)
{
	dbg_printf("SD Card\n");

	#ifndef AS_SECONDARY_BOOTLOADER
	volatile char useless = 0;
	if (useless) call_spm(0); // this forces the garbage collector to not collect it
	#endif

	DWORD fa; // Flash address
	WORD br;  // Bytes read
	DWORD bw; // Bytes written
	WORD i;   // Index for page difference check
	Buff = (BYTE*)master_buffer;
	char canjump;

	CARDDETECT_DDRx &= ~_BV(CARDDETECT_BIT); // pin as input
	CARDDETECT_PORTx |= _BV(CARDDETECT_BIT); // enable internal pull-up resistor
	BUTTON_DDRx &= ~_BV(BUTTON_BIT); // pin as input
	BUTTON_PORTx |= _BV(BUTTON_BIT); // enable internal pull-up resistor

	#ifdef AS_SECONDARY_BOOTLOADER
	char end_of_file = 0;
	check_reset_vector();
	#endif
	canjump = can_jump();

	if (canjump)
	{
		#ifndef ENABLE_DEBUG
		dly_100us(); // only done to wait for signals to rise
		#endif
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

	FRESULT fres;
	#ifdef ENABLE_DEBUG
	fres = 
	#endif
	pf_mount(&Fatfs); // Initialize file system
	#ifdef ENABLE_DEBUG
	if (fres != FR_OK)
	{
		dbg_printf("fs mnt ERR 0x%02X\n", fres);
	}
	#endif
	fres = pf_open("APP.BIN");
	if (fres != FR_OK) // Open application file
	{
		dbg_printf("file open ERR 0x%02X\n", fres);
		while (1) {
			LED_blink_pattern(0xB38F0F82);
		}
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
				if (pgm_read_byte_at(fa) != Buff[i])
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
			LED_TOG(); // blink the LED while writing
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
		while (1) {
			LED_blink_pattern(0x40002);
		}
	}
}

void app_start(void)
{
	char canjump = can_jump();

	if (canjump) {
		dbg_printf("jmp2app\n");
	}
	else {
		dbg_printf("no app\n");
	}

	// long blink to indicate blank app
	while (!canjump)
	{
		LED_blink_pattern(0x87FF);
	}

	dbg_deinit();

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