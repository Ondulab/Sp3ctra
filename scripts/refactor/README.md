# Sp3ctra Refactoring Scripts

This directory contains scripts for major refactoring operations on the Sp3ctra codebase.

## rename_to_lux.sh

**Purpose:** Rename all synthesis engines to unified "Lux" nomenclature.

### Renaming Scheme

| Old Name | New Name | Description |
|----------|----------|-------------|
| `polyphonic` | `luxsynth` | LuxSynth FFT-based synthesis → **LuxSynth** |
| `additive` | `luxstral` | LuxStral spectral synthesis → **LuxStral** |
| `photowave` | `luxwave` | Photo-to-waveform synthesis → **LuxWave** |

### What Gets Renamed

1. **Directory Structure**
   - `src/synthesis/luxsynth/` → `src/synthesis/luxsynth/`
   - `src/synthesis/luxstral/` → `src/synthesis/luxstral/`
   - `src/synthesis/luxwave/` → `src/synthesis/luxwave/`

2. **File Names**
   - `synth_luxsynth.*` → `synth_luxsynth.*`
   - `synth_luxstral.*` → `synth_luxstral.*`
   - `synth_luxwave.*` → `synth_luxwave.*`
   - `config_synth_luxsynth.h` → `config_synth_luxsynth.h`
   - `config_luxwave.h` → `config_luxwave.h`

3. **Code Identifiers**
   - Functions: `synth_luxsynth_*()` → `synth_luxsynth_*()`
   - Variables: `g_luxsynth_*` → `g_luxsynth_*`
   - Types: `LuxSynthVoice` → `LuxSynthVoice`
   - Macros: `MAX_POLY_VOICES` → `MAX_LUXSYNTH_VOICES`

4. **Configuration (sp3ctra.ini)**
   - `[synth_luxsynth]` → `[synth_luxsynth]`
   - `[synth_luxstral]` → `[synth_luxstral]`
   - `[synth_luxwave]` → `[synth_luxwave]`
   - `[image_processing_*]` sections updated accordingly

5. **Documentation**
   - All `.md` files in `docs/` directory
   - Comments in source code
   - README files

### Usage

#### Dry-Run Mode (Default - Safe)

Preview what would be changed without modifying anything:

```bash
cd /path/to/Sp3ctra_Application_2nd
./scripts/refactor/rename_to_lux.sh
```

This will show you:
- Which directories would be renamed
- Which files would be renamed
- Which files would have content modified
- Estimated number of changes

#### Apply Mode (Makes Changes)

Once you've reviewed the dry-run output and are ready to proceed:

```bash
./scripts/refactor/rename_to_lux.sh --apply
```

This will:
1. Rename all directories (using `git mv` to preserve history)
2. Rename all files (using `git mv`)
3. Replace content in all source files
4. Attempt to compile the project
5. Show a summary report

### Safety Features

- ✅ **Dry-run by default** - No changes unless you explicitly use `--apply`
- ✅ **Git integration** - Uses `git mv` to preserve file history
- ✅ **Pre-flight checks** - Verifies project structure and Git status
- ✅ **Uncommitted changes warning** - Alerts if you have uncommitted work
- ✅ **Compilation verification** - Tests build after changes (in apply mode)
- ✅ **Detailed logging** - Shows exactly what's being changed
- ✅ **Progress indicators** - Visual feedback during long operations

### Prerequisites

- Git must be installed and repository initialized
- Bash shell (macOS/Linux)
- Standard Unix tools: `sed`, `grep`, `find`
- Make build system configured

### Expected Output

#### Dry-Run Mode
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Pre-flight Checks
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
✓ Project root directory verified
✓ Git is available
✓ Working directory is clean
✓ Target directories don't exist yet

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Phase 1: Renaming Directories
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
ℹ [DRY-RUN] Would rename: src/synthesis/polyphonic → src/synthesis/luxsynth
ℹ [DRY-RUN] Would rename: src/synthesis/additive → src/synthesis/luxstral
ℹ [DRY-RUN] Would rename: src/synthesis/photowave → src/synthesis/luxwave

... (more output) ...

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Renaming Complete - Summary
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Statistics:
  • Directories/Files renamed: 45
  • Files modified: 127

This was a DRY-RUN. No changes were made.

To apply changes, run:
  ./scripts/refactor/rename_to_lux.sh --apply
```

#### Apply Mode
```
... (similar output) ...

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Phase 4: Compilation Verification
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
ℹ Running: make clean
✓ Clean successful
ℹ Running: make (this may take a minute...)
✓ Compilation successful!

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Renaming Complete - Summary
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Statistics:
  • Directories/Files renamed: 45
  • Files modified: 127
  • Total replacements: 1847

All changes have been applied!

Next steps:
  1. Review changes: git status
  2. Test the application
  3. Commit changes: git add -A && git commit -m 'refactor: rename synthesis engines to Lux nomenclature'
```

### Troubleshooting

#### "Target directories already exist"
The script has already been run or directories were manually created. Remove them first:
```bash
rm -rf src/synthesis/luxsynth src/synthesis/luxstral src/synthesis/luxwave
```

#### "Compilation failed"
Check the build log at `/tmp/sp3ctra_build.log` for details. Common issues:
- Missing includes after file renames
- Typos in manual edits
- Configuration mismatches

You can fix issues manually and re-run `make` to verify.

#### "You have uncommitted changes"
Commit or stash your changes before running the script:
```bash
git stash
./scripts/refactor/rename_to_lux.sh --apply
git stash pop  # After verifying changes
```

### Rollback

If you need to undo changes after running in apply mode:

```bash
# If you haven't committed yet
git reset --hard HEAD

# If you've committed but want to undo
git revert HEAD
```

### Post-Refactoring Checklist

After running the script successfully:

- [ ] Review changes: `git status` and `git diff`
- [ ] Test compilation: `make clean && make`
- [ ] Test each synthesis mode:
  - [ ] LuxStral (additive) - MIDI CC controls
  - [ ] LuxSynth (polyphonic) - MIDI note on/off
  - [ ] LuxWave (photowave) - MIDI note on/off
- [ ] Verify configuration loading from `sp3ctra.ini`
- [ ] Check MIDI mappings still work
- [ ] Test audio output quality
- [ ] Commit changes with descriptive message

### Commit Message Template

```
refactor: rename synthesis engines to Lux nomenclature

- polyphonic → luxsynth (LuxSynth)
- additive → luxstral (LuxStral)
- photowave → luxwave (LuxWave)

This unifies the naming scheme across all synthesis engines with
a consistent "Lux" (light) prefix, reflecting the project's core
concept of transforming light/images into sound.

Changes:
- Renamed 3 synthesis directories
- Renamed 45+ source files
- Updated 127 files with ~1850 identifier replacements
- Updated configuration sections in sp3ctra.ini
- Updated all documentation

Verified:
- Compilation successful
- All synthesis modes functional
- MIDI control working
- Configuration loading correct
```

## Notes

- This script is designed to be run **once** during the refactoring process
- It preserves Git history by using `git mv` instead of regular `mv`
- The script is idempotent-safe: it checks if target directories exist before proceeding
- All changes are atomic: either all succeed or none are applied (in apply mode)

## Author

Created by Cline AI Assistant on 2025-11-26 for the Sp3ctra project refactoring.
