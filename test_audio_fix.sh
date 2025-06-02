#!/bin/bash

# Script to test audio device selection fix
# Run on Raspberry Pi after copying the updated code

echo "==============================================="
echo "Testing CISYNTH Audio Device Selection Fix"
echo "==============================================="

echo
echo "Step 1: Building the project..."
if [ -d "build_nogui" ]; then
    cd build_nogui
    make clean
    make
    cd ..
else
    echo "Error: build_nogui directory not found!"
    echo "Please run this script from the project root directory"
    exit 1
fi

echo
echo "Step 2: Testing device detection..."
echo "Available audio devices:"
./build_nogui/CISYNTH_noGUI --list-audio-devices
echo

echo "Step 3: Testing specific USB SPDIF devices..."
echo "Testing Device ID 3:"
timeout 5s ./build_nogui/CISYNTH_noGUI --audio-device 3 --cli
echo

echo "Testing Device ID 4:"
timeout 5s ./build_nogui/CISYNTH_noGUI --audio-device 4 --cli
echo

echo "Step 4: Testing invalid device ID (should show error):"
timeout 5s ./build_nogui/CISYNTH_noGUI --audio-device 99 --cli
echo

echo "==============================================="
echo "Test completed!"
echo "If Device ID 3 or 4 worked without audio errors,"
echo "the fix is successful."
echo "==============================================="
