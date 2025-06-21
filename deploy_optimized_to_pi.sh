#!/bin/bash

# Optimized deployment script for Raspberry Pi
# Deploys the 48kHz configuration and uses optimized compilation

# Configuration
PI_USER="sp3ctra"
PI_HOST="pi.local"
PI_PATH="/home/sp3ctra/CISYNTH_MIDI"

echo "============================================="
echo "Deploying optimized build to Raspberry Pi"
echo "============================================="

# Check if Pi is reachable
echo "Step 1: Checking Pi connectivity..."
if ! ping -c 1 $PI_HOST > /dev/null 2>&1; then
    echo "❌ Cannot reach $PI_HOST. Please ensure:"
    echo "   - Raspberry Pi is powered on"
    echo "   - Network connection is active"
    echo "   - SSH is enabled on the Pi"
    exit 1
fi
echo "✅ Pi is reachable"

# Sync source code and build scripts
echo "Step 2: Syncing source code to Pi..."
rsync -avz --delete --exclude='build_nogui/' --exclude='.git/' \
      ./ $PI_USER@$PI_HOST:$PI_PATH/

if [ $? -ne 0 ]; then
    echo "❌ Failed to sync files to Pi"
    exit 1
fi
echo "✅ Files synced successfully"

# Build on Pi with optimizations
echo "Step 3: Building optimized version on Pi..."
ssh $PI_USER@$PI_HOST "cd $PI_PATH && ./build_pi_optimized.sh"

if [ $? -ne 0 ]; then
    echo "❌ Build failed on Pi"
    exit 1
fi
echo "✅ Build completed successfully"

# Test the build
echo "Step 4: Testing the build..."
ssh $PI_USER@$PI_HOST "cd $PI_PATH && timeout 5s ./build_nogui/CISYNTH_noGUI --test 2>&1 | head -20"

# Performance tuning recommendations
echo ""
echo "🚀 Performance tuning commands for Pi:"
echo "Run these on the Pi for optimal performance:"
echo ""
echo "# Set CPU governor to performance mode"
echo "sudo cpufreq-set -g performance"
echo ""
echo "# Increase audio buffer priority"
echo "echo '@audio - rtprio 99' | sudo tee -a /etc/security/limits.conf"
echo "echo '@audio - memlock unlimited' | sudo tee -a /etc/security/limits.conf"
echo ""
echo "# Disable unnecessary services"
echo "sudo systemctl disable bluetooth.service"
echo "sudo systemctl disable wpa_supplicant.service"
echo ""
echo "# Add to /boot/config.txt for better performance:"
echo "gpu_mem=128"
echo "arm_freq=1800  # Only if you have adequate cooling"
echo "core_freq=500"
echo "sdram_freq=500"
echo "over_voltage=2  # Only with arm_freq overclock"
echo ""

echo "============================================="
echo "Deployment completed!"
echo "============================================="
echo ""
echo "Changes made:"
echo "✅ Sample rate reduced from 96kHz to 48kHz"
echo "✅ Audio buffer size reduced from 1024 to 512 samples"
echo "✅ ARM-specific compiler optimizations enabled"
echo "✅ Vectorization with NEON instructions"
echo "✅ Aggressive math optimizations (-ffast-math)"
echo "✅ Function inlining and loop unrolling"
echo ""
echo "To run the optimized version:"
echo "ssh $PI_USER@$PI_HOST 'cd $PI_PATH && sudo nice -n -20 ./build_nogui/CISYNTH_noGUI'"
