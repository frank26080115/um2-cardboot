#------------------------------------------------------------------
# Makefile for stand-alone MMC boot strap loader
#------------------------------------------------------------------
# Change these three defs for the target device

MCU_TARGET  = atmega2560
F_CPU       = 16000000

# Boot loader start address, as hex in bytes
BOOT_ADR     = 0x30000
# use 0x3C000 for under 4096 bytes compiled size (no debug)

# Compile as a secondary bootloader
AS_2NDARY_BOOTLOADER = 1

TARGET      = um2_cardboot
CSRC        = main.c pff.c mmcbbp.c spm_helper.c
ASRC        = asmfunc.S spm_helper_asm.S
OPTIMIZE    = -Os -mcall-prologues -mrelax -ffunction-sections -fpack-struct -fshort-enums -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions
DEFS        = -DBOOT_ADR=$(BOOT_ADR) -DF_CPU=$(F_CPU)
LIBS        =
DEBUG       = dwarf-2

ifdef AS_2NDARY_BOOTLOADER
DEFS += -DAS_2NDARY_BOOTLOADER
DEFS += -DMAGIC_1=0x4D -DMAGIC_2=0x61 -DMAGIC_3=0x67 -DMAGIC_4=0x69
endif

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

cleanartifacts: cleanmergedartifacts
	rm -rf *.o $(TARGET).elf *.eps *.bak *.a
	rm -rf *.lst *.map $(EXTRA_CLEAN_FILES)

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf

lst:  $(TARGET).lst
%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

%.o : %.S
	$(CC) -c $(ALL_ASFLAGS) $< -o $@

text: $(TARGET).hex

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -j .fuse -O ihex $< $@

%.bin: %.hex
	$(OBJCOPY) -I ihex -O binary $< $@

%.disasm: %.hex
	$(OBJDUMP) -D -z --no-show-raw-insn --stop-address=0xFFFFFFFF -m avr6 $< > $@

toolversion:
	make --version | tee -a $@.txt
	$(CC) --version | tee -a $@.txt
	$(LD) --version | tee -a $@.txt
	$(AS) --version | tee -a $@.txt

# The code below takes an existing firmware (for the Ultimaker2) and attaches the SD card bootloader to it
# This requires some swapping and insertion of jump instructions, which is performed with disassembled code
# There are some weird code here, mainly because makefiles can't do math and makefiles have stupid rules about parenthesis
# This implementation only works on MCUs that support JMP instructions

UM2FW  ?= MarlinUltimaker2-15.02.1
BOOTFW ?= $(TARGET)
BOOT_ADR_HEX = $(strip $(subst 0x,,$(BOOT_ADR)))
TRAMPOLINE_ADR = $(shell echo "$(BOOT_ADR)" | gawk --non-decimal-data '{ printf "0x%X\n", $$1 - 4 }')
RESET_FIND_JMP  = sed -n -r "s/^\s*?[0]+:\s+jmp\s+0x([0-9a-fA-F]+).*?$$/0x\U\1/p"
RESET_FIND_RJMP = sed -n -r "s/^\s*?[0]+:\s+rjmp\s+\.\+([0-9]+).*?$$/\1/p"

cleanmerged: cleanmergedartifacts
	rm -rf $(UM2FW)-cardboot.hex

cleanmergedartifacts:
	rm -rf $(UM2FW).disasm $(UM2FW).reasm $(UM2FW).asm trampoline.elf trampoline.hex trampoline.asm retargeted.hex retargeted.elf retargeted.asm finalmerge.hex

allmerged: $(UM2FW)-cardboot.hex size

$(UM2FW)-cardboot.hex: finalmerge.hex
	mv $< $@

%.o: %.asm
	$(AS) -mmcu=$(MCU_TARGET) -mall-opcodes -W -o $@ $<

%.elf: %.o %.asm
	$(LD) -mavr6 -nostartfiles -o $@ $<

$(UM2FW).disasm: $(UM2FW).hex

# make a assembly file from the disassembly, clean it up so the assembler doesn't see errors
$(UM2FW).reasm: $(UM2FW).disasm
	cat $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format ihex" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/^\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

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
	make $@ RECURSIVE_MAKE=1
endif

# replaces the old reset vector with a jump to the bootloader
# removes all .org because they cause avr-as to not work right if RJMPs are used
retargeted.asm: $(UM2FW).reasm
	sed -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x[0-9a-fA-F]+.*?$$/.org 0x0\r\n\tjmp 0x$(BOOT_ADR_HEX)\t; to bootloader/" < $< | \
	grep -v "\.org 0x" > $@

# only the first line of retargeted.hex is still guaranteed to be correct, the rest comes from the original FW
# then the trampoline is added, but the trampoline hex has a lot of blanks which need to be removed
# then the bootloader is appended to the end
finalmerge.hex: retargeted.hex $(UM2FW).hex trampoline.hex $(BOOTFW).hex
	head -n 1 retargeted.hex > $@
	tail -n +2 $(UM2FW).hex | grep -v ":00000001FF" >> $@
	cat trampoline.hex | grep -v -E "(00000000|FFFFFFFF)[0-9A-F][0-9A-F]$$" | grep -v ":00000001FF" | tail -n 2 >> $@
	cat $(BOOTFW).hex >> $@

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
DUDE_OPTIONS = -D
# option -D is required for Arduino STK500v2 bootloader because it does not implement chip erase

flash: $(TARGET).hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTIONS) -Uflash:w:$<:i

flashmerged: $(UM2FW)-cardboot.hex
	$(AVRDUDE) -c$(PROG_TOOL) -p$(MCU_TARGET) -P$(PROG_PORT) -b$(PROG_BAUD) $(DUDE_OPTIONS) -Uflash:w:$<:i