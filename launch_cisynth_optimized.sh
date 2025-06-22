#!/bin/bash

# Optimized CISYNTH launcher for Pi Module 5 with real-time scheduling
echo "🚀 LAUNCHING CISYNTH WITH PI MODULE 5 OPTIMIZATIONS"
echo "=================================================="

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

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    print_warning "Running as root - real-time scheduling will be enabled"
    USE_SUDO=""
else
    print_info "Checking real-time capabilities..."
    # Check if user can use real-time scheduling
    if ! ulimit -r unlimited 2>/dev/null; then
        print_warning "Need sudo for real-time audio scheduling"
        print_info "This is required to eliminate audio dropouts"
        USE_SUDO="sudo"
    else
        print_success "Real-time capabilities available"
        USE_SUDO=""
    fi
fi

# Set CPU governor to performance if not already
print_info "Setting CPU governor to performance..."
if [ -w "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
    echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1
    print_success "CPU governor set to performance"
else
    print_warning "Cannot set CPU governor (may need sudo)"
fi

# Check if binary exists and is up to date
print_info "Checking CISYNTH binary..."
if [ ! -f "build_nogui/CISYNTH_noGUI" ]; then
    print_warning "CISYNTH binary not found - building..."
    ./rebuild_cli.sh --cli
elif [ "src/core/audio_rtaudio.cpp" -nt "build_nogui/CISYNTH_noGUI" ] || \
     [ "src/core/config.h" -nt "build_nogui/CISYNTH_noGUI" ] || \
     [ "CISYNTH_noGUI.pro" -nt "build_nogui/CISYNTH_noGUI" ]; then
    print_warning "Source files newer than binary - rebuilding..."
    ./rebuild_cli.sh --cli
else
    print_success "CISYNTH binary is up to date"
fi

# Verify binary exists after potential build
if [ ! -f "build_nogui/CISYNTH_noGUI" ]; then
    print_error "Failed to build CISYNTH binary"
    exit 1
fi

# Show system info
print_info "System information:"
echo "  Architecture: $(uname -m)"
echo "  CPU cores: $(nproc)"
echo "  Memory: $(free -h | grep '^Mem:' | awk '{print $2}')"

# Check CPU frequency
if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" ]; then
    freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
    freq_mhz=$((freq / 1000))
    echo "  CPU frequency: ${freq_mhz}MHz"
fi

# Check thermal state
if command -v vcgencmd >/dev/null 2>&1; then
    temp=$(vcgencmd measure_temp 2>/dev/null | cut -d= -f2 || echo "unknown")
    echo "  Temperature: $temp"
    
    throttled=$(vcgencmd get_throttled 2>/dev/null || echo "throttled=unknown")
    if [ "$throttled" = "throttled=0x0" ]; then
        print_success "No thermal throttling"
    else
        print_warning "Throttling detected: $throttled"
    fi
fi

echo

# Launch with optimizations
print_info "🎵 Launching CISYNTH with Pi Module 5 optimizations..."
print_info "   - Real-time scheduling (priority 85)"
print_info "   - CPU affinity to cores 2-3 (leaving 0-1 for system)"
print_info "   - Memory locking to prevent swapping"
print_info "   - Large audio buffers (1024 frames)"
echo

# Prepare launch command with optimizations
LAUNCH_CMD="taskset -c 2,3 chrt -r 85 ionice -c 1 -n 4"

# Add memory locking if available
if command -v setcap >/dev/null 2>&1; then
    LAUNCH_CMD="$LAUNCH_CMD setcap cap_sys_nice,cap_ipc_lock=eip"
fi

# Launch with or without sudo based on capabilities
if [ -n "$USE_SUDO" ]; then
    print_info "Launching with sudo for real-time privileges..."
    exec $USE_SUDO $LAUNCH_CMD ./build_nogui/CISYNTH_noGUI "$@"
else
    print_info "Launching with user privileges..."
    # Try real-time launch first, fall back to normal if it fails
    if ! $LAUNCH_CMD ./build_nogui/CISYNTH_noGUI "$@" 2>/dev/null; then
        print_warning "Real-time launch failed, trying normal launch..."
        exec taskset -c 2,3 nice -n -19 ./build_nogui/CISYNTH_noGUI "$@"
    fi
fi
