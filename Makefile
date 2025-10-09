# Multi-platform Makefile with separate build subfolders
# Supports Linux, macOS, and Windows (MinGW)

# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -O2 -Iinclude
LDFLAGS =

# Platform-specific executable extension
ifeq ($(OS),Windows_NT)
    EXE_EXT = .exe
else
    EXE_EXT =
endif

# Directories
SRC_DIR = src
BUILD_ROOT = build
RELEASE_DIR = release

# Create root directories if they don't exist
$(shell mkdir -p $(BUILD_ROOT) $(RELEASE_DIR))

# Explicitly list your executables here
EXECUTABLES = fwpack fwcrypt fwcomp

# Define sources for each executable (relative to src/)
fwpack_SRCS = fwpack.cpp encryption.cpp keydata.cpp part12_comp.cpp part345_comp.cpp bitstream.cpp tree.cpp
fwcrypt_SRCS = fwcrypt.cpp encryption.cpp keydata.cpp
fwcomp_SRCS = fwcomp.cpp part12_comp.cpp part345_comp.cpp bitstream.cpp tree.cpp

# Prepend source directory to all source files
fwpack_SRCS := $(addprefix $(SRC_DIR)/, $(fwpack_SRCS))
fwcrypt_SRCS := $(addprefix $(SRC_DIR)/, $(fwcrypt_SRCS))
fwcomp_SRCS := $(addprefix $(SRC_DIR)/, $(fwcomp_SRCS))

# Build rules for each executable with separate build folders
fwpack: BUILD_DIR=$(BUILD_ROOT)/fwpack
fwcrypt: BUILD_DIR=$(BUILD_ROOT)/fwcrypt
fwcomp: BUILD_DIR=$(BUILD_ROOT)/fwcomp

# Generate object file paths
fwpack: OBJS=$(patsubst $(SRC_DIR)/%.cpp, $(BUILD_ROOT)/fwpack/%.o, $(fwpack_SRCS))
fwcrypt: OBJS=$(patsubst $(SRC_DIR)/%.cpp, $(BUILD_ROOT)/fwcrypt/%.o, $(fwcrypt_SRCS))
fwcomp: OBJS=$(patsubst $(SRC_DIR)/%.cpp, $(BUILD_ROOT)/fwcomp/%.o, $(fwcomp_SRCS))

# Default target: build all executables
all: $(EXECUTABLES)

# Build each executable with chained compilation
$(EXECUTABLES):
	# Create build subfolder
	mkdir -p $(BUILD_DIR)
	# Compile with chained steps: preprocess → assemble → compile → link
	$(foreach src, $($@_SRCS), \
		$(CXX) $(CXXFLAGS) -E $(src) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.i, $(src)) && \
		$(CXX) $(CXXFLAGS) -S $(BUILD_DIR)/$(notdir $(patsubst %.cpp, %.i, $(src))) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.s, $(src)) && \
		$(CXX) $(CXXFLAGS) -c $(BUILD_DIR)/$(notdir $(patsubst %.cpp, %.s, $(src))) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(src));)
	# Link object files to create executable
	$(CXX) $(CXXFLAGS) -o $(RELEASE_DIR)/$@$(EXE_EXT) $(OBJS) $(LDFLAGS)

# Clean target: remove entire build and release folders
clean:
	rm -rf $(BUILD_ROOT) $(RELEASE_DIR)

# Phony targets
.PHONY: all clean
