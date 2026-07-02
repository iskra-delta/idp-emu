.PHONY: all configure build clean test fetch partos-smoke

BUILD_DIR := build
BIN_DIR := bin

# udap (https://github.com/retro-vault/udap) provides the DAP debugger
# library. It is fetched, never committed: a fresh clone always tracks
# the latest version.
UDAP_DIR := third_party/udap
UDAP_REPO := https://github.com/retro-vault/udap.git

all: build

fetch: $(UDAP_DIR)/CMakeLists.txt

$(UDAP_DIR)/CMakeLists.txt:
	git clone --depth 1 $(UDAP_REPO) $(UDAP_DIR)

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: | fetch
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

partos-smoke: build
	$(MAKE) -C partos disks
	python3 tools/check_partos_layout.py
	cd $(BUILD_DIR) && ctest --output-on-failure -R 'partos_(bootload|kernel_boot|full_boot|full_boot_fd0)'
