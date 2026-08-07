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
CC      := sdcc
CCFLAGS := -mz80 --sdcccall 1 --disable-warning 323
LD      := sdldz80
OBJCOPY := objcopy
ZX0     := ../tools/zx0/bin/zx0
MAKECOM := ../tools/makecom.py

# x compiler suite (docker-wrapped, runs as the current user; uses ONLY the
# wischner/xcc-z80 image). Userland (src/apps/**) is compiled/assembled/linked
# with these; the ROM/kernel/OS asm still use the sdcc tools above for now.
XBIN     := $(abspath ../tools/xcc)
XCC      := $(XBIN)/xcc
XAS      := $(XBIN)/xas
XLD      := $(XBIN)/xld
XCC_FIX_PROLOGUE := ../tools/fix_xcc_prologue.py
# xcc lowers some ops (e.g. 16-bit multiply) to runtime helpers; pull them from
# the suite's runtime lib. `-nostdlib` keeps crt0/libc/platform libs out.
XRUNTIME := -L/opt/x/z80/lib -lruntime

BUILD := build
BIN   := bin
ROM   := $(BIN)/partos.rom

# Route only the userland object builds through the x suite (per-pattern
# overrides of the generic .rel rules below). Kernel/OS/ROM keep the sdcc tools.
$(BUILD)/apps/%.rel: AS      := $(XAS)
$(BUILD)/apps/%.rel: CC      := $(XCC)
# xcc 1.9.3 fixed the -O2+/-Os digit-drop optimizer bug (was 1.9.2) AND the
# sdcccall(1) stack-arg cleanup ABI bug (was BUG 2). cp/mv now work and -Os is
# usable.
# NOTE: the userland is NOT non-deterministic. Verified: two clean -Os builds
# produce byte-for-byte identical .rel/.com, and the same binary yields the same
# ctest result on every run. The earlier "16/19 then 12/19" flapping was the
# GOLDEN SUITE, not the build: several tests (e.g. gdp_shell_edit) boot the
# SHARED disks/*.img + NVRAM shadow in place (no per-test copy), and PartOS boots
# mutate them -- so results are RUN-ORDER dependent (a test can pass alone and
# fail mid-suite from the identical binary). Fix belongs in the tests (copy
# fixtures per test, like test_partos_cd_return does), not in the optimizer.
$(BUILD)/apps/%.rel: CCFLAGS := -Os
ASM_DEPS := $(shell find src -name '*.inc' | sort)
C_DEPS   := $(shell find include src/apps -name '*.h' | sort)

# physical rom size (hardware: 2 KB EPROM)
ROM_SIZE := 2048

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
KERNEL_SYSVARS_BASE := $(call inc_equ,KERNEL_SYSVARS_BASE)
KERNEL_EXEC_SIZE := $(call inc_equ,KERNEL_EXEC_SIZE)
# OS ceiling = lowest reserve address = KERNEL_SYSVARS_BASE - KERNEL_EXEC_SIZE.
KERNEL_RESERVE_FLOOR := $(shell printf '0x%04X' $$(( $(KERNEL_SYSVARS_BASE) - $(KERNEL_EXEC_SIZE) )))
UKERNEL_SECTORS := $(call inc_equ,UKERNEL_SECTORS)
SERVICES_LOAD_BASE := $(call inc_equ,SERVICES_LOAD_BASE)
OS_SYSVARS_BASE := $(call inc_equ,OS_SYSVARS_BASE)

# discover sources from the current tree instead of hardcoding deleted paths.
# stage 0 is always the same two-file bootstrap image at rom address 0x0000.
BOOTSTRAP_SRC := src/rom/bootstrap.s src/rom/dzx0_standard.s

# stage 1 is the rest of the bios, linked for ram execution at STAGE1_BASE.
# `start.s` must stay first so the bootstrap can jump straight to STAGE1_BASE.
BIOS_STAGE1_ALL := $(sort $(wildcard src/rom/*.s))
BIOS_STAGE1_ENTRY := $(filter src/rom/start.s,$(BIOS_STAGE1_ALL))
BIOS_STAGE1_OTHER := $(filter-out $(BIOS_STAGE1_ENTRY) src/rom/bootstrap.s src/rom/dzx0_standard.s,$(BIOS_STAGE1_ALL))
BIOS_STAGE1_SRC := $(BIOS_STAGE1_ENTRY) $(BIOS_STAGE1_OTHER)

BOOTSTRAP_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(BOOTSTRAP_SRC))
STAGE1_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(BIOS_STAGE1_SRC))
BOOTSTRAP_MAP := $(BUILD)/bootstrap.map
STAGE1_MAP := $(BUILD)/stage1.map

# --- split images (micro-kernel low / OS payload high) ---------------------
# kernel.sys: src/kernel/*.s linked at 0x0000.
# page0.s is the low page and MUST link FIRST so the rst/nmi dispatch lands on
# its fixed addresses. _HEAP/_IM2 are reserved high (not emitted); the disk
# image spans _CODE.._INITIALIZED.
KSYS_LOWPAGE  := src/kernel/page0.s
KSYS_REST     := $(filter-out $(KSYS_LOWPAGE),$(sort $(wildcard src/kernel/*.s)))
KSYS_SRC      := $(KSYS_LOWPAGE) $(KSYS_REST)
KSYS_RELS     := $(patsubst src/%.s,$(BUILD)/%.rel,$(KSYS_SRC))
KERNEL_SYS    := $(BIN)/kernel.sys
KERNEL_MAP    := $(BUILD)/kernel.map

# os.sys: one fixed entry stub at 0xc000, then drivers + the higher-level OS.
# it resolves the kernel symbols it calls directly through an absolute-equate
# stub generated from the kernel link map (kernel_imports.s); a bare payload
# would instead discover the ABI via rst 0x08.
OSYS_LAYOUT   := src/os/layout.s
OSYS_ENTRY    := src/os/entry.s
OSYS_DRIVERS  := $(sort $(wildcard src/drivers/*.s))
OSYS_OTHER    := $(filter-out $(OSYS_LAYOUT) $(OSYS_ENTRY),$(sort $(wildcard src/os/*.s)))
OSYS_SRC      := $(OSYS_LAYOUT) $(OSYS_ENTRY) $(OSYS_DRIVERS) $(OSYS_OTHER)
OSYS_RELS     := $(patsubst src/%.s,$(BUILD)/%.rel,$(OSYS_SRC))
OS_SYS        := $(BIN)/os.sys
KIMPORTS_S    := $(BUILD)/kernel_imports.s
KIMPORTS_REL  := $(BUILD)/kernel_imports.rel
PARTOS_LAYOUT_HEADER := $(BUILD)/partos_layout.hpp

# userland `.COM` images now wrap relocatable `.XL` files produced by `xld`.
# we still keep the flat shell payload map around because the emulator probes
# use it for symbol lookup when tracing the live relocated shell image.
# first userland payload: shell.com
SHELL_PAYLOAD_SRC := src/apps/shell/shell.s
SHELL_PAYLOAD_REL := $(patsubst src/%.s,$(BUILD)/%.rel,$(SHELL_PAYLOAD_SRC))
SHELL_PAYLOAD_IHX := $(BUILD)/shell_payload.ihx
SHELL_PAYLOAD_MAP := $(BUILD)/shell_payload.map
SHELL_PAYLOAD_BIN := $(BUILD)/shell_payload.bin
SHELL_XL          := $(BUILD)/shell.xl
SHELL_COM         := $(BIN)/shell.com
SHELL_STACK       := 2048

# second userland payload: ls.com
LS_PAYLOAD_SRC := src/apps/ls/ls.c
LS_PAYLOAD_REL := $(patsubst src/%.c,$(BUILD)/%.rel,$(LS_PAYLOAD_SRC))
LS_PAYLOAD_IHX := $(BUILD)/ls_payload.ihx
LS_PAYLOAD_MAP := $(BUILD)/ls_payload.map
LS_PAYLOAD_BIN := $(BUILD)/ls_payload.bin
LS_XL          := $(BUILD)/ls.xl
LS_COM         := $(BIN)/ls.com
LS_STACK       := 1024

# third userland payload: ps.com
PS_PAYLOAD_SRC := src/apps/ps/ps.c
PS_PAYLOAD_REL := $(patsubst src/%.c,$(BUILD)/%.rel,$(PS_PAYLOAD_SRC))
PS_PAYLOAD_IHX := $(BUILD)/ps_payload.ihx
PS_PAYLOAD_MAP := $(BUILD)/ps_payload.map
PS_PAYLOAD_BIN := $(BUILD)/ps_payload.bin
PS_XL          := $(BUILD)/ps.xl
PS_COM         := $(BIN)/ps.com
PS_STACK       := 1024

# shared support module for the newer syscall-only tools
# NOTE: call_hl.s / call_iy.s (SDCC's ___sdcc_call_hl/_iy indirect-call helpers)
# are gone: xcc lowers function-pointer calls to ___sdcc_call_bc, which it
# supplies from its own -lruntime, so those SDCC-named helpers are never
# referenced.
APP_SUPPORT_ASM_SRC := src/apps/lib/crt0.s src/apps/lib/bootstrap.s src/apps/lib/cwdcall.s src/apps/lib/fscall.s src/apps/lib/svccall.s
APP_SUPPORT_ASM_RELS := $(patsubst src/%.s,$(BUILD)/%.rel,$(APP_SUPPORT_ASM_SRC))
APP_SUPPORT_C_SRC := src/apps/lib/runtime.c src/apps/lib/partos_c.c src/apps/lib/libc.c
APP_SUPPORT_C_REL := $(patsubst src/%.c,$(BUILD)/%.rel,$(APP_SUPPORT_C_SRC))
APP_SUPPORT_RELS := $(APP_SUPPORT_ASM_RELS) $(APP_SUPPORT_C_REL)

SHELL_SUPPORT_C_SRC := src/apps/shell/libc.c
SHELL_SUPPORT_C_REL := $(patsubst src/%.c,$(BUILD)/%.rel,$(SHELL_SUPPORT_C_SRC))

# additional syscall-only tools
MEM_APP_SRC    := src/apps/mem/mem.c
MEM_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(MEM_APP_SRC))
MEM_APP_XL     := $(BUILD)/mem.xl
MEM_APP_COM    := $(BIN)/mem.com
MEM_APP_STACK  := 1024

CAT_APP_SRC    := src/apps/cat/cat.c
CAT_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(CAT_APP_SRC))
CAT_APP_XL     := $(BUILD)/cat.xl
CAT_APP_COM    := $(BIN)/cat.com
CAT_APP_STACK  := 1024

CD_APP_SRC     := src/apps/cd/cd.c
CD_APP_REL     := $(patsubst src/%.c,$(BUILD)/%.rel,$(CD_APP_SRC))
CD_APP_XL      := $(BUILD)/cd.xl
CD_APP_COM     := $(BIN)/cd.com
CD_APP_STACK   := 1024

MKDIR_APP_SRC   := src/apps/mkdir/mkdir.c
MKDIR_APP_REL   := $(patsubst src/%.c,$(BUILD)/%.rel,$(MKDIR_APP_SRC))
MKDIR_APP_XL    := $(BUILD)/mkdir.xl
MKDIR_APP_COM   := $(BIN)/mkdir.com
MKDIR_APP_STACK := 1024

RMDIR_APP_SRC   := src/apps/rmdir/rmdir.c
RMDIR_APP_REL   := $(patsubst src/%.c,$(BUILD)/%.rel,$(RMDIR_APP_SRC))
RMDIR_APP_XL    := $(BUILD)/rmdir.xl
RMDIR_APP_COM   := $(BIN)/rmdir.com
RMDIR_APP_STACK := 1024

DEL_APP_SRC    := src/apps/del/del.c
DEL_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(DEL_APP_SRC))
DEL_APP_XL     := $(BUILD)/del.xl
DEL_APP_COM    := $(BIN)/del.com
DEL_APP_STACK  := 1024

CP_APP_SRC    := src/apps/cp/cp.c
CP_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(CP_APP_SRC))
CP_APP_XL     := $(BUILD)/cp.xl
CP_APP_COM    := $(BIN)/cp.com
CP_APP_STACK  := 1024

MV_APP_SRC    := src/apps/mv/mv.c
MV_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(MV_APP_SRC))
MV_APP_XL     := $(BUILD)/mv.xl
MV_APP_COM    := $(BIN)/mv.com
MV_APP_STACK  := 1024

CLEAR_APP_SRC    := src/apps/clear/clear.c
CLEAR_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(CLEAR_APP_SRC))
CLEAR_APP_XL     := $(BUILD)/clear.xl
CLEAR_APP_COM    := $(BIN)/clear.com
CLEAR_APP_STACK  := 512

ECHO_APP_SRC    := src/apps/echo/echo.c
ECHO_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(ECHO_APP_SRC))
ECHO_APP_XL     := $(BUILD)/echo.xl
ECHO_APP_COM    := $(BIN)/echo.com
ECHO_APP_STACK  := 512

HELP_APP_SRC    := src/apps/help/help.c
HELP_APP_REL    := $(patsubst src/%.c,$(BUILD)/%.rel,$(HELP_APP_SRC))
HELP_APP_XL     := $(BUILD)/help.xl
HELP_APP_COM    := $(BIN)/help.com
HELP_APP_STACK  := 512

APP_TOOL_COMS := $(MEM_APP_COM) $(CAT_APP_COM) $(CD_APP_COM) $(MKDIR_APP_COM) $(RMDIR_APP_COM) \
                 $(DEL_APP_COM) $(CP_APP_COM) $(MV_APP_COM) $(CLEAR_APP_COM) \
                 $(ECHO_APP_COM) $(HELP_APP_COM)

DISKS_DIR := ../disks

.PHONY: all sys kernel os shell rom disks clean
all: sys rom
sys: $(KERNEL_SYS) $(OS_SYS) $(PARTOS_LAYOUT_HEADER) $(SHELL_COM) $(LS_COM) $(PS_COM) $(APP_TOOL_COMS)
kernel: $(KERNEL_SYS)
os: $(OS_SYS)
shell: $(SHELL_COM) $(LS_COM) $(PS_COM) $(APP_TOOL_COMS)
rom: $(ROM)
disks: sys
	python3 ../tools/mkdosdisk.py $(DISKS_DIR)

# assemble from within the source directory so .include paths resolve.
$(BUILD)/%.rel: src/%.s $(ASM_DEPS)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(AS) -o $(abspath $@) $(notdir $<)

$(BUILD)/apps/%.rel: src/apps/%.c $(C_DEPS) $(XCC_FIX_PROLOGUE)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(CC) $(CCFLAGS) -S -o $(abspath $@).s $(notdir $<)
	python3 $(XCC_FIX_PROLOGUE) $(abspath $@).s $(abspath $@).fixed.s
	$(AS) -o $(abspath $@) $(abspath $@).fixed.s

$(BUILD)/%.rel: src/%.c $(C_DEPS)
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(CC) $(CCFLAGS) -c -o $(abspath $@) $(notdir $<)

$(BUILD)/bootstrap.ihx: $(BOOTSTRAP_RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _BOOT=$(BOOT_BASE) \
	      $(BOOTSTRAP_RELS)

$(BUILD)/stage1.ihx: $(STAGE1_RELS)
	$(LD) -n -m -i $(abspath $@) \
	      -b _BOOT=$(STAGE1_BASE) -b _SYSVARS=$(SYSVARS_BASE) \
	      $(STAGE1_RELS)

# --- kernel.sys: micro-kernel image linked at 0x0000 -----------------------
# page0.rel links first (low page at 0x0000). _HEAP/_IM2 are reserved at their
# fixed high addresses (no data emitted); the disk image is the packed low-page
# code/data plus any separately linked _INITIALIZED bytes.
# the wide (-w) map keeps full symbol names so the OS import stub can be
# generated from it. on the same pass we emit kernel_imports.s for the OS link.
$(KERNEL_SYS): $(KSYS_RELS)
	@mkdir -p $(BIN)
	$(LD) -n -m -w -i $(abspath $(BUILD)/kernel.ihx) \
	      -b _CODE=0x0000 \
	      -b _SYSVARS=$(KERNEL_SYSVARS_BASE) \
	      -b _HEAP=$(KERNEL_HEAP_BASE) \
	      -b _IM2=$(KERNEL_IM2_BASE) \
	      $(KSYS_RELS)
	@CODE_SZ=$$(printf '%d' 0x$$(awk '$$1=="_CODE"{print $$3; exit}' $(KERNEL_MAP))); \
	INIT_HEX=$$(awk 'BEGIN{v="0"} $$1=="_INITIALIZED"{v=$$3; found=1; exit} END{print v}' $(KERNEL_MAP)); \
	INIT_SZ=$$(printf '%d' 0x$$INIT_HEX); \
	IMG=$$(( $$CODE_SZ + $$INIT_SZ )); \
	BUDGET=$$(( $(UKERNEL_SECTORS) * 256 )); \
	$(OBJCOPY) -I ihex -O binary $(BUILD)/kernel.ihx $@; \
	truncate -s $$IMG $@; \
	SECT=$$(( ($$IMG + 255) / 256 )); \
	echo "$@: $$IMG-byte image ($$SECT sectors) = _CODE $$CODE_SZ (mirrored low page) + _INITIALIZED $$INIT_SZ (table/vectors)"; \
	if [ $$CODE_SZ -gt $$BUDGET ]; then \
	    echo "  WARNING: mirrored _CODE ($$CODE_SZ) over the low-page budget ($$BUDGET) by $$(( $$CODE_SZ - $$BUDGET )) bytes."; \
	else \
	    echo "  micro-kernel _CODE fits the low-page budget ($$(( $$BUDGET - $$CODE_SZ )) bytes free)."; \
	fi
	@# generate the absolute-equate import stub the OS links against. every
	@# kernel global becomes `name == 0xADDR`; OS externals resolve to these --
	@# EXCEPT the kernel primitives the OS now reaches through rst 0x08
	@# trampolines (src/os/kcall.s). those would clash with kcall.rel at link,
	@# so exclude exactly the trampoline `::` labels (auto-derived, no list to
	@# keep in sync). data heads, vector targets and bcd/bin leaf helpers are
	@# not defined in kcall.s and therefore stay as absolute imports.
	@SKIP=$$(grep -oE '^[A-Za-z_][A-Za-z0-9_]*::' src/os/kcall.s | sed 's/:://'); \
	awk -v skip="$$SKIP" 'BEGIN{ ns=split(skip,s," "); for(i=1;i<=ns;i++) sk[s[i]]=1 } \
	    /^ +[0-9A-Fa-f]{8}  +[A-Za-z_]/ { \
	        a=$$1; n=$$2; \
	        if (n ~ /^[sl]__/) next; \
	        if (n in sk) next; \
	        if (n in seen) next; seen[n]=1; \
	        printf "            %s == 0x%s\n", n, a; \
	    }' $(KERNEL_MAP) > $(KIMPORTS_S)
	@echo "$(KIMPORTS_S): $$(grep -c '==' $(KIMPORTS_S)) kernel symbols exported to the OS (kcall.s trampolines excluded)"

# --- os.sys: OS/payload image linked at 0xc000 -----------------------------
$(KIMPORTS_REL): $(KERNEL_SYS)
	cd $(BUILD) && $(AS) -o kernel_imports.rel kernel_imports.s

$(OS_SYS): $(OSYS_RELS) $(KIMPORTS_REL)
	@mkdir -p $(BIN)
	$(LD) -n -m -w -i $(abspath $(BUILD)/os.ihx) \
	      -b _CODE=$(SERVICES_LOAD_BASE) \
	      -b _SYSVARS=$(OS_SYSVARS_BASE) \
	      $(OSYS_RELS) $(KIMPORTS_REL)
	@OS_MAP=$(BUILD)/os.map; \
	CODE_SZ=$$(printf '%d' 0x$$(awk '$$1=="_CODE"{print $$3; exit}' $$OS_MAP)); \
	INIT_HEX=$$(awk 'BEGIN{v="0"} $$1=="_INITIALIZED"{v=$$3; found=1; exit} END{print v}' $$OS_MAP); \
	INIT_SZ=$$(printf '%d' 0x$$INIT_HEX); \
	IMG=$$(( $$CODE_SZ + $$INIT_SZ )); \
	RAW=$(BUILD)/os.full.bin; \
	LOW_ADDR=$$(python3 -c 'from pathlib import Path; addrs=[int(line[3:7], 16) for line in Path("$(BUILD)/os.ihx").read_text().splitlines() if line.startswith(":") and int(line[7:9], 16) == 0]; print(min(addrs) if addrs else 0)'); \
	LOAD_SKIP=$$(( $(SERVICES_LOAD_BASE) - $$LOW_ADDR )); \
	$(OBJCOPY) -I ihex -O binary $(BUILD)/os.ihx $$RAW; \
	dd if=$$RAW of=$@ bs=1 skip=$$LOAD_SKIP count=$$IMG status=none; \
	truncate -s $$IMG $@; \
	rm -f $$RAW; \
	SZ=$$(stat -c%s $@); \
	BASE=$$(printf '%d' $(SERVICES_LOAD_BASE)); \
	TOP=$$(( $$BASE + $$SZ )); \
	FLOOR=$$(printf '%d' $(KERNEL_RESERVE_FLOOR)); \
	echo "$@: $$SZ bytes = _CODE $$CODE_SZ + _INITIALIZED $$INIT_SZ (loads at $(SERVICES_LOAD_BASE), ends $$(printf '0x%04X' $$TOP); kernel reserve floor $(KERNEL_RESERVE_FLOOR))"; \
	if [ $$TOP -gt $$FLOOR ]; then \
	    echo "  WARNING: os.sys overruns the kernel runtime reserve ($(KERNEL_RESERVE_FLOOR)) by $$(( $$TOP - $$FLOOR )) bytes."; \
	fi

$(PARTOS_LAYOUT_HEADER): $(KERNEL_SYS) $(OS_SYS) ../tools/check_partos_layout.py
	python3 ../tools/check_partos_layout.py --header $@
	@echo "$@: generated from kernel/os maps"

# --- shell payload / shell.com ---------------------------------------------
$(SHELL_PAYLOAD_BIN): $(SHELL_PAYLOAD_REL) $(SHELL_SUPPORT_C_REL)
	$(LD) -n -m -w -i $(abspath $(SHELL_PAYLOAD_IHX)) \
	      -b _CODE=0x0000 \
	      $(SHELL_PAYLOAD_REL) $(SHELL_SUPPORT_C_REL)
	@CODE_SZ=$$(printf '%d' 0x$$(awk '$$1=="_CODE"{print $$3; exit}' $(SHELL_PAYLOAD_MAP))); \
	$(OBJCOPY) -I ihex -O binary $(SHELL_PAYLOAD_IHX) $@; \
	truncate -s $$CODE_SZ $@; \
	echo "$@: $$CODE_SZ-byte flat payload"

$(SHELL_PAYLOAD_MAP): $(SHELL_PAYLOAD_BIN)
	@:

$(SHELL_XL): $(SHELL_PAYLOAD_REL) $(SHELL_SUPPORT_C_REL) Makefile
	@mkdir -p $(BUILD)
	$(XLD) --mode=sdcc -nostdlib --no-default-runtime \
	      -Ttext=0x0000 \
	      -e shell_entry \
	      -o $@ \
	      $(SHELL_PAYLOAD_REL) $(SHELL_SUPPORT_C_REL) $(XRUNTIME)
	@echo "$@: $$(stat -c%s $@) bytes (relocatable XL via xld)"

$(SHELL_COM): $(SHELL_XL) $(SHELL_PAYLOAD_MAP) $(MAKECOM) Makefile
	@mkdir -p $(BIN)
	python3 $(MAKECOM) \
	    --input-xl $(SHELL_XL) \
	    --stack $(SHELL_STACK) \
	    -o $@
	@echo "$@: $$(stat -c%s $@) bytes (stack=$(SHELL_STACK), padded COM)"

# --- ls payload / ls.com ---------------------------------------------------
$(LS_PAYLOAD_BIN): $(APP_SUPPORT_RELS) $(LS_PAYLOAD_REL)
	$(LD) -n -m -w -i $(abspath $(LS_PAYLOAD_IHX)) \
	      -b _CODE=0x0000 \
	      $(APP_SUPPORT_RELS) $(LS_PAYLOAD_REL)
	@CODE_SZ=$$(printf '%d' 0x$$(awk '$$1=="_CODE"{print $$3; exit}' $(LS_PAYLOAD_MAP))); \
	$(OBJCOPY) -I ihex -O binary $(LS_PAYLOAD_IHX) $@; \
	truncate -s $$CODE_SZ $@; \
	echo "$@: $$CODE_SZ-byte flat payload"

$(LS_PAYLOAD_MAP): $(LS_PAYLOAD_BIN)
	@:

$(LS_XL): $(APP_SUPPORT_RELS) $(LS_PAYLOAD_REL) Makefile
	@mkdir -p $(BUILD)
	$(XLD) --mode=sdcc -nostdlib --no-default-runtime \
	      -Ttext=0x0000 \
	      -e _app_crt0_entry \
	      -o $@ \
	      $(APP_SUPPORT_RELS) $(LS_PAYLOAD_REL) $(XRUNTIME)
	@echo "$@: $$(stat -c%s $@) bytes (relocatable XL via xld)"

$(LS_COM): $(LS_XL) $(MAKECOM) Makefile
	@mkdir -p $(BIN)
	python3 $(MAKECOM) \
	    --input-xl $(LS_XL) \
	    --stack $(LS_STACK) \
	    -o $@
	@echo "$@: $$(stat -c%s $@) bytes (stack=$(LS_STACK), padded COM)"

# --- ps payload / ps.com ---------------------------------------------------
$(PS_PAYLOAD_BIN): $(APP_SUPPORT_RELS) $(PS_PAYLOAD_REL)
	$(LD) -n -m -w -i $(abspath $(PS_PAYLOAD_IHX)) \
	      -b _CODE=0x0000 \
	      $(APP_SUPPORT_RELS) $(PS_PAYLOAD_REL)
	@CODE_SZ=$$(printf '%d' 0x$$(awk '$$1=="_CODE"{print $$3; exit}' $(PS_PAYLOAD_MAP))); \
	$(OBJCOPY) -I ihex -O binary $(PS_PAYLOAD_IHX) $@; \
	truncate -s $$CODE_SZ $@; \
	echo "$@: $$CODE_SZ-byte flat payload"

$(PS_PAYLOAD_MAP): $(PS_PAYLOAD_BIN)
	@:

$(PS_XL): $(APP_SUPPORT_RELS) $(PS_PAYLOAD_REL) Makefile
	@mkdir -p $(BUILD)
	$(XLD) --mode=sdcc -nostdlib --no-default-runtime \
	      -Ttext=0x0000 \
	      -e _app_crt0_entry \
	      -o $@ \
	      $(APP_SUPPORT_RELS) $(PS_PAYLOAD_REL) $(XRUNTIME)
	@echo "$@: $$(stat -c%s $@) bytes (relocatable XL via xld)"

$(PS_COM): $(PS_XL) $(MAKECOM) Makefile
	@mkdir -p $(BIN)
	python3 $(MAKECOM) \
	    --input-xl $(PS_XL) \
	    --stack $(PS_STACK) \
	    -o $@
	@echo "$@: $$(stat -c%s $@) bytes (stack=$(PS_STACK), padded COM)"

define APP_TOOL_template
$($(1)_XL): $(APP_SUPPORT_RELS) $$($(1)_REL) Makefile
	@mkdir -p $(BUILD)
	$(XLD) --mode=sdcc -nostdlib --no-default-runtime \
	      -Ttext=0x0000 \
	      -e _app_crt0_entry \
	      -o $$@ \
	      $(APP_SUPPORT_RELS) $$($(1)_REL) $(XRUNTIME)
	@echo "$$@: $$(stat -c%s $$@) bytes (relocatable XL via xld)"

$($(1)_COM): $$($(1)_XL) $(MAKECOM) Makefile
	@mkdir -p $(BIN)
	python3 $(MAKECOM) \
	    --input-xl $$($(1)_XL) \
	    --stack $$($(1)_STACK) \
	    -o $$@
	@echo "$$@: $$(stat -c%s $$@) bytes (stack=$$($(1)_STACK), padded COM)"
endef

$(eval $(call APP_TOOL_template,MEM_APP,mem))
$(eval $(call APP_TOOL_template,CAT_APP,cat))
$(eval $(call APP_TOOL_template,CD_APP,cd))
$(eval $(call APP_TOOL_template,MKDIR_APP,mkdir))
$(eval $(call APP_TOOL_template,RMDIR_APP,rmdir))
$(eval $(call APP_TOOL_template,DEL_APP,del))
$(eval $(call APP_TOOL_template,CP_APP,cp))
$(eval $(call APP_TOOL_template,MV_APP,mv))
$(eval $(call APP_TOOL_template,CLEAR_APP,clear))
$(eval $(call APP_TOOL_template,ECHO_APP,echo))
$(eval $(call APP_TOOL_template,HELP_APP,help))

$(ZX0):
	$(MAKE) -C ../tools/zx0 -s

$(BUILD)/bootstrap.bin: $(BUILD)/bootstrap.ihx
	$(OBJCOPY) -I ihex -O binary $< $@

$(BUILD)/stage1.bin: $(BUILD)/stage1.ihx
	$(OBJCOPY) -I ihex -O binary $< $@

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
