#!/bin/bash

# Deploy final fix for audio device argument handling
# Usage: ./deploy_final_fix.sh [user@hostname]

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

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Check if target is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 [user@hostname]"
    echo "Example: $0 sp3ctra@pi"
    exit 1
fi

TARGET=$1
PI_DIR="~/Sp3ctra_Application"

echo "🎯 DEPLOYING FINAL AUDIO DEVICE FIX"
echo "==================================="
echo "Target: $TARGET"
echo

print_error "CRITICAL BUG IDENTIFIED:"
echo "Application completely ignores --audio-device parameter!"
echo "It always uses device 0 regardless of what you specify."
echo

print_info "This fix will:"
echo "  ✅ Force BossDAC usage by modifying the code"
echo "  ✅ Bypass the broken command-line argument handling"
echo "  ✅ Automatically detect and use BossDAC device"
echo

# Deploy the fix script
print_info "Deploying audio device argument fix..."
scp fix_audio_device_argument.sh $TARGET:$PI_DIR/
ssh $TARGET "chmod +x $PI_DIR/fix_audio_device_argument.sh"

print_success "Final fix deployed!"

print_info "Running audio device argument fix on Pi..."
ssh -t $TARGET "cd $PI_DIR && ./fix_audio_device_argument.sh"

echo
print_success "Audio device argument fix completed!"
echo
print_info "Next steps on Pi:"
echo "1. Test the fix: ./test_fixed_device.sh"
echo "2. Or run directly: ./build_nogui/CISYNTH_noGUI"
echo "3. BossDAC will be forced automatically - audio should be smooth!"

print_warning "Note: --audio-device parameter is now ignored (BossDAC forced)"
