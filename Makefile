.PHONY: all configure build stage clean test fetch

BUILD_DIR := build
BIN_DIR := bin

# udap (https://github.com/retro-vault/udap) provides the DAP debugger
# library. It is fetched, never committed, and pinned to a tested revision.
UDAP_DIR := third_party/udap
UDAP_REPO := https://github.com/retro-vault/udap.git
UDAP_REF := 0bde11227670f22971a4771b0646718ab66badd2
UDAP_STAMP := $(UDAP_DIR)/.git/idp-ref-$(UDAP_REF)

all: build

fetch: $(UDAP_STAMP)

$(UDAP_STAMP):
	@if [ ! -d $(UDAP_DIR)/.git ]; then \
		git clone --no-checkout --depth 1 $(UDAP_REPO) $(UDAP_DIR); \
	fi
	git -C $(UDAP_DIR) fetch --depth 1 origin $(UDAP_REF)
	git -C $(UDAP_DIR) checkout --detach $(UDAP_REF)
	@touch $@

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: | fetch
	cmake -S . -B $(BUILD_DIR)

build: configure
	+cmake --build $(BUILD_DIR)
	cmake -E remove_directory $(abspath $(BIN_DIR))
	cmake --install $(BUILD_DIR) --prefix $(abspath $(BIN_DIR))

stage: build

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure
