/*****************************************************************************
Title:     STK500v2 compatible bootloader
           Modified for Wiring board ATMega128-16MHz
Author:    Peter Fleury <pfleury@gmx.ch>   http://jump.to/fleury
File:      $Id: stk500boot.c,v 1.11 2006/06/25 12:39:17 peter Exp $
Compiler:  avr-gcc 3.4.5 or 4.1 / avr-libc 1.4.3
Hardware:  All AVRs with bootloader support, tested with ATmega8
License:   GNU General Public License

Modified:   Frank26080115
Date:       21 March 2015
Descrption: Merged with SD card bootloader

Modified:  Worapoht Kornkaewwattanakul <dev@avride.com>   http://www.avride.com
Date:      17 October 2007
Update:    1st, 29 Dec 2007 : Enable CMD_SPI_MULTI but ignore unused command by return 0x00 byte response..
Compiler:  WINAVR20060421
Description: add timeout feature like previous Wiring bootloader

DESCRIPTION:
    This program allows an AVR with bootloader capabilities to
    read/write its own Flash/EEprom. To enter Programming mode
    an input pin is checked. If this pin is pulled low, programming mode
    is entered. If not, normal execution is done from $0000
    "reset" vector in Application area.
    Size fits into a 1024 word bootloader section
    when compiled with avr-gcc 4.1
    (direct replace on Wiring Board without fuse setting changed)

USAGE:
    - Set AVR MCU type and clock-frequency (F_CPU) in the Makefile.
    - Set baud rate below (AVRISP only works with 115200 bps)
    - compile/link the bootloader with the supplied Makefile
    - program the "Boot Flash section size" (BOOTSZ fuses),
      for boot-size 1024 words:  program BOOTSZ01
    - enable the BOOT Reset Vector (program BOOTRST)
    - Upload the hex file to the AVR using any ISP programmer
    - Program Boot Lock Mode 3 (program BootLock 11 and BootLock 12 lock bits) // (leave them)
    - Reset your AVR while keeping PROG_PIN pulled low // (for enter bootloader by switch)
    - Start AVRISP Programmer (AVRStudio/Tools/Program AVR)
    - AVRISP will detect the bootloader
    - Program your application FLASH file and optional EEPROM file using AVRISP

Note:
    Erasing the device without flashing, through AVRISP GUI button "Erase Device"
    is not implemented, due to AVRStudio limitations.
    Flash is always erased before programming.

	AVRdude:
	Please uncomment #define REMOVE_CMD_SPI_MULTI when using AVRdude.
	Comment #define REMOVE_PROGRAM_LOCK_BIT_SUPPORT to reduce code size
	Read Fuse Bits and Read/Write Lock Bits is not supported

NOTES:
    Based on Atmel Application Note AVR109 - Self-programming
    Based on Atmel Application Note AVR068 - STK500v2 Protocol

LICENSE:
    Copyright (C) 2006 Peter Fleury

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*****************************************************************************/

//************************************************************************
//*	Edit History
//************************************************************************
//*	Jul  7,	2010	<MLS> = Mark Sproul msproul@skycharoit.com
//*	Jul  7,	2010	<MLS> Working on mega2560. No Auto-restart
//*	Jul  7,	2010	<MLS> Switched to 8K bytes (4K words) so that we have room for the monitor
//*	Jul  8,	2010	<MLS> Found older version of source that had auto restart, put that code back in
//*	Jul  8,	2010	<MLS> Adding monitor code
//*	Jul 11,	2010	<MLS> Added blinking LED while waiting for download to start
//*	Jul 11,	2010	<MLS> Added EEPROM test
//*	Jul 29,	2010	<MLS> Added ser_readch_timeout for timing out on bootloading
//*	Aug 23,	2010	<MLS> Added support for atmega2561
//*	Aug 26,	2010	<MLS> Removed support for BOOT_BY_SWITCH
//*	Sep  8,	2010	<MLS> Added support for atmega16
//*	Nov  9,	2010	<MLS> Issue 392:Fixed bug that 3 !!! in code would cause it to jump to monitor
//*	Jun 24,	2011	<MLS> Removed analogRead (was not used)
//*	Dec 29,	2011	<MLS> Issue 181: added watch dog timmer support
//*	Dec 29,	2011	<MLS> Issue 505:  bootloader is comparing the seqNum to 1 or the current sequence 
//*	Jan  1,	2012	<MLS> Issue 543: CMD_CHIP_ERASE_ISP now returns STATUS_CMD_FAILED instead of STATUS_CMD_OK
//*	Jan  1,	2012	<MLS> Issue 543: Write EEPROM now does something (NOT TESTED)
//*	Jan  1,	2012	<MLS> Issue 544: stk500v2 bootloader doesn't support reading fuses
//*	Mar 21,	2015	<F26> Frank26080115 merged with SD card bootloader
//************************************************************************

#include	<inttypes.h>
#include	<avr/io.h>
#include	<avr/interrupt.h>
#include	<avr/boot.h>
#include	<avr/pgmspace.h>
#include	<avr/wdt.h>
#include	<avr/sfr_defs.h>
#include	<util/delay.h>
#include	<avr/eeprom.h>
#include	<avr/common.h>
#include	<stdlib.h>
#include	"stk500v2_cmd.h"
#include	"main.h"
#include	"debug.h"

#ifndef EEWE
	#define EEWE    1
#endif
#ifndef EEMWE
	#define EEMWE   2
#endif

/*
 * Uncomment the following lines to save code space
 */
#define	REMOVE_PROGRAM_LOCK_BIT_SUPPORT		// disable program lock bits
//#define	REMOVE_BOOTLOADER_LED				// no LED to show active bootloader
//#define	REMOVE_CMD_SPI_MULTI				// disable processing of SPI_MULTI commands, Remark this line for AVRDUDE <Worapoht>
//

/*
 * HW and SW version, reported to AVRISP, must match version of AVRStudio
 */
#define CONFIG_PARAM_BUILD_NUMBER_LOW	0
#define CONFIG_PARAM_BUILD_NUMBER_HIGH	0
#define CONFIG_PARAM_HW_VER				0x0F
#define CONFIG_PARAM_SW_MAJOR			3
#define CONFIG_PARAM_SW_MINOR			0x01

/*
 * Signature bytes are not available in avr-gcc io_xxx.h
 */
#if defined (__AVR_ATmega2560__)
	#define SIGNATURE_BYTES 0x1E9801
#elif defined (__AVR_ATmega2561__)
	#define SIGNATURE_BYTES 0x1e9802
#else
	#error "no signature definition for MCU available"
#endif

/*
 * States used in the receive state machine
 */
#define	ST_START		0
#define	ST_GET_SEQ_NUM	1
#define ST_MSG_SIZE_1	2
#define ST_MSG_SIZE_2	3
#define ST_GET_TOKEN	4
#define ST_GET_DATA		5
#define	ST_GET_CHECK	6
#define	ST_PROCESS		7

/*
This fixes the problem of requiring -D with avrdude
*/
static char chipEraseRequested;
static addr_t eraseAddress;
static void chip_erase_task(void)
{
	if (chipEraseRequested)
	{
		if (eraseAddress < APP_END)
		{
			if (boot_rww_busy() == 0) {
				boot_page_erase(eraseAddress);
				eraseAddress += SPM_PAGESIZE;
			}
		}
		else
		{
			if (boot_rww_busy() == 0) {
				eraseAddress = 0;
				chipEraseRequested = 0;
			}
		}
	}
}

uint32_t verify_checksum = 0;

#ifdef USE_BUFFERED_SERIAL

#define SERBUF_SIZE 128
volatile uint8_t serbuf_read_ptr = 0;
volatile uint8_t serbuf_write_ptr = 0;
volatile uint8_t serbuf[SERBUF_SIZE];

ISR(USARTn_RX_vect)
{
	uint8_t c = UDRn;
	uint8_t next_ptr = (serbuf_write_ptr + 1) % SERBUF_SIZE;
	if (next_ptr != serbuf_read_ptr) {
		serbuf[serbuf_write_ptr] = c;
		serbuf_write_ptr = next_ptr;
	}
}

uint8_t ser_avail(void)
{
	cli();
	/*
	volatile uint16_t w = serbuf_write_ptr;
	w += SERBUF_SIZE;
	w -= serbuf_read_ptr;
	w %= SERBUF_SIZE;
	//*/
	char w = (serbuf_write_ptr != serbuf_read_ptr);
	sei();
	return (uint8_t)w;
}

#endif

//*****************************************************************************
void ser_putch(unsigned char c)
{
	#ifndef DISABLE_BLINK
	LED_TOG();
	#else
	LED_ON();
	#endif
	// wait for TX to finish
	while (bit_is_clear(UCSRnA, UDREn)) {
		chip_erase_task();
	}
	UDRn = c;
}

unsigned char ser_readch(void)
{
	#ifndef DISABLE_BLINK
	LED_OFF();
	#endif
	#ifdef _FIX_ISSUE_181_
	wdt_enable(WDTO_4S);
	wdt_reset();
	#endif
	while (ser_avail() == 0) {
		chip_erase_task();
	}
	#ifdef _FIX_ISSUE_181_
	wdt_reset();
	wdt_disable();
	#endif
	LED_ON();
	#ifdef USE_BUFFERED_SERIAL
	cli();
	uint8_t r = serbuf[serbuf_read_ptr];
	serbuf_read_ptr = (serbuf_read_ptr + 1) % SERBUF_SIZE;
	sei();
	return r;
	#else
	return UDRn;
	#endif
}

//*****************************************************************************
int main(void)
{
	addr_t			address			=	0;
	unsigned char	msgParseState;
	unsigned int	ii				=	0;
	unsigned char	checksum		=	0;
	unsigned char	seqNum			=	0;
	unsigned int	msgLength		=	0;
	unsigned char*	msgBuffer = (unsigned char*)master_buffer;
	unsigned char	c;
	unsigned char*	p;
	unsigned char	isLeave = 0;

	unsigned long	boot_timeout;
	unsigned long	boot_timer;
	unsigned int	boot_state;

	#ifdef _FIX_STACK_POINTER_2_
	//	some chips dont set the stack properly
	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_HI_ADDR) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16"  :: "i" (AVR_STACK_POINTER_LO_ADDR) );
	#endif

#ifdef _FIX_ISSUE_181_
	//************************************************************************
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
	//************************************************************************
#endif

	chipEraseRequested = 0;
	eraseAddress = 0;

	if (can_jump()) {
		boot_timeout	=	600;
	}
	else {
		boot_timeout	=	0xFFFF;
	}

	/*
	 * Init UART
	 * set baudrate and enable USART receiver and transmitter without interrupts
	 */
	UCSRnA = _BV(U2Xn);
	UBRRn  = 16; // 115200 baud
	UCSRnC = _BV(USBSn) | _BV(UCSZn1) | _BV(UCSZn0);
	UCSRnB = _BV(TXENn) | _BV(RXENn)
	#ifdef USE_BUFFERED_SERIAL
		| _BV(RXCIEn)
	#endif
	;
	#ifdef USE_BUFFERED_SERIAL
	serbuf_read_ptr = 0; serbuf_write_ptr = 0;
	MCUCR = (1 << IVCE);
	MCUCR = (1 << IVSEL);
	sei();
	#endif

#ifndef REMOVE_BOOTLOADER_LED
	LED_DDRx	|=	_BV(LED_BIT);
	LED_OFF();

#ifdef _DEBUG_WITH_LEDS_
	for (ii=0; ii<3; ii++)
	{
		LED_OFF();
		_delay_ms(100);
		LED_ON();
		_delay_ms(100);
	}
#endif

#endif

	dbg_printf("\nUM2 Combo Bootloader\n");

	sd_card_boot(); // this will never return if activated

	dbg_printf("STK500v2\n");

	#ifdef DISABLE_STK500V2
	while (1);
	#endif

	boot_timer	=	0;
	boot_state	=	0;

	while (boot_state == 0)
	{
		while (ser_avail() == 0 && (boot_state == 0)) // wait for data
		{
			_delay_ms(1);
			boot_timer++;
			if (boot_timer > boot_timeout)
			{
				boot_state	=	1;
			}
		}
		boot_state++; // ( if boot_state=1 bootloader received byte from UART, enter bootloader mode)
	}

	if (boot_state == 1)
	{
		c = ser_readch();

		while (!isLeave)
		{
			LED_ON();

			/*
			 * Collect received bytes to a complete message
			 */
			msgParseState = ST_START;
			while ( msgParseState != ST_PROCESS )
			{
				if (boot_state == 1)
				{
					boot_state = 0;
				}
				else
				{
					c	=	ser_readch();
				}

				switch (msgParseState)
				{
					case ST_START:
						if ( c == MESSAGE_START )
						{
							msgParseState	=	ST_GET_SEQ_NUM;
							checksum		=	MESSAGE_START^0;
						}
						break;

					case ST_GET_SEQ_NUM:
					#ifdef _FIX_ISSUE_505_
						seqNum			=	c;
						msgParseState	=	ST_MSG_SIZE_1;
						checksum		^=	c;
					#else
						if ( (c == 1) || (c == seqNum) )
						{
							seqNum			=	c;
							msgParseState	=	ST_MSG_SIZE_1;
							checksum		^=	c;
						}
						else
						{
							msgParseState	=	ST_START;
						}
					#endif
						break;

					case ST_MSG_SIZE_1:
						msgLength		=	c << 8;
						msgParseState	=	ST_MSG_SIZE_2;
						checksum		^=	c;
						break;

					case ST_MSG_SIZE_2:
						msgLength		|=	c;
						msgParseState	=	ST_GET_TOKEN;
						checksum		^=	c;
						break;

					case ST_GET_TOKEN:
						if ( c == TOKEN )
						{
							msgParseState	=	ST_GET_DATA;
							checksum		^=	c;
							ii				=	0;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;

					case ST_GET_DATA:
						msgBuffer[ii++]	=	c;
						checksum		^=	c;
						if (ii >= msgLength )
						{
							msgParseState	=	ST_GET_CHECK;
						}
						break;

					case ST_GET_CHECK:
						if ( c == checksum )
						{
							msgParseState	=	ST_PROCESS;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;
				}	//	switch

				chip_erase_task();

			}	//	while(msgParseState)

			dbg_printf("rx'ed msg len %d cmd 0x%02X\n", msgLength, msgBuffer[0]);

			/*
			 * Now process the STK500 commands, see Atmel Appnote AVR068
			 */

			switch (msgBuffer[0])
			{
	#ifndef REMOVE_CMD_SPI_MULTI
				case CMD_SPI_MULTI:
					{
						unsigned char answerByte;
						unsigned char flag=0;

						if ( msgBuffer[4]== 0x30 )
						{
							unsigned char signatureIndex	=	msgBuffer[6];

							if ( signatureIndex == 0 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 16) & 0x000000FF;
							}
							else if ( signatureIndex == 1 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
							}
							else
							{
								answerByte	=	SIGNATURE_BYTES & 0x000000FF;
							}
						}
						else if ( msgBuffer[4] & 0x50 )
						{
						//*	Issue 544: 	stk500v2 bootloader doesn't support reading fuses
						//*	I cant find the docs that say what these are supposed to be but this was figured out by trial and error
						//	answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
							if (msgBuffer[4] == 0x50)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
							}
							else if (msgBuffer[4] == 0x58)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
							}
							else
							{
								answerByte	=	0;
							}
						}
						else
						{
							answerByte	=	0; // for all others command are not implemented, return dummy value for AVRDUDE happy <Worapoht>
						}
						if ( !flag )
						{
							msgLength		=	7;
							msgBuffer[1]	=	STATUS_CMD_OK;
							msgBuffer[2]	=	0;
							msgBuffer[3]	=	msgBuffer[4];
							msgBuffer[4]	=	0;
							msgBuffer[5]	=	answerByte;
							msgBuffer[6]	=	STATUS_CMD_OK;
						}
					}
					break;
	#endif
				case CMD_SIGN_ON:
					msgLength		=	11;
					msgBuffer[1] 	=	STATUS_CMD_OK;
					msgBuffer[2] 	=	8;
					msgBuffer[3] 	=	'A';
					msgBuffer[4] 	=	'V';
					msgBuffer[5] 	=	'R';
					msgBuffer[6] 	=	'I';
					msgBuffer[7] 	=	'S';
					msgBuffer[8] 	=	'P';
					msgBuffer[9] 	=	'_';
					msgBuffer[10]	=	'2';
					break;

				case CMD_GET_PARAMETER:
					{
						unsigned char value;

						switch(msgBuffer[1])
						{
						case PARAM_BUILD_NUMBER_LOW:
							value	=	CONFIG_PARAM_BUILD_NUMBER_LOW;
							break;
						case PARAM_BUILD_NUMBER_HIGH:
							value	=	CONFIG_PARAM_BUILD_NUMBER_HIGH;
							break;
						case PARAM_HW_VER:
							value	=	CONFIG_PARAM_HW_VER;
							break;
						case PARAM_SW_MAJOR:
							value	=	CONFIG_PARAM_SW_MAJOR;
							break;
						case PARAM_SW_MINOR:
							value	=	CONFIG_PARAM_SW_MINOR;
							break;
						default:
							value	=	0;
							break;
						}
						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	value;
					}
					break;

				case CMD_LEAVE_PROGMODE_ISP:
					dbg_printf("leaving\n");
					isLeave	=	1;
					//*	fall thru

				case CMD_SET_PARAMETER:
				case CMD_ENTER_PROGMODE_ISP:
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					break;

				case CMD_READ_SIGNATURE_ISP:
					{
						unsigned char signatureIndex	=	msgBuffer[4];
						unsigned char signature;

						if ( signatureIndex == 0 )
							signature	=	(SIGNATURE_BYTES >>16) & 0x000000FF;
						else if ( signatureIndex == 1 )
							signature	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
						else
							signature	=	SIGNATURE_BYTES & 0x000000FF;

						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	signature;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

				case CMD_READ_LOCK_ISP:
					msgLength		=	4;
					msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[2]	=	boot_lock_fuse_bits_get( GET_LOCK_BITS );
					msgBuffer[3]	=	STATUS_CMD_OK;
					break;

				case CMD_READ_FUSE_ISP:
					{
						unsigned char fuseBits;

						if ( msgBuffer[2] == 0x50 )
						{
							if ( msgBuffer[3] == 0x08 )
								fuseBits	=	boot_lock_fuse_bits_get( GET_EXTENDED_FUSE_BITS );
							else
								fuseBits	=	boot_lock_fuse_bits_get( GET_LOW_FUSE_BITS );
						}
						else
						{
							fuseBits	=	boot_lock_fuse_bits_get( GET_HIGH_FUSE_BITS );
						}
						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	fuseBits;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

	#ifndef REMOVE_PROGRAM_LOCK_BIT_SUPPORT
				case CMD_PROGRAM_LOCK_ISP:
					{
						unsigned char lockBits	=	msgBuffer[4];

						lockBits	=	(~lockBits) & 0x3C;	// mask BLBxx bits
						boot_lock_bits_set(lockBits);		// and program it
						boot_spm_busy_wait();

						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	STATUS_CMD_OK;
					}
					break;
	#endif
				case CMD_CHIP_ERASE_ISP:
					eraseAddress	=	0;
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					chipEraseRequested = 1;
					break;

				case CMD_LOAD_ADDRESS:
	#if defined(RAMPZ)
					address	=	( ((addr_t)(msgBuffer[1])<<24)|((addr_t)(msgBuffer[2])<<16)|((addr_t)(msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;
	#else
					address	=	( ((msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;		//convert word to byte address
	#endif
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					break;

				case CMD_PROGRAM_FLASH_ISP:
					if (chipEraseRequested) {
						boot_spm_busy_wait();
					}
					chipEraseRequested = 0;
				case CMD_PROGRAM_EEPROM_ISP:
					{
						uint16_t		size		=	((msgBuffer[1])<<8) | msgBuffer[2];
						uint16_t		actualSize	=	msgLength - 10; // Ultimaker's stupid python doesn't send the whole page, the message length is the truly reliable indicator of how many bytes, this is important for checksumming
						unsigned char	*p	=	msgBuffer+10;
						unsigned int	data;
						unsigned char	highByte, lowByte;
						addr_t			tempaddress	=	address;

						if ( msgBuffer[0] == CMD_PROGRAM_FLASH_ISP )
						{
							// erase only main section (bootloader protection)
							if (eraseAddress < APP_END
							#ifdef SPMFUNC_ADR
							 || eraseAddress >= SPMFUNC_ADR
							#endif
							)
							{
								boot_page_erase(eraseAddress);	// Perform page erase
								boot_spm_busy_wait();			// Wait until the memory is erased.
							}
							eraseAddress += SPM_PAGESIZE;	// point to next page to be erase

							if (address == 0) {
								verify_checksum = 0;
							}

							if (address < APP_END
							#ifdef SPMFUNC_ADR
							 || address >= SPMFUNC_ADR
							#endif
							)
							{
								/* Write FLASH */
								do {
									if (actualSize > 0)
									{
										lowByte  = *p++;
										highByte = *p++;
										verify_checksum += lowByte;
										verify_checksum += highByte;
										actualSize -= 2;
										data = (highByte << 8) | lowByte;
									}
									else
									{
										data = 0xFFFF;
									}

									boot_page_fill(address,data);

									address	=	address + 2;	// Select next word in memory
									size	-=	2;				// Reduce number of bytes to write by two
								} while (size);					// Loop until all bytes written

								boot_page_write(tempaddress);
								boot_spm_busy_wait();
								boot_rww_enable();				// Re-enable the RWW section
							}
						}
						else
						{
						//*	issue 543, this should work, It has not been tested.
					//	#if (!defined(__AVR_ATmega1280__) && !defined(__AVR_ATmega2560__)  && !defined(__AVR_ATmega2561__)  && !defined(__AVR_ATmega1284P__)  && !defined(__AVR_ATmega640__))
						#if (defined(EEARL) && defined(EEARH)  && defined(EEMWE)  && defined(EEWE)  && defined(EEDR))
							/* write EEPROM */
							do {
								EEARL	=	address;			// Setup EEPROM address
								EEARH	=	(address >> 8);
								address++;						// Select next EEPROM byte

								EEDR	=	*p++;				// get byte from buffer
								EECR	|=	(1<<EEMWE);			// Write data into EEPROM
								EECR	|=	(1<<EEWE);

								while (EECR & (1<<EEWE));	// Wait for write operation to finish
								size--;						// Decrease number of bytes to write
							} while (size);					// Loop until all bytes written
						#endif
						}
						msgLength	=	2;
						msgBuffer[1]	=	STATUS_CMD_OK;
					}
					break;

				case CMD_READ_FLASH_ISP:
				case CMD_READ_EEPROM_ISP:
					{
						unsigned int	size	=	((msgBuffer[1])<<8) | msgBuffer[2];
						unsigned char	*p		=	msgBuffer+1;
						msgLength				=	size+3;

						*p++	=	STATUS_CMD_OK;
						if (msgBuffer[0] == CMD_READ_FLASH_ISP )
						{
							unsigned int data;

							// Read FLASH
							do {
								data	=	pgm_read_word_at(address);
								*p++	=	(unsigned char)data;		//LSB
								*p++	=	(unsigned char)(data >> 8);	//MSB
								address	+=	2;							// Select next word in memory
								size	-=	2;
							}while (size);
						}
						else
						{
							/* Read EEPROM */
							do {
								EEARL	=	address;			// Setup EEPROM address
								EEARH	=	((address >> 8));
								address++;					// Select next EEPROM byte
								EECR	|=	(1<<EERE);			// Read EEPROM
								*p++	=	EEDR;				// Send EEPROM data
								size--;
							} while (size);
						}
						*p++	=	STATUS_CMD_OK;
					}
					break;

				#ifdef SUPPORT_CHECKSUM
				case 0xEE: // checksum function is available
					msgLength		=	4;
					msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[2]	=	((uint8_t*)&verify_checksum)[0];
					msgBuffer[3]	=	((uint8_t*)&verify_checksum)[1];
					break;
				#endif

				default:
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_FAILED;
					break;
			}

			/*
			 * Now send answer message back
			 */
			ser_putch(MESSAGE_START);
			checksum	=	MESSAGE_START^0;

			ser_putch(seqNum);
			checksum	^=	seqNum;

			c			=	((msgLength>>8)&0xFF);
			ser_putch(c);
			checksum	^=	c;

			c			=	msgLength&0x00FF;
			ser_putch(c);
			checksum ^= c;

			ser_putch(TOKEN);
			checksum ^= TOKEN;

			p	=	msgBuffer;
			while ( msgLength )
			{
				c	=	*p++;
				ser_putch(c);
				checksum ^=c;
				msgLength--;
				chip_erase_task();
			}
			ser_putch(checksum);
			seqNum++;

		#if !defined(REMOVE_BOOTLOADER_LED) && !defined(DISABLE_BLINK)
		#ifndef DISABLE_BLINK
			LED_TOG();
		#else
			LED_ON();
		#endif
		#endif

			chip_erase_task();

		}
	}

	dbg_printf("timeout init\n");

	app_start();
}
