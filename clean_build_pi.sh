#!/bin/bash

# Script to clean and rebuild on Raspberry Pi
# Fixes architecture mismatch between Mac and Pi compilation

echo "==============================================="
echo "Cleaning and rebuilding for Raspberry Pi"
echo "==============================================="

echo "Step 1: Cleaning previous build files..."
rm -rf build_nogui
echo "build_nogui directory removed"

echo
echo "Step 2: Creating fresh build directory..."
mkdir -p build_nogui

echo
echo "Step 3: Running qmake for CLI mode..."
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro CONFIG+=cli_mode CONFIG+=release QMAKE_CXXFLAGS+=-Ofast QMAKE_LFLAGS+=-s

echo
echo "Step 4: Compiling with make..."
cd build_nogui
make clean
make -j$(nproc)

echo
echo "Step 5: Testing compilation result..."
if [ -f "CISYNTH_noGUI" ]; then
    echo "✅ Compilation successful!"
    echo "File info:"
    file CISYNTH_noGUI
    echo
    echo "Testing executable:"
    ./CISYNTH_noGUI --help 2>/dev/null || echo "Binary created successfully"
else
    echo "❌ Compilation failed!"
    exit 1
fi

echo
echo "==============================================="
echo "Build completed successfully!"
echo "You can now test with:"
echo "./build_nogui/CISYNTH_noGUI --audio-device 3"
echo "or"
echo "./build_nogui/CISYNTH_noGUI --audio-device 4"
echo "==============================================="
