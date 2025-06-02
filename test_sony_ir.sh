#!/bin/bash

# Test script for Sony HT-X8500 IR commands
# Usage: ./test_sony_ir.sh [command]

IR_DEVICE="/dev/lirc0"
CARRIER="40000"
DUTY_CYCLE="33"

echo "Testing Sony HT-X8500 IR commands..."
echo "Make sure your soundbar is plugged in and in standby mode."
echo ""

test_command() {
    local cmd_name=$1
    local ir_file=$2
    
    echo "Testing $cmd_name..."
    echo "Sending command 3 times with 0.5s interval..."
    
    for i in {1..3}; do
        echo "  Attempt $i/3"
        ir-ctl -d $IR_DEVICE --carrier=$CARRIER --duty-cycle=$DUTY_CYCLE --send=$ir_file
        sleep 0.5
    done
    
    echo "Did the $cmd_name command work? (Press Enter to continue)"
    read
    echo ""
}

case "${1:-all}" in
    "power")
        test_command "POWER" "sony_power_new.ir"
        ;;
    "volume_up")
        test_command "VOLUME UP" "sony_volume_up.ir"
        ;;
    "volume_down")
        test_command "VOLUME DOWN" "sony_volume_down.ir"
        ;;
    "mute")
        test_command "MUTE" "sony_mute.ir"
        ;;
    "all"|*)
        echo "Testing all commands in sequence..."
        echo ""
        test_command "POWER" "sony_power_new.ir"
        test_command "VOLUME UP" "sony_volume_up.ir"
        test_command "VOLUME DOWN" "sony_volume_down.ir"
        test_command "MUTE" "sony_mute.ir"
        ;;
esac

echo "Test completed!"
echo ""
echo "Usage examples:"
echo "  ./test_sony_ir.sh power      # Test power only"
echo "  ./test_sony_ir.sh volume_up  # Test volume up only"
echo "  ./test_sony_ir.sh all        # Test all commands"
