# Task 04: Object System — Strings

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement `MsObject` base header + `MsObjString` w/ FAM, FNV-1a hashing, string interning.
**Deps:** T02, T03
**Produces:** String create/intern; equality → O(1) ptr compare

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/object.h` | `MsObject` header, `MsObjString`, type macros |
| Create | `include/ms/memory.h` | Memory alloc interface |
| Create | `src/object.c` | Object create/destroy/print |
| Create | `src/memory.c` | `ms_reallocate` base impl |
| Create | `tests/unit/test_object_string.c` | String object tests |

## Key Data Structures / API

```c
// include/ms/object.h
typedef enum {
    MS_OBJ_STRING, MS_OBJ_FUNCTION, MS_OBJ_NATIVE, MS_OBJ_CLOSURE,
    MS_OBJ_UPVALUE, MS_OBJ_CLASS, MS_OBJ_INSTANCE, MS_OBJ_BOUND_METHOD,
    MS_OBJ_LIST, MS_OBJ_MAP, MS_OBJ_MODULE, MS_OBJ_STRING_BUILDER,
    MS_OBJ_TUPLE, MS_OBJ_FILE, MS_OBJ_WEAK_REF, MS_OBJ_COROUTINE,
} MsObjectType;

struct MsObject {
    MsObjectType type;
    bool is_marked;
    bool in_remembered_set;
    ms_u8 generation;  // 0=young, 1=old
    ms_u8 age;
    struct MsObject* next;  // GC intrusive list
};

typedef struct MsObjString {
    MsObject obj;
    uint32_t hash;
    int length;
    char data[];  // FAM: null-terminated chars inline
} MsObjString;

// --- Type check / cast macros ---
#define MS_OBJ_TYPE(v)       (MS_AS_OBJECT(v)->type)
#define MS_IS_OBJ_TYPE(v, t) (MS_IS_OBJECT(v) && MS_OBJ_TYPE(v) == (t))
#define MS_IS_STRING(v)      MS_IS_OBJ_TYPE(v, MS_OBJ_STRING)
#define MS_AS_STRING(v)      ((MsObjString*)MS_AS_OBJECT(v))
#define MS_AS_CSTRING(v)     (MS_AS_STRING(v)->data)

// --- Object allocation ---
struct MsVM;
MsObject* ms_alloc_object(struct MsVM* vm, size_t size, MsObjectType type);
#define MS_ALLOC_OBJ(vm, type_enum, T, extra) \
    (T*)ms_alloc_object((vm), sizeof(T) + (extra), (type_enum))

// --- String API ---
uint32_t    ms_fnv1a(const char* data, int length);
MsObjString* ms_obj_string_copy(struct MsVM* vm, const char* chars, int length);
MsObjString* ms_obj_string_take(struct MsVM* vm, char* chars, int length);
MsObjString* ms_obj_string_concat(struct MsVM* vm, MsObjString* a, MsObjString* b);

// --- Devirtualized dispatch ---
void ms_object_print(MsObject* obj);
void ms_object_free(struct MsVM* vm, MsObject* obj);
```

```c
// include/ms/memory.h
#pragma once
#include "ms/common.h"
struct MsVM;

void* ms_reallocate(struct MsVM* vm, void* ptr, size_t old_size, size_t new_size);

#define MS_ALLOC(vm, T, count) \
    (T*)ms_reallocate((vm), NULL, 0, sizeof(T) * (size_t)(count))
#define MS_FREE(vm, T, ptr) \
    ms_reallocate((vm), (ptr), sizeof(T), 0)
#define MS_FREE_ARRAY(vm, T, ptr, count) \
    ms_reallocate((vm), (ptr), sizeof(T) * (size_t)(count), 0)
#define MS_GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)
```

## Impl Notes

- **`ms_alloc_object`**: `malloc(size)` → init header (`type`, `is_marked=false`, `generation=0`, `age=0`) → prepend to `vm->objects`
- **FNV-1a**: `hash = 2166136261u; for each byte: hash ^= byte; hash *= 16777619u;`
- **String interning**: `ms_obj_string_copy` → hash → `ms_table_find_string` in `vm->strings`. Hit → return existing. Miss → alloc `sizeof(MsObjString) + length + 1`, copy, null-terminate, insert
- **`ms_obj_string_take`**: same as copy; owns `chars` → frees after FAM copy
- **`ms_obj_string_concat`**: alloc new FAM string, copy `a`+`b`, run intern flow
- **`ms_object_free`**: `switch(obj->type)`; `MS_OBJ_STRING` → free obj (FAM released w/ struct)
- **`ms_value_equals`**: `OBJECT` type → ptr compare (interned → same content = same ptr)

## C Unit Tests

```c
// tests/unit/test_object_string.c
#include "test_assert.h"
#include "ms/object.h"
#include "ms/vm.h"  // requires MsVM for interning (a mini vm struct suffices for now)

static void test_string_interning(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "hello", 5);
    MsObjString* b = ms_obj_string_copy(vm, "hello", 5);
    TEST_ASSERT(a == b);  // same pointer: interned
}

static void test_string_concat(MsVM* vm) {
    MsObjString* a = ms_obj_string_copy(vm, "foo", 3);
    MsObjString* b = ms_obj_string_copy(vm, "bar", 3);
    MsObjString* c = ms_obj_string_concat(vm, a, b);
    TEST_ASSERT(c->length == 6);
    TEST_ASSERT_STR_EQ(c->data, "foobar");
}

static void test_fnv1a(void) {
    uint32_t h1 = ms_fnv1a("hello", 5);
    uint32_t h2 = ms_fnv1a("hello", 5);
    uint32_t h3 = ms_fnv1a("world", 5);
    TEST_ASSERT(h1 == h2);
    TEST_ASSERT(h1 != h3);
}

int main(void) {
    MsVM vm;
    ms_vm_init(&vm);
    test_fnv1a();
    test_string_interning(&vm);
    test_string_concat(&vm);
    ms_vm_free(&vm);
    printf("test_object_string: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/strings_basic.ms (run after T13)
print("hello")
// expect: hello
print("hello" + " " + "world")
// expect: hello world
print("abc" == "abc")
// expect: true
print("abc" == "def")
// expect: false
```
