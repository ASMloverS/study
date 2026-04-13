# T22: Main Entry Point & REPL

**Phase**: 13 — Integration
**Deps**: T21 (Builtins), T20 (Modules)
**Complexity**: Medium

## Goal

CLI entry point: arg parsing, script exec, interactive REPL. Wires all components → usable CLI tool.

## Files

| File | Action |
|------|--------|
| `src/main.c` | Rewrite: CLI + REPL + script exec + args |

## TDD Cycles

### Cycle 1: VM Init/Free in main

**RED** — `echo "" | maple` → crash or undef ref.

**GREEN**:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "builtins.h"
#include "platform.h"

int main(int argc, char* argv[]) {
    MsVM vm;
    ms_vm_init(&vm);
    ms_builtins_define_all(&vm);

    /* placeholder — will add REPL/file logic next */

    ms_vm_free(&vm);
    return 0;
}
```

**Verify**: `cmake --build build && echo "" | build\maple` → exit 0.

**REFACTOR**: None.

### Cycle 2: Script File Exec

**RED** — `build\maple tests\basic\hello.ms` → no output or crash. Create `tests/basic/hello.ms`: `print "Hello, World!"`.

**GREEN**:
```c
static MsInterpretResult run_file(MsVM* vm, const char* path) {
    char* source = ms_platform_read_file(path);
    if (source == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return MS_INTERPRET_COMPILE_ERROR;
    }
    MsInterpretResult result = ms_vm_interpret(vm, source);
    free(source);
    if (result == MS_INTERPRET_COMPILE_ERROR) exit(65);
    if (result == MS_INTERPRET_RUNTIME_ERROR) exit(70);
    return result;
}
```
- `argc == 2` → `run_file(&vm, argv[1])`.
- Exit codes: 0=ok, 65=compile err, 70=runtime err (lox convention).

**Verify**: `build\maple tests\basic\hello.ms` → `Hello, World!`, exit 0.

### Cycle 3: Error Exit Codes

**RED** — `syntax_error.ms` (`var x =`) → expect exit 65. `runtime_error.ms` (`var x = unknown_var`) → expect exit 70.

**GREEN** — `run_file()` checks `ms_vm_interpret()` result: compile err → `exit(65)`, runtime err → `exit(70)`, ok → return.

**Verify**: `build\maple tests\errors\syntax_error.ms || echo %ERRORLEVEL%` → prints 65.

**REFACTOR**: Errors → stderr, not stdout.

### Cycle 4: REPL Mode

**RED** — `echo "print 1 + 2" | build\maple` → no output `3`.

**GREEN**:
```c
static void repl(MsVM* vm) {
    char line[1024];
    for (;;) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        ms_vm_interpret(vm, line);
    }
}
```
- `argc == 1` → `repl(&vm)`.

**Verify**: `echo "print 1 + 2" | build\maple` → `3`.

**REFACTOR**: Multi-line input for incomplete blocks → future enhancement.

### Cycle 5: `--version` & `--help`

**RED** — `build\maple --version` → "Could not open file --version".

**GREEN** — Arg loop:
```c
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        printf("Usage: maple [script.ms] [options]\n");
        printf("Options:\n");
        printf("  --path <dir>    Add module search path\n");
        printf("  --log-level <l> Set log level\n");
        printf("  --version        Show version\n");
        printf("  --help           Show this help\n");
        return 0;
    } else if (strcmp(argv[i], "--version") == 0) {
        printf("Maple v0.1\n");
        return 0;
    } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
        ms_vm_add_module_path(&vm, argv[++i]);
    } else {
        run_file(&vm, argv[i]);
    }
}
```

**Verify**: `build\maple --version` → `Maple v0.1`.

**REFACTOR**: Extract arg parsing if it grows.

### Cycle 6: `--path` Flag

**RED** — `build\maple --path tests\modules tests\cli\test_path_flag.ms` → module not found.

**GREEN** — Handle `--path <dir>`: call `ms_vm_add_module_path(&vm, argv[++i])`. Process `--path` before script arg.

**Verify**: `build\maple --path tests\modules tests\cli\test_path_flag.ms` → `ok`.

**REFACTOR**: Handle missing path arg after `--path`.

## Acceptance

- [ ] `maple script.ms` runs script
- [ ] `maple` → REPL, interactive eval
- [ ] `maple --version` → version string
- [ ] `maple --help` → usage info
- [ ] `maple --path /custom/path script.ms` → adds module search path
- [ ] Compile err → exit 65
- [ ] Runtime err → exit 70
- [ ] Success → exit 0
- [ ] REPL handles Ctrl+D (EOF) gracefully
- [ ] No mem leaks (VM freed)

## Notes

- Exit codes: lox convention — 65 compile, 70 runtime
- REPL: line-based, `fgets()`, 1024-byte buffer
- Multi-line REPL → future enhancement
- `ms_vm_free()` frees all resources before exit
- Args: flags processed in order, mixable with script paths
