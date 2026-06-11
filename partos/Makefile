# partos rom build
#
# assembles all modules with sdas and links them with sdld. the rom is
# packed automatically from the link results -- no size constants:
#
#   rom 0x0000-0x00ff  page 0 (rst/nmi vectors + rom cold boot init)
#   rom 0x0100-......  boot (_BOOT area, runs from the rom in place)
#   rom ......-......  bios image (_CODE area, stored right after boot;
#                      page 0 init copies it to memory top using linker
#                      area symbols)
#
# link bases are read from src/partos.inc (single source of truth);
# segment offsets and sizes are read back from the linker map.

AS      := sdasz80
LD      := sdldz80
MAKEBIN := makebin

BUILD := build
BIN   := bin
ROM   := $(BIN)/partos.rom

# physical rom size (hardware: 2 KB EPROM)
ROM_SIZE := 2048

# read a .equ value from partos.inc so the memory map exists only there.
inc_equ = $(strip $(shell awk -v n="$(1)," '$$1==".equ" && $$2==n {print $$3}' src/partos.inc))
BOOT_BASE    := $(call inc_equ,BOOT_BASE)
BIOS_BASE    := $(call inc_equ,BIOS_BASE)
SYSVARS_BASE := $(call inc_equ,SYSVARS_BASE)

# link order: page 0 first, then the bios modules, boot last.
PAGE0_SRC := src/page0/page0.s
BIOS_SRC  := src/bios/sbrk.s \
             src/bios/list.s \
             src/bios/dev.s \
             src/bios/ir.s \
             src/bios/lock.s \
             src/bios/delay.s
BOOT_SRC  := src/boot/boot.s

SRCS := $(PAGE0_SRC) $(BIOS_SRC) $(BOOT_SRC)
RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(SRCS))

MAP := $(BUILD)/partos.map

.PHONY: all clean
all: $(ROM)

# assemble from within the source directory so .include paths resolve.
$(BUILD)/%.rel: src/%.s
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/partos.ihx: $(RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _BOOT=$(BOOT_BASE) -b _CODE=$(BIOS_BASE) -b _SYSVARS=$(SYSVARS_BASE) \
	      $(RELS)

$(BUILD)/partos.mem: $(BUILD)/partos.ihx
	$(MAKEBIN) -s 65536 $< $@

# pack the rom: [page 0 + boot, straight from the bottom of the memory
# image] + [bios image, extracted from its runtime address] + 0xff pad.
# all offsets/sizes come from the area table in the linker map.
$(ROM): $(BUILD)/partos.mem
	@mkdir -p $(BIN)
	@BOOT_ADDR=$$(awk '$$1=="_BOOT" {print "0x"$$2; exit}' $(MAP)); \
	BOOT_SIZE=$$(awk '$$1=="_BOOT" {print "0x"$$3; exit}' $(MAP)); \
	BIOS_ADDR=$$(awk '$$1=="_CODE" {print "0x"$$2; exit}' $(MAP)); \
	BIOS_SIZE=$$(awk '$$1=="_CODE" {print "0x"$$3; exit}' $(MAP)); \
	LOW_END=$$(( $$(printf '%d' $$BOOT_ADDR) + $$(printf '%d' $$BOOT_SIZE) )); \
	BIOS_OFF=$$(printf '%d' $$BIOS_ADDR); \
	BIOS_LEN=$$(printf '%d' $$BIOS_SIZE); \
	TOTAL=$$(( $$LOW_END + $$BIOS_LEN )); \
	if [ $$TOTAL -gt $(ROM_SIZE) ]; then \
	    echo "error: rom overflow: page0+boot ($$LOW_END) + bios ($$BIOS_LEN) = $$TOTAL > $(ROM_SIZE)"; \
	    exit 1; \
	fi; \
	dd if=$(BUILD)/partos.mem of=$(BUILD)/low.bin bs=1 count=$$LOW_END status=none; \
	dd if=$(BUILD)/partos.mem of=$(BUILD)/bios.bin bs=1 skip=$$BIOS_OFF count=$$BIOS_LEN status=none; \
	cat $(BUILD)/low.bin $(BUILD)/bios.bin > $@; \
	head -c $$(( $(ROM_SIZE) - $$TOTAL )) /dev/zero | tr '\0' '\377' >> $@; \
	echo "$@: $$(stat -c%s $@) bytes (page0+boot=$$LOW_END, bios=$$BIOS_LEN, free=$$(( $(ROM_SIZE) - $$TOTAL )))"

clean:
	rm -rf $(BUILD) $(BIN)
