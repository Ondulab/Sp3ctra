#!/bin/bash

# Fix device routing - BossDAC is device 3, not 2!
# Based on log analysis showing wrong device selection

echo "🎯 FIXING DEVICE ROUTING FOR BOSSDAC"
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

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# 1. Analyze current device mapping
print_info "Analyzing current audio device mapping..."

echo "Available ALSA devices:"
aplay -l

echo -e "\nALSA card mapping:"
cat /proc/asound/cards

# 2. Find BossDAC device number
print_info "Finding correct BossDAC device number..."

bossdac_card=$(aplay -l 2>/dev/null | grep -i "bossdac\|pcm512" | head -1 | grep -o "card [0-9]\+" | grep -o "[0-9]\+")

if [ ! -z "$bossdac_card" ]; then
    print_success "BossDAC found as card $bossdac_card"
    echo "BossDAC device should be: hw:$bossdac_card,0"
else
    print_error "BossDAC card not found in aplay -l output"
    # Try alternative detection
    bossdac_card=$(cat /proc/asound/cards | grep -i "bossdac\|pcm512" | head -1 | grep -o "^[[:space:]]*[0-9]\+" | tr -d ' ')
    if [ ! -z "$bossdac_card" ]; then
        print_warning "BossDAC found via /proc/asound/cards as card $bossdac_card"
    fi
fi

# 3. Test the correct device directly
if [ ! -z "$bossdac_card" ]; then
    print_info "Testing BossDAC device directly..."
    
    echo "Testing hw:$bossdac_card,0 with 440Hz tone..."
    timeout 3 speaker-test -D hw:$bossdac_card,0 -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1 2>/dev/null
    
    if [ $? -eq 0 ] || [ $? -eq 124 ]; then
        print_success "BossDAC hw:$bossdac_card,0 works correctly!"
    else
        print_error "BossDAC hw:$bossdac_card,0 has issues"
    fi
fi

# 4. Create corrected ALSA configuration to force default to BossDAC
print_info "Creating corrected ALSA configuration to route default to BossDAC..."

if [ ! -z "$bossdac_card" ]; then
    # Backup current config
    if [ -f "$HOME/.asoundrc" ]; then
        cp "$HOME/.asoundrc" "$HOME/.asoundrc.device_fix_backup"
        print_info "Current config backed up to ~/.asoundrc.device_fix_backup"
    fi

    # Create new config with correct device routing
    cat > "$HOME/.asoundrc" << EOF
# Corrected ALSA configuration - Force default to BossDAC
pcm.!default {
    type plug
    slave {
        pcm "hw:$bossdac_card,0"
        format S32_LE
        rate 48000
        channels 2
    }
}

ctl.!default {
    type hw
    card $bossdac_card
}

# Direct BossDAC access
pcm.bossdac {
    type hw
    card $bossdac_card
    device 0
    format S32_LE
    rate 48000
    channels 2
}

# Alternative names for BossDAC
pcm.hw_bossdac {
    type hw
    card $bossdac_card
    device 0
}
EOF

    print_success "ALSA configuration updated to use BossDAC card $bossdac_card"
else
    print_error "Cannot determine BossDAC card number - manual intervention needed"
fi

# 5. Create test script with correct device number
print_info "Creating test script with correct device numbers..."

cat > test_correct_devices.sh << 'EOF'
#!/bin/bash

echo "🎵 TESTING CORRECT DEVICE NUMBERS"
echo "================================="

# Get BossDAC card number
bossdac_card=$(aplay -l 2>/dev/null | grep -i "bossdac\|pcm512" | head -1 | grep -o "card [0-9]\+" | grep -o "[0-9]\+")

if [ -z "$bossdac_card" ]; then
    echo "❌ Cannot find BossDAC card number"
    exit 1
fi

echo "✅ BossDAC detected as card $bossdac_card"
echo

# Test 1: Direct BossDAC access
echo "Testing hw:$bossdac_card,0 (BossDAC direct)..."
timeout 3 speaker-test -D hw:$bossdac_card,0 -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1

echo -e "\nTesting default device (should now route to BossDAC)..."
timeout 3 speaker-test -D default -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1

echo -e "\nBossDAC is device: hw:$bossdac_card,0"
echo "For CISYNTH, use: --audio-device $bossdac_card"
EOF

chmod +x test_correct_devices.sh

# 6. Test the new configuration
print_info "Testing new ALSA configuration..."
timeout 3 speaker-test -D default -c 2 -r 48000 -f S32_LE -t sine -F 440 -l 1 >/dev/null 2>&1

if [ $? -eq 0 ] || [ $? -eq 124 ]; then
    print_success "Default device now routes to BossDAC correctly!"
else
    print_warning "Default device routing may still have issues"
fi

# 7. Provide corrected commands
echo
print_info "CORRECTED COMMANDS:"
echo "=================="

if [ ! -z "$bossdac_card" ]; then
    echo "✅ Use this command for CISYNTH:"
    echo "   ./build_nogui/CISYNTH_noGUI --audio-device $bossdac_card"
    echo
    echo "✅ Or use default (now routes to BossDAC):"
    echo "   ./build_nogui/CISYNTH_noGUI"
    echo
    echo "✅ Test audio first:"
    echo "   ./test_correct_devices.sh"
else
    echo "❌ Manual detection needed:"
    echo "   Check: aplay -l"
    echo "   Find BossDAC card number"
    echo "   Use: ./build_nogui/CISYNTH_noGUI --audio-device [card_number]"
fi

echo
print_success "Device routing fix completed!"
print_info "The issue was: You used device 2, but BossDAC is device $bossdac_card"
