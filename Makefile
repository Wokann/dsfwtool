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
EXECUTABLES = fwunpack # fwunpack2

# Define sources for each executable (relative to src/)
fwunpack_SRCS = fwunpack.cpp bitstream.cpp crc.cpp encryption.cpp get_encrypted_data.cpp get_normal_data.cpp keydata.cpp lz77.cpp part12_comp.cpp part345_comp.cpp tree.cpp 
# fwunpack2_SRCS = encryption.cpp fwunpack.cpp get_encrypted_data.cpp get_normal_data.cpp keydata.cpp lz77.cpp part345_comp.cpp

# Prepend source directory to all source files
fwunpack_SRCS := $(addprefix $(SRC_DIR)/, $(fwunpack_SRCS))
# fwunpack2_SRCS := $(addprefix $(SRC_DIR)/, $(fwunpack2_SRCS))

# Build rules for each executable with separate build folders
fwunpack: BUILD_DIR=$(BUILD_ROOT)/fwunpack
# fwunpack2: BUILD_DIR=$(BUILD_ROOT)/fwunpack2

# Generate object file paths
fwunpack: OBJS=$(patsubst $(SRC_DIR)/%.cpp, $(BUILD_ROOT)/fwunpack/%.o, $(fwunpack_SRCS))
# fwunpack2: OBJS=$(patsubst $(SRC_DIR)/%.cpp, $(BUILD_ROOT)/fwunpack2/%.o, $(fwunpack2_SRCS))

# Default target: build all executables
all: $(EXECUTABLES)

# Build each executable
$(EXECUTABLES):
	# Create build subfolder
	mkdir -p $(BUILD_DIR)
	# Compile source files to intermediate and object files in build subfolder
	$(foreach src, $($@_SRCS), \
		$(CXX) $(CXXFLAGS) -E $(src) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.i, $(src)); \
		$(CXX) $(CXXFLAGS) -S $(src) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.s, $(src)); \
		$(CXX) $(CXXFLAGS) -c $(src) -o $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(src));)
	# Link object files to create executable
	$(CXX) $(CXXFLAGS) -o $(RELEASE_DIR)/$@$(EXE_EXT) $(OBJS) $(LDFLAGS)

# Clean target: remove entire build and release folders
clean:
	rm -rf $(BUILD_ROOT) $(RELEASE_DIR)

# Phony targets
.PHONY: all clean
    