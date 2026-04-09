# Task 21: Collections — List, Map, Tuple

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement ObjList, ObjMap, ObjTuple with creation opcodes, index access, and literal syntax.
**Dependencies:** T18
**Produces:** List `[1,2,3]`, map `{"a":1}`, tuple `(1,2,3)` creation and indexing

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | ObjList, ObjMap, ObjTuple structs |
| Create | `include/ms/vtable.h` | MsValueTable (Value-keyed hash table for ObjMap) |
| Create | `src/vtable.c` | ValueTable implementation |
| Modify | `src/object.c` | Collection object create/destroy/print |
| Modify | `src/compiler_expr.c` | List/map/tuple literals, subscript ops |
| Modify | `src/vm.c` | NEWLIST, NEWMAP, NEWTUPLE, GETIDX, SETIDX |
| Create | `tests/unit/test_collections.c` | Collection tests |

## Key Data Structures / API

```c
// ObjList: dynamic array
typedef struct {
    MsObject obj;
    MsValueArray items;
} MsObjList;

// ObjTuple: immutable, hashable
typedef struct {
    MsObject obj;
    uint32_t hash;
    int count;
    MsValue items[];  // FAM
} MsObjTuple;

// ObjMap: Value-keyed hash table
typedef struct {
    MsObject obj;
    MsValueTable table;
} MsObjMap;

#define MS_IS_LIST(v)   MS_IS_OBJ_TYPE(v, MS_OBJ_LIST)
#define MS_AS_LIST(v)   ((MsObjList*)MS_AS_OBJECT(v))
#define MS_IS_MAP(v)    MS_IS_OBJ_TYPE(v, MS_OBJ_MAP)
#define MS_AS_MAP(v)    ((MsObjMap*)MS_AS_OBJECT(v))
#define MS_IS_TUPLE(v)  MS_IS_OBJ_TYPE(v, MS_OBJ_TUPLE)
#define MS_AS_TUPLE(v)  ((MsObjTuple*)MS_AS_OBJECT(v))

MsObjList*  ms_obj_list_new(MsVM* vm);
MsObjTuple* ms_obj_tuple_new(MsVM* vm, MsValue* items, int count);
MsObjMap*   ms_obj_map_new(MsVM* vm);
```

```c
// include/ms/vtable.h — Value-keyed hash table
typedef struct {
    MsValue key;
    MsValue value;
    bool used;
} MsVEntry;

typedef struct {
    MsVEntry* entries;
    int count;
    int capacity;
} MsValueTable;

void ms_vtable_init(MsValueTable* t);
void ms_vtable_free(MsValueTable* t);
bool ms_vtable_set(MsValueTable* t, MsValue key, MsValue val);
bool ms_vtable_get(MsValueTable* t, MsValue key, MsValue* out);
bool ms_vtable_delete(MsValueTable* t, MsValue key);
```

## Implementation Notes

### NEWLIST A B

Create `ObjList` from `B` elements in `R(A)..R(A+B-1)`:
```c
case MS_OP_NEWLIST: {
    int count = MS_GET_B(instr);
    MsObjList* list = ms_obj_list_new(vm);
    for (int i = 0; i < count; i++)
        ms_value_array_push(&list->items, R(A + i));
    R(A) = MS_OBJ_VAL(list);
    break;
}
```

### GETIDX A B C — `R(A) = R(B)[RK(C)]`

```c
case MS_OP_GETIDX: {
    MsValue obj = R(B);
    MsValue idx = RK(C);
    if (MS_IS_LIST(obj) && MS_IS_INT(idx)) {
        MsObjList* list = MS_AS_LIST(obj);
        int i = (int)MS_AS_INT(idx);
        if (i < 0) i += list->items.count;  // negative index
        R(A) = list->items.data[i];
    } else if (MS_IS_MAP(obj)) {
        MsObjMap* map = MS_AS_MAP(obj);
        MsValue val;
        ms_vtable_get(&map->table, idx, &val);
        R(A) = val;
    } else if (MS_IS_STRING(obj) && MS_IS_INT(idx)) {
        // string char indexing
    }
    break;
}
```

### ValueTable Hashing

Value hash by type: `nil` → 0; `bool` → 0/1; `int` → `(uint32_t)val`; `number` → bit-cast to u64 then fold; `string` → `string.hash`; `tuple` → `tuple.hash`.

### Compiler Side

- `[expr, ...]` → compile elements to consecutive registers, emit `NEWLIST A count`
- `{"key": val, ...}` → compile key-value pairs, emit `NEWMAP A count`
- `(expr, expr)` → compile elements, emit `NEWTUPLE A count` (disambiguate from grouping: `(x)` is grouping, `(x,)` is tuple)
- `expr[idx]` → compile obj and idx, emit `GETIDX`
- `expr[idx] = val` → compile obj, idx, val, emit `SETIDX`

## C Unit Tests

```c
// tests/unit/test_collections.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_list(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var a = [1, 2, 3]\nprint(a[1])", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_list();
    printf("test_collections: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/list.ms
var a = [10, 20, 30]
print(a[0])
// expect: 10
print(a[2])
// expect: 30
a[1] = 99
print(a[1])
// expect: 99

var b = []
b.push(1)
b.push(2)
print(b[0])
// expect: 1

// tests/fixtures/map.ms
var m = {"name": "Alice", "age": 30}
print(m["name"])
// expect: Alice
print(m["age"])
// expect: 30
m["city"] = "Paris"
print(m["city"])
// expect: Paris

// tests/fixtures/tuple.ms
var t = (1, "hello", true)
print(t[0])
// expect: 1
print(t[1])
// expect: hello
print(t[2])
// expect: true

// tests/fixtures/nested_collections.ms
var matrix = [[1, 2], [3, 4]]
print(matrix[0][0])
// expect: 1
print(matrix[1][1])
// expect: 4

// tests/fixtures/negative_index.ms
var a = [10, 20, 30]
print(a[-1])
// expect: 30
print(a[-2])
// expect: 20
```
