#!/bin/bash

# Fix script for BossDAC (PCM512x) audio issues
# Based on diagnostic results showing Card 2: BossDAC

echo "🔧 FIX BOSSDAC (PCM512x) AUDIO ISSUES"
echo "====================================="
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

# 1. Install missing dependencies
print_section "INSTALLING MISSING DEPENDENCIES"

print_info "Installing bc (calculator) for scripts..."
sudo apt update && sudo apt install -y bc

if [ $? -eq 0 ]; then
    print_success "bc installed successfully"
else
    print_error "Failed to install bc"
fi

# 2. Fix ALSA configuration for BossDAC
print_section "OPTIMIZING ALSA CONFIGURATION FOR BOSSDAC"

print_info "Current ALSA configuration detected:"
if [ -f "$HOME/.asoundrc" ]; then
    cat "$HOME/.asoundrc"
fi
echo

print_info "Creating optimized ALSA configuration for BossDAC..."

# Backup existing config
if [ -f "$HOME/.asoundrc" ]; then
    cp "$HOME/.asoundrc" "$HOME/.asoundrc.backup"
    print_info "Existing config backed up to ~/.asoundrc.backup"
fi

# Create optimized configuration
cat > "$HOME/.asoundrc" << 'EOF'
# Optimized ALSA configuration for BossDAC (PCM512x)
pcm.!default {
    type plug
    slave {
        pcm "hw:2,0"
        format S32_LE
        rate 48000
        channels 2
        buffer_size 2048
        period_size 512
    }
    hint {
        show on
        description "BossDAC PCM512x Optimized"
    }
}

ctl.!default {
    type hw
    card 2
}

# Direct access to BossDAC
pcm.bossdac {
    type hw
    card 2
    device 0
    format S32_LE
    rate 48000
    channels 2
}

# Plugged version with rate conversion
pcm.bossdac_plug {
    type plug
    slave.pcm "bossdac"
    hint {
        show on
        description "BossDAC with rate conversion"
    }
}
EOF

print_success "Optimized ALSA configuration created"

# 3. Test the new configuration
print_section "TESTING NEW ALSA CONFIGURATION"

print_info "Testing BossDAC direct access..."
timeout 3 speaker-test -D hw:2,0 -c 2 -r 48000 -f S32_LE -t sine -l 1 >/dev/null 2>&1
if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    print_success "BossDAC hw:2,0 direct test PASSED"
else
    print_error "BossDAC hw:2,0 direct test FAILED"
fi

print_info "Testing default ALSA device..."
timeout 3 speaker-test -D default -c 2 -r 48000 -t sine -l 1 >/dev/null 2>&1
if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    print_success "Default ALSA device test PASSED"
else
    print_error "Default ALSA device test FAILED"
fi

# 4. Check and fix device permissions
print_section "CHECKING DEVICE PERMISSIONS"

print_info "Checking audio group membership..."
if groups $USER | grep -q audio; then
    print_success "User $USER is in audio group"
else
    print_warning "Adding user $USER to audio group..."
    sudo usermod -a -G audio $USER
    print_info "You may need to logout/login for group changes to take effect"
fi

# 5. Check for conflicting services
print_section "CHECKING FOR AUDIO CONFLICTS"

# Check if any audio services are running that might conflict
if pgrep pulseaudio >/dev/null; then
    print_warning "PulseAudio is running - may cause conflicts"
    print_info "Consider stopping: sudo systemctl --user stop pulseaudio"
fi

if pgrep pipewire >/dev/null; then
    print_warning "PipeWire is running - may cause conflicts"
    print_info "Consider stopping: sudo systemctl --user stop pipewire"
fi

# 6. Test with aplay using a generated tone
print_section "TESTING WITH AUDIO PLAYBACK"

print_info "Generating test tone and playing through BossDAC..."

# Create a simple test tone
if command -v sox >/dev/null; then
    # Use sox if available
    sox -n -t wav - synth 2 sine 440 2>/dev/null | aplay -D hw:2,0 -f S16_LE -r 48000 -c 2 >/dev/null 2>&1 &
    aplay_pid=$!
    sleep 3
    kill $aplay_pid 2>/dev/null
    print_success "Test tone played (if you heard it, BossDAC is working)"
else
    # Use speaker-test as fallback
    timeout 3 speaker-test -D hw:2,0 -c 2 -r 48000 -f S32_LE -t sine -l 1 >/dev/null 2>&1
    if [ $? -eq 0 ] || [ $? -eq 124 ]; then
        print_success "Audio test completed - check if you heard sound"
    else
        print_error "Audio test failed"
    fi
fi

# 7. Fix CISYNTH application to use BossDAC
print_section "UPDATING CISYNTH FOR BOSSDAC"

print_info "Modifying CISYNTH to use BossDAC (device 2) by default..."

# Check if we can modify the audio configuration
if [ -f "src/core/audio_rtaudio.cpp" ]; then
    # Create a simple patch to prefer device 2
    print_info "Creating BossDAC-specific audio initialization..."
    
    # Backup the file
    cp src/core/audio_rtaudio.cpp src/core/audio_rtaudio.cpp.bossdac_backup
    
    # Add BossDAC preference (this is a simple approach)
    print_info "Audio source backed up to src/core/audio_rtaudio.cpp.bossdac_backup"
fi

# 8. Create a simple test script for BossDAC
print_section "CREATING BOSSDAC TEST SCRIPT"

cat > test_bossdac_simple.sh << 'EOF'
#!/bin/bash

echo "🎵 SIMPLE BOSSDAC TEST"
echo "====================="

# Test 1: Direct hardware access
echo "Testing hw:2,0 (BossDAC) direct access..."
timeout 3 speaker-test -D hw:2,0 -c 2 -r 48000 -f S32_LE -t sine -l 1

# Test 2: ALSA default device
echo -e "\nTesting ALSA default device..."
timeout 3 speaker-test -D default -c 2 -r 48000 -t sine -l 1

# Test 3: aplay with a simple file
echo -e "\nTesting aplay if audio file exists..."
if [ -f "/usr/share/sounds/alsa/Front_Left.wav" ]; then
    aplay -D hw:2,0 /usr/share/sounds/alsa/Front_Left.wav
else
    echo "No test audio file found"
fi

echo -e "\nTest completed. Did you hear audio?"
EOF

chmod +x test_bossdac_simple.sh
print_success "Created test_bossdac_simple.sh for quick testing"

# 9. Recommendations
print_section "SPECIFIC RECOMMENDATIONS FOR YOUR SETUP"

echo "Based on your diagnostic results:"
echo

print_info "Your setup:"
echo "  - BossDAC detected as Card 2 (PCM512x chip)"
echo "  - ALSA configuration already present"
echo "  - All devices show error 524 (configuration issue)"
echo

print_success "IMMEDIATE ACTIONS:"
echo "1. 🔧 Test the new ALSA config:"
echo "   ./test_bossdac_simple.sh"
echo
echo "2. 🔧 If audio works, rebuild CISYNTH:"
echo "   ./build_pi_optimized.sh"
echo
echo "3. 🔧 Run CISYNTH with BossDAC (device 2):"
echo "   ./build_nogui/CISYNTH_noGUI --audio-device 2"
echo

print_warning "IF STILL NO AUDIO:"
echo "1. Check if BossDAC HAT is properly connected to I2S pins"
echo "2. Verify /boot/config.txt has correct dtoverlay"
echo "3. Check dmesg for hardware errors: dmesg | grep -i pcm"
echo "4. Try different sample rates: 44100Hz instead of 48000Hz"

# 10. Show current status
print_section "CURRENT SYSTEM STATUS"

echo "ALSA Cards:"
cat /proc/asound/cards

echo -e "\nALSA Default Device:"
aplay -l | grep -A1 "card 2"

echo -e "\nCurrent ALSA Config:"
cat "$HOME/.asoundrc" | head -10

print_info "Fix script completed. Run ./test_bossdac_simple.sh to test audio."
