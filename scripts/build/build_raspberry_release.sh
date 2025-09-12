#!/bin/bash

# Build script for RELEASE version of Sp3ctra on Raspberry Pi
# Uses the cross-platform Makefile with automatic OS detection
# Enhanced with diagnostics and error recovery

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Stop on error
set -e

# Process command line arguments
RUN_AFTER_BUILD=0
VERBOSE=0

while [[ $# -gt 0 ]]; do
  case $1 in
    --run)
      RUN_AFTER_BUILD=1
      shift
      ;;
    --verbose)
      VERBOSE=1
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--run] [--verbose]"
      echo "  --run       Run the application after successful build"
      echo "  --verbose   Show detailed build output"
      exit 1
      ;;
  esac
done

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}  Building Sp3ctra for Raspberry Pi (Release)  ${NC}"
echo -e "${BLUE}================================================${NC}"

echo -e "${YELLOW}This script requires the dependencies to be installed.${NC}"
echo -e "${YELLOW}Run scripts/raspberry/install_dependencies_raspberry.sh first if needed.${NC}"
echo ""

# Step 1: Verify we're in the correct directory
echo -e "${BLUE}=== Step 1: Directory and Makefile Verification ===${NC}"

if [ ! -f "Makefile" ]; then
    echo -e "${RED}❌ Makefile not found in current directory: $(pwd)${NC}"
    echo -e "${YELLOW}💡 Please run this script from the Sp3ctra_Application root directory${NC}"
    echo -e "${CYAN}Expected structure:${NC}"
    echo -e "  Sp3ctra_Application/"
    echo -e "  ├── Makefile"
    echo -e "  ├── src/"
    echo -e "  ├── scripts/"
    echo -e "  └── build/"
    exit 1
else
    echo -e "${GREEN}✅ Makefile found${NC}"
fi

# Step 2: Check system information
echo -e "\n${BLUE}=== Step 2: System Information ===${NC}"
echo -e "${CYAN}OS: $(uname -s) $(uname -r)${NC}"
echo -e "${CYAN}Architecture: $(uname -m)${NC}"
echo -e "${CYAN}Processor count: $(nproc 2>/dev/null || echo 'unknown')${NC}"
echo -e "${CYAN}Working directory: $(pwd)${NC}"

# Step 3: Check build dependencies
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

# Step 4: Show verbose information if requested
if [ "$VERBOSE" -eq 1 ]; then
    echo -e "\n${BLUE}=== Step 4: Build Configuration (Verbose) ===${NC}"
    make debug || echo -e "${YELLOW}⚠️  Make debug failed, continuing anyway...${NC}"
fi

# Step 5: Clean previous build files
echo -e "\n${BLUE}=== Step 5: Cleaning Previous Build ===${NC}"

if make clean 2>/dev/null; then
    echo -e "${GREEN}✅ Build cleaned successfully${NC}"
else
    echo -e "${YELLOW}⚠️  Clean failed or no previous build found${NC}"
    echo -e "${CYAN}Manually removing build directory...${NC}"
    rm -rf build/
    echo -e "${GREEN}✅ Build directory removed${NC}"
fi

# Step 6: Create build directory if it doesn't exist
echo -e "\n${BLUE}=== Step 6: Build Directory Setup ===${NC}"
mkdir -p build/obj/core
mkdir -p build/obj/audio/rtaudio
mkdir -p build/obj/audio/buffers
mkdir -p build/obj/audio/pan
mkdir -p build/obj/audio/effects
mkdir -p build/obj/synthesis/additive
mkdir -p build/obj/synthesis/polyphonic/kissfft
mkdir -p build/obj/communication/network
mkdir -p build/obj/communication/midi
mkdir -p build/obj/communication/dmx
mkdir -p build/obj/display
mkdir -p build/obj/threading
mkdir -p build/obj/utils
echo -e "${GREEN}✅ Complete build directory structure created${NC}"

# Step 7: Build with enhanced error handling
echo -e "\n${BLUE}=== Step 7: Building Sp3ctra ===${NC}"

# Determine parallel jobs
JOBS=$(nproc 2>/dev/null || echo 2)
echo -e "${CYAN}Using $JOBS parallel jobs${NC}"

# Show build progress
if [ "$VERBOSE" -eq 1 ]; then
    set -x
fi

BUILD_SUCCESS=0
if make -j$JOBS CFLAGS="-O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DENABLE_IMAGE_DEBUG -DNO_SFML -D__LINUX__ -Wno-deprecated-declarations -march=native -mtune=native" 2>&1; then
    BUILD_SUCCESS=1
    echo -e "\n${GREEN}✅ Build completed successfully!${NC}"
else
    echo -e "\n${RED}❌ Build failed!${NC}"
    echo -e "${YELLOW}💡 Trying fallback build without optimizations...${NC}"
    
    # Fallback build with basic flags
    if make -j1 CFLAGS="-O2 -fPIC -DUSE_RTAUDIO -DNO_SFML -D__LINUX__" 2>&1; then
        BUILD_SUCCESS=1
        echo -e "${GREEN}✅ Fallback build successful!${NC}"
    else
        echo -e "\n${RED}❌ Both builds failed!${NC}"
        echo -e "${YELLOW}💡 Troubleshooting suggestions:${NC}"
        echo -e "   1. Run: ${CYAN}./scripts/raspberry/install_dependencies_raspberry.sh${NC}"
        echo -e "   2. Check: ${CYAN}make help${NC}"
        echo -e "   3. Try: ${CYAN}make no-sfml${NC}"
        exit 1
    fi
fi

if [ "$VERBOSE" -eq 1 ]; then
    set +x
fi

# Step 8: Verify build result
echo -e "\n${BLUE}=== Step 8: Build Verification ===${NC}"

if [ -f "build/Sp3ctra" ]; then
    echo -e "${GREEN}✅ Executable created: build/Sp3ctra${NC}"
    EXEC_SIZE=$(stat -c%s build/Sp3ctra 2>/dev/null || stat -f%z build/Sp3ctra 2>/dev/null || echo "unknown")
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

echo -e "\n${GREEN}================================================${NC}"
echo -e "${GREEN}  ✓ Release build completed successfully!       ${NC}"
echo -e "${GREEN}  ✓ Executable location: build/Sp3ctra         ${NC}"
echo -e "${GREEN}  ✓ Enhanced USB audio detection included       ${NC}"
echo -e "${GREEN}================================================${NC}"

# Step 9: Run the application if requested
if [ "$RUN_AFTER_BUILD" -eq 1 ]; then
    echo -e "\n${BLUE}=== Step 9: Starting Sp3ctra ===${NC}"
    echo -e "${YELLOW}Press Ctrl+C to stop the application${NC}"
    echo ""
    ./build/Sp3ctra
else
    echo -e "\n${CYAN}To run the application:${NC}"
    echo -e "  ${YELLOW}./build/Sp3ctra${NC}"
    echo -e "  ${YELLOW}./build/Sp3ctra --list-audio-devices${NC}"
    echo ""
    echo -e "${CYAN}Or run this script with --run to build and run automatically:${NC}"
    echo -e "  ${YELLOW}$0 --run${NC}"
fi

echo -e "\n${CYAN}💡 Next steps to test enhanced USB detection:${NC}"
echo -e "   1. Update and reboot: ${YELLOW}git pull && sudo reboot${NC}"
echo -e "   2. Test USB detection: ${YELLOW}./build/Sp3ctra --list-audio-devices${NC}"
echo -e "   3. Run realtime setup: ${YELLOW}sudo ./scripts/raspberry/fix_pi_realtime_audio.sh${NC}"
