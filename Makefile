#------------------------------------------------------------------
# Makefile for stand-alone MMC boot strap loader
#------------------------------------------------------------------
# Change these three defs for the target device

MCU_TARGET  = atmega2560	# Target device to be used
F_CPU       = 16000000	# CPU clock frequency [Hz]

# Boot loader start address, as hex in bytes
#BOOT_ADR    = 0x3F000
BOOT_ADR     = 0x3E000
# under 4096 bytes compiled size

AS_2NDARY_BOOTLOADER = 1	# Compile as a secondary bootloader

TARGET      = um2_cardboot
CSRC        = main.c pff.c mmcbbp.c
ASRC        = asmfunc.S
OPTIMIZE    = -Os -mcall-prologues -mrelax -ffunction-sections -fpack-struct -fshort-enums -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions
DEFS        = -DBOOT_ADR=$(BOOT_ADR) -DF_CPU=$(F_CPU)
LIBS        =
DEBUG       = dwarf-2

ifdef AS_2NDARY_BOOTLOADER
DEFS += -DAS_2NDARY_BOOTLOADER
endif

ASFLAGS     = -Wa,-adhlns=$(<:.S=.lst),-gstabs $(DEFS)
ALL_ASFLAGS = -mmcu=$(MCU_TARGET) -I. -x assembler-with-cpp $(ASFLAGS)
CFLAGS      = -g$(DEBUG) -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) $(DEFS)
LDFLAGS     = -Wl,--relax,--gc-sections -Wl,-Map,$(TARGET).map -Wl,--section-start,.text=$(BOOT_ADR) -mrelax
OBJ         = $(CSRC:.c=.o) $(ASRC:.S=.o)

CC          = avr-gcc
OBJCOPY     = avr-objcopy
OBJDUMP     = avr-objdump
SIZE        = avr-size


all:	$(TARGET).elf lst text size

$(TARGET).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean: cleanmerged
	rm -rf *.o $(TARGET).elf *.eps *.bak *.a
	rm -rf *.lst *.map $(EXTRA_CLEAN_FILES)
	rm -rf $(TARGET).hex $(TARGET).size.txt

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU_TARGET) $(TARGET).elf > $(TARGET).size.txt

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

# The code below takes an existing firmware (for the Ultimaker2) and attaches the SD card bootloader to it
# This requires some swapping and insertion of jump instructions, which is performed with disassembled code
# There are some weird code here, mainly because makefiles can't do math and makefiles have stupid rules about parenthesis

UM2FW ?= MarlinUltimaker2-15.02.1
BOOT_ADR_HEX = $(strip $(subst 0x,,$(BOOT_ADR)))
TRAMPOLINE_ADR = $(shell echo "$(BOOT_ADR)" | gawk --non-decimal-data '{ printf "0x%X\n", $$1 - 4 }')
RESET_FIND_JMP = sed -n -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x([0-9a-fA-F]+).*?$$/0x\U\1/p"
RESET_FIND_RJMP = sed -n -r "N;s/^\.org\s+0x[0]+\s+rjmp\s+\.\+([0-9]+).*?$$/\1/p"

cleanmerged:
	rm -rf $(UM2FW)-cardboot.hex merged_post.hex merged_post.elf merged_post.asm merged_pre.asm merged_pre.hex

allmerged: $(UM2FW)-cardboot.hex

mergecompare: $(UM2FW)-cardboot.disasm merged_pre.disasm $(UM2FW)-cardboot.lst

$(UM2FW)-cardboot.elf: merged_post.asm
	avr-as -mmcu=$(MCU_TARGET) -mall-opcodes -W -o $@ $<
# I discovered that avr-as won't assemble the resulting asm file correctly, even if the asm file seems correct

# insert trampoline jump before bootloader region, and replace reset vector jump with a jump to the bootloader
# to check between whether the FW uses JMP or RJMP, Make is recursively invoked to re-evaluate the search results
merged_post.asm: merged_pre.asm
ifdef RECURSIVE_MAKE
ifneq ($(strip $(shell $(RESET_FIND_JMP) < merged_pre.asm)),)
	@echo "JMP found: $(shell $(RESET_FIND_JMP) < $<)"
	sed -r "s/^(\.org\s+0x[0]*?$(BOOT_ADR_HEX)\s*?)$$/.org $(TRAMPOLINE_ADR)\r\n\tjmp $(shell $(RESET_FIND_JMP) <$<)\t; trampoline\r\n\1/" <$< | \
	sed -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x[0-9a-fA-F]+.*?$$/.org 0x0\r\n\tjmp 0x$(BOOT_ADR_HEX)\t; to bootloader/" > $@
endif
ifneq ($(strip $(shell $(RESET_FIND_RJMP) < merged_pre.asm)),)
	@echo "RJMP found: $(shell $(RESET_FIND_RJMP) < $<)"
	sed -r "s/^(\.org\s+0x[0]*?$(BOOT_ADR_HEX)\s*?)$$/.org $(TRAMPOLINE_ADR)\r\n\tjmp $(shell printf "0x%X" $(shell $(RESET_FIND_RJMP) <$<))\t; trampoline\r\n\1/" <$< | \
	sed -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x[0-9a-fA-F]+.*?$$/.org 0x0\r\n\tjmp 0x$(BOOT_ADR_HEX)\t; to bootloader/" > $@
endif
else
	@echo "Recursive Make"
	make merged_post.asm RECURSIVE_MAKE=1
endif

# disassemble, then remove lines that might be syntax errors, then turn address labels into origin labels
merged_pre.asm: merged_pre.hex
	$(OBJDUMP) -D -z --no-show-raw-insn --stop-address=0xFFFFFFFF -m avr6 $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format ihex" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

# merge the two ihex files but eliminate "end of file" indicator
merged_pre.hex: $(UM2FW).hex $(TARGET).hex
	cat $^ | grep -v ":00000001FF" > $@

cleanappbin:
	rm -rf app.bin

appbin: app.bin

app.bin: $(UM2FW).bin
	mv $< $@