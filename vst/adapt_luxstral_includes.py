#!/usr/bin/env python3
"""
Script to adapt includes in LuxStral source files for VST context
Replaces relative paths with vst_adapters.h where appropriate
"""

import os
import re

LUXSTRAL_DIR = "source/luxstral"

# Mapping of includes to remove/replace
INCLUDES_TO_REMOVE = [
    r'#include "config\.h"',
    r'#include "\.\.\/\.\.\/config\/config_instrument\.h"',
    r'#include "\.\.\/\.\.\/config\/config_loader\.h"',
    r'#include "\.\.\/\.\.\/config\/config_debug\.h"',
    r'#include "\.\.\/\.\.\/config\/config_synth_luxstral\.h"',
    r'#include "\.\.\/\.\.\/config\/config_audio\.h"',
    r'#include "\.\.\/\.\.\/core\/config\.h"',
    r'#include "\.\.\/\.\.\/core\/context\.h"',
    r'#include "\.\.\/\.\.\/core\/global\.h"',
    r'#include "\.\.\/\.\.\/utils\/logger\.h"',
    r'#include "\.\.\/\.\.\/utils\/error\.h"',
    r'#include "\.\.\/\.\.\/utils\/image_debug_stubs\.h"',
    r'#include "\.\.\/\.\.\/audio\/buffers\/doublebuffer\.h"',
    r'#include "\.\.\/\.\.\/audio\/buffers\/audio_image_buffers\.h"',
    r'#include "\.\.\/\.\.\/audio\/rtaudio\/audio_c_api\.h"',
    r'#include "\.\.\/\.\.\/audio\/pan\/lock_free_pan\.h"',
    r'#include "\.\.\/\.\.\/threading\/multithreading\.h"',
    r'#include "audio_c_api\.h"',
    r'#include "error\.h"',
    r'#include "lock_free_pan\.h"',
]

def adapt_file(filepath):
    """Adapt includes in a single file"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    original_content = content
    
    # Check if file already includes vst_adapters.h
    has_vst_adapters = '#include "vst_adapters.h"' in content
    
    # Remove includes that are handled by vst_adapters.h
    for pattern in INCLUDES_TO_REMOVE:
        content = re.sub(pattern + r'\s*\n', '', content)
    
    # Add vst_adapters.h at the top if not already present and if we removed something
    if not has_vst_adapters and content != original_content:
        # Find the position after initial comments and includes guards
        # Look for first real include or after header guard
        lines = content.split('\n')
        insert_pos = 0
        
        for i, line in enumerate(lines):
            if line.strip().startswith('#ifndef') or line.strip().startswith('#define'):
                insert_pos = i + 1
            elif line.strip().startswith('#include'):
                insert_pos = i
                break
        
        # Insert vst_adapters.h
        if insert_pos > 0:
            lines.insert(insert_pos, '#include "vst_adapters.h"')
            content = '\n'.join(lines)
    
    # Only write if content changed
    if content != original_content:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"✓ Adapted: {filepath}")
        return True
    else:
        print(f"  Skipped: {filepath} (no changes)")
        return False

def main():
    """Process all C/H files in luxstral directory"""
    changed_count = 0
    
    print("🔧 Adapting LuxStral includes for VST context...\n")
    
    for filename in os.listdir(LUXSTRAL_DIR):
        if filename.endswith(('.c', '.h')) and filename not in ['vst_adapters.c', 'vst_adapters.h', 
                                                                  'vst_adapters.cpp']:
            filepath = os.path.join(LUXSTRAL_DIR, filename)
            if adapt_file(filepath):
                changed_count += 1
    
    print(f"\n✅ Adapted {changed_count} files")

if __name__ == "__main__":
    main()
