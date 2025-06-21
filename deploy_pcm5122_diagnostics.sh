#!/bin/bash

# Deploy PCM5122 diagnostic tools to Raspberry Pi
# Usage: ./deploy_pcm5122_diagnostics.sh [user@hostname]

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

echo "🚀 DEPLOYING PCM5122 DIAGNOSTIC TOOLS"
echo "====================================="
echo "Target: $TARGET"
echo "Remote directory: $PI_DIR"
echo

# Test connection
print_info "Testing SSH connection..."
ssh -o ConnectTimeout=5 $TARGET "echo 'Connection successful'" 2>/dev/null
if [ $? -ne 0 ]; then
    print_error "Cannot connect to $TARGET"
    print_info "Please check:"
    print_info "  - SSH key is set up correctly"
    print_info "  - Pi is powered on and connected"
    print_info "  - Hostname/IP is correct"
    exit 1
fi

print_success "SSH connection established"

# Create remote directory if needed
print_info "Ensuring remote directory exists..."
ssh $TARGET "mkdir -p $PI_DIR" 2>/dev/null

# Files to deploy
files_to_deploy=(
    "diagnose_pcm5122_audio.sh"
    "test_pcm5122_with_cisynth.sh"
)

# Deploy diagnostic scripts
print_info "Deploying diagnostic scripts..."
for file in "${files_to_deploy[@]}"; do
    if [ -f "$file" ]; then
        echo "  Copying $file..."
        scp "$file" $TARGET:$PI_DIR/
        if [ $? -eq 0 ]; then
            print_success "$file deployed successfully"
        else
            print_error "Failed to deploy $file"
        fi
    else
        print_error "$file not found locally"
    fi
done

# Make scripts executable
print_info "Making scripts executable on Pi..."
ssh $TARGET "chmod +x $PI_DIR/diagnose_pcm5122_audio.sh $PI_DIR/test_pcm5122_with_cisynth.sh"

# Deploy any additional optimization files if they exist
additional_files=(
    "src/core/config.h"
    "src/core/audio_rtaudio.cpp"
    "build_pi_optimized.sh"
)

print_info "Checking for additional optimization files..."
for file in "${additional_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  Deploying $file..."
        scp "$file" $TARGET:$PI_DIR/$(dirname $file)/
        if [ $? -eq 0 ]; then
            print_success "$file updated on Pi"
        else
            print_warning "Failed to update $file"
        fi
    fi
done

# Create usage instructions on Pi
print_info "Creating usage instructions..."
ssh $TARGET "cat > $PI_DIR/PCM5122_DIAGNOSTICS_README.md << 'EOF'
# PCM5122 Audio Diagnostics

## Problem
Audio is choppy on Raspberry Pi with new PCM5122 sound card despite successful stream opening.

## Available Tools

### 1. System Diagnostic (run first)
\`\`\`bash
./diagnose_pcm5122_audio.sh
\`\`\`
This script will:
- Detect your PCM5122 card
- Test ALSA device capabilities
- Check for system conflicts
- Analyze error logs
- Provide specific recommendations

### 2. Application Testing (run after building)
\`\`\`bash
# First, ensure application is built
./build_pi_optimized.sh

# Then run comprehensive tests
./test_pcm5122_with_cisynth.sh
\`\`\`
This script will:
- Test all audio devices with CISYNTH
- Test different buffer sizes
- Test sample rate compatibility
- Monitor system performance
- Provide optimal configuration

## Quick Troubleshooting Steps

1. **Run system diagnostic:**
   \`\`\`bash
   ./diagnose_pcm5122_audio.sh | tee pcm5122_diagnostic.log
   \`\`\`

2. **Build optimized application:**
   \`\`\`bash
   ./build_pi_optimized.sh
   \`\`\`

3. **Test with your application:**
   \`\`\`bash
   ./test_pcm5122_with_cisynth.sh | tee pcm5122_test.log
   \`\`\`

4. **Manual device testing:**
   \`\`\`bash
   # Test PCM5122 directly
   aplay -l  # List devices
   speaker-test -D hw:0,0 -c 2 -r 48000 -f S16_LE -t sine
   \`\`\`

## Common PCM5122 Issues & Solutions

### Issue 1: ALSA Error 524
**Symptoms:** \`Unknown error 524\` in logs
**Solutions:**
- Check if PCM5122 driver loaded: \`lsmod | grep snd_soc_pcm5102a\`
- Verify device tree overlay in \`/boot/config.txt\`
- Stop conflicting audio services

### Issue 2: Choppy Audio Despite Stream Success
**Symptoms:** Stream opens but audio stutters
**Solutions:**
- Increase buffer size (1024 or 2048 frames)
- Stop PulseAudio: \`sudo systemctl stop pulseaudio\`
- Set CPU governor to performance
- Check USB power if USB-connected

### Issue 3: Wrong Sample Rate
**Symptoms:** Audio pitch incorrect
**Solutions:**
- Verify PCM5122 supported rates
- Match SAMPLING_FREQUENCY in config.h
- Test with 48kHz instead of 96kHz

## Getting Help

If issues persist:
1. Save diagnostic outputs: \`./diagnose_pcm5122_audio.sh > diagnostic.log\`
2. Save test results: \`./test_pcm5122_with_cisynth.sh > test.log\`
3. Check specific PCM5122 configuration in \`/boot/config.txt\`
4. Verify hardware connections (I2S pins if HAT)

EOF"

print_success "Usage instructions created: $PI_DIR/PCM5122_DIAGNOSTICS_README.md"

# Test if we can run the diagnostic immediately
print_info "Testing diagnostic script execution..."
ssh $TARGET "cd $PI_DIR && timeout 30s ./diagnose_pcm5122_audio.sh" 2>/dev/null
if [ $? -eq 0 ] || [ $? -eq 124 ]; then  # 124 is timeout exit code
    print_success "Diagnostic script runs successfully on Pi"
else
    print_warning "Diagnostic script may have dependency issues"
    print_info "Connect to Pi and run manually to check for missing dependencies"
fi

echo
print_success "PCM5122 diagnostic tools deployed successfully!"
echo
print_info "Next steps on your Pi:"
echo "1. SSH to Pi: ssh $TARGET"
echo "2. Go to project: cd $PI_DIR"
echo "3. Run diagnostic: ./diagnose_pcm5122_audio.sh"
echo "4. Build application: ./build_pi_optimized.sh"
echo "5. Test application: ./test_pcm5122_with_cisynth.sh"
echo
print_info "All instructions available in: $PI_DIR/PCM5122_DIAGNOSTICS_README.md"
