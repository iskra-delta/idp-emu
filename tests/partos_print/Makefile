AS      := sdasz80
LD      := sdldz80
MAKEBIN := makebin

BUILD := build
ROM   := $(BUILD)/print_harness.rom
MAP   := $(BUILD)/print_harness.map

SRC_MAIN  := main.s
SRC_STUBS := stubs.s
SRC_DEV   := ../../partos/src/bios/core/dev.s
SRC_DRV   := ../../partos/src/bios/util/drv.s
SRC_DELAY := ../../partos/src/bios/util/delay.s
SRC_PRINT := ../../partos/src/bios/util/print.s
SRC_GDP   := ../../partos/src/bios/drivers/gdp.s

RELS := $(BUILD)/main.rel \
        $(BUILD)/stubs.rel \
        $(BUILD)/dev.rel \
        $(BUILD)/drv.rel \
        $(BUILD)/delay.rel \
        $(BUILD)/print.rel \
        $(BUILD)/gdp.rel

.PHONY: all clean
all: $(ROM)

$(BUILD)/main.rel: $(SRC_MAIN)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/stubs.rel: $(SRC_STUBS)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/dev.rel: $(SRC_DEV)
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

$(BUILD)/gdp.rel: $(SRC_GDP)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/print_harness.ihx: $(RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _CODE=0x0000 -b _SYSVARS=0x8000 \
	      $(RELS)

$(BUILD)/print_harness.mem: $(BUILD)/print_harness.ihx
	$(MAKEBIN) -s 65536 $< $@

$(ROM): $(BUILD)/print_harness.mem
	@CODE_SIZE=$$(awk '$$1=="_CODE" {print "0x"$$3; exit}' $(MAP)); \
	CODE_LEN=$$(printf '%d' $$CODE_SIZE); \
	dd if=$(BUILD)/print_harness.mem of=$@ bs=1 count=$$CODE_LEN status=none; \
	head -c $$(( 2048 - $$CODE_LEN )) /dev/zero | tr '\0' '\377' >> $@

clean:
	rm -rf $(BUILD)
