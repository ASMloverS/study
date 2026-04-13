# T24: Testing & Polish

**Phase**: 14 — Polish & Testing
**Deps**: T23 (Error Handling Polish)
**Complexity**: Medium

## Goal

Test framework, unit + integration tests, example scripts, warning-free compilation, zero mem leaks.

## Files

| File | Action |
|------|--------|
| `tests/test_framework.h` | Create: test macros, suite mgmt |
| `tests/test_framework.c` | Create: test runner impl |
| `tests/CMakeLists.txt` | Rewrite: test targets |
| `tests/unit/test_scanner.c` | Scanner tests |
| `tests/unit/test_parser.c` | Parser tests |
| `tests/unit/test_compiler.c` | Compiler tests |
| `tests/unit/test_vm.c` | VM tests |
| `tests/unit/test_gc.c` | GC tests |
| `tests/unit/test_table.c` | Table tests |
| `tests/unit/test_logger.c` | Logger tests |
| `tests/basic/*.ms` | Basic integration scripts |
| `tests/functions/*.ms` | Function integration scripts |
| `tests/classes/*.ms` | Class integration scripts |
| `tests/modules/*.ms` | Module integration scripts |
| `examples/*.ms` | Example programs |

## TDD Cycles

### Cycle 1: Test Framework — Core Macros & Suite

**RED** — `cmake --build build` → compile error: cannot find `test_framework.h`.

**GREEN** — Create `tests/test_framework.h`:
```c
typedef struct { const char* name; void (*func)(void); bool passed; char error[256]; } MsTest;
typedef struct { MsTest* tests; int count; int capacity; int passed; int failed; } MsTestSuite;

void ms_test_suite_init(MsTestSuite* suite);
void ms_test_suite_free(MsTestSuite* suite);
void ms_test_suite_add(MsTestSuite* suite, const char* name, void (*func)(void));
int ms_test_suite_run(MsTestSuite* suite);
void ms_test_suite_report(const MsTestSuite* suite);

extern MsTestSuite* ms_current_suite;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(suite, name) ms_test_suite_add(suite, #name, test_##name)
#define ASSERT_TRUE(cond) do { if (!(cond)) { snprintf(ms_current_suite->tests[ms_current_suite->count-1].error, 256, "Assertion failed: %s", #cond); return; } } while(0)
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { snprintf(ms_current_suite->tests[ms_current_suite->count-1].error, 256, "Expected %d, got %d", (int)(expected), (int)(actual)); return; } } while(0)
#define ASSERT_STR_EQ(expected, actual) do { if (strcmp((expected),(actual)) != 0) { snprintf(ms_current_suite->tests[ms_current_suite->count-1].error, 256, "Expected \"%s\", got \"%s\"", (expected), (actual)); return; } } while(0)
```
- Create `tests/test_framework.c`: init (zero fields), free, add (grow array), run (iterate, call, track pass/fail), report (print results).

**Verify**: `build\test_framework_self` → all self-tests pass.

**REFACTOR**: Add `ASSERT_NULL`, `ASSERT_NOT_NULL`, `ASSERT_DOUBLE_EQ` as needed.

### Cycle 2: CMake Test Infrastructure

**RED** — `ctest --output-on-failure` → "No tests were found".

**GREEN** — Rewrite `tests/CMakeLists.txt`: each test → separate exe, link `maple_lib`, `add_test()`. Top-level: `enable_testing()` + `add_subdirectory(tests)` when `BUILD_TESTS=ON`.

**Verify**: `ctest --output-on-failure` → framework self-test passes.

**REFACTOR**: CMake function/macro to reduce boilerplate for new test targets.

### Cycle 3: Scanner Unit Tests

**RED** — `build\test_scanner` → some tests fail, revealing scanner bugs.

**GREEN** — Tests: single-char tokens, two-char (`==`, `!=`, `<=`, `>=`), numbers (int, float), strings (escapes), identifiers, keywords, whitespace/newline tracking, EOF.

**Verify**: `build\test_scanner` → all pass.

**REFACTOR**: Sub-suites (tokens, numbers, strings, keywords).

### Cycle 4: Compiler & VM Unit Tests

**RED** — `build\test_compiler && build\test_vm` → some tests fail.

**GREEN** — Compiler tests: expressions, var decl/assign, control flow, fn defs, class defs. VM tests: arithmetic, vars, control flow, fn calls, closures, class instantiation.

**Verify**: `build\test_compiler && build\test_vm` → all pass.

**REFACTOR**: Shared VM setup/teardown helpers.

### Cycle 5: GC, Table & Logger Tests

**RED** — `build\test_gc && build\test_table && build\test_logger` → potential failures.

**GREEN** — GC: alloc, mark, sweep, cycle detection, stress test. Table: insert, lookup, delete, resize, string interning, collisions. Logger: levels, formatting, file output.

**Verify**: All pass.

**REFACTOR**: GC stress mode (GC on every alloc).

### Cycle 6: Integration Test Scripts

**RED** — `ctest --output-on-failure` → some integration tests fail.

**GREEN** — Scripts per feature area:
- `tests/basic/`: arithmetic, strings, variables, control_flow
- `tests/functions/`: basic, closures, recursion
- `tests/classes/`: basic, inheritance
- `tests/modules/`: test_import, test_from_import
- Each uses `print` assertions, CTest checks exit code 0.
- `examples/`: hello.ms, fibonacci.ms, classes.ms, closures.ms

**Verify**: `ctest --output-on-failure` → all integration tests pass.

**REFACTOR**: Output comparison (expected vs actual).

### Cycle 7: Warning-Free & Leak-Free

**RED** — Build with `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) or `/W4 /WX` (MSVC) → possible `-Werror` failure.

**GREEN** — Fix all warnings: unused vars, missing prototypes, implicit conversions, sign comparison. Fix all leaks: every `ms_reallocate()` matched with free, every `ms_xxx_init()` matched with `ms_xxx_free()`. Verify ASan / valgrind.

**Verify**: Strict build + `ctest --output-on-failure` → clean, all pass.

**REFACTOR**: CI config to enforce warning-free builds.

## Acceptance

- [ ] All unit tests pass (`ctest --output-on-failure`)
- [ ] All integration scripts execute without error
- [ ] Coverage: scanner, parser, compiler, VM, GC, table, logger, modules, builtins
- [ ] No warnings: `-Wall -Wextra -Wpedantic` (GCC/Clang), `/W4` (MSVC)
- [ ] No leaks: valgrind (Linux) / ASan (both)
- [ ] Framework reports pass/fail counts correctly
- [ ] Example scripts run correctly

## Notes

- Framework uses `ms_reallocate()` (project convention)
- Test files link `maple_lib` static lib for internal fn access
- Integration tests: run `.ms` via `maple`, check exit codes
- Examples serve as docs + regression tests
- Cross-platform: Windows (MSVC) + Linux (GCC/Clang)
