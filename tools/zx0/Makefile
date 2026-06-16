.PHONY: all clean asm-check

CXX ?= g++
AS := sdasz80

CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic
LDFLAGS ?=

BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/zx0
ASM_TARGET := $(BUILD_DIR)/dzx0_standard.rel

SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

asm-check: $(ASM_TARGET)

$(ASM_TARGET): src/dzx0_standard.s
	@mkdir -p $(BUILD_DIR)
	$(AS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
