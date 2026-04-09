# Task 07: Disassembler

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement bytecode disassembler that prints human-readable instruction listings, essential for debugging compiler output.
**Dependencies:** T06
**Produces:** `ms_disasm_chunk` / `ms_disasm_instr` print disassembly for any chunk

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/debug.h` | Disassembler API |
| Create | `src/debug.c` | Disassembler implementation |
| Create | `tests/unit/test_debug.c` | Disassembly output validation |

## Key Data Structures / API

```c
// include/ms/debug.h
#pragma once
#include "ms/chunk.h"

// Print full chunk disassembly
void ms_disasm_chunk(const MsChunk* chunk, const char* name);
// Print one instruction; returns offset of next instruction
int  ms_disasm_instr(const MsChunk* chunk, int offset);
```

## Implementation Notes

Output format:
```
=== function_name ===
0000    1 LOADK      R(0)  K(0)    ; 42
0001    | ADD        R(2)  R(0) R(1)
0002    2 JMP        +5
0003    | CLOSURE    R(0)  proto[2]
```

- Column 1: instruction offset (4 digits, zero-padded)
- Column 2: line number (new line → number; same line → `|`)
- Column 3: opcode name (left-aligned, 12 chars)
- Remaining: operands by format

Operand formatting by instruction type:
- **iABC** (arithmetic/compare): `R(A)  RK(B) RK(C)` — if B/C ≥ 128 print `K(n)` and append `; value`
- **iABx** (`LOADK`/`GETGLOBAL`/`DEFGLOBAL`): `R(A)  K(Bx)` with appended `; value`
- **iAsBx** (`JMP`): `+sBx` or `-sBx`
- **`CLOSURE`**: `R(A)  proto[Bx]`
- **`CALL`**: `R(A)  args=B-1 rets=C-1`
- **`RETURN`**: `R(A)  count=B-1`

`ms_opcode_name(op)` (defined in T06) provides the opcode string.

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
    // Verify no crash; output goes to stdout
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

None — disassembler is a dev/debug tool, not exposed to the script layer.
