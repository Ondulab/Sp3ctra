#!/bin/bash

# Build script for RELEASE version of Sp3ctra on Raspberry Pi

# Stop on error
set -e

# Show executed commands
set -x

echo "Building Sp3ctra for Raspberry Pi (Release mode)..."
echo "This script requires the dependencies to be installed."
echo "Run scripts/deploy_raspberry/install_dependencies_raspberry.sh first if needed."

# Clean previous build files then compile the project in release mode
make clean

# Build with Raspberry Pi specific settings
make CC=gcc \
     CXX=g++ \
     CFLAGS="-O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DENABLE_IMAGE_DEBUG -Wno-deprecated-declarations -march=native -mtune=native" \
     CXXFLAGS="-std=c++17 -O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -Wno-unused-but-set-variable -Wno-deprecated-declarations -march=native -mtune=native" \
     INCLUDES="-I/usr/include -I/usr/local/include -Isrc/core -Isrc/config -Isrc/audio/rtaudio -Isrc/audio/buffers -Isrc/audio/effects -Isrc/audio/pan -Isrc/synthesis/additive -Isrc/synthesis/polyphonic -Isrc/synthesis/polyphonic/kissfft -Isrc/communication/network -Isrc/communication/midi -Isrc/communication/dmx -Isrc/display -Isrc/threading -Isrc/utils" \
     LIBS="-L/usr/lib -L/usr/local/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi -lsfml-graphics -lsfml-window -lsfml-system -lcsfml-graphics -lcsfml-window -lcsfml-system -lasound -lpthread -lm"

echo "Release build completed successfully!"
echo "Executable is located at build/Sp3ctra"
echo "You can now run it on your Raspberry Pi."
