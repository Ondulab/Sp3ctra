#!/bin/bash
# fix_pi_realtime_audio.sh - Configure Raspberry Pi for real-time audio
# Sp3ctra USB SPDIF Configuration Script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

USER_NAME="sp3ctra"
SCRIPT_NAME="fix_pi_realtime_audio.sh"

echo -e "${BLUE}===========================================${NC}"
echo -e "${BLUE} Sp3ctra USB SPDIF Real-time Audio Setup ${NC}"
echo -e "${BLUE}===========================================${NC}"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}❌ This script must be run as root${NC}"
    echo "Usage: sudo ./$SCRIPT_NAME"
    exit 1
fi

# Backup directory
BACKUP_DIR="/etc/sp3ctra-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"
echo -e "${YELLOW}📁 Creating backups in: $BACKUP_DIR${NC}"

# Function to backup file
backup_file() {
    local file="$1"
    if [ -f "$file" ]; then
        cp "$file" "$BACKUP_DIR/"
        echo -e "${GREEN}✅ Backed up: $file${NC}"
    fi
}

echo -e "\n${BLUE}=== Step 1: User Groups Configuration ===${NC}"

# Add user to audio groups (should already be done but ensure it)
usermod -a -G audio,pulse,pulse-access "$USER_NAME"
echo -e "${GREEN}✅ User $USER_NAME added to audio groups${NC}"

echo -e "\n${BLUE}=== Step 2: Real-time Limits Configuration ===${NC}"

# Backup and configure limits
backup_file "/etc/security/limits.conf"

# Create limits configuration for real-time audio
cat >> /etc/security/limits.conf << EOF

# Sp3ctra Real-time Audio Configuration
# Added by $SCRIPT_NAME on $(date)
$USER_NAME soft rtprio 75
$USER_NAME hard rtprio 75
$USER_NAME soft memlock 131072
$USER_NAME hard memlock 131072
$USER_NAME soft nice -10
$USER_NAME hard nice -10
EOF

echo -e "${GREEN}✅ Real-time limits configured for $USER_NAME${NC}"

echo -e "\n${BLUE}=== Step 3: SystemD Configuration ===${NC}"

# Backup and configure systemd for real-time
backup_file "/etc/systemd/system.conf"

# Add real-time settings to systemd
if ! grep -q "DefaultLimitRTPRIO" /etc/systemd/system.conf; then
    cat >> /etc/systemd/system.conf << EOF

# Sp3ctra Real-time Audio Configuration
# Added by $SCRIPT_NAME on $(date)
DefaultLimitRTPRIO=75
DefaultLimitMEMLOCK=131072
EOF
    echo -e "${GREEN}✅ SystemD real-time limits configured${NC}"
else
    echo -e "${YELLOW}⚠️  SystemD real-time limits already configured${NC}"
fi

echo -e "\n${BLUE}=== Step 4: Kernel Real-time Optimizations ===${NC}"

# Backup and configure sysctl for audio
backup_file "/etc/sysctl.conf"

# Add kernel optimizations for real-time audio
if ! grep -q "vm.swappiness" /etc/sysctl.conf; then
    cat >> /etc/sysctl.conf << EOF

# Sp3ctra Real-time Audio Kernel Optimizations  
# Added by $SCRIPT_NAME on $(date)
vm.swappiness=10
kernel.sched_rt_runtime_us=950000
kernel.sched_rt_period_us=1000000
EOF
    echo -e "${GREEN}✅ Kernel real-time optimizations configured${NC}"
else
    echo -e "${YELLOW}⚠️  Kernel optimizations already configured${NC}"
fi

echo -e "\n${BLUE}=== Step 5: Audio Test Script Creation ===${NC}"

# Create audio validation script
cat > /usr/local/bin/sp3ctra-audio-test << 'EOF'
#!/bin/bash
# sp3ctra-audio-test - Validate real-time audio configuration

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Sp3ctra Audio Configuration Test ===${NC}"

# Test 1: User groups
echo -n "User groups: "
if groups $USER | grep -q audio; then
    echo -e "${GREEN}✅ audio group OK${NC}"
else
    echo -e "${RED}❌ Missing audio group${NC}"
fi

# Test 2: RT Priority limits
echo -n "RT Priority limit: "
RT_LIMIT=$(ulimit -r)
if [ "$RT_LIMIT" -ge 70 ]; then
    echo -e "${GREEN}✅ $RT_LIMIT (sufficient)${NC}"
else
    echo -e "${RED}❌ $RT_LIMIT (insufficient, need ≥70)${NC}"
fi

# Test 3: Memory lock limits  
echo -n "Memory lock limit: "
MEM_LIMIT=$(ulimit -l)
if [ "$MEM_LIMIT" -ge 131072 ]; then
    echo -e "${GREEN}✅ ${MEM_LIMIT}KB (sufficient)${NC}"
else
    echo -e "${RED}❌ ${MEM_LIMIT}KB (insufficient, need ≥131072KB)${NC}"
fi

# Test 4: Nice limits
echo -n "Nice limit: "  
NICE_LIMIT=$(ulimit -e)
if [ "$NICE_LIMIT" -le 0 ] 2>/dev/null || [ "$NICE_LIMIT" = "unlimited" ]; then
    echo -e "${GREEN}✅ Priority scheduling available${NC}"
else
    echo -e "${YELLOW}⚠️  Limited priority scheduling${NC}"
fi

# Test 5: Practical RT test
echo -n "RT Scheduling test: "
if chrt -f 70 true 2>/dev/null; then
    echo -e "${GREEN}✅ FIFO scheduling works${NC}"
else
    echo -e "${RED}❌ FIFO scheduling failed${NC}"
fi

echo -e "\n${YELLOW}USB SPDIF Device Check:${NC}"
aplay -l | grep -i spdif || echo -e "${RED}❌ No SPDIF device found${NC}"

echo -e "\nTest complete. Run 'sudo reboot' if any tests failed."
EOF

chmod +x /usr/local/bin/sp3ctra-audio-test
echo -e "${GREEN}✅ Audio test script created: /usr/local/bin/sp3ctra-audio-test${NC}"

echo -e "\n${BLUE}=== Configuration Complete ===${NC}"
echo -e "${GREEN}✅ Real-time audio configuration applied${NC}"
echo -e "${YELLOW}📁 Backups saved in: $BACKUP_DIR${NC}"
echo -e "\n${YELLOW}🔄 IMPORTANT: Reboot required to apply all changes!${NC}"
echo -e "After reboot, run: ${BLUE}sp3ctra-audio-test${NC}"
echo -e "\n${BLUE}===========================================${NC}"
