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
/--------------------------------------------------------------------------/
/ March 21, 2015
/--------------------------------------------------------------------------/
/
/ Frank26080115 combined SD card bootloading with STK500v2 bootloading
/ for use with Ultimaker2
/
/ The original concept of a "secondary bootloader" is impossible due to limitations of AVR8
/ so the two are merged instead of implemented separately
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

void flash_write_page(addr_t adr, const uint8_t* dat);

FATFS Fatfs;              // Petit-FatFs work area
BYTE  Buff[SPM_PAGESIZE]; // Page data buffer

// Hardware configuration below

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

void flash_write_page(addr_t adr, const uint8_t* dat)
{
	boot_spm_busy_wait();
	boot_page_erase(adr);
	boot_spm_busy_wait();
	uint32_t i;
	uint16_t j;
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += 2, j += 2)
	{
		#ifdef ENABLE_DEBUG
		// validate that the erase worked
		uint16_t r = pgm_read_word_at(i);
		if (r != 0xFFFF) {
			dbg_printf("flash erase failed at 0x%04X%04X, data 0x%04X\r\n", ((uint16_t*)&i)[1], ((uint16_t*)&i)[0], r);
		}
		#endif
		boot_page_fill(i, *((uint16_t*)(&dat[j])));
	}
	boot_page_write(adr);
	boot_spm_busy_wait();
	boot_rww_enable();
	boot_spm_busy_wait();

	// validate every byte
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += 2, j += 2) {
		uint16_t r, m;
		r = pgm_read_word_at(i);
		m = *((uint16_t*)(&dat[j]));
		if (r != m)
		{
			dbg_printf("flash verification failed at 0x%04X%04X, read 0x%04X, should be 0x%04X\r\n", ((uint16_t*)&i)[1], ((uint16_t*)&i)[0], r, m);
			boot_page_erase(0); // this will invalidate the app, so it cannot be launched, preventing rogue code from causing damage
			boot_spm_busy_wait();
			while (1) {
				LED_blink_pattern(0x83E1E39A);
			}
		}
	}
}

void sd_card_boot(void)
{
	dbg_printf("\r\nUM2 SD Card Bootloader\r\n");
	#ifdef ENABLE_DEBUG
	dbg_printf("LFUSE 0x%02X, HFUSE 0x%02X\r\n", boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS));
	dbg_printf("EFUSE 0x%02X, LOCKBITS 0x%02X\r\n", boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), boot_lock_fuse_bits_get(GET_LOCK_BITS));
	#endif

	DWORD fa; // Flash address
	WORD br;  // Bytes read
	DWORD bw; // Bytes written
	WORD i;   // Index for page difference check

	CARDDETECT_DDRx &= ~_BV(CARDDETECT_BIT); // pin as input
	CARDDETECT_PORTx |= _BV(CARDDETECT_BIT); // enable internal pull-up resistor
	BUTTON_DDRx &= ~_BV(BUTTON_BIT); // pin as input
	BUTTON_PORTx |= _BV(BUTTON_BIT); // enable internal pull-up resistor

	#ifndef ENABLE_DEBUG
	dly_100us(); // only done to wait for signals to rise
	#endif

	if (!CARD_DETECTED()) {
		dbg_printf("card not detected\r\n");
		return;
	}

	if (!BUTTON_PRESSED()) {
		dbg_printf("button not pressed\r\n");
		return;
	}

	FRESULT fres;
	#ifdef ENABLE_DEBUG
	fres = 
	#endif
	pf_mount(&Fatfs); // Initialize file system
	#ifdef ENABLE_DEBUG
	if (fres != FR_OK)
	{
		dbg_printf("card mount failed, err 0x%02X\r\n", fres);
	}
	#endif
	fres = pf_open("APP.BIN");
	if (fres != FR_OK) // Open application file
	{
		dbg_printf("file failed to open, err 0x%02X\r\n", fres);
		while (1) {
			LED_blink_pattern(0xB38F0F82);
		}
	}

	LED_ON();

	// wait for button release
	while (BUTTON_PRESSED()) {
		// blink the LED while waiting
		LED_blink_pattern(0x10C);
	}

	for (fa = 0, bw = 0; fa < BOOT_ADR; fa += SPM_PAGESIZE) // Update all application pages
	{
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		pf_read(Buff, SPM_PAGESIZE, &br); // Load a page data

		char to_write = 0;

		if (br > 0) // If data is available
		{
			for (i = 0; i < SPM_PAGESIZE && to_write == 0; i++)
			{ // check if the page has differences
				if (pgm_read_byte_at(fa) != Buff[i])
				{
					to_write = 1;
				}
			}
		}

		if (to_write) // write only if required
		{
			LED_TOG(); // blink the LED while writing
			flash_write_page(fa, Buff);
			bw += br;
			dbg_printf("bytes written: %d\r\n", bw);
		}
	}

	if (bw > 0)
	{
		dbg_printf("all done\r\n");
		// triple blink the LED to indicate that new firmware written
		LED_blink_pattern(0x40000); // used as a delay
		LED_blink_pattern(0x402A02A);
		app_start();
	}
	else
	{
		dbg_printf("all done, nothing written\r\n");
		// single blink the LED to indicate that nothing was actually written
		while (1) {
			LED_blink_pattern(0x40002);
		}
	}
}

void app_start(void)
{
	char canjump = can_jump();

	// long blink to indicate blank app
	while (!canjump)
	{
		LED_blink_pattern(0x87FF);
	}

	dbg_deinit();

	// this "xjmp to 0" approach is better than the "function pointer to 0" approach when dealing with a larger chip
	#ifdef __AVR_HAVE_JMP_CALL__
		asm volatile("jmp 0000");
	#else
		asm volatile("rjmp 0000");
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
	// 1001100110 would mean "blink the LED twice slowly"

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
	// check if app exists by seeing if there's a valid instruction
	uint16_t x = pgm_read_word_at(0);
	return (x != 0x0000 && x != 0xFFFF);
}