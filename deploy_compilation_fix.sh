#!/bin/bash

# Deploy compilation fix for audio_rtaudio.cpp
# Usage: ./deploy_compilation_fix.sh [user@hostname]

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

echo "🔧 DEPLOYING COMPILATION FIX"
echo "============================"
echo "Target: $TARGET"
echo

print_error "COMPILATION ERROR TO FIX:"
echo "foundSpecificPreferred variable scope error"
echo "Lines 438 and 457 in audio_rtaudio.cpp"
echo

print_info "This fix will:"
echo "  ✅ Restore original audio_rtaudio.cpp from backup"
echo "  ✅ Add BossDAC detection in correct variable scope"
echo "  ✅ Test compilation to ensure it works"
echo "  ✅ Create test script for verification"
echo

# Deploy the fix script
print_info "Deploying compilation fix..."
scp fix_compilation_error.sh $TARGET:$PI_DIR/
ssh $TARGET "chmod +x $PI_DIR/fix_compilation_error.sh"

print_success "Compilation fix deployed!"

print_info "Running compilation fix on Pi..."
ssh -t $TARGET "cd $PI_DIR && ./fix_compilation_error.sh"

echo
print_info "Testing compilation on Pi..."
ssh -t $TARGET "cd $PI_DIR && ./test_compilation.sh"

echo
print_success "Compilation fix completed!"
echo
print_info "Next steps on Pi:"
echo "1. If compilation succeeded: ./build_nogui/CISYNTH_noGUI"
echo "2. BossDAC should be auto-detected and forced"
echo "3. Audio should now be smooth (no choppy audio)"

print_warning "Note: The fix includes automatic BossDAC detection"
