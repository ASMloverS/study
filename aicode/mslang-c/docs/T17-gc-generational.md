# Task 17: GC — Generational Collection

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Extend GC to generational (young/old), write barriers, remembered set, ObjectPool slab allocator.
**Dependencies:** T16
**Produces:** 高效分代 GC; 短命对象由 minor GC 快速回收

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm_gc.c` | minor/major GC, promotion, remembered set |
| Modify | `src/memory.c` | write_barrier, ObjectPool |
| Modify | `include/ms/memory.h` | write_barrier API, MsObjectPool |
| Modify | `include/ms/vm.h` | 分代 GC 字段 |
| Create | `tests/unit/test_gc_gen.c` | 分代 GC 测试 |

## Key Data Structures / API

```c
// include/ms/memory.h 追加
typedef struct MsPoolSlab {
    struct MsPoolSlab* next;
    int used;
    char data[];  // FAM: obj_size * MS_POOL_SLAB_SIZE
} MsPoolSlab;

typedef struct {
    MsPoolSlab* slabs;
    void* free_list;
    size_t obj_size;
} MsObjectPool;

void  ms_pool_init(MsObjectPool* pool, size_t obj_size);
void* ms_pool_alloc(MsObjectPool* pool);
void  ms_pool_free_obj(MsObjectPool* pool, void* ptr);
void  ms_pool_destroy(MsObjectPool* pool);

// Write barrier: call when old-gen object stores young-gen reference
void ms_write_barrier(MsVM* vm, MsObject* owner, MsValue val);
```

```c
// include/ms/vm.h 追加字段
MsObject* young_objects;     // young 代链表
MsObject* old_objects;       // old 代链表
MsObject** remembered_set;   // old 对象引用了 young 对象
int remembered_count;
int remembered_capacity;
size_t young_bytes;          // nursery 已分配字节数
MsObjectPool upvalue_pool;   // ObjUpvalue 池
MsObjectPool bound_pool;     // ObjBoundMethod 池 (T18)
```

## Implementation Notes

### 分配策略

新对象进入 young 代: `obj->generation = 0; obj->age = 0;` 挂到 `vm->young_objects`.

### Minor GC (triggered when young_bytes > MS_GC_NURSERY_SIZE)

1. mark_roots (同 T16)
2. mark remembered_set 中的所有对象
3. trace (仅 young 代中已标记的灰色对象)
4. sweep young_objects:
   - 未标记 → 释放
   - 已标记 → `age++`; 若 `age >= MS_GC_PROMOTE_AGE`: 移到 old_objects, generation=1
5. 清空 remembered_set
6. 重置 young_bytes = 0

### Major GC (触发条件: 多次 minor 后 old 代增长过快)

完整标记-清扫: sweep 两条链表 (young + old).

### Write Barrier

```c
void ms_write_barrier(MsVM* vm, MsObject* owner, MsValue val) {
    if (owner->generation != 1) return;          // owner must be old
    if (!MS_IS_OBJECT(val)) return;
    MsObject* child = MS_AS_OBJECT(val);
    if (child->generation != 0) return;           // child must be young
    if (owner->in_remembered_set) return;
    owner->in_remembered_set = true;
    MS_ARRAY_PUSH(/*remembered_set dynamic array*/);
}
```

需在以下写操作后调用 write_barrier:
- SETUPVAL: `ms_write_barrier(vm, (MsObject*)frame->closure, R(A))`
- SETPROP: `ms_write_barrier(vm, (MsObject*)instance, value)`
- SETGLOBAL: 全局表的 value 变更 (可简化: 全局表不做分代, 总是 root)

### ObjectPool

Slab 分配器用于频繁分配/释放的小对象 (ObjUpvalue, ObjBoundMethod):
- 每个 slab 容纳 64 个对象
- free_list: 已释放对象的单链表 (在对象内存中存放 next 指针)
- alloc: 从 free_list 取; 空则分配新 slab
- free: 归还到 free_list

## C Unit Tests

```c
// tests/unit/test_gc_gen.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_young_gen_collection(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var total = 0\n"
        "var i = 0\n"
        "while (i < 5000) {\n"
        "  var temp = \"x\"\n"
        "  total = total + 1\n"
        "  i = i + 1\n"
        "}\n"
        "print(total)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_young_gen_collection();
    printf("test_gc_gen: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/gc_generational.ms
// 大量短命对象 (nursery 压力)
fun stress() {
  var total = 0
  var i = 0
  while (i < 10000) {
    var s = "item"
    total = total + 1
    i = i + 1
  }
  return total
}
print(stress())
// expect: 10000

// tests/fixtures/gc_promotion.ms
// 长命对象应晋升到 old 代
var long_lived = "permanent"
var i = 0
while (i < 5000) {
  var _ = "temp"
  i = i + 1
}
print(long_lived)
// expect: permanent

// tests/fixtures/gc_closure_survival.ms
fun make() {
  var x = "captured"
  return fun() { return x }
}
var f = make()
// 大量分配触发 GC
var i = 0
while (i < 5000) { var _ = "gc_pressure"; i = i + 1 }
print(f())
// expect: captured
```
