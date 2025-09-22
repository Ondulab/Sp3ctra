1) Purpose and Scope
- Purpose: Provide a clear, safe, and repeatable way for Cline to plan and implement changes while respecting real-time (RT) audio constraints and repository standards.
- Scope: Applies to all Cline-led work within this repository.
- Separation of concerns:
  - Rules = how code must be written (see custom_instructions.md).
  - Workflow = how Cline must execute work (this file).
- Developer role: Cline proposes and implements changes, but the repository owner compiles and tests all changes locally.

2) Operating Principles
- Always start in Plan Mode; only implement in Act Mode after explicit approval.
- Minimize RT risk: no long-running audio sessions, no blocking operations without approval.
- Prefer small, atomic changes with clear acceptance criteria.
- Never modify files outside this repository.
- Use idiomatic and measurable phrasing in plans (avoid vague directives like “optimize where possible”).
- Compilation and runtime testing are performed manually by the repository owner, not by Cline.

3) Plan Mode (analysis and planning)
1. Context gathering
   - Read the relevant files (README, Makefile/CMake, specific module files).
   - Identify any RT hot path impact and system-level constraints.
2. Questions and assumptions
   - Confirm target platforms (macOS, Raspberry Pi 5), headless-by-default posture, and RT budgets.
   - Clarify testability needs (must work without hardware?).
3. Proposed plan
   - List exact files to add/modify/remove.
   - Outline approach, risks, and rollback strategy.
4. Acceptance criteria (measurable)
   - Headless build succeeds.
   - Strict RT rules respected (no alloc/lock/log in callback).
   - clang-format applied; no French in code; compile warnings clean where feasible.
   - Unit/integration tests updated or added (when applicable).
   - Build and runtime validation will be performed by the repository owner.
5. Execution guardrails
   - Flag any intrusive action requiring approval (package installs, network ops, system services).
   - Provide the command(s) to be run and their expected, short-lived nature.

4) Act Mode (implementation)
1. Editing rules
   - Prefer replace_in_file for small, localized changes.
   - Use write_to_file for new files or major rewrites.
   - Keep changes minimal and incremental to limit RT risk.
2. Execution guardrails
   - Allowed by default (short, non-intrusive):
     - make, make no-sfml, make sfml-check
     - ./build/Sp3ctra --list-audio-devices (short, non-RT listing)
   - Requires explicit approval:
     - Long-running audio sessions or demos
     - Package installation, network operations, system/service changes
   - Never edit outside this repository.
   - Cline must not run these commands directly; they are executed by the repository owner.

3. Local quality checks
   - Apply clang-format (LLVM + Allman).
   - Enforce English-only in code (no French words or accents).
   - Build headless by default (no SFML unless required).
   - Run available unit tests (if present).
   - Owner compiles/tests changes; Cline only prepares commands.

4. Output and traceability
   - Use Conventional Commits (English).
   - Keep commits atomic and scoped.
   - Document RT-related risks and provide rollback steps in PR description.

5) Checklists (copy into plan/PR as needed)
A. “PR ready” checklist
- [ ] Headless build succeeds on target platforms.
- [ ] No French text in code (.c/.h/.cpp/.hpp).
- [ ] clang-format (LLVM + Allman) applied to changed files.
- [ ] No allocation/lock/log inside audio callback or RT hot path.
- [ ] Unit/integration tests updated or added (if applicable).
- [ ] Conventional Commit(s) in English; atomic changes.

B. “RT change” checklist (when touching RT paths)
- [ ] Callback compute time target respected (< 50% of buffer duration).
- [ ] No blocking I/O or mutex in hot path.
- [ ] Preallocated buffers; no runtime (re)allocations in RT.
- [ ] Logging routed via lock-free queue to a non-RT logger thread (if needed).

6) Playbooks
1. Add a new Core module (pure C, testable without hardware)
- Plan: define header and C file in core/ with English comments; zero I/O/locks/alloc in hot path; define ownership and invariants.
- Act: write_to_file for new files; update build (Makefile/CMake).
- Validate: format, i18n, build, add unit tests if applicable.

2. Patch an RT audio path
- Plan: show the exact section impacted; explain how you avoid alloc/lock/log; define budget target.
- Act: replace_in_file with minimal changes; no I/O added.
- Validate: build Release/headless; manual verification of hot path.

3. Add a new CLI option (non-RT)
- Plan: document option name, help text, and side effects; confirm no RT impact.
- Act: patch main/help and relevant modules; ensure English-only logs/messages.
- Validate: build; run ./build/Sp3ctra --help to verify.

4. Update DMX mapping/config
- Plan: confine changes to dmx/ and config; avoid RT path impact.
- Act: targeted patches; English logs outside RT only.
- Validate: build; avoid long hardware sessions without approval.

7) Quality Gates and Acceptance
- Minimal local gate before merge:
  - Headless build OK
  - clang-format applied
  - Zero French in code
  - Manual review focused on RT constraints
- Optional (recommended when available):
  - clang-tidy/cppcheck on changed C/C++ code
  - Unit/integration tests pass

8) Safety Boundaries for Automation
- Never start long-running RT audio without explicit approval.
- Always call out intrusive operations for approval first.
- Do not modify global system configuration or external repositories.
- Compilation and runtime tests are never executed by Cline, but only by the repository owner.

9) Maintenance and Evolution
- Treat this workflow as living documentation.
- Review quarterly or when adopting new processes.
- Keep MIGRATION.md up to date for reorganizations and terminology changes.

10) Quick Reference (Commands)
- Typical headless build: make no-sfml
- SFML diagnostics: make sfml-check
- List audio devices (short, safe): ./build/Sp3ctra --list-audio-devices

