#!/bin/bash

# Script de diagnostic pour carte son PCM5122
# Diagnostique les problèmes ALSA et audio haché

echo "🔍 DIAGNOSTIC CARTE SON PCM5122"
echo "================================="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to print colored output
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

# 1. System Information
print_section "SYSTEM INFORMATION"
echo "Hostname: $(hostname)"
echo "Kernel: $(uname -r)"
echo "OS: $(cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)"
echo "Architecture: $(uname -m)"
echo

# 2. ALSA Configuration
print_section "ALSA SYSTEM STATUS"

# Check if ALSA is running
if systemctl is-active --quiet alsa-state; then
    print_success "ALSA service is running"
else
    print_warning "ALSA service status unknown"
fi

# List all sound cards
echo
print_info "Sound cards detected:"
cat /proc/asound/cards 2>/dev/null || print_error "Cannot read /proc/asound/cards"
echo

# Check ALSA version
echo "ALSA version information:"
cat /proc/asound/version 2>/dev/null || print_error "Cannot read ALSA version"
echo

# 3. PCM5122 Specific Detection
print_section "PCM5122 DETECTION"

# Search for PCM5122 in kernel messages
echo "Checking kernel messages for PCM5122..."
dmesg | grep -i pcm5122 | tail -10
if [ $? -eq 0 ]; then
    print_success "PCM5122 detected in kernel messages"
else
    print_warning "No PCM5122 references found in kernel messages"
fi
echo

# Search for PCM5122 in ALSA cards
if grep -i pcm5122 /proc/asound/cards 2>/dev/null; then
    print_success "PCM5122 found in ALSA cards"
else
    print_warning "PCM5122 not explicitly found in ALSA cards"
fi
echo

# 4. USB Audio Devices (if PCM5122 is USB)
print_section "USB AUDIO DEVICES"
lsusb | grep -i audio
if [ $? -eq 0 ]; then
    print_info "USB audio devices found"
else
    print_info "No USB audio devices detected"
fi
echo

# 5. Detailed ALSA Device Information
print_section "DETAILED ALSA DEVICE ANALYSIS"

for card in /proc/asound/card*; do
    if [ -d "$card" ]; then
        card_num=$(basename "$card" | sed 's/card//')
        echo "=== Card $card_num ==="
        
        # Card ID and name
        if [ -f "$card/id" ]; then
            card_id=$(cat "$card/id")
            echo "Card ID: $card_id"
        fi
        
        # PCM devices
        if [ -d "$card/pcm0p" ]; then
            echo "PCM 0 Playback: Available"
            if [ -f "$card/pcm0p/info" ]; then
                echo "PCM 0 Info:"
                cat "$card/pcm0p/info" | head -5
            fi
        fi
        
        echo
    fi
done

# 6. Test ALSA devices with aplay
print_section "ALSA DEVICE CAPABILITY TESTING"

echo "Testing ALSA devices with speaker-test..."
echo

# Get list of PCM devices
aplay -l 2>/dev/null | grep "^card" | while IFS= read -r line; do
    # Extract card and device numbers
    card_num=$(echo "$line" | grep -o "card [0-9]\+" | grep -o "[0-9]\+")
    device_num=$(echo "$line" | grep -o "device [0-9]\+" | grep -o "[0-9]\+")
    device_name=$(echo "$line" | cut -d':' -f2- | sed 's/^[ \t]*//')
    
    echo "Testing Card $card_num, Device $device_num: $device_name"
    
    # Test basic functionality
    timeout 3 speaker-test -D hw:$card_num,$device_num -c 2 -r 48000 -f S16_LE -t sine -l 1 >/dev/null 2>&1
    if [ $? -eq 0 ] || [ $? -eq 124 ]; then  # 124 is timeout exit code
        print_success "hw:$card_num,$device_num - Basic test PASSED"
    else
        print_error "hw:$card_num,$device_num - Basic test FAILED"
    fi
    
    # Test different sample rates
    for rate in 44100 48000 96000; do
        timeout 2 speaker-test -D hw:$card_num,$device_num -c 2 -r $rate -f S16_LE -t sine -l 1 >/dev/null 2>&1
        if [ $? -eq 0 ] || [ $? -eq 124 ]; then
            echo "  ✓ ${rate}Hz supported"
        else
            echo "  ✗ ${rate}Hz failed"
        fi
    done
    
    echo
done

# 7. ALSA Configuration Files
print_section "ALSA CONFIGURATION FILES"

echo "Checking for custom ALSA configuration..."

# Check system-wide config
if [ -f "/etc/asound.conf" ]; then
    print_info "System ALSA config found: /etc/asound.conf"
    echo "Content preview:"
    head -20 /etc/asound.conf
    echo
else
    print_info "No system ALSA config (/etc/asound.conf)"
fi

# Check user config
if [ -f "$HOME/.asoundrc" ]; then
    print_info "User ALSA config found: ~/.asoundrc"
    echo "Content preview:"
    head -20 "$HOME/.asoundrc"
    echo
else
    print_info "No user ALSA config (~/.asoundrc)"
fi

# 8. System Audio Status
print_section "SYSTEM AUDIO STATUS"

# Check for PulseAudio
if pgrep pulseaudio >/dev/null; then
    print_warning "PulseAudio is running (may interfere with direct ALSA access)"
    echo "PulseAudio devices:"
    pactl list short sinks 2>/dev/null || echo "Cannot list PulseAudio sinks"
else
    print_info "PulseAudio not running"
fi

# Check for PipeWire
if pgrep pipewire >/dev/null; then
    print_warning "PipeWire is running"
else
    print_info "PipeWire not running"
fi

# Check for JACK
if pgrep jackd >/dev/null; then
    print_warning "JACK is running"
else
    print_info "JACK not running"
fi

echo

# 9. Error Analysis
print_section "ERROR ANALYSIS"

echo "Analyzing recent ALSA errors in system logs..."
journalctl --since "1 hour ago" | grep -i "alsa\|snd_\|pcm\|audio" | tail -20
echo

echo "Checking for USB device errors..."
dmesg | grep -i "usb.*audio\|usb.*sound" | tail -10
echo

# 10. Recommendations
print_section "DIAGNOSTIC SUMMARY & RECOMMENDATIONS"

echo "Based on the analysis above:"
echo

# Check if we found issues
error_count=0

# Count errors from previous commands
if ! systemctl is-active --quiet alsa-state; then
    print_error "ALSA service issues detected"
    ((error_count++))
fi

if ! grep -q pcm5122 /proc/asound/cards 2>/dev/null; then
    print_warning "PCM5122 not clearly identified in ALSA"
    ((error_count++))
fi

if pgrep pulseaudio >/dev/null; then
    print_warning "PulseAudio may be interfering with direct ALSA access"
    echo "  → Consider stopping PulseAudio: sudo systemctl stop pulseaudio"
    ((error_count++))
fi

# Specific recommendations
echo
print_info "RECOMMENDED NEXT STEPS:"
echo
echo "1. 🔧 Test specific ALSA device:"
echo "   aplay -D hw:0,0 /usr/share/sounds/alsa/Front_Left.wav"
echo "   aplay -D hw:1,0 /usr/share/sounds/alsa/Front_Left.wav"
echo
echo "2. 🔧 Test with different sample rates:"
echo "   speaker-test -D hw:0,0 -c 2 -r 48000 -f S16_LE -t sine"
echo
echo "3. 🔧 Check PCM5122 specific configuration:"
echo "   cat /sys/class/sound/card*/id"
echo
echo "4. 🔧 If USB connected, check USB power:"
echo "   lsusb -v | grep -A5 -B5 -i pcm5122"
echo
echo "5. 🔧 Create custom ALSA config if needed:"
echo "   sudo nano /etc/asound.conf"

if [ $error_count -eq 0 ]; then
    print_success "No major configuration issues detected"
    print_info "Audio problems may be related to buffer sizes or system performance"
else
    print_warning "$error_count potential issues found - review recommendations above"
fi

echo
echo "🏁 Diagnostic completed. Please review the output above for specific issues."
echo "   Save this output for further analysis if needed."
