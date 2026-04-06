# Task 20: OOP — Shapes (Hidden Classes) and Inline Cache

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement Shape-based field layout for ObjInstance (O(1) property access) and polymorphic inline caching for GETPROP/SETPROP/INVOKE.
**Dependencies:** T19
**Produces:** 属性访问 O(1) by slot index; 多态内联缓存 (PIC)

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/shape.h` | MsShape, MsSmallMap, MsInlineCache |
| Create | `src/shape.c` | Shape 转换, SmallMap |
| Modify | `include/ms/object.h` | ObjInstance 改用 Shape |
| Modify | `src/object.c` | 实例字段按 Shape slot 存取 |
| Modify | `src/vm.c` | GETPROP/SETPROP 使用 IC |
| Create | `tests/unit/test_shapes.c` | Shape + IC 测试 |

## Key Data Structures / API

```c
// include/ms/shape.h
#define MS_SMALL_MAP_INLINE 8

typedef struct {
    MsObjString* key;
    uint32_t value;
} MsSmallEntry;

// 小型线性映射, 溢出到 MsTable
typedef struct {
    MsSmallEntry data[MS_SMALL_MAP_INLINE];
    int count;
    MsTable* overflow;  // NULL until spill
} MsSmallMap;

typedef struct MsShape {
    uint32_t id;
    MsSmallMap slots;       // property name → slot index
    uint32_t slot_count;
    // Transitions: property name → child Shape
    MsSmallEntry trans_data[MS_SMALL_MAP_INLINE];
    int trans_count;
    MsTable* trans_overflow;
} MsShape;

MsShape* ms_shape_new(MsVM* vm);
// Find or create transition: adding property `name` to `shape`
MsShape* ms_shape_transition(MsVM* vm, MsShape* shape, MsObjString* name);
// Find slot index for property name; returns -1 if not found
int ms_shape_find_slot(MsShape* shape, MsObjString* name);
```

```c
// ObjInstance 改为 Shape-based 布局
typedef struct {
    MsObject obj;
    MsObjClass* klass;
    MsShape* shape;
    MsValue inline_fields[MS_SBO_FIELDS];  // SBO: 8 内联
    MsValue* overflow_fields;               // NULL 直到超过 8 个
    int field_count;
} MsObjInstance;
```

```c
// Inline Cache
typedef enum { MS_IC_NONE, MS_IC_FIELD, MS_IC_METHOD, MS_IC_GETTER, MS_IC_SETTER } MsICKind;

typedef struct {
    MsObjClass* klass;
    MsICKind kind;
    MsValue cached;
    uint32_t shape_id;
    uint32_t slot_index;
} MsICEntry;

typedef struct MsInlineCache {
    MsICEntry entries[MS_IC_PIC_SIZE];
    uint8_t count;
    bool megamorphic;
} MsInlineCache;
```

## Implementation Notes

### Shape 转换

每个 class 有一个 root Shape (空). 当实例添加属性时:
1. 在当前 shape 的 transitions 中查找 name
2. 找到 → 使用现有 child shape
3. 未找到 → 创建新 shape, slot_count++, 记录 name→slot, 添加 transition

相同属性添加顺序的实例共享同一 Shape 链.

### SBO (Small Buffer Optimization)

前 8 个字段存储在 `inline_fields[]` 中 (无额外分配). 超过 8 个时分配 `overflow_fields`:
```c
static MsValue* get_field_ptr(MsObjInstance* inst, int slot) {
    if (slot < MS_SBO_FIELDS) return &inst->inline_fields[slot];
    return &inst->overflow_fields[slot - MS_SBO_FIELDS];
}
```

### IC 使用流程

GETPROP 后紧跟 EXTRAARG Bx (Bx = IC slot index in function->ic[]):
```c
case MS_OP_GETPROP: {
    MsInstruction extra = READ_INSTR(); // EXTRAARG
    int ic_idx = MS_GET_Bx(extra);
    MsInlineCache* ic = &frame->closure->function->ic[ic_idx];
    MsObjInstance* inst = ...;
    // Check IC
    for (int i = 0; i < ic->count; i++) {
        if (ic->entries[i].shape_id == inst->shape->id) {
            // IC hit: direct slot access
            R(A) = *get_field_ptr(inst, ic->entries[i].slot_index);
            goto done;
        }
    }
    // IC miss: slow path + update IC
    ...
}
```

IC 满 (4 entries) → `megamorphic = true`, 跳过 IC 直接走慢路径.

## C Unit Tests

```c
// tests/unit/test_shapes.c
#include "test_assert.h"
#include "ms/shape.h"
#include "ms/vm.h"

static void test_shape_transitions(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsShape* root = ms_shape_new(&vm);
    MsObjString* x = ms_obj_string_copy(&vm, "x", 1);
    MsObjString* y = ms_obj_string_copy(&vm, "y", 1);
    MsShape* s1 = ms_shape_transition(&vm, root, x);
    MsShape* s2 = ms_shape_transition(&vm, s1, y);
    TEST_ASSERT(s1 != root);
    TEST_ASSERT(s2 != s1);
    TEST_ASSERT_EQ(ms_shape_find_slot(s2, x), 0);
    TEST_ASSERT_EQ(ms_shape_find_slot(s2, y), 1);
    // Same transition yields same shape
    MsShape* s1b = ms_shape_transition(&vm, root, x);
    TEST_ASSERT(s1b == s1);
    ms_vm_free(&vm);
}

int main(void) {
    test_shape_transitions();
    printf("test_shapes: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/shapes.ms
class Point {
  init(x, y) {
    this.x = x
    this.y = y
  }
}
// Same property order → same shape → IC hit
var p1 = Point(1, 2)
var p2 = Point(3, 4)
print(p1.x + p2.x)
// expect: 4
print(p1.y + p2.y)
// expect: 6

// tests/fixtures/ic_polymorphic.ms
class A { init() { this.val = 1 } }
class B { init() { this.val = 2 } }
class C { init() { this.val = 3 } }

fun get_val(obj) { return obj.val }
print(get_val(A()))
// expect: 1
print(get_val(B()))
// expect: 2
print(get_val(C()))
// expect: 3
```
