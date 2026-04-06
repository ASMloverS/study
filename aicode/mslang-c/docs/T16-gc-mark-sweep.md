# Task 16: GC — Basic Mark-and-Sweep

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement tri-color mark-and-sweep garbage collector with root marking, gray stack tracing, and sweep.
**Dependencies:** T14
**Produces:** 自动内存回收; 长时间运行的脚本不会耗尽内存

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `src/vm_gc.c` | GC mark/trace/sweep 实现 |
| Modify | `src/memory.c` | ms_reallocate 触发 GC |
| Modify | `include/ms/memory.h` | GC API: mark_object, mark_value, collect |
| Modify | `include/ms/vm.h` | 添加 gray_stack 等 GC 字段 |
| Create | `tests/unit/test_gc.c` | GC 测试 |

## Key Data Structures / API

```c
// 追加到 include/ms/memory.h
void ms_gc_collect(MsVM* vm);
void ms_mark_object(MsVM* vm, MsObject* obj);
void ms_mark_value(MsVM* vm, MsValue val);
void ms_mark_table(MsVM* vm, MsTable* table);
```

MsVM 需要的 GC 字段 (已在 T13 预声明):
```c
MsObject** gray_stack;
int gray_count;
int gray_capacity;
```

## Implementation Notes

### 三色标记-清扫

```
Phase 1: mark_roots   — 白色 → 灰色 (push to gray_stack)
Phase 2: trace         — 灰色 → 黑色 (pop from gray_stack, trace children)
Phase 3: sweep         — 删除仍为白色的对象
```

### 根标记

```c
static void mark_roots(MsVM* vm) {
    // 1. 栈值
    for (MsValue* slot = vm->stack; slot < vm->stack_top; slot++)
        ms_mark_value(vm, *slot);
    // 2. 调用帧闭包
    for (int i = 0; i < vm->frame_count; i++)
        ms_mark_object(vm, (MsObject*)vm->frames[i].closure);
    // 3. Open upvalues
    for (MsObjUpvalue* uv = vm->open_upvalues; uv; uv = uv->next)
        ms_mark_object(vm, (MsObject*)uv);
    // 4. 全局变量表
    ms_mark_table(vm, &vm->globals);
    // 5. 编译器 (编译期间 GC 安全)
    if (vm->compiler) mark_compiler_roots(vm);
    // 6. 特殊字符串
    if (vm->init_string) ms_mark_object(vm, (MsObject*)vm->init_string);
}
```

### ms_mark_object

```c
void ms_mark_object(MsVM* vm, MsObject* obj) {
    if (obj == NULL || obj->is_marked) return;
    obj->is_marked = true;
    // Push to gray stack (使用 raw realloc 避免递归触发 GC)
    if (vm->gray_count >= vm->gray_capacity) {
        vm->gray_capacity = vm->gray_capacity < 8 ? 8 : vm->gray_capacity * 2;
        vm->gray_stack = realloc(vm->gray_stack,
                                  sizeof(MsObject*) * (size_t)vm->gray_capacity);
    }
    vm->gray_stack[vm->gray_count++] = obj;
}
```

### blacken_object (trace children)

按对象类型 trace 引用:
- STRING: 无子引用
- FUNCTION: trace name, trace 常量池中的对象
- CLOSURE: trace function, trace 每个 upvalue
- UPVALUE: trace closed 值
- NATIVE: trace name

### sweep

```c
static void sweep(MsVM* vm) {
    MsObject** obj = &vm->objects;
    while (*obj) {
        if ((*obj)->is_marked) {
            (*obj)->is_marked = false;  // 为下轮重置
            obj = &(*obj)->next;
        } else {
            MsObject* dead = *obj;
            *obj = dead->next;
            ms_object_free(vm, dead);
        }
    }
}
```

### GC 触发

在 `ms_reallocate` 中: `if (vm->bytes_allocated > vm->next_gc) ms_gc_collect(vm);`
收集后: `vm->next_gc = vm->bytes_allocated * 2;`
初始 next_gc = 1024 * 1024 (1MB).

### 驻留表清理

sweep 前调用 `ms_table_remove_white(&vm->strings)` — 移除未标记的已驻留字符串, 防止悬垂指针.

## C Unit Tests

```c
// tests/unit/test_gc.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_gc_runs_without_crash(void) {
    MsVM vm;
    ms_vm_init(&vm);
    ms_vm_interpret(&vm,
        "var i = 0\n"
        "while (i < 500) {\n"
        "  var s = \"temp\" + \"garbage\"\n"
        "  i = i + 1\n"
        "}", "<test>");
    ms_vm_free(&vm);
}

static void test_gc_preserves_live_objects(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var kept = \"alive\"\n"
        "var i = 0\n"
        "while (i < 500) { var _ = \"trash\"\n i = i + 1 }\n"
        "print(kept)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_gc_runs_without_crash();
    test_gc_preserves_live_objects();
    printf("test_gc: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/gc_basic.ms
fun make_garbage() {
  var i = 0
  while (i < 1000) {
    var s = "temp" + "garbage"
    i = i + 1
  }
  return "done"
}
print(make_garbage())
// expect: done

// tests/fixtures/gc_closures.ms
fun make_closure() {
  var data = "kept_alive"
  return fun() { return data }
}
var fn = make_closure()
// Force GC with allocations
var i = 0
while (i < 500) { var _ = "junk"; i = i + 1 }
print(fn())
// expect: kept_alive
```
