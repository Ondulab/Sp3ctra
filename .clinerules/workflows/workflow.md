# CLINE WORKFLOW — JUCE VST3 (RT-SAFE, UI-FOCUSED)

## 1) PURPOSE AND SCOPE

### Purpose
Provide a clear, safe, and repeatable workflow for **Cline** to plan and implement changes on a **JUCE VST3 plugin**, while strictly respecting:
- Real-time (RT) audio constraints
- JUCE / VST3 lifecycle rules
- Repository-specific coding rules (`custom_instructions.md`)

### Scope
- Applies to **all Cline-led work** in this JUCE VST repository.
- Covers:
  - DSP core
  - JUCE `AudioProcessor`
  - JUCE `AudioProcessorEditor` (UI)
- Excludes:
  - DMX
  - External hardware control
  - System services

### Separation of concerns
- **Rules** → how code must be written  
  (`custom_instructions.md`)
- **Workflow** → how Cline executes work  
  (this document)

### Roles
- **Cline**: analysis, planning, code preparation, documentation
- **Repository owner**: compilation, plugin validation in DAW, performance checks

---

## 2) OPERATING PRINCIPLES

- Always start in **Plan Mode**
- Switch to **Act Mode only after explicit approval**
- Prefer **small, atomic, reviewable changes**
- RT safety has absolute priority over UI or features
- Never modify files outside this repository
- Never infer undocumented JUCE behavior
- Compilation, DAW loading, and audio testing are **never performed by Cline**

---

## 3) PLAN MODE (ANALYSIS AND PLANNING)

### 3.1 Context gathering
Cline must:
- Read relevant files:
  - `README`
  - JUCE project files
  - `AudioProcessor` implementation
  - DSP core modules
  - `AudioProcessorEditor`
- Identify:
  - RT hot paths (`processBlock`, DSP inner loops)
  - JUCE lifecycle boundaries:
    - `prepareToPlay`
    - `processBlock`
    - `releaseResources`
  - UI ↔ DSP communication paths
  - Threading model (message thread vs audio thread)

---

### 3.2 Questions and assumptions
Cline must explicitly confirm:
- Target plugin formats (VST3 only, AU optional?)
- Target platforms (macOS only, Windows later?)
- Headless build expectations (plugin builds without UI tests)
- DSP testability without DAW (offline/unit tests)

No assumptions without confirmation.

---

### 3.3 Proposed plan
Plans **must be concrete and technical**:
- Exact files to add / modify / remove
- DSP vs JUCE vs UI responsibility split
- Parameter flow (`AudioProcessorValueTreeState`, atomics, smoothing)
- RT impact analysis
- Identified risks
- Rollback strategy

Avoid vague wording (“clean up”, “improve performance”).

---

### 3.4 Acceptance criteria (mandatory)
Every plan must include:

- [ ] Plugin builds successfully (headless where applicable)
- [ ] Strict RT rules respected:
  - no allocation
  - no locks
  - no logging
  - no exceptions in `processBlock`
- [ ] JUCE lifecycle respected
- [ ] `clang-format` (LLVM + Allman) applied
- [ ] English-only code (no French text)
- [ ] Warnings clean where feasible
- [ ] Parameter automation works correctly
- [ ] Validation performed by repository owner in a DAW

---

### 3.5 Execution guardrails
Cline must flag **any intrusive action**, including:
- Adding JUCE modules
- Changing plugin format or target
- Enabling network, filesystem access
- Long-running audio demos

Commands may be suggested but **never executed** by Cline.

---

## 4) ACT MODE (IMPLEMENTATION)

### 4.1 Editing rules
- `replace_in_file` for small, localized changes
- `write_to_file` for:
  - new DSP modules
  - new UI components
  - major refactors
- Keep diffs minimal to reduce RT and regression risk

---

### 4.2 Execution guardrails

#### Allowed by default (short, non-intrusive)
- Build commands (`make`, JUCE exporter builds)
- Plugin validation commands (build only)

#### Require explicit approval
- DAW-based audio playback sessions
- Stress tests or long renders
- Adding dependencies
- Network or filesystem features

Cline **never runs commands**.

---

### 4.3 Local quality checks (prepared by Cline)
- Apply `clang-format` (LLVM + Allman)
- Enforce English-only identifiers and comments
- Validate UI code stays off RT thread
- Ensure parameters are:
  - atomic or lock-free
  - smoothed when needed

Execution is done by the repository owner.

---

### 4.4 Output and traceability
- Conventional Commits (English)
- Atomic commits
- RT risks and rollback steps documented in PR description

---

## 5) CHECKLISTS

### A. PR-ready checklist
- [ ] Plugin builds successfully
- [ ] No French text in `.c/.h/.cpp/.hpp`
- [ ] `clang-format` applied
- [ ] No alloc/lock/log in `processBlock`
- [ ] UI code isolated from DSP
- [ ] Parameters automated safely
- [ ] Conventional Commits (English)

---

### B. RT-change checklist (DSP / processBlock)
- [ ] O(1) per-sample or per-block work
- [ ] Callback time < 50% buffer duration
- [ ] Preallocated buffers only
- [ ] No blocking calls
- [ ] No JUCE UI calls in RT path

---

### C. UI-change checklist (Editor)
- [ ] No DSP logic in `AudioProcessorEditor`
- [ ] UI runs only on message thread
- [ ] Parameter attachments used correctly
- [ ] No polling of DSP internals from UI

---

## 6) PLAYBOOKS

### 6.1 Add a new DSP module
**Plan**
- Pure C/C++ DSP
- No JUCE types
- RT-safe by construction
- Ownership and invariants documented

**Act**
- `write_to_file`
- Wire into processor in `prepareToPlay`

**Validate**
- Build
- Manual DAW test by owner

---

### 6.2 Patch `processBlock`
**Plan**
- Identify exact code section
- Explain RT safety
- Define CPU budget target

**Act**
- `replace_in_file`
- Minimal diff

**Validate**
- Release build
- Manual DAW verification

---

### 6.3 Add or modify a parameter
**Plan**
- Parameter type and range
- Automation behavior
- Smoothing strategy
- UI binding method

**Act**
- Patch processor and editor
- Use `AudioProcessorValueTreeState`

**Validate**
- Build
- Automation test in DAW

---

### 6.4 Add a new UI component
**Plan**
- Component role
- Parameters affected
- No RT interaction

**Act**
- `write_to_file`
- Attach via JUCE mechanisms

**Validate**
- Build
- Visual + interaction test

---

## 7) QUALITY GATES AND ACCEPTANCE

### Mandatory gate
- Plugin builds
- `clang-format` applied
- RT constraints respected
- Manual review of DSP / UI separation

### Optional (recommended)
- `clang-tidy`
- `cppcheck`
- Offline DSP unit tests

---

## 8) SAFETY BOUNDARIES FOR AUTOMATION

- Never start DAW playback or rendering
- Never profile live audio without approval
- Never modify global system configuration
- Never touch external repositories
- All runtime tests are owner-only

---

## 9) MAINTENANCE AND EVOLUTION

- This workflow is a **living document**
- Review quarterly
- Update when JUCE, plugin formats, or architecture evolve
- Keep `MIGRATION.md` in sync with refactors

---

## 10) QUICK REFERENCE

- DSP entry point: `processBlock`
- UI thread: JUCE message thread
- Parameter binding: `AudioProcessorValueTreeState`
- RT rule of thumb: **no alloc, no lock, no log**