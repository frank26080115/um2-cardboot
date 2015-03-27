#------------------------------------------------------------------
# Makefile for STK500v2 and SD card combo bootloader for Ultimaker2
# https://github.com/frank26080115/um2-cardboot
#------------------------------------------------------------------

USER_CURA_INSTALLATION_PATH ?= ./Cura
USER_DESIRED_UM2_FW_NAME    ?= MarlinUltimaker2
USER_UM2_SERIAL_PORT        ?= AUTO

# Warning, the make file is not perfectly automatic yet

MCU_TARGET  = atmega2560
F_CPU       = 16000000
MCU_ARCH    = avr6
BOARD       = ULTIMAKER2

AS_SECONDARY_BOOTLOADER ?= 1

# the start address of the actual bootloader region
PRI_BOOT_ADR ?= 0x3E000

# this should be placed at the beginning of the very last page
SPMFUNC_ADR ?= 0x3FF00

TARGET      = um2_cardboot
CSRC        = cardboot.c pff.c mmcbbp.c
ASRC        = asmfunc.S
OPTIMIZE    = -Os -ffunction-sections -fpack-struct -fshort-enums -fno-move-loop-invariants -fno-inline-small-functions -mcall-prologues -mrelax -fno-tree-scev-cprop -fno-jump-tables
DEFS        = -DF_CPU=$(F_CPU) -D_BOARD_$(BOARD)_=1 -DSPMFUNC_ADR=$(SPMFUNC_ADR)
LIBS        =
DEBUG       = dwarf-2

ifeq ($(AS_SECONDARY_BOOTLOADER),1)
DEFS += -DAS_SECONDARY_BOOTLOADER=$(AS_SECONDARY_BOOTLOADER)
ifdef ENABLE_DEBUG
BOOT_ADR ?= 0x30000
else
BOOT_ADR ?= 0x3C800
endif
else
CSRC += stk500boot.c
BOOT_ADR ?= $(PRI_BOOT_ADR)
endif

ifdef ENABLE_DEBUG
CSRC += debug.c
DEFS += -DENABLE_DEBUG=1
endif

DEFS += -DBOOT_ADR=$(BOOT_ADR)

ASFLAGS     = -Wa,-adhlns=$(<:.S=.lst),-gstabs $(DEFS)
ALL_ASFLAGS = -mmcu=$(MCU_TARGET) -I. -x assembler-with-cpp $(ASFLAGS)
CFLAGS      = -g$(DEBUG) -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) -std=gnu99 $(DEFS)
LDFLAGS     = -Wl,--relax,--gc-sections -Wl,--section-start,.text=$(BOOT_ADR) -mrelax
OBJS        = $(CSRC:.c=.o) $(ASRC:.S=.o)

CC          = avr-gcc
LD          = avr-ld
AS          = avr-as
OBJCOPY     = avr-objcopy
OBJDUMP     = avr-objdump
SIZE        = avr-size

CURA_DIR   = $(USER_CURA_INSTALLATION_PATH)
UM2FW_DIR  = $(CURA_DIR)/resources/firmware
UM2FW_NAME = $(USER_DESIRED_UM2_FW_NAME)
UM2FW_PATH = $(UM2FW_DIR)/$(UM2FW_NAME)
UM2FW = $(basename $(notdir $(UM2FW_PATH)))
BOOTFW ?= $(TARGET)
BOOT_ADR_HEX = $(strip $(subst 0x,,$(BOOT_ADR)))

ALL_TARGETS = $(TARGET).hex $(UM2FW)-cardboot-firstinstall.hex $(UM2FW)-cardboot.hex $(UM2FW).APP.BIN size
ifeq ($(AS_SECONDARY_BOOTLOADER),1)
ALL_TARGETS += $(TARGET)_withspmfunc.hex
endif
ifdef TESTBUILD
ALL_TARGETS += $(TARGET).lst $(UM2FW)-cardboot.lst $(UM2FW)-cardboot-firstinstall.lst
endif
all: $(ALL_TARGETS)

clean:
	rm -rf $(TARGET).hex spmfunc.hex retargeted.hex retargeted.asm trampoline.hex trampoline.asm
	rm -rf *_withspmfunc.hex *_nospmfunc.hex
	rm -rf *.hex
	rm -rf app.bin APP.BIN *.APP.BIN
	rm -rf *.o *.elf *.eps *.bak *.a *.disasm *.reasm *.lst *.lss *.map
	rm -rf $(EXTRA_CLEAN_FILES)

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

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

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -j .fuse -j .bootloader -O ihex $< $@

%.bin: %.hex
	$(OBJCOPY) -I ihex -O binary $< $@

%.APP.BIN: %.bin
	mv -f $< $(notdir $@)

%.disasm: %.hex
	$(OBJDUMP) -D -z --no-show-raw-insn --stop-address=0xFFFFFFFF -m $(MCU_ARCH) $< > $@

# make a assembly file from the disassembly, clean it up so the assembler doesn't see errors
%.reasm: %.disasm
	cat $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/^\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

%.elf: %.o %.S
	$(LD) -m $(MCU_ARCH) -nostartfiles -o $@ $<

%.elf: %.o %.asm
	$(LD) -m $(MCU_ARCH) -nostartfiles -o $@ $<

SPMFUNCADR_HIGH_FIND = echo $(SPMFUNC_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F])[0-9a-fA-F]{4}\s*?$$/\U\1/p"
SPMFUNCADR_LOW_FIND  = echo $(SPMFUNC_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F]{2})[0-9a-fA-F]{2}\s*?$$/\U\1/p"

ifeq ($(AS_SECONDARY_BOOTLOADER),1)

# The code below takes an existing firmware (for the Ultimaker2) and attaches the SD card bootloader to it

# This requires some swapping and insertion of jump instructions, which is performed with disassembled code
# There are some weird code here, mainly because makefiles can't do math and makefiles have stupid rules about parenthesis
# This implementation only works on MCUs that support JMP instructions

TRAMPOLINE_ADR = $(shell echo "$(BOOT_ADR)" | gawk --non-decimal-data '{ printf "0x%X\n", $$1 - 4 }')
RESET_FIND_JMP  = sed -n -r "s/^\s*?[0]+:\s+jmp\s+0x([0-9a-fA-F]+).*?$$/0x\U\1/p"
RESET_FIND_RJMP = sed -n -r "s/^\s*?[0]+:\s+rjmp\s+\.\+([0-9]+).*?$$/\1/p"

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
	grep -v "\.org 0x" | \
	grep -v "is out of bounds" > $@

# only the first line of retargeted.hex is still guaranteed to be correct, the rest comes from the original FW
# then the trampoline is added, but the trampoline hex has a lot of blanks which need to be removed
# then the bootloader is appended
# then the injected spm function is appended to the absolute end
# all occurrences of the "end of file" indicator is removed

TRAMPADR_HIGH_FIND   = echo $(TRAMPOLINE_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F])[0-9a-fA-F]{4}\s*?$$/\U\1/p"
TRAMPADR_LOW_FIND    = echo $(TRAMPOLINE_ADR) | sed -n -r "s/^\s*?0x[0-9a-fA-F]*?([0-9a-fA-F]{2})[0-9a-fA-F]{2}\s*?$$/\U\1/p"

$(BOOTFW)_nospmfunc.hex: $(BOOTFW).elf
	$(OBJCOPY) -j .text -j .data -j .fuse -j .bootloader -O ihex $< $@

$(BOOTFW)_withspmfunc.hex: $(BOOTFW)_nospmfunc.hex spmfunc.o spmfunc.hex
	cat $< | grep -v ":00000001FF" > $@
	cat spmfunc.hex | grep -E "^(:02000002$(shell $(SPMFUNCADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:[0-9A-F]{2}$(shell $(SPMFUNCADR_LOW_FIND))[0-9A-F]{2}00)" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@

$(UM2FW)-cardboot.hex: retargeted.hex $(UM2FW_PATH).hex trampoline.hex $(BOOTFW)_nospmfunc.hex
	head -n 1 retargeted.hex > $@
	tail -n +2 $(UM2FW_PATH).hex | grep -v ":00000001FF" >> $@
	cat trampoline.hex | grep -E "^(:02000002$(shell $(TRAMPADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:[0-9A-F]{2}$(shell $(TRAMPADR_LOW_FIND))[0-9A-F]{2}00)" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@
	cat $(BOOTFW)_nospmfunc.hex | grep -v ":00000001FF" >> $@

$(UM2FW)-cardboot-firstinstall.hex: $(UM2FW)-cardboot.hex spmfunc.o spmfunc.hex
	cat $< | grep -v ":00000001FF" > $@
	cat spmfunc.hex | grep -E "^(:02000002$(shell $(SPMFUNCADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:[0-9A-F]{2}$(shell $(SPMFUNCADR_LOW_FIND))[0-9A-F]{2}00)" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@

else

# the "end of file" indicator is removed
# blank space is removed
# spm function is appended to the absolute end
$(UM2FW)-cardboot.hex: $(UM2FW_PATH).hex $(BOOTFW).hex spmfunc.o spmfunc.hex
	cat $(UM2FW_PATH).hex $(BOOTFW).hex | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" > $@
	cat spmfunc.hex | grep -E "^(:02000002$(shell $(SPMFUNCADR_HIGH_FIND))000[0-9A-F]{2}$$)|(:[0-9A-F]{2}$(shell $(SPMFUNCADR_LOW_FIND))[0-9A-F]{2}00)" | grep -v -E "^:10[0-9A-F]*?(0|F){32}[0-9A-F]{2}$$" | grep -v ":00000001FF" >> $@

$(UM2FW)-cardboot-firstinstall.hex: $(UM2FW)-cardboot.hex
	cp -f $< $@

endif

$(UM2FW).hex: $(UM2FW_PATH).hex
	cp -f $< $@

appbin: $(UM2FW).APP.BIN
	mv -f $< APP.BIN

# bootloader installation procedure

PROG_PORT ?= $(USER_UM2_SERIAL_PORT)

ifeq ($(AS_SECONDARY_BOOTLOADER),1)
# installation must be done through a Python script that skips the original bootloader memory

firstinstall: ./release/$(UM2FW)-cardboot-firstinstall.hex
	cp -f $< $(notdir $<)
	$(CURA_DIR)/python/python.exe ./installer/stk500v2.py $(PROG_PORT) $(notdir $<)

firstinstall: $(UM2FW)-cardboot-firstinstall.hex
	$(CURA_DIR)/python/python.exe ./installer/stk500v2.py $(PROG_PORT) $<

otherinstall: ./release/$(UM2FW)-cardboot.hex
	cp -f $< $(notdir $<)
	$(CURA_DIR)/python/python.exe ./installer/stk500v2.py $(PROG_PORT) $(notdir $<)

otherinstall: $(UM2FW)-cardboot.hex
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

flashmerged: ./release/$(UM2FW)-cardboot.hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTS) -Uflash:w:$<:i

endif

include build_helper.mk