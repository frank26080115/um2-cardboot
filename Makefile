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

allmerged: $(UM2FW)-cardboot.hex

$(UM2FW)-cardboot.hex: finalmerge.hex
	mv $< $@

%.elf: %.asm
	avr-as -mmcu=$(MCU_TARGET) -mall-opcodes -W -o $@ $<

$(UM2FW).disasm: $(UM2FW).hex

# make a assembly file from the disassembly, clean it up so the assembler doesn't see errors
$(UM2FW).reasm: $(UM2FW).disasm
	cat $< | \
	grep -v "Disassembly of section" | \
	grep -v "file format ihex" | \
	grep -v "<\.sec[0-9]>:" | \
	sed -r "s/\s+([0-9a-f]+):\s+(.*+)$$/.org 0x\U\1\r\n\t\E\2/" > $@

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
	sed -r "N;s/^\.org\s+0x[0]+\s+jmp\s+0x[0-9a-fA-F]+.*?$$/.org 0x0\r\n\tjmp 0x$(BOOT_ADR_HEX)\t; to bootloader/" < $< | 
	grep -v "\.org 0x" > $@

# only the first line of retargeted.hex is still guaranteed to be correct, the rest comes from the original FW
# then the trampoline is added, but the trampoline hex has a lot of blanks which need to be removed
# then the bootloader is appended to the end
finalmerge.hex: retargeted.hex $(UM2FW).hex trampoline.hex $(BOOTFW).hex
	head -n 1 retargeted.hex > $@
	tail -n +2 $(UM2FW).hex | grep -v ":00000001FF" >> $@
	cat trampoline.hex | grep -v -E "(00000000|FFFFFFFF)[0-9A-F][0-9A-F]$$" | grep -v ":00000001FF" | tee exam.hex >> $@
	cat $(BOOTFW).hex >> $@

cleanappbin:
	rm -rf app.bin

appbin: app.bin

app.bin: $(UM2FW).bin
	mv $< $@