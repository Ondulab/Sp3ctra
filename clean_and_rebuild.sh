#!/bin/bash

# Clean and rebuild script for CISYNTH_noGUI project
# Resolves macOS SDK version conflicts

echo "Cleaning qmake cache and build directory..."
rm -f .qmake.stash
rm -rf build_nogui/

echo "Cache cleaned, starting clean rebuild..."
./build_nogui.sh --cli

echo "Clean rebuild completed!"
