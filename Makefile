# Makefile for Sp3ctra - Modular Architecture
# Cross-platform audio synthesis application

# OS Detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Compiler settings
CC = gcc
CXX = g++

# Base compiler flags
BASE_CFLAGS = -O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -Wno-deprecated-declarations
BASE_CXXFLAGS = -std=c++17 -O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -Wno-unused-but-set-variable -Wno-deprecated-declarations

# OS-specific flags and libraries
ifeq ($(UNAME_S),Darwin)
    # macOS specific settings
    CFLAGS = $(BASE_CFLAGS)
    CXXFLAGS = $(BASE_CXXFLAGS)
    
    # Include directories for macOS
    INCLUDES = -I/opt/homebrew/include \
               -Isrc/core -Isrc/config -Isrc/audio/rtaudio -Isrc/audio/buffers -Isrc/audio/effects \
               -Isrc/audio/pan -Isrc/synthesis/additive -Isrc/synthesis/polyphonic \
               -Isrc/synthesis/polyphonic/kissfft -Isrc/communication/network -Isrc/communication/midi \
               -Isrc/communication/dmx -Isrc/display -Isrc/threading -Isrc/utils
    
    # macOS Libraries - Support both standard and versioned SFML paths
    SFML_PATH := $(shell if [ -d "/opt/homebrew/opt/sfml@2/lib" ]; then echo "/opt/homebrew/opt/sfml@2/lib"; else echo "/opt/homebrew/lib"; fi)
    LIBS = -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework Cocoa \
           -L/opt/homebrew/lib -L$(SFML_PATH) -lfftw3 -lsndfile -lrtaudio -lrtmidi \
           -lsfml-graphics -lsfml-window -lsfml-system \
           -lcsfml-graphics -lcsfml-window -lcsfml-system
else
    # Linux/Raspberry Pi specific settings
    CFLAGS = $(BASE_CFLAGS) -DNO_SFML -D__LINUX__ -march=native -mtune=native
    CXXFLAGS = $(BASE_CXXFLAGS) -DNO_SFML -D__LINUX__ -march=native -mtune=native
    
    # Include directories for Linux
    INCLUDES = -I/usr/include -I/usr/local/include \
               -Isrc/core -Isrc/config -Isrc/audio/rtaudio -Isrc/audio/buffers -Isrc/audio/effects \
               -Isrc/audio/pan -Isrc/synthesis/additive -Isrc/synthesis/polyphonic \
               -Isrc/synthesis/polyphonic/kissfft -Isrc/communication/network -Isrc/communication/midi \
               -Isrc/communication/dmx -Isrc/display -Isrc/threading -Isrc/utils
    
    # Linux Libraries
    LIBS = -L/usr/lib -L/usr/local/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi -lasound -lpthread -lm
endif

# Build directories
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/Sp3ctra

# Source files organized by module
CORE_SOURCES = src/core/main.c
AUDIO_RTAUDIO_SOURCES = src/audio/rtaudio/audio_c_interface.cpp src/audio/rtaudio/audio_rtaudio.cpp
AUDIO_BUFFERS_SOURCES = src/audio/buffers/audio_image_buffers.c
AUDIO_PAN_SOURCES = src/audio/pan/lock_free_pan.c
AUDIO_EFFECTS_SOURCES = src/audio/effects/auto_volume.c src/audio/effects/pareq.cpp \
                        src/audio/effects/three_band_eq.cpp src/audio/effects/ZitaRev1.cpp
SYNTHESIS_ADDITIVE_SOURCES = src/synthesis/additive/synth_additive.c src/synthesis/additive/wave_generation.c
SYNTHESIS_POLYPHONIC_SOURCES = src/synthesis/polyphonic/synth_polyphonic.c \
                               src/synthesis/polyphonic/kissfft/kiss_fft.c \
                               src/synthesis/polyphonic/kissfft/kiss_fftr.c
COMMUNICATION_SOURCES = src/communication/network/udp.c \
                        src/communication/midi/midi_controller.cpp \
                        src/communication/dmx/dmx.c
THREADING_SOURCES = src/threading/multithreading.c
UTILS_SOURCES = src/utils/shared.c src/utils/error.c src/utils/image_debug.c

# Conditional display sources (only include if NO_SFML is not defined)
ifeq ($(findstring -DNO_SFML,$(CFLAGS)),)
DISPLAY_SOURCES = src/display/display.c
else
DISPLAY_SOURCES =
endif

# All sources
ALL_SOURCES = $(CORE_SOURCES) $(AUDIO_RTAUDIO_SOURCES) $(AUDIO_BUFFERS_SOURCES) \
              $(AUDIO_PAN_SOURCES) $(AUDIO_EFFECTS_SOURCES) $(SYNTHESIS_ADDITIVE_SOURCES) \
              $(SYNTHESIS_POLYPHONIC_SOURCES) $(COMMUNICATION_SOURCES) \
              $(DISPLAY_SOURCES) $(THREADING_SOURCES) $(UTILS_SOURCES)

# Object files
OBJECTS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(ALL_SOURCES))) \
          $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(ALL_SOURCES)))

# Default target
all: $(TARGET)

# Create build directories
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/audio/rtaudio
	@mkdir -p $(OBJ_DIR)/audio/buffers
	@mkdir -p $(OBJ_DIR)/audio/pan
	@mkdir -p $(OBJ_DIR)/audio/effects
	@mkdir -p $(OBJ_DIR)/synthesis/additive
	@mkdir -p $(OBJ_DIR)/synthesis/polyphonic/kissfft
	@mkdir -p $(OBJ_DIR)/communication/network
	@mkdir -p $(OBJ_DIR)/communication/midi
	@mkdir -p $(OBJ_DIR)/communication/dmx
	@mkdir -p $(OBJ_DIR)/display
	@mkdir -p $(OBJ_DIR)/threading
	@mkdir -p $(OBJ_DIR)/utils

# Compile C files
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile C++ files
$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Link target
$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LIBS)

# Clean build files
clean:
	rm -rf $(BUILD_DIR)/obj $(TARGET)

# Full clean including build directory
distclean:
	rm -rf $(BUILD_DIR)

# Install target (optional)
install: $(TARGET)
	@echo "Installing Sp3ctra..."
	@echo "Installation complete"

# Debug information
debug:
	@echo "Sources: $(ALL_SOURCES)"
	@echo "Objects: $(OBJECTS)"
	@echo "Includes: $(INCLUDES)"

# Build without SFML (headless mode) - Cross-platform
no-sfml:
ifeq ($(UNAME_S),Darwin)
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -DNO_SFML" CXXFLAGS="$(BASE_CXXFLAGS) -DNO_SFML" \
	        LIBS="-framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework Cocoa -L/opt/homebrew/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi"
else
	$(MAKE) CFLAGS="$(BASE_CFLAGS) -DNO_SFML -D__LINUX__" CXXFLAGS="$(BASE_CXXFLAGS) -DNO_SFML -D__LINUX__" \
	        LIBS="-L/usr/lib -L/usr/local/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi -lasound -lpthread -lm"
endif

# Diagnostic target for SFML issues
sfml-check:
	@echo "=== SFML Diagnostic ==="
	@echo "Checking SFML installation..."
	@if command -v brew >/dev/null 2>&1; then \
		echo "Homebrew found"; \
		if brew list sfml@2 >/dev/null 2>&1; then \
			echo "✓ SFML@2 is installed"; \
			echo "SFML path: $$(brew --prefix sfml@2)"; \
			echo "Library files:"; \
			ls -la "$$(brew --prefix sfml@2)/lib/" | grep libsfml; \
		else \
			echo "✗ SFML@2 not found. Install with: brew install sfml@2"; \
		fi; \
	else \
		echo "✗ Homebrew not found"; \
	fi
	@echo "Current SFML_PATH: $(SFML_PATH)"
	@echo ""
	@echo "If SFML linking fails, try:"
	@echo "  make no-sfml    # Build without graphics interface"
	@echo "  brew install sfml@2  # Install SFML if missing"

# Help target
help:
	@echo "Sp3ctra Makefile - Modular Architecture"
	@echo ""
	@echo "Available targets:"
	@echo "  all         - Build the application (default)"
	@echo "  no-sfml     - Build without SFML (headless mode)"
	@echo "  clean       - Remove object files and executable"
	@echo "  distclean   - Remove entire build directory"
	@echo "  install     - Install the application"
	@echo "  debug       - Show build configuration"
	@echo "  sfml-check  - Diagnose SFML installation issues"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Troubleshooting:"
	@echo "  If build fails with 'library sfml-graphics not found':"
	@echo "    1. Run 'make sfml-check' to diagnose the issue"
	@echo "    2. Try 'make no-sfml' for headless operation"
	@echo "    3. Install SFML with 'brew install sfml@2'"
	@echo ""
	@echo "Modular structure:"
	@echo "  src/core/           - Application core (main, config, context)"
	@echo "  src/audio/          - Audio system (RtAudio, buffers, effects)"
	@echo "  src/synthesis/      - Synthesis engines (additive, polyphonic)"
	@echo "  src/communication/  - External communication (UDP, MIDI, DMX)"
	@echo "  src/display/        - Display and visualization"
	@echo "  src/threading/      - Threading and concurrency"
	@echo "  src/utils/          - Utilities and helpers"

.PHONY: all clean distclean install debug help
