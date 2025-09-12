#!/bin/bash

# Test script for DMX baud rate configuration on Raspberry Pi
# This script compiles and tests the enhanced DMX baud rate functionality

echo "🔧 Sp3ctra DMX Baud Rate Test Script"
echo "===================================="

# Check if we're on Raspberry Pi
if [ ! -f /proc/device-tree/model ]; then
    echo "⚠️  Warning: This script is designed for Raspberry Pi"
fi

# Check if running as root (needed for some USB operations)
if [ "$EUID" -ne 0 ]; then
    echo "⚠️  Note: Some operations may require sudo privileges"
fi

# Go to project root
cd "$(dirname "$0")/../.."

echo "📍 Current directory: $(pwd)"

# Clean previous build
echo "🧹 Cleaning previous build..."
make clean 2>/dev/null || true

# Compile with DMX support
echo "🔨 Compiling with enhanced DMX support..."
if make -j$(nproc) 2>&1 | tee build.log; then
    echo "✅ Compilation successful!"
else
    echo "❌ Compilation failed. Check build.log for details."
    exit 1
fi

# Check if DMX device exists
DMX_DEVICE="/dev/sp3ctra-dmx"
if [ ! -e "$DMX_DEVICE" ]; then
    echo "⚠️  DMX device $DMX_DEVICE not found"
    echo "🔍 Looking for USB serial devices..."
    
    # List USB serial devices
    echo "Available USB devices:"
    ls -la /dev/ttyUSB* 2>/dev/null || echo "No /dev/ttyUSB* devices found"
    ls -la /dev/ttyACM* 2>/dev/null || echo "No /dev/ttyACM* devices found"
    
    # Check if udev rules are installed
    if [ -f "/etc/udev/rules.d/99-sp3ctra-dmx.rules" ]; then
        echo "✅ udev rules are installed"
        echo "💡 Try unplugging and reconnecting the USB DMX adapter"
    else
        echo "⚠️  udev rules not found. Installing..."
        sudo cp scripts/raspberry/99-sp3ctra-dmx.rules /etc/udev/rules.d/
        sudo udevadm control --reload-rules
        sudo udevadm trigger
        echo "✅ udev rules installed. Please reconnect the USB adapter."
        exit 0
    fi
    
    # Try to find a USB device to test with
    if [ -e "/dev/ttyUSB0" ]; then
        DMX_DEVICE="/dev/ttyUSB0"
        echo "🔧 Using $DMX_DEVICE for testing"
    else
        echo "❌ No USB serial device found for testing"
        exit 1
    fi
fi

# Test the application with enhanced debugging
echo ""
echo "🚀 Testing DMX baud rate configuration..."
echo "Device: $DMX_DEVICE"
echo "Target baud rate: 250000 bps"
echo ""

# Run the test with detailed output
if [ -x "./build/Sp3ctra" ]; then
    echo "🔧 Running Sp3ctra with DMX port $DMX_DEVICE..."
    echo "Press Ctrl+C to stop the test"
    echo "----------------------------------------"
    
    # Run with timeout to prevent hanging
    timeout 30s ./build/Sp3ctra --dmx-port="$DMX_DEVICE" 2>&1 | tee dmx_test.log
    
    echo ""
    echo "----------------------------------------"
    echo "🔍 Test completed. Check dmx_test.log for full output."
    
    # Analyze the results
    if grep -q "🎉.*DMX baud rate.*successfully configured" dmx_test.log; then
        echo "✅ SUCCESS: DMX baud rate was successfully configured!"
        
        if grep -q "250000" dmx_test.log; then
            echo "🚀 PERFECT: Achieved exact 250000 bps target!"
        else
            echo "⚠️  Achieved baud rate configuration but not exact 250000 bps"
        fi
    elif grep -q "⚠️.*DMX baud rate still problematic" dmx_test.log; then
        echo "⚠️  PARTIAL: Some configuration succeeded but still problematic"
        echo "💡 Check the detailed logs above for more information"
    else
        echo "❌ FAILURE: DMX baud rate configuration failed"
        echo "💡 This may indicate hardware compatibility issues"
    fi
    
    # Show final baud rate
    if grep -q "Final baud rate value:" dmx_test.log; then
        FINAL_RATE=$(grep "Final baud rate value:" dmx_test.log | tail -1 | awk '{print $NF}')
        echo "🔍 Final baud rate value: $FINAL_RATE"
    fi
    
else
    echo "❌ Executable not found: ./build/Sp3ctra"
    exit 1
fi

echo ""
echo "📋 SUMMARY:"
echo "- Build log: build.log"
echo "- Test log: dmx_test.log"
echo "- Target rate: 250000 bps"
echo ""
echo "💡 Next steps if successful:"
echo "1. Test actual DMX functionality with your lighting setup"
echo "2. Monitor for any communication issues"
echo "3. Fine-tune DMX parameters if needed"
