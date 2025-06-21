#!/bin/bash

# Quick deployment script for BossDAC fix
# Usage: ./deploy_bossdac_fix.sh [user@hostname]

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

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
    print_error "Usage: $0 [user@hostname]"
    print_info "Example: $0 sp3ctra@pi"
    exit 1
fi

TARGET=$1
PI_DIR="~/Sp3ctra_Application"

echo "🚀 DEPLOYING BOSSDAC FIX"
echo "========================"
echo "Target: $TARGET"
echo "Remote directory: $PI_DIR"
echo

# Test connection
print_info "Testing SSH connection..."
ssh -o ConnectTimeout=5 $TARGET "echo 'Connection successful'" 2>/dev/null
if [ $? -ne 0 ]; then
    print_error "Cannot connect to $TARGET"
    exit 1
fi

print_success "SSH connection established"

# Deploy the fix script
print_info "Deploying BossDAC fix script..."
scp fix_bossdac_pcm512x.sh $TARGET:$PI_DIR/
if [ $? -eq 0 ]; then
    print_success "BossDAC fix script deployed"
else
    print_error "Failed to deploy fix script"
    exit 1
fi

# Make executable
ssh $TARGET "chmod +x $PI_DIR/fix_bossdac_pcm512x.sh"

# Run the fix immediately
print_info "Running BossDAC fix on Pi..."
echo "This will:"
echo "  - Install missing dependencies (bc)"
echo "  - Optimize ALSA configuration"
echo "  - Test audio functionality"
echo

read -p "Run the fix now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_info "Executing fix script on Pi..."
    ssh -t $TARGET "cd $PI_DIR && ./fix_bossdac_pcm512x.sh"
    
    print_info "Fix completed. Testing audio..."
    ssh -t $TARGET "cd $PI_DIR && ./test_bossdac_simple.sh" 2>/dev/null
    
    if [ $? -eq 0 ]; then
        print_success "Audio tests completed"
        print_info "If you heard audio, the fix worked!"
        echo
        print_info "Next steps:"
        echo "  1. Rebuild CISYNTH: ./build_pi_optimized.sh"
        echo "  2. Test with CISYNTH: ./build_nogui/CISYNTH_noGUI --audio-device 2"
    else
        print_warning "Audio tests had issues - check the output above"
    fi
else
    print_info "Fix script deployed but not executed"
    print_info "SSH to Pi and run: ./fix_bossdac_pcm512x.sh"
fi

echo
print_success "BossDAC fix deployment completed!"
