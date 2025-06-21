#!/bin/bash

# Optimized build script for Raspberry Pi
# Maximizes performance with ARM-specific optimizations

echo "============================================="
echo "Optimized build for Raspberry Pi"
echo "============================================="

# Detect Raspberry Pi model and architecture for specific optimizations
PI_MODEL=""
ARCH=$(uname -m)

echo "Detected architecture: $ARCH"

if [ "$ARCH" = "aarch64" ]; then
    # ARM64 architecture
    if grep -q "Raspberry Pi 4" /proc/cpuinfo; then
        PI_MODEL="pi4_64"
        CPU_FLAGS="-mcpu=cortex-a72"
        echo "Detected: Raspberry Pi 4 (ARM64) - Using Cortex-A72 optimizations"
    elif grep -q "Raspberry Pi 3" /proc/cpuinfo; then
        PI_MODEL="pi3_64"
        CPU_FLAGS="-mcpu=cortex-a53"
        echo "Detected: Raspberry Pi 3 (ARM64) - Using Cortex-A53 optimizations"
    else
        PI_MODEL="generic_64"
        CPU_FLAGS="-march=armv8-a"
        echo "Generic ARM64 optimization flags"
    fi
else
    # ARM32 architecture
    if grep -q "Raspberry Pi 4" /proc/cpuinfo; then
        PI_MODEL="pi4_32"
        CPU_FLAGS="-mcpu=cortex-a72 -mfpu=neon-vfpv4 -mfloat-abi=hard"
        echo "Detected: Raspberry Pi 4 (ARM32) - Using Cortex-A72 optimizations"
    elif grep -q "Raspberry Pi 3" /proc/cpuinfo; then
        PI_MODEL="pi3_32"
        CPU_FLAGS="-mcpu=cortex-a53 -mfpu=neon-vfpv4 -mfloat-abi=hard"
        echo "Detected: Raspberry Pi 3 (ARM32) - Using Cortex-A53 optimizations"
    else
        PI_MODEL="generic_32"
        CPU_FLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=hard"
        echo "Generic ARM32 optimization flags"
    fi
fi

# Clean any existing build files
echo "Step 1: Cleaning existing build files..."
rm -rf build_nogui
rm -f Makefile
rm -f .qmake.stash

# Create fresh build directory
echo "Step 2: Creating fresh build directory..."
mkdir -p build_nogui

# Set aggressive compiler optimizations
export COMMON_FLAGS="-O3 -ffast-math -funroll-loops -fprefetch-loop-arrays -fomit-frame-pointer"
export ARM_FLAGS="$CPU_FLAGS"
export PERF_FLAGS="-fno-signed-zeros -fno-trapping-math -fassociative-math -ffinite-math-only"
export SIZE_FLAGS="-ffunction-sections -fdata-sections"
export LINK_FLAGS="-Wl,--gc-sections -Wl,-O1 -Wl,--as-needed"

# Add vectorization flags only if supported
if [ "$ARCH" = "aarch64" ]; then
    # ARM64 specific optimizations
    ARM_FLAGS="$ARM_FLAGS -ftree-vectorize"
else
    # ARM32 specific optimizations
    ARM_FLAGS="$ARM_FLAGS -ftree-vectorize"
fi

# Combine all optimization flags
export CFLAGS="$COMMON_FLAGS $ARM_FLAGS $PERF_FLAGS $SIZE_FLAGS"
export CXXFLAGS="$COMMON_FLAGS $ARM_FLAGS $PERF_FLAGS $SIZE_FLAGS"
export LDFLAGS="$LINK_FLAGS"

echo "Using optimization flags:"
echo "CFLAGS: $CFLAGS"
echo "CXXFLAGS: $CXXFLAGS"
echo "LDFLAGS: $LDFLAGS"

# Run qmake with optimized configuration
echo "Step 3: Running qmake with optimizations..."
qmake -o build_nogui/Makefile CISYNTH_noGUI.pro \
    CONFIG+=cli_mode \
    CONFIG+=release \
    "QMAKE_CFLAGS_RELEASE=$CFLAGS" \
    "QMAKE_CXXFLAGS_RELEASE=$CXXFLAGS" \
    "QMAKE_LFLAGS_RELEASE=$LDFLAGS" \
    QMAKE_STRIP=strip

# Build the project with optimal thread count
echo "Step 4: Building project with optimizations..."
cd build_nogui

# Use all available cores but cap at 4 for Pi stability
CORES=$(nproc)
if [ $CORES -gt 4 ]; then
    CORES=4
fi

echo "Using $CORES parallel jobs for compilation..."

make clean
make -j$CORES

cd ..

# Strip the binary to reduce size
if [ -f "build_nogui/CISYNTH_noGUI" ]; then
    echo "Step 5: Stripping binary..."
    strip build_nogui/CISYNTH_noGUI
    
    echo "✅ Build successful!"
    echo "Executable: build_nogui/CISYNTH_noGUI"
    echo "File info:"
    file build_nogui/CISYNTH_noGUI
    echo "Size: $(du -h build_nogui/CISYNTH_noGUI | cut -f1)"
    
    # Performance recommendations
    echo ""
    echo "🚀 Performance recommendations:"
    echo "1. Set CPU governor to performance: sudo cpufreq-set -g performance"
    echo "2. Increase GPU memory split: Add 'gpu_mem=128' to /boot/config.txt"
    echo "3. Overclock (if cooling adequate): Add 'arm_freq=1800' to /boot/config.txt"
    echo "4. Run with real-time priority: sudo nice -n -20 ./build_nogui/CISYNTH_noGUI"
    
else
    echo "❌ Build failed!"
    exit 1
fi

echo "============================================="
echo "Optimized build completed successfully!"
echo "============================================="
