.PHONY: all configure build clean test

BUILD_DIR := build
BIN_DIR := bin

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR)

build: configure
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure
