#!/bin/bash
# Script de lancement rapide pour Sp3ctra Standalone
# Usage: ./scripts/run_standalone.sh [release|debug|lldb]

set -e

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

# Default configuration
BUILD_CONFIG="Release"
USE_DEBUGGER=0

# Parse arguments
case "$1" in
    debug|Debug|DEBUG)
        BUILD_CONFIG="Debug"
        echo -e "${YELLOW}Using Debug build${NC}"
        ;;
    lldb|LLDB)
        USE_DEBUGGER=1
        echo -e "${YELLOW}Using build with lldb${NC}"
        ;;
    release|Release|RELEASE|"")
        BUILD_CONFIG="Release"
        ;;
    help|--help|-h)
        echo ""
        echo "Usage: $0 [release|debug|lldb|help]"
        echo ""
        echo "Options:"
        echo "  release  - Launch Release build (default)"
        echo "  debug    - Launch Debug build"
        echo "  lldb     - Launch with lldb debugger (prefer Debug if available)"
        echo "  help     - Show this help message"
        echo ""
        echo "Examples:"
        echo "  $0               # Launch Release standalone"
        echo "  $0 debug         # Launch Debug standalone"
        echo "  $0 lldb          # Debug with lldb"
        echo ""
        exit 0
        ;;
    *)
        echo -e "${RED}Unknown option: $1${NC}"
        echo "Use '$0 help' for usage information"
        exit 1
        ;;
esac

# Construct paths (no Release/Debug subfolder in this build system)
STANDALONE_APP="$BUILD_DIR/Sp3ctraVST_artefacts/Standalone/Sp3ctra.app"
STANDALONE_EXEC="$STANDALONE_APP/Contents/MacOS/Sp3ctra"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Sp3ctra Standalone Launcher${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if standalone exists
if [ ! -d "$STANDALONE_APP" ]; then
    echo -e "${RED}✗ Standalone app not found:${NC}"
    echo "  $STANDALONE_APP"
    echo ""
    echo -e "${YELLOW}Build the project first:${NC}"
    if [ "$BUILD_CONFIG" == "Debug" ]; then
        echo "  ./scripts/build_vst.sh debug"
    else
        echo "  ./scripts/build_vst.sh"
    fi
    exit 1
fi

if [ ! -f "$STANDALONE_EXEC" ]; then
    echo -e "${RED}✗ Standalone executable not found:${NC}"
    echo "  $STANDALONE_EXEC"
    exit 1
fi

echo -e "${GREEN}✓ Found standalone:${NC} $STANDALONE_APP"
echo ""

# Launch with or without debugger
if [ $USE_DEBUGGER -eq 1 ]; then
    echo -e "${BLUE}Launching with lldb debugger...${NC}"
    echo -e "${YELLOW}Tips:${NC}"
    echo "  - Type 'run' to start the application"
    echo "  - Set breakpoints: 'b PluginProcessor::processBlock'"
    echo "  - Continue: 'c', Step: 's', Next: 'n'"
    echo "  - Quit: 'quit'"
    echo ""
    lldb "$STANDALONE_EXEC"
else
    echo -e "${BLUE}Launching standalone application...${NC}"
    echo -e "${YELLOW}Press Ctrl+C to stop${NC}"
    echo ""
    "$STANDALONE_EXEC"
fi
