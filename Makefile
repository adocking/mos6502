CXX ?= g++
CXXFLAGS ?= -std=c++17 -g -O0 -Wall -Wextra
BUILD_DIR := build
TARGET := $(BUILD_DIR)/mos6502_demo

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): demo.cpp mos6502.cpp mos6502.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) demo.cpp mos6502.cpp -I. -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
