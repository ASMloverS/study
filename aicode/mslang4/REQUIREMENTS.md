# Maple Scripting Language — Requirements

## 1. Overview

- **Lang**: Maple (`ms` prefix)
- **Impl**: C11, CMake 3.10+
- **Targets**: Win (MSVC 2022+) + Linux (GCC 5+ / Clang 3.6+)
- **Goal**: Production-ready bytecode interp, inspired by `clox` ([Crafting Interpreters](https://craftinginterpreters.com/)). Pure C, zero ext deps.
- **Features**: Bytecode compilation, stack-based VM, mark-and-sweep GC, closures/upvalues, classes/inheritance, first-class fns.

## 2. Core Requirements

### 2.1 Language Features

**Types**: `nil`, bool (`true`/`false`), double, heap-immutable strings, fns (first-class, closures), classes (inheritance), instances, lists/arrays (opt).

**Stmt termination**: Newline-terminated (Go-style). `;` → multi-stmt on same line.

**Control flow**: `if`/`else`, `while`, `for` (C-style), `break`, `continue`, `return`.

**Fns**: `fn` keyword. User-defined, closures + upvalues, variadic (opt), native/built-in, recursion.

**OOP**: Class decl, `init` ctor, methods, field get/set, single inheritance, `this`, `super`.

**Operators**: Arithmetic (`+`,`-`,`*`,`/`,`%`), comparison (`==`,`!=`,`<`,`<=`,`>`,`>=`), logical (`and`,`or`,`!`), assignment (`=`), string concat (`+`).

### 2.2 Enhanced Features (Beyond clox)

#### 2.2.1 Module System

```maple
import math
from math import sqrt, pow
from math import sqrt as squareRoot
from collections import List, Map as HashMap
```

- `.ms` file = module. Singletons → loaded once, cached.
- Scope isolation, circular import detection.
- Relative + absolute paths, configurable search paths.
- Resolution: `.ms` auto-appended; cwd + configured paths.
- Top-level decls exported. Clear errors for missing modules.

#### 2.2.2 Debugging Logger

**Levels**: TRACE (Gray), DEBUG (Blue), INFO (Green), WARN (Yellow), ERROR (Red), FATAL (Magenta).

**Features**: ANSI/Win32 color, opt timestamps, source loc (`__FILE__`/`__LINE__`/`__func__`), console/file output, runtime level config, compile-time filtering.

```c
ms_logger_set_level(MS_LOG_DEBUG);
ms_logger_info("VM initialized");
ms_logger_error("Undefined variable '%s' at line %d", name, line);
```

#### 2.2.3 Built-in Functions

`print(value)`, `clock()`, `type(value)`, `len(string|array)`, `input([prompt])` (opt), `str(value)`, `num(value)`.

#### 2.2.4 List/Array Support (Optional)

```maple
var list = [1, 2, 3]
list.append(4)
print(list[0])
list[1] = 10
print(list.length)
```

### 2.3 Implementation Requirements

#### 2.3.1 Code Organization

```
mslang4/
├── CMakeLists.txt              # Build config
├── README.md, REQUIREMENTS.md, DESIGN.md
├── src/
│   ├── main.c                  # Entry point
│   ├── common.h                # Definitions, macros, types
│   ├── logger.h / .c           # Logger
│   ├── token.h                 # Token defs
│   ├── scanner.h / .c          # Lexer
│   ├── ast.h                   # AST nodes
│   ├── parser.h / .c           # Parser
│   ├── compiler.h / .c         # Compiler
│   ├── chunk.h / .c            # Bytecode chunk
│   ├── value.h / .c            # Value repr
│   ├── object.h / .c           # Object system
│   ├── table.h / .c            # Hash table
│   ├── memory.h / .c           # Mem mgmt
│   ├── vm.h / .c               # VM
│   ├── module.h / .c           # Module system
│   ├── builtins.h / .c         # Built-in fns
│   └── platform.h / .c         # Platform utils
├── tests/
│   ├── unit/                   # C unit tests
│   ├── basic/, functions/, classes/, modules/, regression/
└── examples/
```

#### 2.3.2 C11 Features

Anonymous struct/union, `_Generic`, `static_assert`, `_Alignof`/`alignas`, designated initializers, `snprintf`, `<stdbool.h>`, `<stdint.h>`. No VLAs → dynamic arrays.

#### 2.3.3 Cross-Platform

File I/O, path sep (`/` vs `\\`), console color (ANSI/Win32), time fns. Platform code → `platform.h`/`.c` only, via `#ifdef`.

#### 2.3.4 Memory Management

`ms_realloc` wrapping `realloc`/`free`. Mark-and-sweep GC (incremental/generational opt). Debug alloc tracking. Init/free lifecycle pairs for all types.

#### 2.3.5 Error Handling

- **Return codes**: `MsResult` enum (`MS_OK`, `MS_COMPILE_ERROR`, `MS_RUNTIME_ERROR`)
- **Output params**: Success via pointer
- **Error ctx**: `MsError` with line/col + source snippet
- **Recovery**: Lexer → skip to next token; parser → sync to stmt boundary; VM → unwind stack + trace

#### 2.3.6 Code Style

- `#ifndef`/`#define`/`#endif` guards; `.h`/`.c`
- Types `MsPascalCase`, fns `ms_snake_case`, vars `snake_case`, macros `MS_UPPER_CASE`
- 4-space indent, no tabs, no trailing ws, UTF-8/LF
- No global state → pass context structs
- [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)

#### 2.3.7 Testing

- Custom lightweight C unit test framework (macros)
- `.ms` script integration suite
- Categories: lexer, parser, compiler, VM, GC, modules, builtins, errors, perf benchmarks (opt)
- High coverage; bugs → regression tests

#### 2.3.8 Perf Targets

Startup < 50ms. Competitive w/ Lua/Python. GC pause < 10ms.

### 2.4 Build & CLI

**CMake targets**: `maple` exe, `maple_lib` static lib (opt). Build types: Debug, Release, RelWithDebInfo.

```bash
maple script.ms              # Run script
maple                        # REPL
maple -c script.ms -o script.mbc   # Compile → bytecode (opt)
maple -x script.mbc                # Exec bytecode (opt)
maple --path /usr/lib/maple script.ms
maple --log-level debug script.ms
maple --version / --help
```

## 3. Non-Functional

- Zero ext deps → C stdlib only
- Pure C11, no C++
- Binary < 2MB
- Open-source license

## 4. Implementation Phases

1. **Foundation**: Project structure, CMake, common.h, logger, platform, tokens
2. **Lexer & Parser**: Scanner, token stream, AST, recursive descent + Pratt, error recovery
3. **Bytecode & Compiler**: Instruction set, chunk (dynarray), single-pass compiler, debug info
4. **Value System**: Tagged union, object system, string interning, hash table
5. **VM**: Core + stack, dispatch, call frames, stack ops
6. **Mem Mgmt**: Alloc wrapper, mark-and-sweep GC, gray stack, stress mode
7. **Fns & Closures**: Fn/upvalue/closure objects, native fn binding
8. **Classes & Inheritance**: Class/instance/bound method, inheritance chain, `super` lookups
9. **Module System**: Loader, import compilation, caching, circular dep detection
10. **Polish & Testing**: Comprehensive tests, optimization, docs, examples

## 5. Success Criteria

- Compiles on Win (MSVC) + Linux (GCC)
- Passes craftinginterpreters test suite
- Module system → all import forms
- Colored logger output
- Working GC, classes, inheritance
- Clear errors w/ source locations
- No mem leaks (sanitizer-verified)

## 6. Future (Out of Scope)

JIT, static types, exceptions, coroutines/async, stdlib, pkg manager, debugger, LSP, WASM target, multi-threading, FFI.
