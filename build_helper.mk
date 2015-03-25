BUILDLOG = buildlog.txt

testbuild:
	@echo "test build config default" | tee $(BUILDLOG)
	@make clean all allmerged TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_default
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_default/
	@echo "test build config default debug" | tee -a $(BUILDLOG)
	@make clean all allmerged ENABLE_DEBUG=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_default_debug
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_default_debug/
	@echo "test build config as_secondary" | tee -a $(BUILDLOG)
	@make clean all allmerged AS_SECONDARY_BOOTLOADER=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_secondary
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_secondary/
	@echo "test build config as_secondary debug" | tee -a $(BUILDLOG)
	@make clean all allmerged AS_SECONDARY_BOOTLOADER=1 ENABLE_DEBUG=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_secondary_debug
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_secondary_debug/

testbuildclean:
	rm -rf testbuild_*

ALL_UM2FW_HEX = $(wildcard $(UM2FW_DIR)/*.hex))
ALL_UM2FW = $(basename $(ALL_UM2FW_HEX))
ALL_UM2FW_NEW = $(notdir $(ALL_UM2FW))

ifdef AS_SECONDARY_BOOTLOADER
RELEASE_OPTS = AS_SECONDARY_BOOTLOADER=1
endif

release: clean
	@mkdir -p release
	$(foreach var,$(ALL_UM2FW),make clean allmerged $(RELEASE_OPTS) UM2FW_PATH="$(var)" ;)
	$(foreach var,$(ALL_UM2FW_NEW),mv "$(var)-cardboot.hex" ./release/ ;)

cleanrelease:
	rm -rf release