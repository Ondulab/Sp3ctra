#!/bin/bash

# Quick fix for ALSA configuration issues
# Based on test results showing audio working but config errors

echo "🔧 FIXING ALSA CONFIGURATION ERRORS"
echo "===================================="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

print_info "Audio is working! Just fixing configuration errors..."

# Backup current config
if [ -f "$HOME/.asoundrc" ]; then
    cp "$HOME/.asoundrc" "$HOME/.asoundrc.before_fix"
    print_info "Current config backed up to ~/.asoundrc.before_fix"
fi

# Create corrected ALSA configuration
print_info "Creating corrected ALSA configuration..."

cat > "$HOME/.asoundrc" << 'EOF'
# Corrected ALSA configuration for BossDAC (PCM512x)
pcm.!default {
    type plug
    slave {
        pcm "hw:2,0"
        format S32_LE
        rate 48000
        channels 2
    }
    hint {
        show on
        description "BossDAC PCM512x Corrected"
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

# BossDAC with plug for compatibility
pcm.bossdac_plug {
    type plug
    slave.pcm "bossdac"
    hint {
        show on
        description "BossDAC with format conversion"
    }
}
EOF

print_success "Fixed ALSA configuration (removed problematic buffer_size field)"

# Create improved test script
print_info "Creating improved audio test..."

cat > test_bossdac_corrected.sh << 'EOF'
#!/bin/bash

echo "🎵 CORRECTED BOSSDAC TEST"
echo "========================="

# Test 1: Direct hardware access with audible frequency
echo "Testing hw:2,0 (BossDAC) with 440Hz tone..."
timeout 3 speaker-test -D hw:2,0 -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1

# Test 2: ALSA default device (should work now)
echo -e "\nTesting ALSA default device..."
timeout 3 speaker-test -D default -c 2 -r 48000 -t sine -F 440 -l 1

# Test 3: aplay with stereo format
echo -e "\nTesting aplay with correct format..."
if [ -f "/usr/share/sounds/alsa/Front_Left.wav" ]; then
    # Convert to stereo and play
    aplay -D hw:2,0 -f S32_LE -r 48000 -c 2 /usr/share/sounds/alsa/Front_Left.wav 2>/dev/null
    if [ $? -ne 0 ]; then
        # Try with original format
        aplay -D hw:2,0 /usr/share/sounds/alsa/Front_Left.wav 2>/dev/null
    fi
else
    echo "No test audio file found"
fi

# Test 4: Generate a pleasant test tone
echo -e "\nGenerating pleasant test tone (A4 = 440Hz)..."
if command -v sox >/dev/null; then
    sox -n -t wav - synth 2 sine 440 vol 0.5 2>/dev/null | aplay -D hw:2,0 -f S16_LE -r 48000 -c 2 2>/dev/null
    echo "Did you hear a clear 440Hz tone? That's the musical note A4!"
else
    timeout 3 speaker-test -D hw:2,0 -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1 2>/dev/null
fi

echo -e "\nTest completed!"
echo "If you heard clear tones (not very low frequency), BossDAC is working perfectly!"
EOF

chmod +x test_bossdac_corrected.sh
print_success "Created test_bossdac_corrected.sh with audible frequencies"

# Test the corrected configuration
print_info "Testing corrected configuration..."

timeout 3 speaker-test -D default -c 2 -r 48000 -t sine -F 440 -l 1 >/dev/null 2>&1
if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    print_success "Default ALSA device now works correctly!"
else
    print_warning "Default device still has issues, but hw:2,0 works"
fi

# Show corrected configuration
echo
print_info "Corrected ALSA configuration:"
echo "=================================="
cat "$HOME/.asoundrc"

echo
echo "🎯 NEXT STEPS:"
echo "1. Test with audible frequencies: ./test_bossdac_corrected.sh"
echo "2. If audio is clear, rebuild CISYNTH: ./build_pi_optimized.sh"
echo "3. Test CISYNTH: ./build_nogui/CISYNTH_noGUI --audio-device 2"
echo
print_success "BossDAC audio fix completed!"
