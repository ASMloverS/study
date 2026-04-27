# Task 15 - Conformance, Golden Diagnostics, Release Gate

## Goal

Turn the earlier task work into a stable verification and release baseline so
future changes do not silently break the language contract.

## Design Links

- Test baseline and diagnostic families: [../mslangc-design.md](../mslangc-design.md)
- Repository rules: [../AGENTS.md](../AGENTS.md)

## Dependencies

1. Task 01 through Task 14

## Scope

1. Define the test taxonomy and directory conventions for `tests/ms/`,
   `tests/e2e/`, `tests/stress/`, and `tests/unit/`.
2. Add conformance coverage for the language surface in small static and
   runtime slices.
3. Add golden diagnostics that lock `phase + code + line`.
4. Add end-to-end regression coverage for modules and GC stress.
5. Define the local and CI release gate.

## Implementation Boundaries

1. This task owns test governance and release criteria, not new runtime
   features.
2. Golden updates must be explicit and justified rather than incidental.
3. The final gate must use one entrypoint that developers can run locally.
4. Task 15 should be updated incrementally as earlier tasks land, then
   finalized after Task 14.

## File Ownership

1. `tests/ms/`
2. `tests/e2e/`
3. `tests/stress/`
4. `tests/unit/`
5. `tests/CMakeLists.txt`
6. `tests/check_mslangc_run.cmake`
7. `tests/run_gc_stress.ps1`
8. `tests/run_gc_modules.ps1`
9. one release-gate doc if extra process documentation is needed

## Release Gate

1. Configure and Debug build succeed.
2. Unit, integration, conformance, golden, and stress suites all pass.
3. `.ms` coverage exists for `fn`, `self`, `super`, `break`, `continue`,
   evaluation order, short-circuiting, containers, and modules.
4. Diagnostics lock `phase + code + line`.
5. GC stress and module regressions are part of the standard gate.

## TDD and Process Policy

1. Every new language feature must add at least one `.ms` conformance case.
2. Any feature that introduces new failures must add negative diagnostic
   coverage.
3. Golden updates require an explicit rationale in the change description.
4. Conformance coverage must include module-namespace isolation and illegal
   `return <expr>` in `init`.
5. No task is done while the full gate is red.

## Execution Breakdown

Every subtask must start with failing tests, then the minimal implementation,
then the focused verification command listed for that subtask.

Use these status markers consistently in this document:

- `TODO`: not started
- `DOING`: in progress
- `BLOCKED`: waiting on another subtask or prerequisite fix
- `DONE`: completed and verified

| Subtask | Status | Depends on | Summary |
| --- | --- | --- | --- |
| 15.1 | `DONE` | Task 01 through Task 14 | Test taxonomy, naming, and runner entrypoints |
| 15.2 | `DONE` | Tasks 04, 05, 06, 07, 08, 09 | Static conformance coverage for parser and resolver rules |
| 15.3 | `TODO` | Tasks 10, 11, 12, 13, 14 | Runtime conformance coverage for functions, classes, containers, and modules |
| 15.4 | `TODO` | 15.2, 15.3 | Golden diagnostics with stable `phase + code + line` assertions |
| 15.5 | `TODO` | 15.3, 14.6 | Module and GC stress regressions under the standard test gate |
| 15.6 | `TODO` | 15.1 through 15.5 | Unified local and CI release gate definition |

### Subtask 15.1 - Test taxonomy, naming, and runner entrypoints

**Status:** `DONE`

**Depends on:** Task 01 through Task 14.

**Deliverables**

1. Confirm the directory split between `tests/ms/`, `tests/e2e/`,
   `tests/stress/`, and `tests/unit/`.
2. Standardize test naming so the suite labels reflect feature area and failure
   class.
3. Keep one documented entrypoint for the local gate so developers do not need
   to guess which commands matter.
4. Keep the taxonomy stable enough that later conformance and golden updates do
   not need file moves.

**Tests to add or update**

1. Update `tests/CMakeLists.txt` so the labels used by `ctest` match the
   documented taxonomy.
2. Add or adjust one smoke check for the release entrypoint if the gate command
   changes.
3. Avoid creating a separate runner unless the current `ctest` entrypoint is no
   longer sufficient.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -N
```

**Done when**

1. The suite labels and directory conventions are documented in one place.
2. The release entrypoint is stable and easy to run locally.
3. The test taxonomy matches the file layout used by the repository.

**Implementation summary**

1. `tests/CMakeLists.txt` now tags the CLI, e2e, resolver, module, class, and
   container suites with `integration` plus `conformance`, and
   `tests/unit/CMakeLists.txt` does the same for the lexer and parser fixture
   suites.
2. `ctest --test-dir build -C Debug --output-on-failure -L conformance` now
   selects the language-contract suites instead of returning zero tests.
3. The existing `tests/` layout already matches the documented `tests/ms/`,
   `tests/e2e/`, `tests/stress/`, and `tests/unit/` split, so no file moves or
   new runner entrypoint were required for this subtask.
4. `ctest --test-dir build -C Debug --output-on-failure` remains the single
   local gate entrypoint for developers.

### Subtask 15.2 - Static conformance coverage for parser and resolver rules

**Status:** `DONE`

**Depends on:** Tasks 04, 05, 06, 07, 08, and 09.

**Deliverables**

1. Add `.ms` conformance cases for syntax and static semantics that should be
   accepted or rejected before runtime.
2. Keep cases focused on one rule per file so failures are easy to triage.
3. Cover the control-flow and binding rules that were introduced earlier:
   `break`, `continue`, `self`, `super`, `fn`, and illegal `return <expr>` in
   `init`.
4. Keep parser and resolver diagnostics stable enough to feed golden checks
   later.

**Tests to add or update**

1. Add or update fixtures under `tests/ms/parser_expr/` and
   `tests/ms/parser_decl_stmt/` for precedence, postfix, and control-flow
   shapes that need stable parse output.
2. Add or update fixtures under `tests/ms/resolver/` for static rule
   violations, especially `break`, `continue`, `self`, `super`, and
   `init`-return restrictions.
3. Keep each fixture small enough that one diagnostic maps to one rule.

**Implementation summary**

1. Added `tests/ms/parser_decl_stmt/while_break.ms` and
   `tests/ms/parser_decl_stmt/for_continue.ms` to lock the AST shapes for loop
   bodies that contain `break` and `continue`.
2. Registered both fixtures in `tests/unit/CMakeLists.txt` so they run through
   the existing parser-declaration fixture harness.
3. The existing resolver fixtures already cover the static legality checks for
   `break`, `continue`, `self`, `super`, and illegal `return <expr>` in `init`,
   so no resolver-side code change was needed for this subtask.
4. The targeted conformance sweep passed with
   `ctest --test-dir build -C Debug --output-on-failure -R "parser_expr\\.fixture|parser_decl_stmt\\.fixture|resolver_static"`.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "parser_expr\.fixture|parser_decl_stmt\.fixture|resolver_static"
```

**Done when**

1. Static syntax and resolver rules have explicit `.ms` coverage.
2. Each failure class is isolated to a targeted fixture.
3. Later golden tests can rely on these diagnostics staying stable.

### Subtask 15.3 - Runtime conformance coverage for functions, classes, containers, and modules

**Status:** `TODO`

**Depends on:** Tasks 10, 11, 12, 13, and 14.

**Deliverables**

1. Add `.ms` conformance cases for runtime behavior that should succeed end to
   end.
2. Cover evaluation order, short-circuiting, container semantics, function
   calls, closure behavior, class behavior, and module import behavior.
3. Keep one scenario per file so the runtime contract is easy to audit.
4. Use the runtime suite to anchor language surface behavior that the static
   tests do not cover.

**Tests to add or update**

1. Add or update runtime fixtures under `tests/e2e/basic/`,
   `tests/e2e/functions/`, `tests/e2e/closures/`, `tests/e2e/class/`,
   `tests/e2e/containers/`, and `tests/e2e/modules/`.
2. Add any missing `.out` files so the success-path output is explicit.
3. Keep module namespace isolation and import chaining covered by end-to-end
   scripts rather than by unit tests alone.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "e2e_basic|functions|closures|class|containers|modules"
```

**Done when**

1. The runtime surface has explicit success-path coverage for the supported
   language features.
2. Modules, containers, classes, functions, and closures are exercised through
   real scripts.
3. The runtime suite is broad enough to catch regressions before golden updates
   are needed.

### Subtask 15.4 - Golden diagnostics with stable `phase + code + line` assertions

**Status:** `TODO`

**Depends on:** Subtasks 15.2 and 15.3.

**Deliverables**

1. Add golden coverage for parse, resolve, module, and runtime failures that
   need exact diagnostics.
2. Lock the diagnostic contract to `phase + code + line` so later changes do
   not drift silently.
3. Keep golden updates explicit and reviewable.
4. Make the expected-output files easy to update only when the failure contract
   changes intentionally.

**Tests to add or update**

1. Add or update `.diag` fixtures under `tests/ms/` for parser and resolver
   failures that need exact phase/code/line checks.
2. Add or update `.out` files under `tests/e2e/` for module and runtime failure
   paths that the CLI already runs.
3. Keep one golden fixture per failure mode so the diagnostic delta is clear.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "runner_basic\.(parse_error|resolve_error|runtime_error)|resolver_static|modules\.(missing_module|missing_export|cyclic_dependency|import_parse_failure|import_resolve_failure|import_runtime_failure)"
```

**Done when**

1. Diagnostic failures are pinned to the expected phase, code, and line.
2. Golden output is updated only with an explicit reason.
3. Parser, resolver, module, and runtime failure modes are easy to compare
   against the expected contract.

### Subtask 15.5 - Module and GC stress regressions under the standard test gate

**Status:** `TODO`

**Depends on:** Subtasks 15.3 and 14.6.

**Deliverables**

1. Keep the GC stress scripts deterministic enough for repeated local and CI
   runs.
2. Keep module regressions in the same gate so GC changes do not break import
   behavior quietly.
3. Add any missing churn or repeated-load cases that surface object lifetime
   bugs.
4. Make stress failures actionable by keeping the scripts small and named after
   the behavior they check.

**Tests to add or update**

1. Keep `tests/stress/gc/alloc_churn.ms` and `tests/stress/gc/modules/module_gc.ms`
   in the standard gate.
2. Keep `tests/run_gc_stress.ps1` and `tests/run_gc_modules.ps1` as the
   deterministic runners for stress and module regression subsets.
3. Add or update module regressions such as cache reuse, shared dependency
   reuse, and temporary-root coverage when needed.

**Verification**

```powershell
ctest --test-dir build -C Debug --output-on-failure -R "gc\.stress|gc\.modules|modules\.(cache_once|shared_dependency_once|temp_root_success|temp_root_failure)"
```

**Done when**

1. GC stress runs deterministically enough for repeated execution.
2. Module regressions run as part of the same release gate.
3. Stress failures clearly point to one behavior family instead of a broad
   runtime area.

### Subtask 15.6 - Unified local and CI release gate definition

**Status:** `TODO`

**Depends on:** Subtasks 15.1 through 15.5.

**Deliverables**

1. Define the single local command that developers should run before
   submitting changes.
2. Keep the CI gate equivalent to the local gate so one environment does not
   hide regressions from the other.
3. Document the order of operations: configure, build, test.
4. Make the release gate strict enough to block incomplete task work.

**Tests to add or update**

1. Update the task doc and any release-gate helper doc so the command sequence
   is unambiguous.
2. If a CI workflow file exists later, mirror the same command scope there
   instead of inventing a looser subset.
3. Keep the gate based on `cmake`, `cmake --build`, and `ctest` unless a
   concrete need appears for an extra wrapper.

**Verification**

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

**Done when**

1. One documented local command runs the full matrix.
2. The CI gate matches the local gate scope.
3. The release definition is strict enough to keep the baseline stable.

## Acceptance

1. All subtasks above are complete in order.
2. Reachable behavior stays stable under the conformance and runtime suites.
3. Diagnostics stay pinned to `phase + code + line` where a golden is defined.
4. Module and GC regressions stay part of the standard gate.
5. The task is not complete until build passes, the full matrix passes, and all
   edited files are UTF-8 with LF and no trailing whitespace.

## Acceptance Commands

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Out of Scope

1. New language syntax.
2. New runtime capabilities.
