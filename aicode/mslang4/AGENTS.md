# AGENTS.md — AI Config

## Project

**Maple Scripting Language** (`ms`) — pure C11 bytecode-compiled lang. Inspired by `clox` ([Crafting Interpreters](https://craftinginterpreters.com/)).

**Model**: GLM-5 (`zai-coding-plan/glm-5`)

## Build

→ [REQUIREMENTS.md §2.4](REQUIREMENTS.md).

## Code Standards

- **Style**: [Linux Kernel](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- **Encoding**: UTF-8, LF, no trailing ws
- **Lang**: C11 only. No C++.
- **Files**: `.h`/`.c`, `#ifndef`/`#define`/`#endif` guards
- **Naming**:
  - Types: `MsPascalCase` (`MsToken`, `MsVM`)
  - Fns: `ms_snake_case` (`ms_scanner_init`)
  - Vars/members: `snake_case`
  - Consts/macros: `MS_UPPER_CASE`
- **Mem**: `ms_reallocate()` wraps `malloc`/`realloc`/`free`. init/free lifecycle.
- **Includes**: `"common.h"` → `<stdlib.h>` → `""` project hdrs
- **No global state**: pass context structs

→ [REQUIREMENTS.md §2.3](REQUIREMENTS.md), [DESIGN.md](DESIGN.md).

## Doc Map

| Doc | Purpose |
|-----|---------|
| [REQUIREMENTS.md](REQUIREMENTS.md) | Features, style, tests, phases |
| [DESIGN.md](DESIGN.md) | Arch, structs, APIs, algos, build |

## AI Rules

1. Pure C11. No C++ features, no ext deps.
2. Read REQUIREMENTS.md + DESIGN.md before impl.
3. `Ms` types, `ms_` fns, `MS_` macros.
4. `ms_xxx_init()` ↔ `ms_xxx_free()`. GC via `ms_alloc_object()`.
5. `MsResult` + line/col. Output params for success.
6. `platform.h` abstraction. `#ifdef` in platform layer only.
7. Tests: `tests/unit/` + Maple scripts.
8. 10 phases → REQUIREMENTS.md §4.
9. Gitmoji commits, EN msgs.
10. `git` cmd → `git add` all under `mslang4/`, gitmoji msg, commit. No push.
