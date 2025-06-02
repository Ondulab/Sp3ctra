#!/bin/bash

# Script to test audio optimizations on Raspberry Pi
# Tests buffer size increases and monitoring for audio dropouts

echo "🎵 Testing Audio Optimizations for Raspberry Pi"
echo "=============================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Current optimizations applied:${NC}"
echo "  - AUDIO_BUFFER_SIZE: 1024 frames (was 512)"
echo "  - RtAudio numberOfBuffers: 8 (was 4)"
echo "  - Expected latency: ~10.67ms (was ~5.33ms)"
echo ""

# Clean and rebuild
echo -e "${YELLOW}Step 1: Clean rebuild...${NC}"
make clean
if ! make -j$(nproc); then
    echo -e "${RED}❌ Build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✅ Build successful${NC}"
echo ""

# Check if we're on Raspberry Pi
if grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    echo -e "${GREEN}📍 Running on Raspberry Pi${NC}"
    
    # Show CPU and memory info
    echo -e "${BLUE}System Info:${NC}"
    echo "  CPU: $(nproc) cores"
    echo "  Memory: $(free -h | awk '/^Mem:/ {print $2}') total"
    echo "  Load: $(uptime | awk -F'load average:' '{print $2}')"
    echo ""
    
    # Check USB audio devices
    echo -e "${BLUE}USB Audio devices:${NC}"
    lsusb | grep -i audio || echo "  No USB audio devices found"
    echo ""
    
    # Show ALSA devices
    echo -e "${BLUE}ALSA devices:${NC}"
    aplay -l 2>/dev/null | grep "card" || echo "  No ALSA devices found"
    echo ""
else
    echo -e "${YELLOW}⚠️  Not running on Raspberry Pi - results may differ${NC}"
    echo ""
fi

# Test with different device IDs
echo -e "${YELLOW}Step 2: Testing audio devices...${NC}"

# Test with Steinberg UR22C (device 5 based on your log)
echo -e "${BLUE}Testing with Steinberg UR22C (device 5):${NC}"
timeout 10s ./build_nogui/CISYNTH_noGUI --audio-device=5 --no-dmx 2>&1 | tee test_device5.log &
PID=$!

# Monitor for a few seconds
sleep 8

if kill -0 $PID 2>/dev/null; then
    echo -e "${GREEN}✅ Device 5 test running successfully${NC}"
    kill $PID 2>/dev/null
    wait $PID 2>/dev/null
else
    echo -e "${RED}❌ Device 5 test failed or crashed${NC}"
fi
echo ""

# Test with USB SPDIF devices (1 and 2 based on your log)
for device in 1 2; do
    echo -e "${BLUE}Testing with USB SPDIF device $device:${NC}"
    timeout 8s ./build_nogui/CISYNTH_noGUI --audio-device=$device --no-dmx 2>&1 | tee test_device$device.log &
    PID=$!
    
    sleep 6
    
    if kill -0 $PID 2>/dev/null; then
        echo -e "${GREEN}✅ Device $device test running successfully${NC}"
        kill $PID 2>/dev/null
        wait $PID 2>/dev/null
    else
        echo -e "${RED}❌ Device $device test failed or crashed${NC}"
    fi
    echo ""
done

# Analyze the logs
echo -e "${YELLOW}Step 3: Analyzing results...${NC}"

for log in test_device*.log; do
    if [ -f "$log" ]; then
        device=$(echo $log | grep -o 'device[0-9]' | grep -o '[0-9]')
        echo -e "${BLUE}Results for device $device:${NC}"
        
        # Check for successful initialization
        if grep -q "RtAudio initialisé" "$log"; then
            sample_rate=$(grep "RtAudio initialisé" "$log" | grep -o "SR=[0-9]*Hz" | grep -o "[0-9]*")
            buffer_size=$(grep "RtAudio initialisé" "$log" | grep -o "BS=[0-9]*" | grep -o "[0-9]*")
            latency=$(grep "RtAudio initialisé" "$log" | grep -o "Latence=[0-9.]*ms" | grep -o "[0-9.]*")
            
            echo "  ✅ Initialized: $sample_rate Hz, $buffer_size frames, ${latency}ms latency"
        else
            echo "  ❌ Failed to initialize"
        fi
        
        # Check for ALSA errors
        alsa_errors=$(grep -c "RtApiAlsa" "$log" 2>/dev/null || echo "0")
        if [ "$alsa_errors" -gt 0 ]; then
            echo "  ⚠️  $alsa_errors ALSA errors detected"
        fi
        
        # Check for buffer underruns (if any are logged)
        underruns=$(grep -c -i "underrun\|xrun\|dropout" "$log" 2>/dev/null || echo "0")
        if [ "$underruns" -gt 0 ]; then
            echo "  🚨 $underruns buffer underruns detected"
        else
            echo "  ✅ No buffer underruns detected"
        fi
        
        echo ""
    fi
done

# Performance recommendations
echo -e "${YELLOW}Step 4: Performance recommendations...${NC}"

# Check system load
load=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
if (( $(echo "$load > 1.0" | bc -l) )); then
    echo -e "${RED}⚠️  High system load detected ($load). Consider:${NC}"
    echo "  - Stopping unnecessary services"
    echo "  - Increasing buffer sizes further"
else
    echo -e "${GREEN}✅ System load is acceptable ($load)${NC}"
fi

# Check if we can increase buffer size even more if needed
echo ""
echo -e "${BLUE}If audio is still choppy, try these optimizations:${NC}"
echo "  1. Increase AUDIO_BUFFER_SIZE to 2048 in config.h"
echo "  2. Disable reverb temporarily: --no-reverb flag"
echo "  3. Use lower quality audio device (48kHz instead of 96kHz)"
echo "  4. Reduce CPU load by stopping other processes"
echo ""

# Show CPU frequency and governor
if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
    governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
    freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo "unknown")
    echo -e "${BLUE}CPU Performance:${NC}"
    echo "  Governor: $governor"
    echo "  Current freq: ${freq} kHz"
    
    if [ "$governor" != "performance" ]; then
        echo -e "${YELLOW}  💡 Consider: sudo cpufreq-set -g performance${NC}"
    fi
    echo ""
fi

# Clean up log files
echo -e "${YELLOW}Cleaning up test logs...${NC}"
rm -f test_device*.log

echo -e "${GREEN}🎉 Audio optimization test completed!${NC}"
echo ""
echo "Next steps:"
echo "  - If audio is still choppy, increase buffer size to 2048"
echo "  - Test with different audio devices"
echo "  - Consider disabling reverb/EQ for testing"
