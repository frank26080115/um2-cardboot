BUILDLOG = buildlog.txt

testbuild:
	@echo "test build config default" | tee $(BUILDLOG)
	@make clean all AS_SECONDARY_BOOTLOADER=0 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_default
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex $(UM2FW)-cardboot.lst ./testbuild_default/
	@echo "test build config default debug" | tee -a $(BUILDLOG)
	@make clean all AS_SECONDARY_BOOTLOADER=0 ENABLE_DEBUG=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_default_debug
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex $(UM2FW)-cardboot.lst ./testbuild_default_debug/
	@echo "test build config as_secondary" | tee -a $(BUILDLOG)
	@make clean all AS_SECONDARY_BOOTLOADER=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_secondary
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot-firstinstall.hex $(UM2FW)-cardboot-firstinstall.lst ./testbuild_secondary/
	@echo "test build config as_secondary debug" | tee -a $(BUILDLOG)
	@make clean all AS_SECONDARY_BOOTLOADER=1 ENABLE_DEBUG=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_secondary_debug
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot-firstinstall.hex $(UM2FW)-cardboot-firstinstall.lst ./testbuild_secondary_debug/

testbuildclean:
	rm -rf testbuild_*

ALL_UM2FW_HEX = $(wildcard $(UM2FW_DIR)/*.hex))
ALL_UM2FW = $(basename $(ALL_UM2FW_HEX))
ALL_UM2FW_NEW = $(strip $(notdir $(ALL_UM2FW)))

ifeq ($(AS_SECONDARY_BOOTLOADER),1)
RELEASE_OPTS = AS_SECONDARY_BOOTLOADER=1
endif

release: clean
	@mkdir -p release
	$(foreach var,$(ALL_UM2FW),make all $(RELEASE_OPTS) UM2FW_PATH="$(var)" ;)
	$(foreach var,$(ALL_UM2FW_NEW),mv -f "$(var)-cardboot.hex" ./release/ ;)
	$(foreach var,$(ALL_UM2FW_NEW),mv -f "$(var)-cardboot-firstinstall.hex" ./release/ ;)
	$(foreach var,$(ALL_UM2FW_NEW),mv -f "$(var).APP.BIN" ./release/ ;)

cleanrelease:
	rm -rf release
	rm -rf Marlin*.hex

toolversion:
	make --version | tee -a $@.txt
	$(CC) --version | tee -a $@.txt
	$(LD) --version | tee -a $@.txt
	$(AS) --version | tee -a $@.txt
	$(OBJDUMP) --version | tee -a $@.txt
	$(CURA_DIR)/python/python.exe --version | tee -a $@.txt
	echo --version | tee -a $@.txt
	printf --version | tee -a $@.txt
	cat --version | tee -a $@.txt
	sed --version | tee -a $@.txt
	grep --version | tee -a $@.txt
	gawk --version | tee -a $@.txt
