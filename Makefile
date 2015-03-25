#------------------------------------------------------------------
# Makefile for STK500v2 and SD card combo bootloader for Ultimaker2
#------------------------------------------------------------------
# Warning, the make file is not perfectly automatic yet

MCU_TARGET  = atmega2560
F_CPU       = 16000000
MCU_ARCH    = avr6
BOARD       = ULTIMAKER2

# this should be placed at the beginning of the very last page
SPMFUNC_ADR ?= 0x3FF00

TARGET      = um2_cardboot
CSRC        = cardboot.c pff.c mmcbbp.c
ASRC        = asmfunc.S
ASMSRC      = spmfunc.asm
OPTIMIZE    = -Os -ffunction-sections -fpack-struct -fshort-enums -fno-move-loop-invariants -fno-inline-small-functions -mcall-prologues -mrelax -fno-tree-scev-cprop -fno-jump-tables
DEFS        = -DF_CPU=$(F_CPU) -D_BOARD_$(BOARD)_=1
LIBS        =
DEBUG       = dwarf-2

ifdef AS_SECONDARY_BOOTLOADER
DEFS += -DAS_SECONDARY_BOOTLOADER=1
DEFS += -DSPMFUNC_ADR=$(SPMFUNC_ADR)
BOOT_ADR ?= 0x3B000
else
CSRC += stk500boot.c
BOOT_ADR ?= 0x3E000
endif

ifdef ENABLE_DEBUG
CSRC += debug.c
DEFS += -DENABLE_DEBUG=1
endif

DEFS += -DBOOT_ADR=$(BOOT_ADR)

ASFLAGS     = -Wa,-adhlns=$(<:.S=.lst),-gstabs $(DEFS)
ALL_ASFLAGS = -mmcu=$(MCU_TARGET) -I. -x assembler-with-cpp $(ASFLAGS)
CFLAGS      = -g$(DEBUG) -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) -std=gnu99 $(DEFS)
LDFLAGS     = -Wl,--relax,--gc-sections -Wl,-Map,$(TARGET).map -Wl,--section-start,.text=$(BOOT_ADR) -mrelax
OBJ         = $(CSRC:.c=.o) $(ASRC:.S=.o) $(ASMSRC:.asm=.o)

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

cleanartifacts: cleanmergedartifacts
	rm -rf *.o $(TARGET).elf *.eps *.bak *.a
	rm -rf *.lst *.map $(EXTRA_CLEAN_FILES)

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf

lst:  $(TARGET).lst
%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@
%.lst: %.hex
	$(OBJDUMP) -D -z --stop-address=0xFFFFFFFF -m $(MCU_ARCH) $< > $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.S
	$(CC) -c $(ALL_ASFLAGS) $< -o $@

%.o: %.asm
	$(AS) -mmcu=$(MCU_TARGET) -mall-opcodes $(subst -D,--defsym ,$(DEFS)) -W -o $@ $<

%.elf: %.asm
	$(AS) -mmcu=$(MCU_TARGET) -mall-opcodes $(subst -D,--defsym ,$(DEFS)) -W -o $@ $<

text: $(TARGET).hex

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -j .fuse -j .bootloader -O ihex $< $@

%.bin: %.hex
	$(OBJCOPY) -I ihex -O binary $< $@

%.disasm: %.hex
	$(OBJDUMP) -D -z --no-show-raw-insn --stop-address=0xFFFFFFFF -m $(MCU_ARCH) $< > $@

# make a assembly file from the disassembly, clean it up so the assembler doesn't see errors
%.reasm: %.disasm
	cat $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/^\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

toolversion:
	make --version | tee -a $@.txt
	$(CC) --version | tee -a $@.txt
	$(LD) --version | tee -a $@.txt
	$(AS) --version | tee -a $@.txt

# The code below takes an existing firmware (for the Ultimaker2) and attaches the SD card bootloader to it
# This requires some swapping and insertion of jump instructions, which is performed with disassembled code
# There are some weird code here, mainly because makefiles can't do math and makefiles have stupid rules about parenthesis
# This implementation only works on MCUs that support JMP instructions

CURA_DIR   ?= "C:\Program Files (x86)\Cura"
UM2FW_DIR  ?= um2fw
UM2FW_NAME ?= MarlinUltimaker2
UM2FW_PATH ?= $(UM2FW_DIR)/$(UM2FW_NAME)
UM2FW = $(notdir $(UM2FW_PATH))
BOOTFW ?= $(TARGET)
BOOT_ADR_HEX = $(strip $(subst 0x,,$(BOOT_ADR)))
TRAMPOLINE_ADR = $(shell echo "$(BOOT_ADR)" | gawk --non-decimal-data '{ printf "0x%X\n", $$1 - 4 }')
RESET_FIND_JMP  = sed -n -r "s/^\s*?[0]+:\s+jmp\s+0x([0-9a-fA-F]+).*?$$/0x\U\1/p"
RESET_FIND_RJMP = sed -n -r "s/^\s*?[0]+:\s+rjmp\s+\.\+([0-9]+).*?$$/\1/p"

cleanmerged: cleanmergedartifacts
	rm -rf $(UM2FW)-cardboot.hex

cleanmergedartifacts:
	rm -rf $(UM2FW).disasm $(UM2FW).reasm $(UM2FW).asm trampoline.elf trampoline.hex trampoline.asm retargeted.hex retargeted.elf retargeted.asm finalmerge.hex spmfunc.lst spmfunc.hex spmfunc.elf spmfunc.o

allmerged: $(UM2FW)-cardboot.hex size

$(UM2FW)-cardboot.hex: finalmerge.hex finalmerge.lst
	mv $< $@

%.elf: %.o %.S
%.elf: %.o %.asm
	$(LD) -m $(MCU_ARCH) -nostartfiles -o $@ $<

# creates an assembly code file that only contains the trampoline jump to application, at the address just before the bootloader
# the make is invoked recursively to cause the if conditions to be re-evaluated
trampoline.asm: $(UM2FW).disasm
ifdef RECURSIVE_MAKE
ifneq ($(strip $(shell $(RESET_FIND_JMP) < $(UM2FW).disasm)),)
	@echo "JMP found: $(shell $(RESET_FIND_JMP) < $<)"
	@printf ".org $(TRAMPOLINE_ADR)\r\n\tjmp $(shell $(RESET_FIND_JMP) <$<)" > $@
endif
ifneq ($(strip $(shell $(RESET_FIND_RJMP) < $(UM2FW).disasm)),)
	@echo "RJMP found: $(shell $(RESET_FIND_RJMP) < $<)"
	@printf ".org $(TRAMPOLINE_ADR)\r\n\tjmp $(shell printf "0x%X" $(shell $(RESET_FIND_RJMP) <$<))" > $@
endif
else
	@echo "Recursive Make"
	make $@ AS_SECONDARY_BOOTLOADER=1 RECURSIVE_MAKE=1
endif

# replaces the old reset vector with a jump to the bootloader
# removes all .org because they cause avr-as to not work right if RJMPs are used
retargeted.asm: $(UM2FW).reasm
	sed -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x[0-9a-fA-F]+.*?$$/.org 0x0\r\n\tjmp 0x$(BOOT_ADR_HEX)\t; to bootloader/" < $< | \
	grep -v "\.org 0x" > $@

ifdef AS_SECONDARY_BOOTLOADER
# only the first line of retargeted.hex is still guaranteed to be correct, the rest comes from the original FW
# then the trampoline is added, but the trampoline hex has a lot of blanks which need to be removed
# then the bootloader is appended
# then the injected spm function is appended to the absolute end
# all occurrences of the "end of file" indicator is removed

TRAMPADR_HIGH_FIND   = echo $(TRAMPOLINE_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F])[0-9a-fA-F]{4}\s*?$$/\U\1/p"
TRAMPADR_LOW_FIND    = echo $(TRAMPOLINE_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F]{2})[0-9a-fA-F]{2}\s*?$$/\U\1/p"
SPMFUNCADR_HIGH_FIND = echo $(SPMFUNC_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F])[0-9a-fA-F]{4}\s*?$$/\U\1/p"
SPMFUNCADR_LOW_FIND  = echo $(SPMFUNC_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F]{2})[0-9a-fA-F]{2}\s*?$$/\U\1/p"

finalmerge.hex: retargeted.hex $(UM2FW_PATH).hex trampoline.hex $(BOOTFW).hex spmfunc.hex
	head -n 1 retargeted.hex > $@
	tail -n +2 $(UM2FW_PATH).hex | grep -v ":00000001FF" >> $@
	cat trampoline.hex | grep -E "^(:02000002$(shell $(TRAMPADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:10$(shell $(TRAMPADR_LOW_FIND)))" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@
	cat $(BOOTFW).hex | grep -v ":00000001FF" >> $@
	cat spmfunc.hex | grep -E "^(:02000002$(shell $(SPMFUNCADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:10$(shell $(SPMFUNCADR_LOW_FIND)))" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@
else
# no special processing, except the "end of file" indicator is removed
finalmerge.hex: $(UM2FW_PATH).hex $(BOOTFW).hex
	cat $^ | grep -v ":00000001FF" > $@
endif

# if a FW is to be loaded via SD card, it must be named "APP.BIN", all capitals

cleanappbin:
	rm -rf APP.BIN

appbin: APP.BIN

APP.BIN: $(UM2FW).bin
	mv $< $@

# bootloader installation procedure

PROG_PORT ?= AUTO

ifdef AS_SECONDARY_BOOTLOADER
# installation must be done through a Python script that skips the original bootloader memory

flash: $(TARGET).hex
flashmerged: $(UM2FW)-cardboot.hex
install: $(UM2FW)-cardboot.hex
	$(CURA_DIR)/python/python.exe ./installer/stk500v2.py $(PROG_PORT) $<

else
# avrdude can be used to flash the bootloader

AVRDUDE   = avrdude
PROG_PORT = COM6
PROG_TOOL = STK500v2
PROG_BAUD = 115200
DUDE_OPTS =
# the -D option might be required if using the default Arduino Mega 2560 stock bootloader

flash: $(TARGET).hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTS) -Uflash:w:$<:i

flashmerged: $(UM2FW)-cardboot.hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTS) -Uflash:w:$<:i

endif

include build_helper.mk