#!/bin/bash
# test_usb_audio_devices.sh - Test complete USB audio device enumeration
# Sp3ctra USB Audio Testing Script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BACKUP_CONFIG="/tmp/asound.conf.backup"

echo -e "${BLUE}=====================================================${NC}"
echo -e "${BLUE}     Sp3ctra USB Audio Device Complete Test        ${NC}"
echo -e "${BLUE}=====================================================${NC}"

# Function to backup current ALSA config
backup_alsa_config() {
    if [ -f "/etc/asound.conf" ]; then
        echo -e "${YELLOW}📁 Backing up current /etc/asound.conf${NC}"
        sudo cp /etc/asound.conf "$BACKUP_CONFIG"
    fi
}

# Function to restore ALSA config
restore_alsa_config() {
    if [ -f "$BACKUP_CONFIG" ]; then
        echo -e "${YELLOW}🔄 Restoring original /etc/asound.conf${NC}"
        sudo cp "$BACKUP_CONFIG" /etc/asound.conf
    fi
}

# Function to test with different ALSA configs
test_alsa_config() {
    local config_file="$1"
    local config_name="$2"
    
    echo -e "\n${CYAN}=== Testing with $config_name configuration ===${NC}"
    
    if [ -f "$config_file" ]; then
        sudo cp "$config_file" /etc/asound.conf
        echo -e "${GREEN}✅ Applied $config_name configuration${NC}"
    else
        echo -e "${RED}❌ Configuration file not found: $config_file${NC}"
        return 1
    fi
    
    # Wait a moment for ALSA to pick up changes
    sleep 2
    
    echo -e "\n${BLUE}--- ALSA Devices (aplay -l) ---${NC}"
    aplay -l 2>/dev/null || echo -e "${RED}❌ aplay -l failed${NC}"
    
    echo -e "\n${BLUE}--- Sp3ctra Device Detection ---${NC}"
    cd "$PROJECT_ROOT"
    ./build/Sp3ctra --list-audio-devices 2>&1 | head -20
    
    return 0
}

echo -e "\n${BLUE}=== Step 1: System Audio Information ===${NC}"

echo -e "\n${CYAN}USB Devices:${NC}"
lsusb | grep -i audio || echo -e "${YELLOW}⚠️  No USB audio devices detected by lsusb${NC}"

echo -e "\n${CYAN}All USB Devices:${NC}"
lsusb

echo -e "\n${CYAN}ALSA Hardware Devices:${NC}"
aplay -l

echo -e "\n${CYAN}ALSA Cards:${NC}"
cat /proc/asound/cards || echo -e "${RED}❌ Cannot read /proc/asound/cards${NC}"

echo -e "\n${BLUE}=== Step 2: Test Default Configuration ===${NC}"
echo -e "${YELLOW}Testing with current ALSA configuration...${NC}"

cd "$PROJECT_ROOT"
if [ ! -f "./build/Sp3ctra" ]; then
    echo -e "${RED}❌ Sp3ctra binary not found. Please run 'make' first.${NC}"
    exit 1
fi

echo -e "\n${CYAN}Current Sp3ctra device detection:${NC}"
./build/Sp3ctra --list-audio-devices 2>&1 | head -20

# Backup current config before testing
backup_alsa_config

echo -e "\n${BLUE}=== Step 3: Test Alternative ALSA Configurations ===${NC}"

# Test 1: Original simple configuration
echo -e "\n${CYAN}--- Test 1: Simple USB SPDIF Configuration ---${NC}"
cat > /tmp/test_simple.conf << 'EOF'
pcm.!default {
    type hw
    card 0
    device 0
}
ctl.!default {
    type hw
    card 0
}
EOF

test_alsa_config "/tmp/test_simple.conf" "Simple USB SPDIF"

# Test 2: Advanced configuration (if exists)
if [ -f "$SCRIPT_DIR/asound_spdif_advanced.conf" ]; then
    test_alsa_config "$SCRIPT_DIR/asound_spdif_advanced.conf" "Advanced USB SPDIF"
fi

# Test 3: No ALSA config (system default)
echo -e "\n${CYAN}=== Test 3: System Default (No Custom ALSA Config) ===${NC}"
if [ -f "/etc/asound.conf" ]; then
    sudo mv /etc/asound.conf /etc/asound.conf.temp
    echo -e "${GREEN}✅ Temporarily removed custom ALSA config${NC}"
    
    sleep 2
    
    echo -e "\n${BLUE}--- System Default Device Detection ---${NC}"
    cd "$PROJECT_ROOT"
    ./build/Sp3ctra --list-audio-devices 2>&1 | head -20
    
    sudo mv /etc/asound.conf.temp /etc/asound.conf
    echo -e "${GREEN}✅ Restored custom ALSA config${NC}"
fi

echo -e "\n${BLUE}=== Step 4: Direct Hardware Access Tests ===${NC}"

echo -e "\n${CYAN}Testing direct hardware access:${NC}"
for card in 0 1 2; do
    echo -e "\n${YELLOW}Testing Card $card:${NC}"
    
    # Test if card exists
    if [ -e "/proc/asound/card$card" ]; then
        echo -e "${GREEN}✅ Card $card exists${NC}"
        
        # Try to get card info
        if cat "/proc/asound/card$card/id" 2>/dev/null; then
            echo -e "${GREEN}✅ Card $card is accessible${NC}"
        else
            echo -e "${RED}❌ Card $card access denied${NC}"
        fi
        
        # Test speaker-test (non-blocking)
        echo -e "${CYAN}Testing audio output to Card $card...${NC}"
        timeout 3 speaker-test -c 2 -r 48000 -D hw:$card,0 -t sine -f 1000 >/dev/null 2>&1 && \
            echo -e "${GREEN}✅ Card $card audio output works${NC}" || \
            echo -e "${RED}❌ Card $card audio output failed${NC}"
    else
        echo -e "${YELLOW}⚠️  Card $card does not exist${NC}"
    fi
done

echo -e "\n${BLUE}=== Step 5: Build and Test Enhanced Code ===${NC}"

echo -e "${YELLOW}Rebuilding Sp3ctra with enhanced USB enumeration...${NC}"
cd "$PROJECT_ROOT"
if make -j4 >/dev/null 2>&1; then
    echo -e "${GREEN}✅ Build successful${NC}"
else
    echo -e "${RED}❌ Build failed${NC}"
    restore_alsa_config
    exit 1
fi

echo -e "\n${CYAN}Testing enhanced device detection:${NC}"
./build/Sp3ctra --list-audio-devices

echo -e "\n${BLUE}=== Step 6: Device-Specific Tests ===${NC}"

# If USB SPDIF is detected, test direct access
if aplay -l | grep -qi spdif; then
    echo -e "\n${CYAN}USB SPDIF detected - testing direct access:${NC}"
    
    # Get the exact card number for USB SPDIF
    SPDIF_CARD=$(aplay -l | grep -i spdif | head -1 | sed 's/card \([0-9]\).*/\1/')
    
    if [ -n "$SPDIF_CARD" ]; then
        echo -e "${YELLOW}Testing direct SPDIF access (Card $SPDIF_CARD)...${NC}"
        timeout 3 speaker-test -c 2 -r 48000 -D hw:$SPDIF_CARD,0 -t sine -f 440 >/dev/null 2>&1 && \
            echo -e "${GREEN}✅ Direct SPDIF access works${NC}" || \
            echo -e "${YELLOW}⚠️  Direct SPDIF access limited (normal for some adapters)${NC}"
    fi
fi

# Restore original config
restore_alsa_config

echo -e "\n${BLUE}=== Test Results Summary ===${NC}"
echo -e "${GREEN}✅ USB device enumeration test completed${NC}"
echo -e "${YELLOW}📋 Check the output above for detailed results${NC}"
echo -e "${CYAN}💡 The enhanced code should now detect more USB audio devices${NC}"

echo -e "\n${BLUE}=== Recommended Next Steps ===${NC}"
echo -e "1. ${YELLOW}Run the real-time audio script:${NC} sudo ./scripts/raspberry/fix_pi_realtime_audio.sh"
echo -e "2. ${YELLOW}Reboot the system${NC} to apply all changes"
echo -e "3. ${YELLOW}Test audio validation:${NC} sp3ctra-audio-test"
echo -e "4. ${YELLOW}Test Sp3ctra with enhanced detection:${NC} ./build/Sp3ctra --list-audio-devices"

echo -e "\n${BLUE}=====================================================${NC}"
echo -e "${GREEN}Test script completed successfully!${NC}"
echo -e "${BLUE}=====================================================${NC}"
