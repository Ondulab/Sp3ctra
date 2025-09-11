#!/bin/bash

# Quick deployment script for audio optimizations to Raspberry Pi
# Usage: ./deploy_audio_fix_to_pi.sh [pi_username@pi_ip]

echo "🚀 Deploying Audio Optimizations to Raspberry Pi"
echo "================================================"
echo ""

# Default Pi connection (modify as needed)
PI_HOST=${1:-"sp3ctra@pi"}
PROJECT_DIR="~/Sp3ctra_Application"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Target: $PI_HOST${NC}"
echo -e "${BLUE}Remote dir: $PROJECT_DIR${NC}"
echo ""

# Check if we can reach the Pi
echo -e "${YELLOW}Step 1: Testing connection to Pi...${NC}"
if ! ssh -o ConnectTimeout=10 "$PI_HOST" "echo 'Connection successful'" 2>/dev/null; then
    echo -e "${RED}❌ Cannot connect to $PI_HOST${NC}"
    echo "Make sure:"
    echo "  - Pi is powered on and connected to network"
    echo "  - SSH is enabled on Pi"
    echo "  - Correct username/IP provided"
    exit 1
fi
echo -e "${GREEN}✅ Connection successful${NC}"
echo ""

# Deploy the optimized files
echo -e "${YELLOW}Step 2: Deploying optimized files...${NC}"

# List of files to deploy
FILES_TO_DEPLOY=(
    "src/core/config.h"
    "src/core/audio_rtaudio.cpp"
    "test_audio_optimization_pi.sh"
)

for file in "${FILES_TO_DEPLOY[@]}"; do
    echo "  📤 Deploying $file..."
    if scp "$file" "$PI_HOST:$PROJECT_DIR/$file" 2>/dev/null; then
        echo -e "    ${GREEN}✅ $file deployed${NC}"
    else
        echo -e "    ${RED}❌ Failed to deploy $file${NC}"
        exit 1
    fi
done
echo ""

# Make test script executable on Pi
echo -e "${YELLOW}Step 3: Setting up test script...${NC}"
ssh "$PI_HOST" "cd $PROJECT_DIR && chmod +x test_audio_optimization_pi.sh"
echo -e "${GREEN}✅ Test script ready${NC}"
echo ""

# Show what optimizations were applied
echo -e "${BLUE}Optimizations applied:${NC}"
echo "  🔧 AUDIO_BUFFER_SIZE: 512 → 1024 frames"
echo "  🔧 RtAudio buffers: 4 → 8 buffers"
echo "  🔧 Expected latency: ~5.33ms → ~10.67ms"
echo ""

# Offer to run the test immediately
echo -e "${YELLOW}Step 4: Ready to test!${NC}"
echo ""
echo -e "${BLUE}Commands to run on Pi:${NC}"
echo "  ssh $PI_HOST"
echo "  cd $PROJECT_DIR"
echo "  ./test_audio_optimization_pi.sh"
echo ""

read -p "Run test automatically now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Running test on Pi...${NC}"
    echo ""
    ssh -t "$PI_HOST" "cd $PROJECT_DIR && ./test_audio_optimization_pi.sh"
else
    echo -e "${GREEN}✅ Deployment complete! Run the test manually when ready.${NC}"
fi

echo ""
echo -e "${GREEN}🎉 Audio optimization deployment completed!${NC}"
echo ""
echo "If audio is still choppy after testing:"
echo "  1. Try AUDIO_BUFFER_SIZE = 2048"
echo "  2. Test different audio devices (1, 2, 5)"
echo "  3. Check system performance with 'htop'"
echo "  4. Consider disabling reverb/EQ temporarily"
