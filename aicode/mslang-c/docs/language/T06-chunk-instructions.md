# Task 06: Chunk and Instruction Encoding

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement 32-bit instruction format (iABC/iABx/iAsBx), 70+ opcodes, `MsChunk` w/ constant pool + RLE source info.
**Deps:** T02
**Produces:** Instructions encodable/decodable; chunk stores bytecode + constant pool; RLE line compression

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/opcode.h` | `MsOpCode` enum, instruction encode/decode |
| Create | `include/ms/chunk.h` | `MsChunk` struct |
| Create | `src/chunk.c` | Chunk op impls |
| Create | `tests/unit/test_chunk.c` | Instruction encode/decode + chunk tests |

## Key Data Structures / API

```c
// include/ms/opcode.h — 32-bit instruction layout (Lua 5 style)
// iABC:  [B:8][C:8][A:8][Op:8]   — binary ops
// iABx:  [Bx:16][A:8][Op:8]      — constants, globals
// iAsBx: [sBx:16][A:8][Op:8]     — signed jumps (biased by 32767)
// RK: field value < 128 = register, >= 128 = constant K(val-128)

typedef uint32_t MsInstruction;

// Decode macros
#define MS_GET_OP(i)  ((int)((i) & 0xFF))
#define MS_GET_A(i)   ((int)(((i) >> 8)  & 0xFF))
#define MS_GET_B(i)   ((int)(((i) >> 24) & 0xFF))
#define MS_GET_C(i)   ((int)(((i) >> 16) & 0xFF))
#define MS_GET_Bx(i)  ((int)(((i) >> 16) & 0xFFFF))
#define MS_GET_sBx(i) (MS_GET_Bx(i) - 32767)

// RK helpers
#define MS_RK_IS_K(rk)  ((rk) >= 128)
#define MS_RK_TO_K(rk)  ((rk) - 128)
#define MS_K_TO_RK(k)   ((k) + 128)
#define MS_sBx_BIAS      32767

// Encode functions
static inline MsInstruction ms_enc_ABC(int op, int A, int B, int C) {
    return (MsInstruction)((ms_u32)op | ((ms_u32)A << 8) |
           ((ms_u32)C << 16) | ((ms_u32)B << 24));
}
static inline MsInstruction ms_enc_ABx(int op, int A, int Bx) {
    return (MsInstruction)((ms_u32)op | ((ms_u32)A << 8) | ((ms_u32)Bx << 16));
}
static inline MsInstruction ms_enc_AsBx(int op, int A, int sBx) {
    return ms_enc_ABx(op, A, sBx + MS_sBx_BIAS);
}

typedef enum {
    MS_OP_LOADK, MS_OP_LOADNIL, MS_OP_LOADTRUE, MS_OP_LOADFALSE, MS_OP_MOVE,
    MS_OP_GETGLOBAL, MS_OP_SETGLOBAL, MS_OP_DEFGLOBAL,
    MS_OP_GETUPVAL, MS_OP_SETUPVAL,
    MS_OP_GETPROP, MS_OP_SETPROP, MS_OP_GETSUPER, MS_OP_EXTRAARG,
    MS_OP_ADD, MS_OP_SUB, MS_OP_MUL, MS_OP_DIV, MS_OP_MOD,
    MS_OP_EQ, MS_OP_LT, MS_OP_LE,
    MS_OP_NEG, MS_OP_NOT, MS_OP_STR,
    MS_OP_BAND, MS_OP_BOR, MS_OP_BXOR, MS_OP_BNOT, MS_OP_SHL, MS_OP_SHR,
    MS_OP_JMP, MS_OP_TEST, MS_OP_TESTSET,
    MS_OP_CALL, MS_OP_INVOKE, MS_OP_SUPERINV, MS_OP_RETURN,
    MS_OP_CLOSURE, MS_OP_CLOSE,
    MS_OP_CLASS, MS_OP_INHERIT, MS_OP_METHOD, MS_OP_STATICMETH,
    MS_OP_GETTER, MS_OP_SETTER, MS_OP_ABSTMETH,
    MS_OP_NEWLIST, MS_OP_NEWMAP, MS_OP_NEWTUPLE, MS_OP_GETIDX, MS_OP_SETIDX,
    MS_OP_IMPORT, MS_OP_IMPFROM, MS_OP_IMPALIAS,
    MS_OP_FORITER,
    MS_OP_THROW, MS_OP_TRY, MS_OP_ENDTRY,
    MS_OP_DEFER,
    MS_OP_YIELD, MS_OP_RESUME,
    // Quickened (runtime-specialized)
    MS_OP_ADD_II, MS_OP_ADD_FF, MS_OP_ADD_SS,
    MS_OP_SUB_II, MS_OP_SUB_FF,
    MS_OP_MUL_II, MS_OP_MUL_FF,
    MS_OP_DIV_FF,
    MS_OP_LT_II, MS_OP_LT_FF,
    MS_OP_EQ_II,
    MS_OP_NOP,
    MS_OP_COUNT,
} MsOpCode;

// Opcode name lookup
const char* ms_opcode_name(MsOpCode op);
```

```c
// include/ms/chunk.h
typedef struct {
    int line;
    int column;
    int count;  // consecutive instructions sharing this location
} MsSourceRun;

typedef struct MsChunk {
    MsInstruction* code;
    int code_count;
    int code_capacity;
    MsValueArray constants;
    MsSourceRun* lines;
    int line_count;
    int line_capacity;
} MsChunk;

void ms_chunk_init(MsChunk* c);
void ms_chunk_free(MsChunk* c);
void ms_chunk_write(MsChunk* c, MsInstruction instr, int line, int col);
int  ms_chunk_add_constant(MsChunk* c, MsValue val);
int  ms_chunk_get_line(MsChunk* c, int offset);
```

## Impl Notes

- **RLE line info**: `ms_chunk_write` → last `SourceRun` same line/col? → `count++`. Else append new `SourceRun`
- **`ms_chunk_get_line`**: iterate `SourceRun` array, accumulate `count` until offset covered
- **`ms_opcode_name`**: static `const char*` array indexed by opcode value

## C Unit Tests

```c
// tests/unit/test_chunk.c
#include "test_assert.h"
#include "ms/chunk.h"

static void test_encode_decode_ABC(void) {
    MsInstruction i = ms_enc_ABC(MS_OP_ADD, 3, 128, 5);
    TEST_ASSERT_EQ(MS_GET_OP(i), MS_OP_ADD);
    TEST_ASSERT_EQ(MS_GET_A(i), 3);
    TEST_ASSERT_EQ(MS_GET_B(i), 128);  // RK constant K(0)
    TEST_ASSERT_EQ(MS_GET_C(i), 5);
    TEST_ASSERT(MS_RK_IS_K(128));
    TEST_ASSERT_EQ(MS_RK_TO_K(128), 0);
}

static void test_encode_decode_AsBx(void) {
    MsInstruction i = ms_enc_AsBx(MS_OP_JMP, 0, -100);
    TEST_ASSERT_EQ(MS_GET_OP(i), MS_OP_JMP);
    TEST_ASSERT_EQ(MS_GET_sBx(i), -100);
    // Also test positive
    MsInstruction j = ms_enc_AsBx(MS_OP_JMP, 0, 200);
    TEST_ASSERT_EQ(MS_GET_sBx(j), 200);
    // Zero
    MsInstruction k = ms_enc_AsBx(MS_OP_JMP, 0, 0);
    TEST_ASSERT_EQ(MS_GET_sBx(k), 0);
}

static void test_chunk_write_and_constants(void) {
    MsChunk c;
    ms_chunk_init(&c);
    int idx = ms_chunk_add_constant(&c, MS_INT_VAL(42));
    MsInstruction instr = ms_enc_ABx(MS_OP_LOADK, 0, idx);
    ms_chunk_write(&c, instr, 1, 1);
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_LOADK);
    TEST_ASSERT(MS_AS_INT(c.constants.data[idx]) == 42);
    ms_chunk_free(&c);
}

static void test_rle_lines(void) {
    MsChunk c;
    ms_chunk_init(&c);
    // 3 instructions on line 10
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, 0), 10, 1);
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 1, 0), 10, 5);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_ADD, 2, 0, 1), 10, 9);
    // 1 instruction on line 11
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 2, 2, 0), 11, 1);
    TEST_ASSERT_EQ(c.line_count, 2);  // 2 SourceRuns
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 0), 10);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 2), 10);
    TEST_ASSERT_EQ(ms_chunk_get_line(&c, 3), 11);
    ms_chunk_free(&c);
}

int main(void) {
    test_encode_decode_ABC();
    test_encode_decode_AsBx();
    test_chunk_write_and_constants();
    test_rle_lines();
    printf("test_chunk: all passed\n");
    return 0;
}
```

## .ms Integration Tests

None — instruction encoding is internal; tested indirectly via compiler + VM.
