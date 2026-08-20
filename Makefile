.PHONY: all configure build stage clean test fetch

BUILD_DIR := build
BIN_DIR := bin

# udap (https://github.com/retro-vault/udap) provides the DAP debugger
# library. It is fetched, never committed, and pinned to a tested revision.
UDAP_DIR := third_party/udap
UDAP_REPO := https://github.com/retro-vault/udap.git
UDAP_REF := 41385b81191bcff03442934f0757ceb803ffc11a

all: build

fetch: $(UDAP_DIR)/CMakeLists.txt

$(UDAP_DIR)/CMakeLists.txt:
	git clone --depth 1 $(UDAP_REPO) $(UDAP_DIR)
	git -C $(UDAP_DIR) fetch --depth 1 origin $(UDAP_REF)
	git -C $(UDAP_DIR) checkout --detach $(UDAP_REF)

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: | fetch
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)
	cmake -E remove_directory $(abspath $(BIN_DIR))
	cmake --install $(BUILD_DIR) --prefix $(abspath $(BIN_DIR))

stage: build

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure
