#!/bin/bash

# Script to kill all CISYNTH processes and clean audio resources
echo "🧹 CLEANING ALL CISYNTH PROCESSES"
echo "================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

# Show current CISYNTH processes
print_info "Current CISYNTH processes:"
ps aux | grep CISYNTH_noGUI | grep -v grep

echo

# Kill all CISYNTH processes
print_info "Killing all CISYNTH_noGUI processes..."
if pkill -f CISYNTH_noGUI; then
    print_success "CISYNTH processes terminated"
else
    print_warning "No CISYNTH processes found or already terminated"
fi

# Wait a bit for processes to clean up
sleep 2

# Force kill if any remain
print_info "Checking for remaining processes..."
remaining=$(ps aux | grep CISYNTH_noGUI | grep -v grep | wc -l)
if [ "$remaining" -gt 0 ]; then
    print_warning "Force killing remaining processes..."
    pkill -9 -f CISYNTH_noGUI
    sleep 1
    
    # Check again
    remaining=$(ps aux | grep CISYNTH_noGUI | grep -v grep | wc -l)
    if [ "$remaining" -gt 0 ]; then
        print_error "Some processes still running - manual intervention may be needed"
        ps aux | grep CISYNTH_noGUI | grep -v grep
    else
        print_success "All processes successfully terminated"
    fi
else
    print_success "No remaining processes found"
fi

# Clean up audio resources
print_info "Cleaning up audio resources..."

# Reset ALSA
if command -v alsa >/dev/null 2>&1; then
    print_info "Resetting ALSA..."
    sudo alsa force-reload 2>/dev/null || true
fi

# List audio processes that might be blocking
print_info "Current audio processes:"
ps aux | grep -E "(pulse|alsa|audio)" | grep -v grep | head -5

echo
print_success "🎯 Cleanup complete! Safe to restart CISYNTH now."
print_info "Recommended: Wait 5 seconds before restarting"
