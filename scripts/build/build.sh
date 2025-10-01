#!/bin/bash

# Unified build script for Sp3ctra
# Cross-platform: macOS and Linux (Raspberry Pi)
# Author: Cline

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Stop on error
set -e

# Default options
NO_DISPLAY=0
VERBOSE=0
DEBUG_BUILD=0
CLEAN_BUILD=0

# Process command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --no-display)
      NO_DISPLAY=1
      shift
      ;;
    --verbose)
      VERBOSE=1
      shift
      ;;
    --debug)
      DEBUG_BUILD=1
      shift
      ;;
    --clean)
      CLEAN_BUILD=1
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [OPTIONS]"
      echo ""
      echo "OPTIONS:"
      echo "  --no-display    Build without SFML display (headless/CLI mode)"
      echo "  --verbose       Show detailed build output"
      echo "  --debug         Build in debug mode (-O0 -g)"
      echo "  --clean         Clean before building"
      echo "  --help, -h      Show this help message"
      echo ""
      echo "Examples:"
      echo "  $0                        # Build with default settings"
      echo "  $0 --no-display           # Build without graphics (CLI mode)"
      echo "  $0 --clean --verbose      # Clean build with verbose output"
      echo "  $0 --debug --no-display   # Debug build without display"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use --help for usage information"
      exit 1
      ;;
  esac
done

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}  Building Sp3ctra                              ${NC}"
echo -e "${BLUE}================================================${NC}"

# Step 1: Verify we're in the correct directory
echo -e "${BLUE}=== Step 1: Directory Verification ===${NC}"

if [ ! -f "Makefile" ]; then
    echo -e "${RED}❌ Makefile not found in current directory: $(pwd)${NC}"
    echo -e "${YELLOW}💡 Please run this script from the Sp3ctra_Application root directory${NC}"
    exit 1
else
    echo -e "${GREEN}✅ Makefile found${NC}"
fi

# Step 2: System information
echo -e "\n${BLUE}=== Step 2: System Information ===${NC}"
UNAME_S=$(uname -s)
UNAME_M=$(uname -m)
echo -e "${CYAN}OS: $UNAME_S${NC}"
echo -e "${CYAN}Architecture: $UNAME_M${NC}"
echo -e "${CYAN}Working directory: $(pwd)${NC}"

if [ "$UNAME_S" = "Darwin" ]; then
    echo -e "${CYAN}Platform: macOS${NC}"
elif [ "$UNAME_S" = "Linux" ]; then
    echo -e "${CYAN}Platform: Linux/Raspberry Pi${NC}"
    NPROC=$(nproc 2>/dev/null || echo 1)
    echo -e "${CYAN}Processor count: $NPROC${NC}"
fi

# Step 3: Check build dependencies (Linux only)
if [ "$UNAME_S" = "Linux" ]; then
    echo -e "\n${BLUE}=== Step 3: Dependency Check ===${NC}"
    
    MISSING_DEPS=0
    
    # Check essential build tools
    for tool in gcc g++ make pkg-config; do
        if command -v $tool >/dev/null 2>&1; then
            echo -e "${GREEN}✅ $tool: $(which $tool)${NC}"
        else
            echo -e "${RED}❌ $tool: not found${NC}"
            MISSING_DEPS=1
        fi
    done
    
    # Check essential libraries
    for lib in rtaudio rtmidi fftw3 sndfile alsa; do
        if pkg-config --exists $lib 2>/dev/null; then
            echo -e "${GREEN}✅ lib$lib: $(pkg-config --modversion $lib)${NC}"
        else
            echo -e "${YELLOW}⚠️  lib$lib: not found or not properly configured${NC}"
        fi
    done
    
    if [ "$MISSING_DEPS" -eq 1 ]; then
        echo -e "\n${RED}❌ Missing dependencies detected!${NC}"
        echo -e "${YELLOW}💡 Run the dependency installation script first:${NC}"
        echo -e "   ${CYAN}./scripts/raspberry/install_dependencies_raspberry.sh${NC}"
        exit 1
    fi
fi

# Step 4: Clean if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo -e "\n${BLUE}=== Step 4: Cleaning Previous Build ===${NC}"
    
    if make clean 2>/dev/null; then
        echo -e "${GREEN}✅ Build cleaned successfully${NC}"
    else
        echo -e "${YELLOW}⚠️  Clean failed, manually removing build directory...${NC}"
        rm -rf build/
        echo -e "${GREEN}✅ Build directory removed${NC}"
    fi
fi

# Step 5: Build
echo -e "\n${BLUE}=== Step 5: Building Sp3ctra ===${NC}"

# Determine build target and flags
if [ "$NO_DISPLAY" -eq 1 ]; then
    echo -e "${CYAN}Build mode: Headless (no display)${NC}"
    BUILD_TARGET="no-sfml"
else
    echo -e "${CYAN}Build mode: With display${NC}"
    BUILD_TARGET="all"
fi

if [ "$DEBUG_BUILD" -eq 1 ]; then
    echo -e "${CYAN}Build type: Debug${NC}"
else
    echo -e "${CYAN}Build type: Release${NC}"
fi

# Determine parallel jobs (Linux only)
if [ "$UNAME_S" = "Linux" ]; then
    JOBS=$(nproc 2>/dev/null || echo 2)
    echo -e "${CYAN}Using $JOBS parallel jobs${NC}"
    MAKE_JOBS="-j$JOBS"
else
    MAKE_JOBS=""
fi

# Show verbose output if requested
if [ "$VERBOSE" -eq 1 ]; then
    set -x
fi

# Build the project
BUILD_SUCCESS=0
if make $MAKE_JOBS $BUILD_TARGET 2>&1; then
    BUILD_SUCCESS=1
    echo -e "\n${GREEN}✅ Build completed successfully!${NC}"
else
    echo -e "\n${RED}❌ Build failed!${NC}"
    exit 1
fi

if [ "$VERBOSE" -eq 1 ]; then
    set +x
fi

# Step 6: Verify build result
echo -e "\n${BLUE}=== Step 6: Build Verification ===${NC}"

if [ -f "build/Sp3ctra" ]; then
    echo -e "${GREEN}✅ Executable created: build/Sp3ctra${NC}"
    
    if [ "$UNAME_S" = "Darwin" ]; then
        EXEC_SIZE=$(stat -f%z build/Sp3ctra 2>/dev/null || echo "unknown")
    else
        EXEC_SIZE=$(stat -c%s build/Sp3ctra 2>/dev/null || echo "unknown")
    fi
    echo -e "${CYAN}Binary size: $EXEC_SIZE bytes${NC}"
    
    # Test basic functionality
    if ./build/Sp3ctra --help >/dev/null 2>&1; then
        echo -e "${GREEN}✅ Basic functionality test passed${NC}"
    else
        echo -e "${YELLOW}⚠️  Basic functionality test failed${NC}"
    fi
else
    echo -e "${RED}❌ Executable not found!${NC}"
    exit 1
fi

# Step 7: Summary and next steps
echo -e "\n${GREEN}================================================${NC}"
echo -e "${GREEN}  ✓ Build completed successfully!               ${NC}"
echo -e "${GREEN}  ✓ Executable: build/Sp3ctra                   ${NC}"
if [ "$NO_DISPLAY" -eq 1 ]; then
    echo -e "${GREEN}  ✓ Mode: Headless (no display)                 ${NC}"
else
    echo -e "${GREEN}  ✓ Mode: With display support                  ${NC}"
fi
echo -e "${GREEN}================================================${NC}"

echo -e "\n${CYAN}To run the application:${NC}"
echo -e "  ${YELLOW}./build/Sp3ctra${NC}"
echo -e "  ${YELLOW}./build/Sp3ctra --list-audio-devices${NC}"
if [ "$NO_DISPLAY" -eq 0 ]; then
    echo -e "  ${YELLOW}./build/Sp3ctra --display${NC}"
fi

if [ "$UNAME_S" = "Linux" ]; then
    echo -e "\n${CYAN}💡 Raspberry Pi specific tips:${NC}"
    echo -e "   1. Test USB detection: ${YELLOW}./build/Sp3ctra --list-audio-devices${NC}"
    echo -e "   2. Run realtime setup: ${YELLOW}sudo ./scripts/raspberry/fix_pi_realtime_audio.sh${NC}"
    echo -e "   3. For DMX support: ${YELLOW}./scripts/raspberry/install_dmx_udev.sh${NC}"
fi
