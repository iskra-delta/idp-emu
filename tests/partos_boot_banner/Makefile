AS      := sdasz80
LD      := sdldz80
MAKEBIN := makebin

BUILD := build
ROM   := $(BUILD)/boot_banner.rom
MAP   := $(BUILD)/boot_banner.map

SRC_MAIN   := main.s
SRC_STUBS  := stubs.s
SRC_DEVLITE := devlite.s
SRC_LIST   := ../../partos/src/bios/core/list.s
SRC_DRV    := ../../partos/src/bios/util/drv.s
SRC_DELAY  := ../../partos/src/bios/util/delay.s
SRC_PRINT  := ../../partos/src/bios/util/print.s
SRC_SIO    := ../../partos/src/bios/drivers/sio.s
SRC_GDP    := ../../partos/src/bios/drivers/gdp.s
SRC_BOOT   := ../../partos/src/boot/boot.s

RELS := $(BUILD)/main.rel \
        $(BUILD)/stubs.rel \
        $(BUILD)/devlite.rel \
        $(BUILD)/list.rel \
        $(BUILD)/drv.rel \
        $(BUILD)/delay.rel \
        $(BUILD)/print.rel \
        $(BUILD)/sio.rel \
        $(BUILD)/gdp.rel \
        $(BUILD)/boot.rel

.PHONY: all clean
all: $(ROM)

$(BUILD)/%.rel: %.s
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/devlite.rel: $(SRC_DEVLITE)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/list.rel: $(SRC_LIST)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/drv.rel: $(SRC_DRV)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/delay.rel: $(SRC_DELAY)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/print.rel: $(SRC_PRINT)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/sio.rel: $(SRC_SIO)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/gdp.rel: $(SRC_GDP)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/boot.rel: $(SRC_BOOT)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/boot_banner.ihx: $(RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _CODE=0x0000 -b _BOOT=0x0700 -b _SYSVARS=0x8000 \
	      $(RELS)

$(BUILD)/boot_banner.mem: $(BUILD)/boot_banner.ihx
	$(MAKEBIN) -s 65536 $< $@

$(ROM): $(BUILD)/boot_banner.mem
	@BOOT_ADDR=$$(awk '$$1=="_BOOT" {print "0x"$$2; exit}' $(MAP)); \
	BOOT_SIZE=$$(awk '$$1=="_BOOT" {print "0x"$$3; exit}' $(MAP)); \
	CODE_ADDR=$$(awk '$$1=="_CODE" {print "0x"$$2; exit}' $(MAP)); \
	CODE_SIZE=$$(awk '$$1=="_CODE" {print "0x"$$3; exit}' $(MAP)); \
	BOOT_END=$$(( $$(printf '%d' $$BOOT_ADDR) + $$(printf '%d' $$BOOT_SIZE) )); \
	CODE_END=$$(( $$(printf '%d' $$CODE_ADDR) + $$(printf '%d' $$CODE_SIZE) )); \
	TOTAL=$$BOOT_END; \
	if [ $$CODE_END -gt $$TOTAL ]; then TOTAL=$$CODE_END; fi; \
	if [ $$TOTAL -gt 2048 ]; then \
	    echo "error: boot banner harness overflow: $$TOTAL > 2048"; \
	    exit 1; \
	fi; \
	dd if=$(BUILD)/boot_banner.mem of=$@ bs=1 count=$$TOTAL status=none; \
	head -c $$(( 2048 - $$TOTAL )) /dev/zero | tr '\0' '\377' >> $@

clean:
	rm -rf $(BUILD)
