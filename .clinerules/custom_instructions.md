1) Scope and Priority
- Scope: Applies to this repository. Complements any global rules you may have.
- Priority order when conflicts arise:
  1. System safety and stability
  2. Real-time (RT) audio constraints
  3. Code conventions and quality
  4. Style and formatting
- Rules vs Requirements: These are implementation rules, not product requirements.

2) Language Policy
- Assistant conversation: French (user preference), but these rules are authored in English upon request.
- Code (C/C++):
  - Identifiers (functions, variables, files) must be clear English.
  - All comments, docstrings, logs, and error messages must be idiomatic English.
  - No French text is allowed in code under any circumstances.
- Formal documentation (README, guides, docs/): default French, unless explicitly requested otherwise.
- Commit messages: Conventional Commits, English only.
  - Examples:
    - feat(audio): add RtAudio adapter
    - fix(dmx): handle FTDI open error on Raspberry Pi
    - refactor(core): extract CIS line parser into pure C module

3) Real-Time (RT) Audio Constraints
- Audio callback (hot path):
  - Forbidden: dynamic allocation, locks (mutex), blocking I/O, logging/printf, C++ exceptions.
  - Allowed: bounded atomic operations, lock-free queues/rings, preallocated buffers, O(1) per frame work.
- Time budget (measurable): callback compute time should not exceed 50% of the buffer duration target; zero underruns in nominal conditions.
- Memory: preallocate at startup; use pools and reuse; no free/malloc/realloc in RT path.
- Logging: strictly off in RT threads; use a lock-free ring buffer to a dedicated logger thread.
- Release builds: no debug traces in RT code paths.

4) Architectural Direction
- Module responsibility separation details
- Dependency management rules between layers
- Configuration file organization specifications
- Rules for integrating new external libraries
- Error handling patterns across architectural boundaries

5) Style, Quality, and Conventions
- Formatting: clang-format required (LLVM base + Allman braces).
- Analysis: clang-tidy (C++) and cppcheck (C/C++) recommended before merge (at least on changed code).
- Conventions:
  - Include guards or #pragma once; include order: standard → external → internal.
  - Const-correctness and explicit ownership in comments (in English).
  - Clean warnings: -Wall -Wextra. -Werror may be enabled in Debug.
- i18n enforcement in code: no French accents or vocabulary in .c/.h/.cpp/.hpp.


6) Build and Profiles
- Primary targets: macOS, Raspberry Pi 5 (ARM64).
- Profiles:
  - Debug: -O0 -g; optionally enable ASan/UBSan.
  - Release: -O3 -ffast-math; RT strict.
  - Profiling: perf/valgrind (never in RT hot path).
  - Conditional build rules (SFML/NO_SFML handling)
  - ARM64-specific optimizations for Raspberry Pi
- Build systems:
  - Makefile

7) Git, Branching, Versioning
- Branches: protected main, integration dev, feature/*, fix/*.
- Versioning: SemVer with git tags and a maintained CHANGELOG.
- Commits: Conventional Commits in English; keep commits atomic.

8) Raspberry Pi 5 Deployment
- Method: SSH/SSHFS + scripts; systemd services for long-running processes.
- Verification: CPU/latency profiling; zero underruns under nominal operating conditions.

9) Execution Guardrails for Automation
- Do not start long-running RT audio sessions without explicit approval.
- Allowed by default (non-intrusive):
  - make, make no-sfml, make sfml-check
  - ./build/Sp3ctra --list-audio-devices (short execution only)
- Intrusive actions (require explicit approval): package installation, network operations, system/service changes.

10) Living Documentation
- Review these rules regularly (e.g., quarterly).
- Update rules when adopting new frameworks/processes (CMake, new adapters, etc.).
