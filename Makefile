# Multi-platform Makefile for dsfwtool.
#
# Windows and Linux builds are linked statically by default so the released
# executable has no MinGW/MSYS2, libgcc, or host-libc runtime dependency.
# Windows still imports its operating-system DLLs (for example KERNEL32.dll),
# as every native Windows executable does.  macOS does not support a fully
# static system runtime; its build is a universal single-file executable that
# uses only the macOS-provided libSystem runtime.

UNAME_S := $(shell uname -s 2>/dev/null)

CC ?= gcc
CFLAGS ?= -Wall -O2 -Iinclude -std=c99
LDFLAGS ?=
LDLIBS ?=

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
RUNTIME_LDFLAGS ?= -static -static-libgcc
else ifeq ($(UNAME_S),Linux)
EXE_EXT :=
RUNTIME_LDFLAGS ?= -static -static-libgcc
else ifeq ($(UNAME_S),Darwin)
EXE_EXT :=
ARCHFLAGS ?= -arch x86_64 -arch arm64
RUNTIME_LDFLAGS ?=
else
EXE_EXT :=
RUNTIME_LDFLAGS ?=
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
	$(CC) $(CFLAGS) $(ARCHFLAGS) $(LDFLAGS) $(RUNTIME_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_ROOT)/dsfwtool/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(ARCHFLAGS) -x c -c $< -o $@

clean:
	rm -rf $(BUILD_ROOT) $(RELEASE_DIR)
