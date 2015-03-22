/*-------------------------------------------------------------------------/
/  Stand-alone MMC boot loader
/--------------------------------------------------------------------------/
/
/  Copyright (C) 2010, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/--------------------------------------------------------------------------/
/ March 18, 2013
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
/ This variation of the code can be easily adapted to other applications
/ Resource cleanup is not performed!
/
/ If the constant AS_2NDARY_BOOTLOADER is defined, then this bootloader will
/ reside in the memory area just before the actual bootloader memory region
/ which means two bootloaders will exist. This type of bootloader mechanism
/ manipulates instructions in the reset vector to work.
/
/ There are two flavours of vectors
/ * large chips use 32 bit vector entries containing JMP instructions
/ * small chips use 16 bit vector entries containing RJMP instructions
/
/--------------------------------------------------------------------------/
/ Dec 6, 2010  R0.01  First release
/--------------------------------------------------------------------------/
/ This is a stand-alone MMC/SD boot loader for megaAVRs. It requires a 4KB
/ boot section for code. To port the boot loader into your project, follow
/ instructions described below.
/
/ 1. Setup the hardware. Attach a memory card socket to the any GPIO port
/    where you like. Select boot size at least 4KB for the boot loader with
/    BOOTSZ fuses and enable boot loader with BOOTRST fuse.
/
/ 2. Setup the software. Change the four port definitions in the asmfunc.S.
/    Change MCU_TARGET, BOOT_ADR and MCU_FREQ in the Makefile. The BOOT_ADR
/    is a BYTE address of boot section in the flash. Build the boot loader
/    and write it to the device with a programmer.
/
/ 3. Build the application program and output it in binary form instead of
/    hex format. Rename the file "app.bin" and put it into the memory card.
/
/-------------------------------------------------------------------------*/

#include "main.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <util/delay.h>
#include <string.h>
#include "pff.h"
#include "debug.h"

void dly_100us(void); // from asmfunc.S
static void start_app(void);
void flash_write_page(addr_t adr, const uint8_t* dat); // a wrapper
#if defined(AS_2NDARY_BOOTLOADER) && defined(SCAN_FOR_SPM_SEQUENCE)
void    flash_erase(DWORD flash_addr, addr_t seq_adr);
#define flash_erase_call(x) flash_erase(x, spm_seq_addr)
void    flash_write(DWORD flash_addr, const BYTE* data, addr_t seq_adr);
#define flash_write_call(x, y) flash_write(x, y, spm_seq_addr)
#else
void    flash_erase(DWORD flash_addr);
#define flash_erase_call(x) flash_erase(x)
void    flash_write(DWORD flash_addr, const BYTE* data);
#define flash_write_call(x, y) flash_write(x, y)
#endif

#ifdef SCAN_FOR_SPM_SEQUENCE
extern addr_t spm_seq_addr;
addr_t scan_for_spm(void);
addr_t try_scan_for_spm(void);
void call_spm(uint8_t csr, uint32_t seq_adr);
#else
void call_spm(uint8_t csr);
#endif

FATFS Fatfs;             // Petit-FatFs work area
BYTE  Buff[SPM_PAGESIZE]; // Page data buffer

#ifdef AS_2NDARY_BOOTLOADER
xjmp_t app_reset_vector;
#endif

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

#ifdef AS_2NDARY_BOOTLOADER
#ifdef VECTORS_USE_JMP
static xjmp_t make_jmp(addr_t x)
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
static xjmp_t make_rjmp(addr_t src, addr_t dst)
{
	addr_t delta = dst - src;
	uint16_t delta16 = (uint16_t)delta;
	delta16 >>= 1;
	delta16 &= 0x0FFF;
	xjmp_t res = 0xCFFF & ((delta16) | 0xC000));
}
#endif

static void check_reset_vector(void)
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
		#ifdef ENABLE_DEBUG
		uint16_t* tmp1p = (uint16_t*)&tmp1;
		uint16_t* tmp2p = (uint16_t*)&tmp2;
		dbg_printf("reset vector requires overwrite, read 0x%04X%04X, should be 0x%04X%04X\r\n", tmp2p[1], tmp2p[0], tmp1p[1], tmp1p[0]);
		#endif
		// this means existing flash will not activate the bootloader
		// so we force a rewrite of this vector
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		(*((xjmp_t*)Buff)) = tmp1;
		flash_write_page(0, Buff);
	}
}

#endif

static void LED_blink_pattern(uint16_t x)
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

static char can_jump(void)
{
	#ifdef AS_2NDARY_BOOTLOADER
	check_reset_vector();
	xjmp_t tmpx = pgm_read_xjmp(BOOT_ADR - sizeof(xjmp_t));
	// check if trampoline exists
	#ifdef VECTORS_USE_JMP
	if ((tmpx & 0xFFFF) == 0xFFFF || (tmpx & 0xFFFF) == 0x0000 || tmpx == make_jmp(BOOT_ADR)) {
		#ifdef ENABLE_DEBUG
		uint16_t* tmpxp = (uint16_t*)&tmpx;
		dbg_printf("trampoline missing, read 0x%04X%04X\r\n", tmpxp[1], tmpxp[0]);
		#endif
	#elif defined(VECTORS_USE_RJMP)
	if (tmpx == 0xFFFF || tmpx == 0x0000 || tmpx == make_rjmp(0, BOOT_ADR)) {
	#endif
		return 0;
	}
	#else
	// check if user app is blank
	uint16_t tmp16 = pgm_read_word_near(0);
	if (tmp16 == 0xFFFF || tmp16 == 0x0000) {
		dbg_printf("user app missing, read 0x%04X\r\n", tmp16);
		return 0;
	}
	#endif

	return 1;
}

void flash_write_page(addr_t adr, const uint8_t* dat)
{
	#ifdef FORCE_USE_LIBC_BOOT_FUNCS
	boot_spm_busy_wait();
	boot_page_erase(adr);
	boot_spm_busy_wait();
	#else
	flash_erase_call(adr);
	#endif
	#if defined(ENABLE_DEBUG) || defined(FORCE_USE_LIBC_BOOT_FUNCS)
	uint32_t i;
	uint16_t j;
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += 2, j += 2) {
		#ifdef ENABLE_DEBUG
		// validate that the erase worked
		uint16_t r = pgm_read_word_at(i);
		if (r != 0xFFFF) {
			dbg_printf("flash erase failed at 0x%04X%04X, data 0x%04X\r\n", ((uint16_t*)&i)[1], ((uint16_t*)&i)[0], r);
		}
		#endif
		#ifdef FORCE_USE_LIBC_BOOT_FUNCS
		boot_page_fill(i, *((uint16_t*)(&dat[j])));
		#endif
	}
	#endif
	#ifdef FORCE_USE_LIBC_BOOT_FUNCS
	boot_page_write(adr);
	boot_spm_busy_wait();
	boot_rww_enable();
	boot_spm_busy_wait();
	#else
	flash_write_call(adr, dat);
	#endif

	#ifdef ENABLE_DEBUG
	// validate every byte
	for (i = adr, j = 0; j < SPM_PAGESIZE; i += 2, j += 2) {
		uint16_t r, m;
		r = pgm_read_word_at(i);
		m = *((uint16_t*)(&dat[j]));
		if (r != m) {
			dbg_printf("flash verification failed at 0x%04X%04X, read 0x%04X, should be 0x%04X\r\n", ((uint16_t*)&i)[1], ((uint16_t*)&i)[0], r, m);
		}
	}
	#endif
}

int main (void)
{
	#ifdef ENABLE_DEBUG
	dbg_init();
	_delay_ms(100);
	#endif
	dbg_printf("\r\nUM2 SD Card Bootloader\r\n");
	#ifdef ENABLE_DEBUG
	dbg_printf("LFUSE 0x%02X, HFUSE 0x%02X\r\n", boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS), boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS));
	dbg_printf("EFUSE 0x%02X, LOCKBITS 0x%02X\r\n", boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS), boot_lock_fuse_bits_get(GET_LOCK_BITS));
	#endif

	DWORD fa; // Flash address
	WORD br;  // Bytes read
	DWORD bw; // Bytes written
	WORD i;   // Index for page difference check
	char canjump;

	CARDDETECT_DDRx &= ~_BV(CARDDETECT_BIT); // pin as input
	CARDDETECT_PORTx |= _BV(CARDDETECT_BIT); // enable internal pull-up resistor
	BUTTON_DDRx &= ~_BV(BUTTON_BIT); // pin as input
	BUTTON_PORTx |= _BV(BUTTON_BIT); // enable internal pull-up resistor

	#ifdef AS_2NDARY_BOOTLOADER
	char end_of_file = 0;

	char can_write = try_scan_for_spm() != 0;

	check_reset_vector();
	#endif

	canjump = can_jump();

	// prepare LED
	LED_DDRx |= _BV(LED_BIT); // pin as output
	LED_OFF();

	if (!can_write && canjump) {
		dbg_printf("can jump, but can't write to flash, launching app\r\n");
		start_app();
	}

	if (canjump)
	{
		#ifndef ENABLE_DEBUG
		dly_100us(); // only done to wait for signals to rise
		#endif

		if (!CARD_DETECTED()) {
			dbg_printf("card not detected\r\n");
			start_app();
		}

		if (!BUTTON_PRESSED()) {
			dbg_printf("button not pressed\r\n");
			start_app();
		}

		dbg_printf("can jump, almost primed\r\n");
	}
	else
	{
		dbg_printf("forced to boot from card\r\n");
	}

	FRESULT fres;
	#ifdef ENABLE_DEBUG
	fres = 
	#endif
	pf_mount(&Fatfs); // Initialize file system
	#ifdef ENABLE_DEBUG
	if (fres == FR_OK)
	{
	#endif
		fres = pf_open("APP.BIN");
		if (fres != FR_OK) // Open application file
		{
			dbg_printf("file failed to open, err 0x%02X\r\n", fres);
			start_app();
		}
	#ifdef ENABLE_DEBUG
	}
	else
	{
		dbg_printf("card mount failed, err 0x%02X\r\n", fres);
		start_app();
	}
	#endif

	LED_ON();

	// wait for button release
	#ifdef ENABLE_DEBUG
	if (canjump) {
		dbg_printf("waiting for button release...");
	}
	#endif
	while (BUTTON_PRESSED() && canjump) {
		// blink the LED while waiting
		LED_blink_pattern(0x10C);
	}
	#ifdef ENABLE_DEBUG
	if (canjump) {
		dbg_printf(" RELEASED!!\r\n");
	}
	#endif

	for (fa = 0, bw = 0; fa < BOOT_ADR; fa += SPM_PAGESIZE) // Update all application pages
	{
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		#ifdef AS_2NDARY_BOOTLOADER
		if (!end_of_file)
		#endif
		pf_read(Buff, SPM_PAGESIZE, &br); // Load a page data

		char to_write = 0;

		if (br > 0 // If data is available
		#ifdef AS_2NDARY_BOOTLOADER
		|| fa == (BOOT_ADR - SPM_PAGESIZE) // If is last page
		#endif
		)
		{
			#ifdef AS_2NDARY_BOOTLOADER
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
				dbg_printf("reset vector, old = 0x%08X , new = 0x%08X\r\n", app_reset_vector, (*((xjmp_t*)Buff)));
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
					dbg_printf("trampoline use JMP, addr 0x%08X, insn 0x%08X\r\n", fa, app_reset_vector);
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0x0000F000) == 0x0000C000) {
					// this is a RJMP instruction
					(*inst_ptr) = make_jmp((app_reset_vector & 0x0FFF) << 1);
					dbg_printf("trampoline RJMP converted to JMP, addr 0x%08X, RJMP 0x%04X, JMP 0x%08X\r\n", fa, app_reset_vector, (*inst_ptr));
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0xFFFF) == 0xFFFF || (app_reset_vector & 0xFFFF) == 0x0000) {
					(*inst_ptr) = make_jmp(BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
					dbg_printf("trampoline, no app, addr 0x%08X, JMP to boot 0x%08X\r\n", fa, (*inst_ptr));
				}
				#elif defined(VECTORS_USE_RJMP)
				if ((app_reset_vector & 0xF000) == 0xC000) {
					// this is a RJMP instruction
					addt_t dst = (app_reset_vector & 0x0FFF) << 1;
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), dst);
					dbg_printf("trampoline, addr 0x%08X, RJMP 0x%04X\r\n", fa, (*inst_ptr));
				}
				else if (app_reset_vector == 0xFFFF || app_reset_vector == 0x0000) {
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
					dbg_printf("trampoline, no app, addr 0x%08X, RJMP 0x%04X\r\n", fa, (*inst_ptr));
				}
				#endif
				else {
					// hmm... it wasn't a JMP or RJMP but it wasn't blank, we put it here and hope for the best
					(*inst_ptr) = app_reset_vector;
					dbg_printf("trampoline, unknown, addr 0x%08X, RJMP 0x%04X\r\n", fa, (*inst_ptr));
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
		#ifdef AS_2NDARY_BOOTLOADER
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
			dbg_printf("bytes written: %d\r\n", bw);
		}
	}

	if (bw > 0)
	{
		dbg_printf("all done\r\n");
		// triple blink the LED to indicate that new firmware written
		while (1) {
			LED_blink_pattern(0x402A);
		}
	}
	else
	{
		dbg_printf("all done, nothing written\r\n");
		// single blink the LED to indicate that nothing was actually written
		while (1) {
			LED_blink_pattern(0x4002);
		}
	}
}

static void start_app(void)
{
	char canjump = can_jump();

	#ifdef ENABLE_DEBUG
	if (!canjump) {
		dbg_printf("no app to start\r\n");
	}
	else {
		dbg_printf("starting app\r\n");
	}
	#endif

	// long blink to indicate blank app
	while (!canjump)
	{
		LED_blink_pattern(0x87FF);
	}

	dbg_deinit();

	#ifdef AS_2NDARY_BOOTLOADER
		// there is an instruction stored here, jump here and execute it
		#ifdef VECTORS_USE_JMP
			asm volatile("rjmp (__vectors - 4)");
		#elif defined(VECTORS_USE_RJMP)
			asm volatile("rjmp (__vectors - 2)");
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