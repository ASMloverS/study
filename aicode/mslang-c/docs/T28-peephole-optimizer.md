# Task 28: Peephole Optimizer

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement 5-pass peephole optimizer on compiled bytecode, with NOP compaction and jump fixup.
**Dependencies:** T12, T13
**Produces:** Compiled bytecode automatically optimized; before/after comparison verifiable

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/optimize.h` | Optimizer API |
| Create | `src/optimize.c` | 5-pass peephole optimizer + NOP compaction |
| Modify | `src/compiler.c` | Call peephole after compilation completes |
| Create | `tests/unit/test_optimize.c` | Before/after optimization validation |

## Key Data Structures / API

```c
// include/ms/optimize.h
#pragma once
#include "ms/chunk.h"

// Perform peephole optimization on a chunk (in-place)
void ms_peephole_optimize(MsChunk* chunk);
```

## Implementation Notes

### Pass 1: Redundant MOVE Elimination

`MOVE R(A), R(A)` → `NOP` (same destination and source)

```c
for (int i = 0; i < chunk->code_count; i++) {
    MsInstruction inst = chunk->code[i];
    if (MS_GET_OP(inst) == MS_OP_MOVE && MS_GET_A(inst) == MS_GET_B(inst)) {
        chunk->code[i] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
    }
}
```

### Pass 2: LOADNIL Merging

Consecutive `LOADNIL R(n)` + `LOADNIL R(n+1)` → merged into `LOADNIL R(n), R(n+1)` (range nil load).

```c
for (int i = 0; i < chunk->code_count - 1; i++) {
    if (MS_GET_OP(chunk->code[i]) == MS_OP_LOADNIL &&
        MS_GET_OP(chunk->code[i+1]) == MS_OP_LOADNIL) {
        int a1 = MS_GET_A(chunk->code[i]), b1 = MS_GET_B(chunk->code[i]);
        int a2 = MS_GET_A(chunk->code[i+1]), b2 = MS_GET_B(chunk->code[i+1]);
        if (a2 == b1 + 1) {
            chunk->code[i] = ms_enc_ABC(MS_OP_LOADNIL, a1, b2, 0);
            chunk->code[i+1] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
        }
    }
}
```

### Pass 3: Dead Code Elimination

Instructions after `RETURN` or `THROW` (not jump targets) are marked NOP.

First build the set of jump targets:
```c
bool* is_jump_target = calloc(chunk->code_count, sizeof(bool));
for (int i = 0; i < chunk->code_count; i++) {
    int op = MS_GET_OP(chunk->code[i]);
    if (op == MS_OP_JMP) {
        int target = i + 1 + MS_GET_sBx(chunk->code[i]);
        if (target >= 0 && target < chunk->code_count)
            is_jump_target[target] = true;
    }
    // same for TRY handler offsets
}
// Scan: after RETURN/THROW, until the next jump target, mark as NOP
```

### Pass 4: LOADK + NEG Folding

```
LOADK R(A) K(n)   ; n is a numeric constant
NEG   R(A) R(A)
```
→ negate K(n) in-place, remove NEG:
```c
if (MS_GET_OP(inst) == MS_OP_LOADK && i + 1 < count) {
    MsInstruction next = chunk->code[i+1];
    if (MS_GET_OP(next) == MS_OP_NEG &&
        MS_GET_A(next) == MS_GET_A(inst) &&
        MS_GET_B(next) == MS_GET_A(inst)) {
        int kidx = MS_GET_Bx(inst);
        MsValue val = chunk->constants.data[kidx];
        if (MS_IS_NUMBER(val)) {
            chunk->constants.data[kidx] = MS_NUMBER_VAL(-MS_AS_NUMBER(val));
            chunk->code[i+1] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
        } else if (MS_IS_INT(val)) {
            chunk->constants.data[kidx] = MS_INT_VAL(-MS_AS_INT(val));
            chunk->code[i+1] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
        }
    }
}
```

### Pass 5: MOVE + RETURN Tail Merging

```
MOVE   R(A), R(B)
RETURN R(A), 2
```
→
```
RETURN R(B), 2
NOP
```

### NOP Compaction + Jump Fixup

After all passes, remove NOPs and fix jump offsets:
```c
static void compact_nops(MsChunk* chunk) {
    // 1. Build offset_map[old_idx] = new_idx
    int* offset_map = malloc(sizeof(int) * chunk->code_count);
    int new_count = 0;
    for (int i = 0; i < chunk->code_count; i++) {
        offset_map[i] = new_count;
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP) new_count++;
    }
    // 2. Fix all JMP/TRY offsets
    for (int i = 0; i < chunk->code_count; i++) {
        int op = MS_GET_OP(chunk->code[i]);
        if (op == MS_OP_JMP) {
            int old_target = i + 1 + MS_GET_sBx(chunk->code[i]);
            int new_sBx = offset_map[old_target] - offset_map[i] - 1;
            chunk->code[i] = ms_enc_AsBx(MS_OP_JMP, MS_GET_A(chunk->code[i]), new_sBx);
        }
    }
    // 3. Compact: remove NOPs
    new_count = 0;
    for (int i = 0; i < chunk->code_count; i++) {
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP)
            chunk->code[new_count++] = chunk->code[i];
    }
    chunk->code_count = new_count;
    // 4. Sync SourceRun line info (update count fields)
}
```

## C Unit Tests

```c
// tests/unit/test_optimize.c
#include "test_assert.h"
#include "ms/optimize.h"
#include "ms/chunk.h"

static void test_redundant_move(void) {
    MsChunk c; ms_chunk_init(&c);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_MOVE, 3, 3, 0), 1, 1);  // MOVE R3 R3
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 1, 0), 1, 5);
    ms_peephole_optimize(&c);
    // MOVE should be eliminated; only RETURN remains
    TEST_ASSERT_EQ(c.code_count, 1);
    TEST_ASSERT_EQ(MS_GET_OP(c.code[0]), MS_OP_RETURN);
    ms_chunk_free(&c);
}

static void test_loadk_neg_fold(void) {
    MsChunk c; ms_chunk_init(&c);
    int k = ms_chunk_add_constant(&c, MS_INT_VAL(5));
    ms_chunk_write(&c, ms_enc_ABx(MS_OP_LOADK, 0, k), 1, 1);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_NEG, 0, 0, 0), 1, 5);
    ms_chunk_write(&c, ms_enc_ABC(MS_OP_RETURN, 0, 2, 0), 1, 9);
    ms_peephole_optimize(&c);
    // NEG should be folded; constant should be -5
    TEST_ASSERT(MS_AS_INT(c.constants.data[k]) == -5);
    TEST_ASSERT_EQ(c.code_count, 2); // LOADK + RETURN
    ms_chunk_free(&c);
}

int main(void) {
    test_redundant_move();
    test_loadk_neg_fold();
    printf("test_optimize: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/optimize_verify.ms
// Optimization must not alter semantics
fun test() {
  var a = nil
  var b = nil
  var c = nil
  return a
}
print(test())
// expect: nil

fun negation() {
  return -42
}
print(negation())
// expect: -42

fun dead_code() {
  return 1
  print("unreachable")
}
print(dead_code())
// expect: 1

// Complex function verifies correct semantics after optimization
fun fib(n) {
  if (n <= 1) return n
  return fib(n-1) + fib(n-2)
}
print(fib(10))
// expect: 55
```
