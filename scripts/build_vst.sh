#!/bin/bash
# Script de build pour Sp3ctra VST
# Usage: ./scripts/build_vst.sh [clean|install|help]

set -e  # Stop on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project paths
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VST_DIR="$PROJECT_DIR/vst"
BUILD_DIR="$VST_DIR/build"

# Installation paths
VST3_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_INSTALL_DIR="/Library/Audio/Plug-Ins/Components"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Sp3ctra VST Build Script${NC}"
echo -e "${BLUE}========================================${NC}"

# Parse command line arguments
CLEAN_BUILD=0
INSTALL_PLUGINS=0
RUN_STANDALONE=0
BUILD_CONFIG="Release"

# Parse all arguments
for arg in "$@"; do
    case "$arg" in
        clean)
            CLEAN_BUILD=1
            echo -e "${YELLOW}Clean build requested${NC}"
            ;;
        install)
            INSTALL_PLUGINS=1
            echo -e "${YELLOW}Installation requested${NC}"
            ;;
        debug)
            BUILD_CONFIG="Debug"
            echo -e "${YELLOW}Debug build requested${NC}"
            ;;
        run)
            RUN_STANDALONE=1
            echo -e "${YELLOW}Will launch standalone after build${NC}"
            ;;
        help|--help|-h)
            echo ""
            echo "Usage: $0 [clean] [install] [debug] [run] [help]"
            echo ""
            echo "Options:"
            echo "  clean    - Clean build directory before building"
            echo "  install  - Install plugins after successful build"
            echo "  debug    - Build in Debug mode (default: Release)"
            echo "  run      - Launch standalone after successful build"
            echo "  help     - Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Standard Release build"
            echo "  $0 clean              # Clean + Release build"
            echo "  $0 install            # Build + Install to system"
            echo "  $0 run                # Build + Launch standalone"
            echo "  $0 debug run          # Debug build + Launch"
            echo "  $0 clean debug run    # Clean + Debug + Launch"
            echo ""
            exit 0
            ;;
    esac
done

# Clean build directory if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"/*
        echo -e "${GREEN}✓ Build directory cleaned${NC}"
    fi
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo -e "${BLUE}Configuring project with CMake ($BUILD_CONFIG)...${NC}"
if cmake -DCMAKE_BUILD_TYPE=$BUILD_CONFIG ..; then
    echo -e "${GREEN}✓ Configuration successful (CMAKE_BUILD_TYPE=$BUILD_CONFIG)${NC}"
else
    echo -e "${RED}✗ Configuration failed${NC}"
    exit 1
fi

# Build
echo ""
echo -e "${BLUE}Building project ($BUILD_CONFIG)...${NC}"
echo -e "${YELLOW}This may take 2-3 minutes on first build...${NC}"

if cmake --build . --config $BUILD_CONFIG -j$(sysctl -n hw.ncpu); then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# Check if artifacts were created (CMake places them in Release/Debug subfolder)
VST3_PATH="$BUILD_DIR/Sp3ctraVST_artefacts/$BUILD_CONFIG/VST3/Sp3ctra.vst3"
AU_PATH="$BUILD_DIR/Sp3ctraVST_artefacts/$BUILD_CONFIG/AU/Sp3ctra.component"
STANDALONE_PATH="$BUILD_DIR/Sp3ctraVST_artefacts/$BUILD_CONFIG/Standalone/Sp3ctra.app"

echo ""
echo -e "${BLUE}Build artifacts:${NC}"

if [ -d "$VST3_PATH" ]; then
    echo -e "${GREEN}✓ VST3:${NC} $VST3_PATH"
else
    echo -e "${YELLOW}⚠ VST3 not found${NC}"
fi

if [ -d "$AU_PATH" ]; then
    echo -e "${GREEN}✓ AU:${NC} $AU_PATH"
else
    echo -e "${YELLOW}⚠ AU not found${NC}"
fi

if [ -d "$STANDALONE_PATH" ]; then
    echo -e "${GREEN}✓ Standalone:${NC} $STANDALONE_PATH"
else
    echo -e "${YELLOW}⚠ Standalone not found${NC}"
fi

# Install if requested
if [ $INSTALL_PLUGINS -eq 1 ]; then
    echo ""
    echo -e "${BLUE}Installing plugins...${NC}"
    
    # Install VST3
    if [ -d "$VST3_PATH" ]; then
        mkdir -p "$VST3_INSTALL_DIR"
        echo -e "${YELLOW}Installing VST3 to $VST3_INSTALL_DIR${NC}"
        cp -r "$VST3_PATH" "$VST3_INSTALL_DIR/"
        echo -e "${GREEN}✓ VST3 installed${NC}"
    fi
    
    # Install AU (requires sudo)
    if [ -d "$AU_PATH" ]; then
        echo -e "${YELLOW}Installing AU to $AU_INSTALL_DIR (requires sudo)${NC}"
        sudo cp -r "$AU_PATH" "$AU_INSTALL_DIR/"
        echo -e "${GREEN}✓ AU installed${NC}"
    fi
    
    echo ""
    echo -e "${GREEN}Installation complete!${NC}"
    echo -e "${BLUE}Next steps:${NC}"
    echo "  1. Rescan plugins in your DAW"
    echo "  2. Look for 'Sp3ctra' in your instrument list"
    echo "  3. Load it and you should hear a 440Hz test tone"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Display next steps if not installing
if [ $INSTALL_PLUGINS -eq 0 ] && [ $RUN_STANDALONE -eq 0 ]; then
    echo -e "${BLUE}To install plugins, run:${NC}"
    echo "  $0 install"
    echo ""
    echo -e "${BLUE}To run standalone, execute:${NC}"
    echo "  ./scripts/run_standalone.sh"
    echo ""
    echo -e "${BLUE}Or manually copy:${NC}"
    echo "  VST3: cp -r \"$VST3_PATH\" \"$VST3_INSTALL_DIR/\""
    echo "  AU:   sudo cp -r \"$AU_PATH\" \"$AU_INSTALL_DIR/\""
    echo ""
fi

# Launch standalone if requested
if [ $RUN_STANDALONE -eq 1 ]; then
    echo ""
    echo -e "${BLUE}Launching standalone...${NC}"
    if [ -d "$STANDALONE_PATH" ]; then
        STANDALONE_EXEC="$STANDALONE_PATH/Contents/MacOS/Sp3ctra"
        if [ -f "$STANDALONE_EXEC" ]; then
            echo -e "${GREEN}Starting Sp3ctra standalone ($BUILD_CONFIG)...${NC}"
            echo -e "${YELLOW}Press Ctrl+C to stop${NC}"
            echo ""
            "$STANDALONE_EXEC"
        else
            echo -e "${RED}✗ Standalone executable not found${NC}"
            exit 1
        fi
    else
        echo -e "${RED}✗ Standalone app not found${NC}"
        exit 1
    fi
fi
