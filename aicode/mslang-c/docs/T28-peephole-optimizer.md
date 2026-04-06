# Task 28: Peephole Optimizer

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement 5-pass peephole optimizer on compiled bytecode, with NOP compaction and jump fixup.
**Dependencies:** T12, T13
**Produces:** 编译后字节码自动优化; 可验证前后对比

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/optimize.h` | 优化器 API |
| Create | `src/optimize.c` | 5 趟窥孔优化 + NOP 压缩 |
| Modify | `src/compiler.c` | 编译完成后调用 peephole |
| Create | `tests/unit/test_optimize.c` | 优化前后验证 |

## Key Data Structures / API

```c
// include/ms/optimize.h
#pragma once
#include "ms/chunk.h"

// 对 chunk 执行窥孔优化 (原地修改)
void ms_peephole_optimize(MsChunk* chunk);
```

## Implementation Notes

### Pass 1: 冗余 MOVE 消除

`MOVE R(A), R(A)` → `NOP` (目标和源相同)

```c
for (int i = 0; i < chunk->code_count; i++) {
    MsInstruction inst = chunk->code[i];
    if (MS_GET_OP(inst) == MS_OP_MOVE && MS_GET_A(inst) == MS_GET_B(inst)) {
        chunk->code[i] = ms_enc_ABC(MS_OP_NOP, 0, 0, 0);
    }
}
```

### Pass 2: LOADNIL 合并

连续的 `LOADNIL R(n), R(n)` + `LOADNIL R(n+1), R(n+1)` → 合并为 `LOADNIL R(n), R(n+1)` (范围 nil 加载).

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

### Pass 3: 死代码消除

`RETURN` 或 `THROW` 之后的指令 (非跳转目标) 标记为 NOP.

需先构建跳转目标集合:
```c
bool* is_jump_target = calloc(chunk->code_count, sizeof(bool));
for (int i = 0; i < chunk->code_count; i++) {
    int op = MS_GET_OP(chunk->code[i]);
    if (op == MS_OP_JMP) {
        int target = i + 1 + MS_GET_sBx(chunk->code[i]);
        if (target >= 0 && target < chunk->code_count)
            is_jump_target[target] = true;
    }
    // 同理处理 TRY 的 handler offset
}
// 扫描: RETURN/THROW 之后, 直到下一个 jump target, 标记为 NOP
```

### Pass 4: LOADK + NEG 折叠

```
LOADK R(A) K(n)   ; n 是数值常量
NEG   R(A) R(A)
```
→ 将 K(n) 改为 -K(n), 移除 NEG:
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

### Pass 5: MOVE + RETURN 尾合并

```
MOVE   R(A), R(B)
RETURN R(A), 2
```
→
```
RETURN R(B), 2
NOP
```

### NOP 压缩 + 跳转修复

所有 pass 完成后, 移除 NOP 并修复跳转偏移:
```c
static void compact_nops(MsChunk* chunk) {
    // 1. 构建 offset_map[old_idx] = new_idx
    int* offset_map = malloc(sizeof(int) * chunk->code_count);
    int new_count = 0;
    for (int i = 0; i < chunk->code_count; i++) {
        offset_map[i] = new_count;
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP) new_count++;
    }
    // 2. 修复所有 JMP/TRY 的偏移
    for (int i = 0; i < chunk->code_count; i++) {
        int op = MS_GET_OP(chunk->code[i]);
        if (op == MS_OP_JMP) {
            int old_target = i + 1 + MS_GET_sBx(chunk->code[i]);
            int new_sBx = offset_map[old_target] - offset_map[i] - 1;
            chunk->code[i] = ms_enc_AsBx(MS_OP_JMP, MS_GET_A(chunk->code[i]), new_sBx);
        }
    }
    // 3. 压缩: 移除 NOP
    new_count = 0;
    for (int i = 0; i < chunk->code_count; i++) {
        if (MS_GET_OP(chunk->code[i]) != MS_OP_NOP)
            chunk->code[new_count++] = chunk->code[i];
    }
    chunk->code_count = new_count;
    // 4. 同步 SourceRun 行号 (需更新 count 字段)
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
// 优化不应改变语义
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

// 复杂函数验证优化后语义正确
fun fib(n) {
  if (n <= 1) return n
  return fib(n-1) + fib(n-2)
}
print(fib(10))
// expect: 55
```
