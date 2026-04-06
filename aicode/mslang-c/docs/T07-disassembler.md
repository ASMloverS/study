# Task 07: Disassembler

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement bytecode disassembler that prints human-readable instruction listings, essential for debugging compiler output.
**Dependencies:** T06
**Produces:** `ms_disasm_chunk` / `ms_disasm_instr` 可打印任意 Chunk 的反汇编输出

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/debug.h` | 反汇编器 API |
| Create | `src/debug.c` | 反汇编实现 |
| Create | `tests/unit/test_debug.c` | 反汇编输出验证 |

## Key Data Structures / API

```c
// include/ms/debug.h
#pragma once
#include "ms/chunk.h"

// 打印整个 Chunk 的反汇编
void ms_disasm_chunk(const MsChunk* chunk, const char* name);
// 打印单条指令, 返回下一条指令的 offset
int  ms_disasm_instr(const MsChunk* chunk, int offset);
```

## Implementation Notes

输出格式:
```
=== function_name ===
0000    1 LOADK      R(0)  K(0)    ; 42
0001    | ADD        R(2)  R(0) R(1)
0002    2 JMP        +5
0003    | CLOSURE    R(0)  proto[2]
```

- 第一列: 指令偏移 (4 位, 0-padded)
- 第二列: 行号 (新行显示数字, 同行显示 `|`)
- 第三列: 操作码名 (左对齐 12 字符)
- 后续: 按格式显示操作数

按操作码格式分类打印:
- **iABC** (算术/比较): `R(A)  RK(B) RK(C)` — 若 B/C >= 128 打印 `K(n)` 并追加 `; value`
- **iABx** (LOADK/GETGLOBAL/DEFGLOBAL): `R(A)  K(Bx)` 追加 `; value`
- **iAsBx** (JMP): `+sBx` 或 `-sBx`
- **CLOSURE**: `R(A)  proto[Bx]`
- **CALL**: `R(A)  args=B-1 rets=C-1`
- **RETURN**: `R(A)  count=B-1`

`ms_opcode_name(op)` 用于获取操作码字符串名, 在 T06 的 opcode.h/chunk.c 中已定义。

## C Unit Tests

```c
// tests/unit/test_debug.c
#include "test_assert.h"
#include "ms/debug.h"

static void test_disasm_basic(void) {
    MsChunk c;
    ms_chunk_init(&c);
    int k = ms_chunk_add_constant(&c, MS_INT_VAL(42));
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, k), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_ADD, 2, 0, 1), 1, 5);
    ms_chunk_write(&c, ms_enc_AsBx(MS_OP_JMP, 0, -2), 2, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 2, 5);
    // 验证不会崩溃, 并输出到 stdout
    ms_disasm_chunk(&c, "test");
    ms_chunk_free(&c);
}

static void test_disasm_offset_return(void) {
    MsChunk c;
    ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_LOADTRUE, 0, 0, 0), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 2, 0), 1, 5);
    int next = ms_disasm_instr(&c, 0);
    TEST_ASSERT_EQ(next, 1);
    next = ms_disasm_instr(&c, 1);
    TEST_ASSERT_EQ(next, 2);
    ms_chunk_free(&c);
}

int main(void) {
    test_disasm_basic();
    test_disasm_offset_return();
    printf("test_debug: all passed\n");
    return 0;
}
```

## .ms Integration Tests

无 — 反汇编器是开发/调试工具, 不直接暴露给脚本层。
