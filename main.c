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

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <string.h>
#include "pff.h"

#define addr_t int32_t
#ifdef __AVR_MEGA__
#define VECTORS_USE_JMP
#define xjmp_t uint32_t
#else
#define VECTORS_USE_RJMP
#define xjmp_t uint16_t
#endif

void flash_erase (DWORD);              //   Erase a flash page (asmfunc.S)
void flash_write (DWORD, const BYTE*); // Program a flash page (asmfunc.S)

FATFS Fatfs;             // Petit-FatFs work area
BYTE Buff[SPM_PAGESIZE]; // Page data buffer

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

static void start_app(void);
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
	return y;
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
	xjmp_t tmp;
	#ifdef VECTORS_USE_JMP
	tmp = make_jmp(BOOT_ADR);
	if (pgm_read_dword(0) != tmp)
	#elif defined(VECTORS_USE_RJMP)
	tmp = make_rjmp(0, BOOT_ADR);
	if (pgm_read_word(0) != tmp)
	#endif
	{
		// this means existing flash will not activate the bootloader
		// so we force a rewrite of this vector
		memset(Buff, 0xFF, SPM_PAGESIZE); // Clear buffer
		(*((xjmp_t*)Buff)) = tmp;
		flash_erase(0);
		flash_write(0, Buff);
	}
}

#endif

static void LED_blink_pattern(uint16_t x)
{
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

int main (void)
{
	DWORD fa; // Flash address
	WORD br; // Bytes read
	DWORD bw; // Bytes written
	WORD i; // Index for page difference check

	#ifdef AS_2NDARY_BOOTLOADER
	char end_of_file = 0;

	check_reset_vector();
	#endif

	// prepare LED
	LED_DDRx |= _BV(LED_BIT); // pin as output
	LED_OFF();

	// check for card
	CARDDETECT_DDRx &= ~_BV(CARDDETECT_BIT); // pin as input
	CARDDETECT_PORTx |= _BV(CARDDETECT_BIT); // enable internal pull-up resistor
	if (!CARD_DETECTED()) {
		start_app();
	}

	// check for button held
	BUTTON_DDRx &= ~_BV(BUTTON_BIT);
	BUTTON_PORTx |= _BV(BUTTON_BIT);
	if (!BUTTON_PRESSED()) {
		start_app();
	}

	pf_mount(&Fatfs); // Initialize file system
	if (pf_open("app.bin") != FR_OK) // Open application file
	{
		// File failed to open
		start_app();
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
				app_reset_vector = (*((xjmp_t*)Buff));
				(*((xjmp_t*)Buff)) = 
				#ifdef VECTORS_USE_JMP
					make_jmp(BOOT_ADR);
				#elif defined(VECTORS_USE_RJMP)
					make_rjmp(0, BOOT_ADR);
				#endif
				if (br <= 0) br += sizeof(xjmp_t);
			}
			else if (fa == (BOOT_ADR - SPM_PAGESIZE))
			{
				xjmp_t* inst_ptr = ((xjmp_t*)(&Buff[SPM_PAGESIZE-sizeof(xjmp_t)]));
				#ifdef VECTORS_USE_JMP
				if ((app_reset_vector & 0xFE0E0000) == 0x940C0000) {
					// this is a JMP instruction, we can put it here without changing it
					(*inst_ptr) = app_reset_vector;
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0x0000F000) == 0x0000C000) {
					// this is a RJMP instruction
					(*inst_ptr) = make_jmp((app_reset_vector & 0x0FFF) << 1);
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
				else if ((app_reset_vector & 0xFFFF) == 0xFFFF || (app_reset_vector & 0xFFFF) == 0x0000) {
					(*inst_ptr) = make_jmp(BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
				}
				#elif defined(VECTORS_USE_RJMP)
				if ((app_reset_vector & 0xF000) == 0xC000) {
					// this is a RJMP instruction
					addt_t dst = (app_reset_vector & 0x0FFF) << 1;
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), dst);
				}
				else if (app_reset_vector == 0xFFFF || app_reset_vector == 0x0000) {
					(*inst_ptr) = make_rjmp(BOOT_ADR - sizeof(xjmp_t), BOOT_ADR); // if app doesn't exist, make it loop back into the bootloader
				}
				#endif
				else {
					// hmm... it wasn't a JMP or RJMP but it wasn't blank, we put it here and hope for the best
					(*inst_ptr) = app_reset_vector;
					if (br <= 0) br += sizeof(xjmp_t); // indicate that we wrote something useful
				}
			}
			#endif

			for (i = 0; i < SPM_PAGESIZE && to_write == 0; i++)
			{ // check if the page has differences
				if (pgm_read_byte(i) != Buff[i]) {
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
			flash_erase(fa);
			flash_write(fa, Buff);
			bw += br;
		}
	}

	if (bw > 0)
	{
		// triple blink the LED to indicate that new firmware written
		while (1) {
			LED_blink_pattern(0x402A);
		}
	}
	else
	{
		// single blink the LED to indicate that nothing was actually written
		while (1) {
			LED_blink_pattern(0x4002);
		}
	}
}

static void start_app(void)
{
	char can_jump = 1;

	#ifdef AS_2NDARY_BOOTLOADER
	check_reset_vector();
	xjmp_t tmpx;
	#ifdef VECTORS_USE_JMP
	tmpx = pgm_read_dword(BOOT_ADR - sizeof(xjmp_t));
	if ((tmpx & 0xFFFF) == 0xFFFF || (tmpx & 0xFFFF) == 0x0000 || tmpx == make_jmp(BOOT_ADR))
	#elif defined(VECTORS_USE_RJMP)
	tmpx = pgm_read_word(BOOT_ADR - sizeof(xjmp_t));
	if (tmpx == 0xFFFF || tmpx == 0x0000 || tmpx == make_rjmp(0, BOOT_ADR))
	#endif
	{
		// jump into user app is missing
		can_jump = 0;
	}
	#else
	uint16_t tmp16 = pgm_read_word(0);
	if (tmp16 == 0xFFFF || tmp16 == 0x0000) {
		can_jump = 0;
	}
	#endif

	// long blink to indicate blank app
	while (!can_jump)
	{
		LED_blink_pattern(0x87FF);
	}

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