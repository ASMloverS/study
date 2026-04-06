# Task 30: Bytecode Serializer and Incremental GC

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement .msc binary serialization/deserialization with auto-caching, and incremental marking for major GC.
**Dependencies:** T17, T28
**Produces:** .msc 缓存加速重复执行; 增量 GC 减少暂停时间

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/serializer.h` | 序列化 API |
| Create | `src/serializer.c` | .msc 二进制格式读写 |
| Modify | `src/vm_gc.c` | 增量标记 |
| Modify | `include/ms/vm.h` | 增量 GC 状态 |
| Create | `tests/unit/test_serializer.c` | 序列化 roundtrip 测试 |

## Key Data Structures / API

```c
// include/ms/serializer.h
#pragma once
#include "ms/object.h"

// .msc 文件头 (16 bytes)
#define MS_MSC_MAGIC  "MSC\0"
#define MS_MSC_VERSION 1

typedef struct {
    char magic[4];     // "MSC\0"
    uint32_t version;  // 格式版本
    uint32_t flags;    // 保留
    uint32_t src_hash; // 源文件 FNV-1a hash (用于验证缓存有效性)
} MsMscHeader;

// 序列化 ObjFunction 到文件
bool ms_serialize(MsObjFunction* fn, const char* path, uint32_t src_hash);
// 反序列化; 返回 NULL 若文件不存在或 hash 不匹配
MsObjFunction* ms_deserialize(MsVM* vm, const char* path, uint32_t src_hash);

// 自动缓存包装
MsObjFunction* ms_compile_cached(MsVM* vm, const char* source,
                                   const char* src_path);
```

```c
// 增量 GC 状态 (追加到 MsVM)
typedef enum {
    MS_GC_IDLE,
    MS_GC_MARKING,
    MS_GC_SWEEPING,
} MsGcPhase;

// 追加到 MsVM:
MsGcPhase gc_phase;
MsObject* sweep_cursor;  // SWEEPING 阶段的当前位置
```

## Implementation Notes

### .msc 二进制格式

```
[Header: 16 bytes]
[FunctionCount: u32]
[Functions: serialized in DFS post-order]
  For each function:
    [name_length: u32] [name: bytes]
    [arity: u32] [min_arity: i32] [upvalue_count: u32]
    [max_stack_size: u32] [is_generator: u8]
    [code_count: u32] [code: u32 * code_count]
    [const_count: u32]
    For each constant:
      [type: u8]
      [data: varies by type]
        NIL: (nothing)
        BOOL: [u8]
        INT: [i64]
        NUMBER: [f64]
        STRING: [length: u32] [chars]
        FUNCTION: [index: u32]  (reference to already-serialized function)
    [line_count: u32]
    For each SourceRun:
      [line: u32] [column: u32] [count: u32]
```

### DFS 后序

内嵌函数先序列化 (post-order), 这样反序列化时读到 FUNCTION 常量引用时, 被引用的函数已经被创建.

### 自动缓存

```c
MsObjFunction* ms_compile_cached(MsVM* vm, const char* source, const char* src_path) {
    uint32_t src_hash = ms_fnv1a(source, strlen(source));
    // 构造 .msc 路径: src_path + ".msc" 或替换 .ms → .msc
    char msc_path[PATH_MAX];
    snprintf(msc_path, sizeof(msc_path), "%sc", src_path); // .ms → .msc

    // 尝试从缓存加载
    MsObjFunction* fn = ms_deserialize(vm, msc_path, src_hash);
    if (fn) return fn;

    // 缓存无效: 编译
    fn = ms_compile(vm, source, src_path, ...);
    if (fn) ms_serialize(fn, msc_path, src_hash);
    return fn;
}
```

### 增量标记

将 major GC 的标记阶段拆分为多个小步:

```c
void ms_gc_incremental_step(MsVM* vm) {
    switch (vm->gc_phase) {
    case MS_GC_IDLE:
        // 开始标记: mark roots
        mark_roots(vm);
        vm->gc_phase = MS_GC_MARKING;
        break;

    case MS_GC_MARKING:
        // 每次处理 MS_GC_INCR_WORK (64) 个灰色对象
        for (int i = 0; i < MS_GC_INCR_WORK && vm->gray_count > 0; i++) {
            MsObject* obj = vm->gray_stack[--vm->gray_count];
            blacken_object(vm, obj);
        }
        if (vm->gray_count == 0) {
            // 标记完成, 进入清扫
            ms_table_remove_white(&vm->strings);
            vm->sweep_cursor = vm->objects;
            vm->gc_phase = MS_GC_SWEEPING;
        }
        break;

    case MS_GC_SWEEPING:
        // 每次清扫 MS_GC_INCR_WORK 个对象
        for (int i = 0; i < MS_GC_INCR_WORK && vm->sweep_cursor; i++) {
            MsObject* obj = vm->sweep_cursor;
            vm->sweep_cursor = obj->next;
            if (!obj->is_marked) {
                // 从链表移除并释放
                // (需要 prev 指针或使用 pointer-to-pointer 技巧)
            } else {
                obj->is_marked = false;
            }
        }
        if (!vm->sweep_cursor) {
            vm->gc_phase = MS_GC_IDLE;
            vm->next_gc = vm->bytes_allocated * 2;
        }
        break;
    }
}
```

在 `ms_reallocate` 中: 若 `gc_phase != IDLE`, 调用 `ms_gc_incremental_step`.
若 `gc_phase == IDLE && bytes_allocated > threshold`, 启动增量 GC.

### 补充特性 (Phase 15 剩余)

同时在此任务实现:
- **运算符重载**: `__add`, `__sub`, `__mul`, `__div`, `__mod`, `__eq`, `__lt`, `__gt`, `__str` — 在算术/比较操作中, 若操作数是 ObjInstance, 查找对应的 dunder 方法
- **枚举声明**: `enum Color { Red, Green, Blue }` → 编译为带整数值的类
- **三元运算符**: `cond ? then : else` → TEST + JMP
- **for-in 迭代**: 完善 FORITER 以支持 list/map/range 迭代

## C Unit Tests

```c
// tests/unit/test_serializer.c
#include "test_assert.h"
#include "ms/serializer.h"
#include "ms/compiler.h"
#include "ms/vm.h"

static void test_roundtrip(void) {
    MsVM vm; ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    const char* src = "fun add(a,b) { return a + b }\nprint(add(1,2))";
    MsObjFunction* fn = ms_compile(&vm, src, "test.ms", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);

    uint32_t hash = ms_fnv1a(src, (int)strlen(src));
    TEST_ASSERT(ms_serialize(fn, "_test.msc", hash));

    MsObjFunction* fn2 = ms_deserialize(&vm, "_test.msc", hash);
    TEST_ASSERT(fn2 != NULL);
    TEST_ASSERT_EQ(fn2->arity, fn->arity);
    TEST_ASSERT_EQ(fn2->chunk.code_count, fn->chunk.code_count);

    // Wrong hash → returns NULL
    MsObjFunction* fn3 = ms_deserialize(&vm, "_test.msc", hash + 1);
    TEST_ASSERT(fn3 == NULL);

    remove("_test.msc");
    ms_vm_free(&vm);
}

int main(void) {
    test_roundtrip();
    printf("test_serializer: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/operator_overload.ms
class Vec {
  init(x, y) {
    this.x = x
    this.y = y
  }
  __add(other) {
    return Vec(this.x + other.x, this.y + other.y)
  }
  __str() {
    return "(" + this.x + ", " + this.y + ")"
  }
}
var a = Vec(1, 2)
var b = Vec(3, 4)
var c = a + b
print(c)
// expect: (4, 6)

// tests/fixtures/enum.ms
enum Color { Red, Green, Blue }
print(Color.Red)
// expect: 0
print(Color.Green)
// expect: 1
print(Color.Blue)
// expect: 2

// tests/fixtures/ternary.ms
var x = 10
print(x > 5 ? "big" : "small")
// expect: big
print(x < 5 ? "big" : "small")
// expect: small

// tests/fixtures/for_in.ms
for (var item in [10, 20, 30]) {
  print(item)
}
// expect: 10
// expect: 20
// expect: 30

for (var key in {"a": 1, "b": 2}) {
  print(key)
}
// expect: a
// expect: b

// tests/fixtures/serializer_cache.ms
// 此测试验证 .msc 缓存: 运行两次, 第二次应从缓存加载
// (需要测试 runner 支持, 此处仅验证语义正确性)
fun compute() {
  var sum = 0
  for (var i = 0; i < 100; i = i + 1) {
    sum = sum + i
  }
  return sum
}
print(compute())
// expect: 4950
```
