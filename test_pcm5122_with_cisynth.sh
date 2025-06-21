#!/bin/bash

# Test script for PCM5122 with CISYNTH application
# Tests different audio configurations to solve choppy audio

echo "🎵 TEST PCM5122 WITH CISYNTH"
echo "============================="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_section() {
    echo -e "${BLUE}### $1 ###${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_info() {
    echo -e "${CYAN}ℹ️  $1${NC}"
}

# Configuration
TEST_DURATION=10
BUILD_DIR="build_nogui"
APP_NAME="CISYNTH_noGUI"

# Check if application exists
if [ ! -f "$BUILD_DIR/$APP_NAME" ]; then
    print_error "Application not found: $BUILD_DIR/$APP_NAME"
    echo "Please build the application first:"
    echo "  ./build_pi_optimized.sh"
    exit 1
fi

# 1. Pre-test System Status
print_section "PRE-TEST SYSTEM STATUS"

echo "CPU Load:"
uptime
echo

echo "Memory Usage:"
free -h
echo

echo "Audio processes:"
ps aux | grep -E "(pulseaudio|pipewire|jackd)" | grep -v grep
echo

# 2. Test different audio devices
print_section "TESTING DIFFERENT AUDIO DEVICES"

# Get available audio devices
devices=$(aplay -l 2>/dev/null | grep "^card" | awk '{print $2}' | sed 's/://')

if [ -z "$devices" ]; then
    print_error "No audio devices found"
    exit 1
fi

echo "Found audio devices: $devices"
echo

# Test each device
for device in $devices; do
    print_info "Testing Device $device with CISYNTH..."
    
    # Create temporary log file
    log_file="/tmp/cisynth_device_${device}_test.log"
    
    # Run CISYNTH with specific device for limited time
    echo "Running: timeout ${TEST_DURATION}s ./$BUILD_DIR/$APP_NAME --audio-device $device"
    
    # Start CISYNTH in background
    timeout ${TEST_DURATION}s ./$BUILD_DIR/$APP_NAME --audio-device $device > "$log_file" 2>&1 &
    cisynth_pid=$!
    
    # Monitor system during test
    echo "  Monitoring system performance..."
    start_time=$(date +%s)
    max_cpu=0
    
    while kill -0 $cisynth_pid 2>/dev/null; do
        cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//')
        if (( $(echo "$cpu_usage > $max_cpu" | bc -l) )); then
            max_cpu=$cpu_usage
        fi
        sleep 1
    done
    
    end_time=$(date +%s)
    test_duration=$((end_time - start_time))
    
    # Analyze results
    echo "  Test completed in ${test_duration}s"
    echo "  Max CPU usage: ${max_cpu}%"
    
    # Check for errors in log
    if grep -q "Stream ouvert avec succès" "$log_file"; then
        print_success "Device $device: Stream opened successfully"
    else
        print_error "Device $device: Failed to open stream"
    fi
    
    if grep -q "Unknown error 524" "$log_file"; then
        print_error "Device $device: ALSA error 524 detected"
    fi
    
    if grep -q "RtAudio error" "$log_file"; then
        print_error "Device $device: RtAudio errors detected"
    fi
    
    # Show negotiated sample rate
    negotiated_rate=$(grep "Fréquence négociée" "$log_file" | tail -1)
    if [ ! -z "$negotiated_rate" ]; then
        echo "  $negotiated_rate"
    fi
    
    # Save detailed log
    echo "  Detailed log saved to: $log_file"
    echo
done

# 3. Test with different buffer sizes
print_section "TESTING DIFFERENT BUFFER SIZES"

# Find the best working device from previous tests
best_device=""
for device in $devices; do
    log_file="/tmp/cisynth_device_${device}_test.log"
    if [ -f "$log_file" ] && grep -q "Stream ouvert avec succès" "$log_file" && ! grep -q "Unknown error 524" "$log_file"; then
        best_device=$device
        break
    fi
done

if [ -z "$best_device" ]; then
    print_warning "No working device found, using device 0 for buffer tests"
    best_device=0
fi

print_info "Using device $best_device for buffer size tests"

# Test different buffer sizes by modifying config.h temporarily
original_buffer_size=$(grep "AUDIO_BUFFER_SIZE" src/core/config.h | grep -o "[0-9]\+")
echo "Original buffer size: $original_buffer_size"

for buffer_size in 512 1024 2048; do
    print_info "Testing buffer size: $buffer_size"
    
    # Backup and modify config.h
    cp src/core/config.h src/core/config.h.backup
    sed -i "s/#define AUDIO_BUFFER_SIZE.*/#define AUDIO_BUFFER_SIZE ($buffer_size)/" src/core/config.h
    
    # Rebuild application
    echo "  Rebuilding with buffer size $buffer_size..."
    make -C $BUILD_DIR clean >/dev/null 2>&1
    ./build_pi_optimized.sh >/dev/null 2>&1
    
    if [ $? -ne 0 ]; then
        print_error "Build failed for buffer size $buffer_size"
        mv src/core/config.h.backup src/core/config.h
        continue
    fi
    
    # Test the application
    log_file="/tmp/cisynth_buffer_${buffer_size}_test.log"
    echo "  Testing for ${TEST_DURATION}s..."
    
    timeout ${TEST_DURATION}s ./$BUILD_DIR/$APP_NAME --audio-device $best_device > "$log_file" 2>&1
    
    # Analyze results
    if grep -q "Stream ouvert avec succès" "$log_file"; then
        latency=$(grep "Latence=" "$log_file" | grep -o "[0-9.]\+ms")
        print_success "Buffer $buffer_size: Success (Latency: $latency)"
    else
        print_error "Buffer $buffer_size: Failed"
    fi
    
    # Check for underruns or xruns
    if grep -q -i "underrun\|xrun" "$log_file"; then
        print_warning "Buffer $buffer_size: Audio underruns detected"
    fi
    
    # Restore original config
    mv src/core/config.h.backup src/core/config.h
    echo
done

# 4. Test with different sample rates
print_section "TESTING DIFFERENT SAMPLE RATES"

print_info "Testing sample rate compatibility with PCM5122"

# Test the PCM5122 directly with ALSA tools
for rate in 44100 48000 96000; do
    print_info "Testing ${rate}Hz direct ALSA compatibility..."
    
    timeout 3 speaker-test -D hw:$best_device,0 -c 2 -r $rate -f S16_LE -t sine -l 1 >/dev/null 2>&1
    result=$?
    
    if [ $result -eq 0 ] || [ $result -eq 124 ]; then
        print_success "${rate}Hz: Supported by PCM5122"
    else
        print_error "${rate}Hz: Not supported by PCM5122"
    fi
done

echo

# 5. System Optimization Tests
print_section "SYSTEM OPTIMIZATION TESTS"

print_info "Testing with audio optimizations..."

# Test 1: Stop PulseAudio if running
if pgrep pulseaudio >/dev/null; then
    print_info "Temporarily stopping PulseAudio..."
    systemctl --user stop pulseaudio 2>/dev/null || pulseaudio -k 2>/dev/null
    
    # Test CISYNTH without PulseAudio
    log_file="/tmp/cisynth_no_pulseaudio_test.log"
    timeout ${TEST_DURATION}s ./$BUILD_DIR/$APP_NAME --audio-device $best_device > "$log_file" 2>&1
    
    if grep -q "Stream ouvert avec succès" "$log_file" && ! grep -q -i "underrun\|xrun" "$log_file"; then
        print_success "Without PulseAudio: Improved performance"
    else
        print_warning "Without PulseAudio: No significant improvement"
    fi
    
    # Restart PulseAudio
    systemctl --user start pulseaudio 2>/dev/null || pulseaudio --start 2>/dev/null
fi

# Test 2: CPU governor performance mode
current_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
if [ ! -z "$current_governor" ]; then
    print_info "Current CPU governor: $current_governor"
    
    if [ "$current_governor" != "performance" ]; then
        echo "  Testing with performance governor..."
        echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1
        
        log_file="/tmp/cisynth_performance_governor_test.log"
        timeout ${TEST_DURATION}s ./$BUILD_DIR/$APP_NAME --audio-device $best_device > "$log_file" 2>&1
        
        if grep -q "Stream ouvert avec succès" "$log_file" && ! grep -q -i "underrun\|xrun" "$log_file"; then
            print_success "Performance governor: Improved stability"
        else
            print_warning "Performance governor: No significant improvement"
        fi
        
        # Restore original governor
        echo $current_governor | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1
    fi
fi

# 6. Final Recommendations
print_section "FINAL RECOMMENDATIONS"

echo "Based on the test results:"
echo

# Analyze all test results
best_buffer=""
best_device_final=""

# Find best buffer size
for buffer_size in 512 1024 2048; do
    log_file="/tmp/cisynth_buffer_${buffer_size}_test.log"
    if [ -f "$log_file" ] && grep -q "Stream ouvert avec succès" "$log_file" && ! grep -q -i "underrun\|xrun" "$log_file"; then
        if [ -z "$best_buffer" ]; then
            best_buffer=$buffer_size
        fi
    fi
done

# Find best device
for device in $devices; do
    log_file="/tmp/cisynth_device_${device}_test.log"
    if [ -f "$log_file" ] && grep -q "Stream ouvert avec succès" "$log_file" && ! grep -q "Unknown error 524" "$log_file"; then
        if [ -z "$best_device_final" ]; then
            best_device_final=$device
        fi
    fi
done

if [ ! -z "$best_device_final" ]; then
    print_success "Recommended audio device: $best_device_final"
    echo "  Start CISYNTH with: ./$BUILD_DIR/$APP_NAME --audio-device $best_device_final"
else
    print_error "No working audio device found - PCM5122 may need system-level configuration"
fi

if [ ! -z "$best_buffer" ]; then
    print_success "Recommended buffer size: $best_buffer frames"
    echo "  Modify AUDIO_BUFFER_SIZE in src/core/config.h to $best_buffer"
else
    print_warning "All buffer sizes had issues - may need larger buffers or system optimization"
fi

echo
print_info "SPECIFIC PCM5122 TROUBLESHOOTING:"
echo "1. 🔧 Check if PCM5122 driver is properly loaded:"
echo "   lsmod | grep snd_soc_pcm5102a"
echo
echo "2. 🔧 Verify PCM5122 device tree overlay (if HAT):"
echo "   grep dtoverlay /boot/config.txt | grep -i pcm"
echo
echo "3. 🔧 Test PCM5122 direct access:"
echo "   aplay -D hw:CARD=sndrpihifiberry,DEV=0 /usr/share/sounds/alsa/Front_Left.wav"
echo
echo "4. 🔧 Check for I2S conflicts:"
echo "   dmesg | grep -i i2s"
echo
echo "5. 🔧 Create PCM5122-specific ALSA configuration:"
echo "   sudo nano /etc/asound.conf"

# Clean up temporary files
print_info "Cleaning up temporary test files..."
rm -f /tmp/cisynth_*_test.log

echo
echo "🏁 Testing completed. Review recommendations above."
echo "   Use diagnose_pcm5122_audio.sh for detailed system analysis."
