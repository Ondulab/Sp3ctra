#!/bin/bash

# Quick deployment for ALSA configuration fix
# Usage: ./deploy_alsa_fix.sh [user@hostname]

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

# Check if target is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 [user@hostname]"
    echo "Example: $0 sp3ctra@pi"
    exit 1
fi

TARGET=$1
PI_DIR="~/Sp3ctra_Application"

echo "🚀 DEPLOYING ALSA CONFIGURATION FIX"
echo "==================================="
echo "Target: $TARGET"
echo

# Deploy the fix script
print_info "Deploying ALSA config fix..."
scp fix_alsa_config.sh $TARGET:$PI_DIR/
ssh $TARGET "chmod +x $PI_DIR/fix_alsa_config.sh"

print_success "ALSA fix deployed!"

print_info "Running ALSA configuration fix on Pi..."
ssh -t $TARGET "cd $PI_DIR && ./fix_alsa_config.sh"

echo
print_info "Next steps on Pi:"
echo "1. Test with audible frequencies: ./test_bossdac_corrected.sh" 
echo "2. If clear audio, rebuild: ./build_pi_optimized.sh"
echo "3. Test CISYNTH: ./build_nogui/CISYNTH_noGUI --audio-device 2"

print_success "ALSA configuration fix completed!"
