# partos rom build
#
# the rom now has two separately linked pieces:
#
#   stage 0: bootstrap.s + dzx0_standard.s linked at 0x0000
#   stage 1: the rest of the bios linked at 0x2000, then zx0-compressed
#
# the final eprom image is:
#
#   [stage 0 bootstrap image][compressed stage 1][0xff pad]
#
# link bases are read from src/partos.inc (single source of truth);
# segment offsets and sizes are read back from the linker maps.

AS      := sdasz80
LD      := sdldz80
OBJCOPY := objcopy
ZX0     := ../tools/zx0/bin/zx0

BUILD := build
BIN   := bin
ROM   := $(BIN)/partos.rom
KERNEL_BIN := $(BIN)/kernel.bin
KERNEL_IHX := $(BUILD)/kernel.ihx
KERNEL_PRE_IHX := $(BUILD)/kernel-pre.ihx

# physical rom size (hardware: 2 KB EPROM)
ROM_SIZE := 2048
KERNEL_TOP := 0x10000

# read a .equ value from partos.inc so the memory map exists only there.
inc_equ = $(strip $(shell awk -v n="$(1)," '$$1==".equ" && $$2==n {print $$3}' src/partos.inc))
BOOT_BASE    := $(call inc_equ,BOOT_BASE)
STAGE1_BASE  := $(call inc_equ,STAGE1_BASE)
BIOS_BASE    := $(call inc_equ,BIOS_BASE)
SYSVARS_BASE := $(call inc_equ,SYSVARS_BASE)
KERNEL_HEAP_BASE := $(call inc_equ,KERNEL_HEAP_BASE)
KERNEL_HEAP_SIZE := $(call inc_equ,KERNEL_HEAP_SIZE)
KERNEL_IM2_BASE := $(call inc_equ,KERNEL_IM2_BASE)
KERNEL_IM2_SIZE := $(call inc_equ,KERNEL_IM2_SIZE)
KERNEL_PAGE0_BASE := $(call inc_equ,KERNEL_PAGE0_BASE)
KERNEL_PAGE0_SIZE := $(call inc_equ,KERNEL_PAGE0_SIZE)

# discover sources from the current tree instead of hardcoding deleted paths.
# stage 0 is always the same two-file bootstrap image at rom address 0x0000.
BOOTSTRAP_SRC := src/rom/bootstrap.s src/rom/dzx0_standard.s

# stage 1 is the rest of the bios, linked for ram execution at STAGE1_BASE.
# `start.s` must stay first so the bootstrap can jump straight to STAGE1_BASE.
BIOS_STAGE1_ALL := $(sort $(wildcard src/rom/*.s))
BIOS_STAGE1_ENTRY := $(filter src/rom/start.s,$(BIOS_STAGE1_ALL))
BIOS_STAGE1_OTHER := $(filter-out $(BIOS_STAGE1_ENTRY) src/rom/bootstrap.s src/rom/dzx0_standard.s,$(BIOS_STAGE1_ALL))
BIOS_STAGE1_SRC := $(BIOS_STAGE1_ENTRY) $(BIOS_STAGE1_OTHER)

KERNEL_DISCOVERED_SRC := $(sort $(wildcard src/kernel/*.s))
KERNEL_ENTRY_SRC := $(filter src/kernel/init.s,$(KERNEL_DISCOVERED_SRC))
KERNEL_PAGE0_SRC := $(filter src/kernel/page0.s,$(KERNEL_DISCOVERED_SRC))
KERNEL_LOCAL_SRC := $(filter-out $(KERNEL_ENTRY_SRC) $(KERNEL_PAGE0_SRC),$(KERNEL_DISCOVERED_SRC))
KERNEL_DRIVER_SRC := $(sort $(wildcard src/drivers/*.s))
KERNEL_SRC := $(KERNEL_ENTRY_SRC) $(KERNEL_LOCAL_SRC) $(KERNEL_DRIVER_SRC) $(KERNEL_PAGE0_SRC)

BOOTSTRAP_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(BOOTSTRAP_SRC))
STAGE1_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(BIOS_STAGE1_SRC))
KERNEL_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(KERNEL_SRC))
BOOTSTRAP_MAP := $(BUILD)/bootstrap.map
STAGE1_MAP := $(BUILD)/stage1.map

.PHONY: all kernel clean
all: $(ROM)
kernel: $(KERNEL_BIN)

# assemble from within the source directory so .include paths resolve.
$(BUILD)/%.rel: src/%.s
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/bootstrap.ihx: $(BOOTSTRAP_RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _BOOT=$(BOOT_BASE) \
	      $(BOOTSTRAP_RELS)

$(BUILD)/stage1.ihx: $(STAGE1_RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _BOOT=$(STAGE1_BASE) -b _SYSVARS=$(SYSVARS_BASE) \
	      $(STAGE1_RELS)

$(KERNEL_IHX): $(KERNEL_RELS)
	@PRE_MAP=$(BUILD)/kernel-pre.map; \
	FINAL_MAP=$(BUILD)/kernel.map; \
	$(LD) -n -m -i $(abspath $(KERNEL_PRE_IHX)) -b _CODE=0x0000 $(KERNEL_RELS); \
	HEAP_ADDR=$$(awk '$$1=="_HEAP" {print "0x"$$2; exit}' $$PRE_MAP); \
	HEAP_SIZE=$$(awk '$$1=="_HEAP" {print "0x"$$3; exit}' $$PRE_MAP); \
	IM2_SIZE=$$(awk '$$1=="_IM2" {print "0x"$$3; exit}' $$PRE_MAP); \
	PAGE0_SIZE=$$(awk '$$1=="_PAGE0" {print "0x"$$3; exit}' $$PRE_MAP); \
	PAGE0_ADDR=$$(awk '$$1=="_PAGE0" {print "0x"$$2; exit}' $$PRE_MAP); \
	if [ $$(printf '%d' $$HEAP_SIZE) -ne $$(printf '%d' $(KERNEL_HEAP_SIZE)) ]; then \
	    echo "error: kernel _HEAP must be $(KERNEL_HEAP_SIZE) bytes, got $$HEAP_SIZE"; \
	    exit 1; \
	fi; \
	if [ $$(printf '%d' $$IM2_SIZE) -ne $$(printf '%d' $(KERNEL_IM2_SIZE)) ]; then \
	    echo "error: kernel _IM2 must be $(KERNEL_IM2_SIZE) bytes, got $$IM2_SIZE"; \
	    exit 1; \
	fi; \
	if [ $$(printf '%d' $$PAGE0_SIZE) -ne $$(printf '%d' $(KERNEL_PAGE0_SIZE)) ]; then \
	    echo "error: kernel _PAGE0 must be exactly 256 bytes, got $$PAGE0_SIZE"; \
	    exit 1; \
	fi; \
	KERNEL_BASE=$$(( $$(printf '%d' $(KERNEL_HEAP_BASE)) - $$(printf '%d' $$HEAP_ADDR) )); \
	$(LD) -n -m -i $(abspath $@) \
	      -b _CODE=$$(printf '0x%04X' $$KERNEL_BASE) \
	      -b _HEAP=$(KERNEL_HEAP_BASE) \
	      -b _IM2=$(KERNEL_IM2_BASE) \
	      -b _PAGE0=$(KERNEL_PAGE0_BASE) \
	      $(KERNEL_RELS); \
	HEAP_FINAL=$$(awk '$$1=="_HEAP" {print "0x"$$2; exit}' $$FINAL_MAP); \
	IM2_FINAL=$$(awk '$$1=="_IM2" {print "0x"$$2; exit}' $$FINAL_MAP); \
	PAGE0_FINAL=$$(awk '$$1=="_PAGE0" {print "0x"$$2; exit}' $$FINAL_MAP); \
	if [ $$(printf '%d' $$HEAP_FINAL) -ne $$(printf '%d' $(KERNEL_HEAP_BASE)) ]; then \
	    echo "error: kernel _HEAP expected at $(KERNEL_HEAP_BASE), got $$HEAP_FINAL"; \
	    exit 1; \
	fi; \
	if [ $$(printf '%d' $$IM2_FINAL) -ne $$(printf '%d' $(KERNEL_IM2_BASE)) ]; then \
	    echo "error: kernel _IM2 expected at $(KERNEL_IM2_BASE), got $$IM2_FINAL"; \
	    exit 1; \
	fi; \
	if [ $$(printf '%d' $$PAGE0_FINAL) -ne $$(printf '%d' $(KERNEL_PAGE0_BASE)) ]; then \
	    echo "error: kernel _PAGE0 expected at $(KERNEL_PAGE0_BASE), got $$PAGE0_FINAL"; \
	    exit 1; \
	fi; \
	TOTAL=$$(( ( $$(printf '%d' $$PAGE0_FINAL) + $$(printf '%d' $(KERNEL_PAGE0_SIZE)) ) - $$KERNEL_BASE )); \
	echo "$@: kernel_base=$$(printf '0x%04X' $$KERNEL_BASE) heap=$$HEAP_FINAL im2=$$IM2_FINAL page0=$$PAGE0_FINAL total=$$TOTAL"

$(ZX0):
	$(MAKE) -C ../tools/zx0 -s

$(BUILD)/bootstrap.bin: $(BUILD)/bootstrap.ihx
	$(OBJCOPY) -I ihex -O binary $< $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.ihx
	$(OBJCOPY) -I ihex -O binary $< $@

$(KERNEL_BIN): $(KERNEL_IHX)
	@mkdir -p $(BIN)
	@CODE_ADDR=$$(awk '$$1=="_CODE" {print "0x"$$2; exit}' $(BUILD)/kernel.map); \
	PAGE0_ADDR=$$(awk '$$1=="_PAGE0" {print "0x"$$2; exit}' $(BUILD)/kernel.map); \
	PAGE0_SIZE=$$(awk '$$1=="_PAGE0" {print "0x"$$3; exit}' $(BUILD)/kernel.map); \
	TOTAL=$$(( ( $$(printf '%d' $$PAGE0_ADDR) + $$(printf '%d' $$PAGE0_SIZE) ) - $$(printf '%d' $$CODE_ADDR) )); \
	$(OBJCOPY) -I ihex -O binary $< $@; \
	truncate -s $$TOTAL $@; \
	echo "$@: $$(stat -c%s $@) bytes"

# pack the rom: [bootstrap at 0x0000] + [zx0-compressed stage 1] + 0xff pad.
$(ROM): $(BUILD)/bootstrap.bin $(BUILD)/stage1.bin $(ZX0)
	@mkdir -p $(BIN)
	@BOOT_SIZE=$$(awk '$$1=="_BOOT" {print "0x"$$3; exit}' $(BOOTSTRAP_MAP)); \
	STAGE1_SIZE=$$(awk '$$1=="_BOOT" {print "0x"$$3; exit}' $(STAGE1_MAP)); \
	BOOT_LEN=$$(printf '%d' $$BOOT_SIZE); \
	STAGE1_LEN=$$(printf '%d' $$STAGE1_SIZE); \
	dd if=$(BUILD)/bootstrap.bin of=$(BUILD)/bootstrap.raw bs=1 count=$$BOOT_LEN status=none; \
	truncate -s $$BOOT_LEN $(BUILD)/bootstrap.raw; \
	dd if=$(BUILD)/stage1.bin of=$(BUILD)/stage1.raw bs=1 count=$$STAGE1_LEN status=none; \
	$(ZX0) -f $(BUILD)/stage1.raw $(BUILD)/stage1.zx0 >/dev/null; \
	CSTAGE1_LEN=$$(stat -c%s $(BUILD)/stage1.zx0); \
	TOTAL=$$(( $$BOOT_LEN + $$CSTAGE1_LEN )); \
	if [ $$TOTAL -gt $(ROM_SIZE) ]; then \
	    echo "error: rom overflow: bootstrap ($$BOOT_LEN) + compressed stage1 ($$CSTAGE1_LEN) = $$TOTAL > $(ROM_SIZE)"; \
	    exit 1; \
	fi; \
	head -c $(ROM_SIZE) /dev/zero | tr '\0' '\377' > $@; \
	dd if=$(BUILD)/bootstrap.raw of=$@ bs=1 conv=notrunc status=none; \
	dd if=$(BUILD)/stage1.zx0 of=$@ bs=1 seek=$$BOOT_LEN conv=notrunc status=none; \
	echo "$@: $$(stat -c%s $@) bytes (bootstrap=$$BOOT_LEN, stage1=$$STAGE1_LEN, compressed=$$CSTAGE1_LEN, free=$$(( $(ROM_SIZE) - $$TOTAL )))"

clean:
	rm -rf $(BUILD) $(BIN) Testing
