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
	@echo "test build config as_2ndary" | tee -a $(BUILDLOG)
	@make clean all allmerged AS_2NDARY_BOOTLOADER=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_2ndary
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_2ndary/
	@echo "test build config as_2ndary debug" | tee -a $(BUILDLOG)
	@make clean all allmerged AS_2NDARY_BOOTLOADER=1 ENABLE_DEBUG=1 TESTBUILD=1 | tee -a $(BUILDLOG)
	@mkdir -p testbuild_2ndary_debug
	@mv $(TARGET).hex $(TARGET).lst $(UM2FW)-cardboot.hex finalmerge.lst ./testbuild_2ndary_debug/

testbuildclean:
	rm -rf testbuild_*

ALL_UM2FW_HEX = $(wildcard $(UM2FW_DIR)/*.hex))
ALL_UM2FW = $(basename $(ALL_UM2FW_HEX))
ALL_UM2FW_NEW = $(notdir $(ALL_UM2FW))

ifdef AS_2NDARY_BOOTLOADER
RELEASE_OPTS = AS_2NDARY_BOOTLOADER=1
endif

release: um2fw
	@mkdir -p release
	for x in $(ALL_UM2FW) ; do \
		@make clean all allmerged $(RELEASE_OPTS) UM2FW_PATH=$$x ; \
	done
	for x in $(ALL_UM2FW_NEW) ; do \
		@mv $$x.hex ./release/ ; \
	done
	@echo "installable files created"