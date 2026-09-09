# Multi-platform Makefile for dsfwtool.
# Supports Linux, macOS, and Windows (MinGW/MSYS2).

CC ?= gcc
CFLAGS ?= -Wall -O2 -Iinclude -std=c99
LDFLAGS =

ifeq ($(OS),Windows_NT)
EXE_EXT = .exe
else
EXE_EXT =
endif

SRC_DIR = src
BUILD_ROOT = build
RELEASE_DIR = release

EXECUTABLES = dsfwtool

dsfwtool_SRCS = dsfwtool.cpp encryption.cpp keydata.cpp part12_comp.cpp part345_comp.cpp bitstream.cpp tree.cpp crc.cpp

dsfwtool_SRCS := $(addprefix $(SRC_DIR)/,$(dsfwtool_SRCS))

dsfwtool_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_ROOT)/dsfwtool/%.o,$(dsfwtool_SRCS))

.PHONY: all clean $(EXECUTABLES)

all: $(EXECUTABLES)

dsfwtool: $(RELEASE_DIR)/dsfwtool$(EXE_EXT)

$(RELEASE_DIR)/dsfwtool$(EXE_EXT): $(dsfwtool_OBJS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_ROOT)/dsfwtool/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -x c -c $< -o $@

clean:
	rm -rf $(BUILD_ROOT) $(RELEASE_DIR)
