#!/bin/bash

# Quick deployment for device routing fix
# Usage: ./deploy_device_fix.sh [user@hostname]

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
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

# Check if target is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 [user@hostname]"
    echo "Example: $0 sp3ctra@pi"
    exit 1
fi

TARGET=$1
PI_DIR="~/Sp3ctra_Application"

echo "🎯 DEPLOYING DEVICE ROUTING FIX"
echo "==============================="
echo "Target: $TARGET"
echo

print_warning "PROBLEM IDENTIFIED:"
echo "You used --audio-device 2, but BossDAC is device 3!"
echo "This is why audio is still choppy."
echo

# Deploy the fix script
print_info "Deploying device routing fix..."
scp fix_device_routing.sh $TARGET:$PI_DIR/
ssh $TARGET "chmod +x $PI_DIR/fix_device_routing.sh"

print_success "Device routing fix deployed!"

print_info "Running device routing fix on Pi..."
ssh -t $TARGET "cd $PI_DIR && ./fix_device_routing.sh"

echo
print_success "Device routing fix completed!"
echo
print_info "Next steps on Pi:"
echo "1. Test correct device: ./test_correct_devices.sh"
echo "2. Use correct device number for CISYNTH"
echo "3. Audio should now be smooth!"
