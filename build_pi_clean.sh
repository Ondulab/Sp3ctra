#!/bin/bash

# Clean build script for Raspberry Pi
# This script ensures a completely clean build to avoid architecture conflicts

echo "============================================="
echo "Clean build for Raspberry Pi"
echo "============================================="

# Clean any existing build files
echo "Step 1: Cleaning existing build files..."
rm -rf build_nogui
rm -f Makefile
rm -f .qmake.stash

# Create fresh build directory
echo "Step 2: Creating fresh build directory..."
mkdir -p build_nogui

# Run qmake with CLI configuration
echo "Step 3: Running qmake..."
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro CONFIG+=cli_mode CONFIG+=release QMAKE_CXXFLAGS+=-Ofast QMAKE_LFLAGS+=-s

# Build the project
echo "Step 4: Building project..."
cd build_nogui
make clean
make -j$(nproc)
cd ..

# Check build result
if [ -f "build_nogui/CISYNTH_noGUI" ]; then
    echo "✅ Build successful!"
    echo "Executable: build_nogui/CISYNTH_noGUI"
    echo "File info:"
    file build_nogui/CISYNTH_noGUI
else
    echo "❌ Build failed!"
    exit 1
fi

echo "============================================="
echo "Build completed successfully!"
echo "============================================="
