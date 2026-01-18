# VST3 JUCE DEVELOPMENT RULES (LEGACY APP PORT)

## SCOPE AND PRIORITY

### Scope
- Applies to this repository only.
- Complements any global rules.

### Priority order (highest first)
1. Real-time (RT) audio safety and determinism  
2. JUCE / VST3 lifecycle correctness  
3. System stability  
4. Architecture and code quality  
5. Style and formatting  

> These are implementation rules, not product requirements.

---

## LANGUAGE POLICY

### Assistant conversation
- French (user preference).

### Ruleset language
- English (normative).

### Code (C / C++)
- Identifiers (functions, variables, classes, files): clear English only  
- Comments, docstrings, logs, assertions, error messages: idiomatic English  
- **No French text allowed in code under any circumstances**

### Documentation
- README / guides / docs: French by default unless explicitly requested otherwise

### Git
- Commit messages: Conventional Commits, English only

### Examples
- `feat(vst): add AudioProcessor base`
- `fix(rt): remove allocation from processBlock`
- `refactor(core): isolate legacy DSP into RT-safe module`

---

## REAL-TIME (RT) AUDIO CONSTRAINTS (HARD RULES)

### Audio thread (`processBlock` / hot path)

#### Forbidden
- Dynamic allocation (`new`, `malloc`, `realloc`, `free`)
- Locks (mutexes, condition variables, `std::lock_guard`)
- Blocking I/O
- Logging, `printf`, `std::cout`
- Exceptions (`throw`, `try/catch`)

#### Allowed
- Preallocated buffers
- Lock-free queues or ring buffers
- Bounded atomic operations
- O(1) per-sample or per-block work

### Timing
- Callback CPU usage must stay below 50% of buffer duration
- Zero underruns under nominal conditions

### Memory
- Allocate once at startup or `prepareToPlay`
- Explicit ownership and lifetime documentation
- No memory release in RT paths

### Logging
- Strictly forbidden in RT threads
- Use a lock-free ring buffer to a dedicated logger thread

---

## JUCE / VST3 ARCHITECTURAL DIRECTION

### Separation of responsibilities

#### Legacy DSP core
- Pure C/C++
- No JUCE types
- RT-safe by design

#### JUCE adapter layer
- `juce::AudioProcessor` implementation
- Parameter binding
- Audio/MIDI buffer translation

#### UI layer
- `juce::AudioProcessorEditor`
- No DSP logic

#### Auxiliary systems
- File I/O, networking, tooling
- Non-RT threads only

### Dependency rules
- DSP core must not depend on JUCE
- UI must not access DSP internals directly
- JUCE layer enforces strict RT / non-RT boundaries

### Error handling
- RT path: atomic flags or state variables only
- Non-RT path: JUCE `Result` or error codes
- No exceptions crossing RT boundaries

---

## STYLE, QUALITY, AND CONVENTIONS

### Formatting
- `clang-format` required
- LLVM base style
- Allman braces

### Static analysis (recommended before merge)
- `clang-tidy` (C++)
- `cppcheck` (C/C++)

### Warnings
- `-Wall -Wextra`
- `-Werror` allowed in Debug

### Includes
- Use include guards or `#pragma once`
- Include order: standard → external → internal

### Code quality
- Const-correctness required
- Explicit ownership and lifetime documented in comments (English)

### Internationalization enforcement
- No French accents or vocabulary in `.c` / `.h` / `.cpp` / `.hpp` files

---

## BUILD AND PROFILES

### Primary targets
- macOS

### Profiles

#### Debug
- `-O0 -g`
- Optional ASan / UBSan (never in RT paths)

#### Release
- `-O3 -ffast-math`
- RT strict

### Profiling
- Instruments / `perf` / `valgrind`
- Never in RT hot path

### Build system
- Makefile

---

## GIT, BRANCHING, VERSIONING

### Branches
- `main` (protected)
- `dev`
- `feature/*`
- `fix/*`

### Versioning
- Semantic Versioning (SemVer)
- Git tags
- Maintained `CHANGELOG`

### Commits
- Conventional Commits
- English only
- Atomic changes

---

## EXECUTION GUARDRAILS (AUTOMATION SAFETY)

### Allowed by default
- `make`
- `./build/Sp3ctra --list-audio-devices` (short execution only)

### Require explicit approval
- Package installation
- Network operations
- System or service modifications
- Long-running RT audio sessions

---

## MCP USAGE (MANDATORY FOR ASSISTANT)

### General rule
- The assistant must explicitly use MCP tools when relevant.
- No guessing, no undocumented assumptions.

### JUCE APIs
- Use JUCE Docs MCP
- JUCE Docs MCP is the only authoritative source for JUCE APIs
- Do not infer behavior not documented in official JUCE docs

### External libraries and protocols
- Use Context7 MCP
- Resolve library ID first
- Query official documentation only

### Enforcement clause
- Do not answer until required MCP queries have been executed.
- If documentation is missing or unclear, state it explicitly.

---

## LIVING DOCUMENT
- Review quarterly
- Update when adopting new frameworks, build systems, or processes