#!/bin/bash

# Build script for RELEASE version of Sp3ctra on Raspberry Pi
# Uses the cross-platform Makefile with automatic OS detection

# Stop on error
set -e

# Process command line arguments
RUN_AFTER_BUILD=0

while [[ $# -gt 0 ]]; do
  case $1 in
    --run)
      RUN_AFTER_BUILD=1
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--run]"
      echo "  --run    Run the application after successful build"
      exit 1
      ;;
  esac
done

echo "Building Sp3ctra for Raspberry Pi (Release mode)..."
echo "This script requires the dependencies to be installed."
echo "Run scripts/raspberry/install_dependencies_raspberry.sh first if needed."
echo ""

# Show executed commands
set -x

# Clean previous build files
echo "Cleaning previous build..."
make clean

# Build with cross-platform Makefile (will auto-detect Linux and use appropriate settings)
echo "Building Sp3ctra with cross-platform Makefile..."
make -j$(nproc 2>/dev/null || echo 2) CFLAGS="-O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DENABLE_IMAGE_DEBUG -DNO_SFML -D__LINUX__ -Wno-deprecated-declarations -march=native -mtune=native"

echo ""
echo "✓ Release build completed successfully!"
echo "✓ Executable is located at: build/Sp3ctra"
echo ""

# Run the application if requested
if [ "$RUN_AFTER_BUILD" -eq 1 ]; then
  echo "Starting Sp3ctra..."
  echo "Press Ctrl+C to stop the application"
  echo ""
  ./build/Sp3ctra
else
  echo "To run the application:"
  echo "  ./build/Sp3ctra"
  echo ""
  echo "Or run this script with --run to build and run automatically:"
  echo "  $0 --run"
fi
