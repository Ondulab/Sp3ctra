#!/bin/bash
################################################################################
# Sp3ctra Content Replacement Script for Lux Nomenclature
# 
# Purpose: Replace all content references from old names to new Lux names
# This script completes Phase 3 of the renaming process
################################################################################

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_success() { echo -e "${GREEN}✓${NC} $1"; }
print_error() { echo -e "${RED}✗${NC} $1"; }
print_info() { echo -e "${BLUE}ℹ${NC} $1"; }

cd "$PROJECT_ROOT"

print_header "Applying Lux Content Changes"

# Find all text files
FILES=$(find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.ini" -o -name "*.md" -o -name "Makefile" \) ! -path "./.git/*" ! -path "./build/*" ! -path "./scripts/refactor/rename_to_lux.sh")

TOTAL=$(echo "$FILES" | wc -l | tr -d ' ')
COUNT=0
MODIFIED=0

print_info "Processing $TOTAL files..."
echo

for file in $FILES; do
    COUNT=$((COUNT + 1))
    
    # Skip if file doesn't exist
    [ ! -f "$file" ] && continue
    
    # Create backup
    cp "$file" "${file}.bak"
    
    # Apply all replacements using sed
    sed -i '' \
        -e 's|synthesis/polyphonic/|synthesis/luxsynth/|g' \
        -e 's|synthesis/additive/|synthesis/luxstral/|g' \
        -e 's|synthesis/photowave/|synthesis/luxwave/|g' \
        -e 's/synth_polyphonic/synth_luxsynth/g' \
        -e 's/synth_additive/synth_luxstral/g' \
        -e 's/synth_photowave/synth_luxwave/g' \
        -e 's/config_synth_poly/config_synth_luxsynth/g' \
        -e 's/config_synth_additive/config_synth_luxstral/g' \
        -e 's/config_photowave/config_luxwave/g' \
        -e 's/_polyphonic/_luxsynth/g' \
        -e 's/_additive/_luxstral/g' \
        -e 's/_photowave/_luxwave/g' \
        -e 's/Polyphonic/LuxSynth/g' \
        -e 's/Additive/LuxStral/g' \
        -e 's/Photowave/LuxWave/g' \
        -e 's/POLYPHONIC/LUXSYNTH/g' \
        -e 's/ADDITIVE/LUXSTRAL/g' \
        -e 's/PHOTOWAVE/LUXWAVE/g' \
        -e 's/\[synth_polyphonic\]/[synth_luxsynth]/g' \
        -e 's/\[synth_additive\]/[synth_luxstral]/g' \
        -e 's/\[synth_photowave\]/[synth_luxwave]/g' \
        -e 's/\[image_processing_polyphonic\]/[image_processing_luxsynth]/g' \
        -e 's/\[image_processing_additive\]/[image_processing_luxstral]/g' \
        -e 's/\[image_processing_photowave\]/[image_processing_luxwave]/g' \
        -e 's/\bpolyphonic\b/luxsynth/g' \
        -e 's/\badditive\b/luxstral/g' \
        -e 's/\bphotowave\b/luxwave/g' \
        "$file" 2>/dev/null || true
    
    # Check if file was modified
    if ! diff -q "$file" "${file}.bak" > /dev/null 2>&1; then
        MODIFIED=$((MODIFIED + 1))
        print_success "Modified: $file"
    fi
    
    # Remove backup
    rm -f "${file}.bak"
    
    # Progress indicator
    if [ $((COUNT % 10)) -eq 0 ]; then
        PERCENT=$((COUNT * 100 / TOTAL))
        echo -ne "\r  Progress: ${PERCENT}%"
    fi
done

echo -ne "\r  Progress: 100%\n"
echo

print_header "Summary"
print_success "Processed $TOTAL files"
print_success "Modified $MODIFIED files"
echo

print_info "Next step: Run 'make clean && make' to verify compilation"
