#!/bin/bash
################################################################################
# Sp3ctra Synthesis Engine Renaming Script
# 
# Purpose: Rename synthesis engines to unified "Lux" nomenclature
#   - polyphonic → luxsynth (LuxSynth)
#   - additive   → luxstral (LuxStral) 
#   - photowave  → luxwave  (LuxWave)
#
# Author: Cline AI Assistant
# Date: 2025-11-26
################################################################################

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DRY_RUN=1  # Default to dry-run mode

# Statistics
FILES_RENAMED=0
FILES_MODIFIED=0
TOTAL_REPLACEMENTS=0

################################################################################
# Helper Functions
################################################################################

print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

################################################################################
# Pre-flight Checks
################################################################################

check_prerequisites() {
    print_header "Pre-flight Checks"
    
    # Check if we're in the right directory
    if [ ! -f "$PROJECT_ROOT/sp3ctra.ini" ]; then
        print_error "Not in Sp3ctra project root directory"
        exit 1
    fi
    print_success "Project root directory verified"
    
    # Check if git is available
    if ! command -v git &> /dev/null; then
        print_error "Git is not installed"
        exit 1
    fi
    print_success "Git is available"
    
    # Check for uncommitted changes
    cd "$PROJECT_ROOT"
    if [ -n "$(git status --porcelain)" ]; then
        print_warning "You have uncommitted changes"
        echo -e "  ${YELLOW}It's recommended to commit or stash them before proceeding${NC}"
        read -p "  Continue anyway? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "Aborted by user"
            exit 0
        fi
    else
        print_success "Working directory is clean"
    fi
    
    # Check if target directories already exist
    if [ -d "$PROJECT_ROOT/src/synthesis/luxsynth" ] || \
       [ -d "$PROJECT_ROOT/src/synthesis/luxstral" ] || \
       [ -d "$PROJECT_ROOT/src/synthesis/luxwave" ]; then
        print_error "Target directories already exist (luxsynth/luxstral/luxwave)"
        print_info "Please remove them first or this script has already been run"
        exit 1
    fi
    print_success "Target directories don't exist yet"
    
    echo
}

################################################################################
# Phase 1: Rename Directories
################################################################################

rename_directories() {
    print_header "Phase 1: Renaming Directories"
    
    cd "$PROJECT_ROOT"
    
    # Rename synthesis directories
    local dirs=(
        "src/synthesis/polyphonic:src/synthesis/luxsynth"
        "src/synthesis/additive:src/synthesis/luxstral"
        "src/synthesis/photowave:src/synthesis/luxwave"
    )
    
    for dir_pair in "${dirs[@]}"; do
        IFS=':' read -r old_dir new_dir <<< "$dir_pair"
        
        if [ -d "$old_dir" ]; then
            if [ $DRY_RUN -eq 1 ]; then
                print_info "[DRY-RUN] Would rename: $old_dir → $new_dir"
            else
                git mv "$old_dir" "$new_dir"
                print_success "Renamed: $old_dir → $new_dir"
                ((FILES_RENAMED++))
            fi
        else
            print_warning "Directory not found: $old_dir"
        fi
    done
    
    echo
}

################################################################################
# Phase 2: Rename Files
################################################################################

rename_files() {
    print_header "Phase 2: Renaming Files"
    
    cd "$PROJECT_ROOT"
    
    # File renaming patterns (old_pattern:new_pattern)
    local patterns=(
        "synth_polyphonic:synth_luxsynth"
        "synth_additive:synth_luxstral"
        "synth_photowave:synth_luxwave"
        "config_synth_poly:config_synth_luxsynth"
        "config_synth_additive:config_synth_luxstral"
        "config_photowave:config_luxwave"
    )
    
    for pattern in "${patterns[@]}"; do
        IFS=':' read -r old_name new_name <<< "$pattern"
        
        # Find all files matching the pattern
        while IFS= read -r file; do
            if [ -f "$file" ]; then
                local dir=$(dirname "$file")
                local base=$(basename "$file")
                local new_base="${base//$old_name/$new_name}"
                local new_file="$dir/$new_base"
                
                if [ "$file" != "$new_file" ]; then
                    if [ $DRY_RUN -eq 1 ]; then
                        print_info "[DRY-RUN] Would rename: $file → $new_file"
                    else
                        git mv "$file" "$new_file"
                        print_success "Renamed: $(basename $file) → $(basename $new_file)"
                        ((FILES_RENAMED++))
                    fi
                fi
            fi
        done < <(find src -type f -name "*${old_name}*" 2>/dev/null || true)
    done
    
    echo
}

################################################################################
# Phase 3: Replace Content in Files
################################################################################

replace_in_file() {
    local file="$1"
    local replacements=0
    
    # Skip binary files and images
    if file "$file" | grep -q "text"; then
        # Apply replacements in order (most specific first)
        
        # 1. Directory paths
        replacements=$((replacements + $(sed -i.bak 's|synthesis/polyphonic/|synthesis/luxsynth/|g' "$file" 2>/dev/null && grep -c "synthesis/luxsynth/" "$file" || echo 0)))
        replacements=$((replacements + $(sed -i.bak 's|synthesis/additive/|synthesis/luxstral/|g' "$file" 2>/dev/null && grep -c "synthesis/luxstral/" "$file" || echo 0)))
        replacements=$((replacements + $(sed -i.bak 's|synthesis/photowave/|synthesis/luxwave/|g' "$file" 2>/dev/null && grep -c "synthesis/luxwave/" "$file" || echo 0)))
        
        # 2. File names
        sed -i.bak 's/synth_polyphonic/synth_luxsynth/g' "$file" 2>/dev/null || true
        sed -i.bak 's/synth_additive/synth_luxstral/g' "$file" 2>/dev/null || true
        sed -i.bak 's/synth_photowave/synth_luxwave/g' "$file" 2>/dev/null || true
        sed -i.bak 's/config_synth_poly/config_synth_luxsynth/g' "$file" 2>/dev/null || true
        sed -i.bak 's/config_photowave/config_luxwave/g' "$file" 2>/dev/null || true
        
        # 3. Function/variable identifiers (lowercase with underscore)
        sed -i.bak 's/_polyphonic/_luxsynth/g' "$file" 2>/dev/null || true
        sed -i.bak 's/_additive/_luxstral/g' "$file" 2>/dev/null || true
        sed -i.bak 's/_photowave/_luxwave/g' "$file" 2>/dev/null || true
        
        # 4. Type names (PascalCase)
        sed -i.bak 's/Polyphonic/LuxSynth/g' "$file" 2>/dev/null || true
        sed -i.bak 's/Additive/LuxStral/g' "$file" 2>/dev/null || true
        sed -i.bak 's/Photowave/LuxWave/g' "$file" 2>/dev/null || true
        
        # 5. Macros (UPPERCASE)
        sed -i.bak 's/POLYPHONIC/LUXSYNTH/g' "$file" 2>/dev/null || true
        sed -i.bak 's/ADDITIVE/LUXSTRAL/g' "$file" 2>/dev/null || true
        sed -i.bak 's/PHOTOWAVE/LUXWAVE/g' "$file" 2>/dev/null || true
        
        # 6. INI sections
        sed -i.bak 's/\[synth_polyphonic\]/[synth_luxsynth]/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\[synth_additive\]/[synth_luxstral]/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\[synth_photowave\]/[synth_luxwave]/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\[image_processing_polyphonic\]/[image_processing_luxsynth]/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\[image_processing_additive\]/[image_processing_luxstral]/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\[image_processing_photowave\]/[image_processing_luxwave]/g' "$file" 2>/dev/null || true
        
        # 7. Lowercase standalone words (in comments/docs)
        sed -i.bak 's/\bpolyphonic\b/luxsynth/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\badditive\b/luxstral/g' "$file" 2>/dev/null || true
        sed -i.bak 's/\bphotowave\b/luxwave/g' "$file" 2>/dev/null || true
        
        # Remove backup file
        rm -f "${file}.bak"
        
        # Count actual changes
        if [ -f "$file" ]; then
            local changes=$(grep -o "luxsynth\|luxstral\|luxwave\|LuxSynth\|LuxStral\|LuxWave\|LUXSYNTH\|LUXSTRAL\|LUXWAVE" "$file" 2>/dev/null | wc -l || echo 0)
            replacements=$changes
        fi
    fi
    
    echo $replacements
}

replace_content() {
    print_header "Phase 3: Replacing Content in Files"
    
    cd "$PROJECT_ROOT"
    
    # Find all text files (C/C++, headers, config, docs)
    local file_patterns=(
        "*.c"
        "*.h"
        "*.cpp"
        "*.hpp"
        "*.ini"
        "*.md"
        "Makefile"
    )
    
    local total_files=0
    for pattern in "${file_patterns[@]}"; do
        while IFS= read -r file; do
            ((total_files++))
        done < <(find . -type f -name "$pattern" ! -path "./.git/*" ! -path "./build/*" 2>/dev/null || true)
    done
    
    print_info "Processing $total_files files..."
    echo
    
    local processed=0
    for pattern in "${file_patterns[@]}"; do
        while IFS= read -r file; do
            ((processed++))
            local progress=$((processed * 100 / total_files))
            
            if [ $DRY_RUN -eq 1 ]; then
                # In dry-run, just check if file would be modified
                if grep -q "polyphonic\|additive\|photowave\|Polyphonic\|Additive\|Photowave\|POLYPHONIC\|ADDITIVE\|PHOTOWAVE" "$file" 2>/dev/null; then
                    print_info "[DRY-RUN] Would modify: $file"
                    ((FILES_MODIFIED++))
                fi
            else
                local replacements=$(replace_in_file "$file")
                if [ $replacements -gt 0 ]; then
                    print_success "Modified: $file ($replacements replacements)"
                    ((FILES_MODIFIED++))
                    TOTAL_REPLACEMENTS=$((TOTAL_REPLACEMENTS + replacements))
                fi
            fi
            
            # Progress indicator every 10%
            if [ $((processed % (total_files / 10 + 1))) -eq 0 ]; then
                echo -ne "\r  Progress: ${progress}%"
            fi
        done < <(find . -type f -name "$pattern" ! -path "./.git/*" ! -path "./build/*" 2>/dev/null || true)
    done
    
    echo -ne "\r  Progress: 100%\n"
    echo
}

################################################################################
# Phase 4: Verify Compilation
################################################################################

verify_compilation() {
    print_header "Phase 4: Compilation Verification"
    
    if [ $DRY_RUN -eq 1 ]; then
        print_info "[DRY-RUN] Skipping compilation test"
        return
    fi
    
    cd "$PROJECT_ROOT"
    
    print_info "Running: make clean"
    if make clean > /dev/null 2>&1; then
        print_success "Clean successful"
    else
        print_warning "Clean had warnings (non-critical)"
    fi
    
    print_info "Running: make (this may take a minute...)"
    if make > /tmp/sp3ctra_build.log 2>&1; then
        print_success "Compilation successful!"
    else
        print_error "Compilation failed!"
        print_info "Build log saved to: /tmp/sp3ctra_build.log"
        print_warning "You may need to fix compilation errors manually"
        return 1
    fi
    
    echo
}

################################################################################
# Final Report
################################################################################

print_report() {
    print_header "Renaming Complete - Summary"
    
    echo -e "${GREEN}Statistics:${NC}"
    echo "  • Directories/Files renamed: $FILES_RENAMED"
    echo "  • Files modified: $FILES_MODIFIED"
    if [ $DRY_RUN -eq 0 ]; then
        echo "  • Total replacements: $TOTAL_REPLACEMENTS"
    fi
    echo
    
    if [ $DRY_RUN -eq 1 ]; then
        echo -e "${YELLOW}This was a DRY-RUN. No changes were made.${NC}"
        echo
        echo "To apply changes, run:"
        echo "  $0 --apply"
    else
        echo -e "${GREEN}All changes have been applied!${NC}"
        echo
        echo "Next steps:"
        echo "  1. Review changes: git status"
        echo "  2. Test the application"
        echo "  3. Commit changes: git add -A && git commit -m 'refactor: rename synthesis engines to Lux nomenclature'"
    fi
    
    echo
}

################################################################################
# Main Execution
################################################################################

main() {
    # Parse arguments
    if [ "$1" == "--apply" ]; then
        DRY_RUN=0
        print_warning "APPLY MODE: Changes will be made to the repository"
        echo
    else
        print_info "DRY-RUN MODE: No changes will be made (use --apply to apply changes)"
        echo
    fi
    
    # Execute phases
    check_prerequisites
    rename_directories
    rename_files
    replace_content
    
    if [ $DRY_RUN -eq 0 ]; then
        verify_compilation
    fi
    
    print_report
}

# Run main function
main "$@"
