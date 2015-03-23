#------------------------------------------------------------------
# Makefile for STK500v2 and SD card combo bootloader for Ultimaker2
#------------------------------------------------------------------
# Change these three defs for the target device

MCU_TARGET  = atmega2560
F_CPU       = 16000000
MCU_ARCH    = avr6
BOARD       = ULTIMAKER2

# Boot loader start address, as hex in bytes
BOOT_ADR     = 0x3E000

TARGET      = um2_cardboot
CSRC        = cardboot.c stk500boot.c pff.c mmcbbp.c
ASRC        = asmfunc.S
OPTIMIZE    = -Os -mcall-prologues -mrelax -ffunction-sections -fpack-struct -fshort-enums -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions
DEFS        = -DBOOT_ADR=$(BOOT_ADR) -DF_CPU=$(F_CPU) -D_BOARD_$(BOARD)_
LIBS        =
DEBUG       = dwarf-2

ifdef ENABLE_DEBUG
CSRC += debug.c
DEFS += -DENABLE_DEBUG
endif

ASFLAGS     = -Wa,-adhlns=$(<:.S=.lst),-gstabs $(DEFS)
ALL_ASFLAGS = -mmcu=$(MCU_TARGET) -I. -x assembler-with-cpp $(ASFLAGS)
CFLAGS      = -g$(DEBUG) -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) -std=gnu99 $(DEFS)
LDFLAGS     = -Wl,--relax,--gc-sections -Wl,-Map,$(TARGET).map -Wl,--section-start,.text=$(BOOT_ADR) -mrelax
OBJ         = $(CSRC:.c=.o) $(ASRC:.S=.o)

CC          = avr-gcc
LD          = avr-ld
AS          = avr-as
OBJCOPY     = avr-objcopy
OBJDUMP     = avr-objdump
SIZE        = avr-size

all:	$(TARGET).elf lst text size

$(TARGET).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean: cleanmerged cleanartifacts
	rm -rf $(TARGET).hex

cleanartifacts:
	rm -rf *.o $(TARGET).elf *.eps *.bak *.a
	rm -rf *.lst *.map $(EXTRA_CLEAN_FILES)

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf

lst:  $(TARGET).lst
%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

%.o: %.c $(OPTIONAL_REQUIRED)
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.S $(OPTIONAL_REQUIRED)
	$(CC) -c $(ALL_ASFLAGS) $< -o $@

text: $(TARGET).hex

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -j .fuse -O ihex $< $@

%.bin: %.hex
	$(OBJCOPY) -I ihex -O binary $< $@

%.disasm: %.hex
	$(OBJDUMP) -D -z --no-show-raw-insn --stop-address=0xFFFFFFFF -m $(MCU_ARCH) $< > $@

%.o: %.asm
	$(AS) -mmcu=$(MCU_TARGET) -mall-opcodes -W -o $@ $<

toolversion:
	make --version | tee -a $@.txt
	$(CC) --version | tee -a $@.txt
	$(LD) --version | tee -a $@.txt
	$(AS) --version | tee -a $@.txt

# The code below takes an existing firmware (for the Ultimaker2) and attaches the bootloader to it

UM2FW  ?= MarlinUltimaker2-15.02.1
BOOTFW ?= $(TARGET)

cleanmerged:
	rm -rf $(UM2FW)-cardboot.hex

allmerged: $(UM2FW)-cardboot.hex size

$(UM2FW)-cardboot.hex: $(UM2FW).hex $(BOOTFW).hex
	cat $^ | grep -v ":00000001FF" > $@

# make a assembly file from the disassembly, clean it up so the assembler doesn't see errors
%.reasm: %.disasm
	cat $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format ihex" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/^\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

# if a FW is to be loaded via SD card, it must be named "APP.BIN", all capitals

cleanappbin:
	rm -rf APP.BIN

appbin: APP.BIN

APP.BIN: $(UM2FW).bin
	mv $< $@

# avr-dude can be used to flash the bootloader

AVRDUDE   = avrdude
PROG_TOOL = STK500v2
PROG_PORT = COM6
PROG_BAUD = 115200
DUDE_OPTS =

flash: $(TARGET).hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTS) -Uflash:w:$<:i

flashmerged: $(UM2FW)-cardboot.hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTS) -Uflash:w:$<:i