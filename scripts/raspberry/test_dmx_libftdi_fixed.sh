#!/bin/bash

# Test script for DMX libftdi FIXED version
# This script compiles and runs the corrected DMX implementation

set -e

echo "🔧 DMX libftdi FIXED - Test Script"
echo "=================================="

# Check if libftdi1 is installed
if ! pkg-config --exists libftdi1; then
    echo "❌ libftdi1 not found. Installing..."
    sudo apt update
    sudo apt install -y libftdi1-dev
fi

# Check if dmx_libftdi_fixed.c exists
if [ ! -f "dmx_libftdi_fixed.c" ]; then
    echo "❌ dmx_libftdi_fixed.c not found in current directory"
    echo "Make sure to run this script from the Sp3ctra root directory"
    exit 1
fi

# Compile the test program
echo "🔨 Compiling dmx_libftdi_fixed..."
gcc -o dmx_libftdi_fixed dmx_libftdi_fixed.c -lftdi1 -Wall

if [ $? -eq 0 ]; then
    echo "✅ Compilation successful"
else
    echo "❌ Compilation failed"
    exit 1
fi

# Check if we're running as root (required for USB access)
if [ "$EUID" -ne 0 ]; then
    echo "🚨 This test requires root privileges for USB access"
    echo "Running with sudo..."
    sudo ./dmx_libftdi_fixed
else
    echo "▶️  Running DMX test as root..."
    ./dmx_libftdi_fixed
fi

echo ""
echo "🎉 DMX test completed!"
echo "If your lights responded correctly, the Pi DMX issue is FIXED!"
