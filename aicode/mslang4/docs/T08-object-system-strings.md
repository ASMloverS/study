# T08: Object System — Strings

**Phase**: 2 — Core Data Types
**Deps**: T07 (Value System)
**Complexity**: Medium

## Goal

Base object system + string object. `MsObject` header, string alloc (copy/take), FNV-1a hashing, concatenation, lifecycle. String interning deferred to VM task.

## Files

| File | Purpose |
|------|---------|
| `src/object.h` | Object types, all object structs (string populated, others placeholder), macros |
| `src/object.c` | String ops, `ms_object_free`/`ms_object_print` (string only initially) |

## TDD Cycles

### Cycle 1: `MsObject` Header + Type Enums

**RED** — Create `tests/unit/test_object_string.c`:

```c
#include "object.h"
#include "value.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_object_header(void) {
    MsString* s = ms_string_copy("hello", 5);
    assert(s != NULL);
    assert(s->base.type == MS_OBJ_STRING);
    assert(s->base.next == NULL);
    assert(s->base.isMarked == false);
    assert(s->length == 5);
    assert(strncmp(s->chars, "hello", 5) == 0);
    assert(s->hash != 0);

    MsValue val = ms_obj_val((MsObject*)s);
    assert(MS_IS_STRING(val));
    assert(MS_AS_STRING(val) == s);

    ms_object_free((MsObject*)s);
    printf("  test_object_header PASSED\n");
}

int main(void) {
    printf("Running object string tests...\n");
    test_object_header();
    printf("All object string tests passed.\n");
    return 0;
}
```

`object.h` doesn't exist → compile error.

**Verify RED**: `gcc -I src -o build/test_object_string tests/unit/test_object_string.c src/object.c src/value.c` → `object.h` not found

**GREEN** — Create `src/object.h`:

```c
#ifndef MS_OBJECT_H
#define MS_OBJECT_H

#include "value.h"

typedef enum {
    MS_OBJ_STRING, MS_OBJ_FUNCTION, MS_OBJ_CLOSURE, MS_OBJ_UPVALUE,
    MS_OBJ_CLASS, MS_OBJ_INSTANCE, MS_OBJ_BOUND_METHOD, MS_OBJ_MODULE,
    MS_OBJ_LIST, MS_OBJ_NATIVE
} MsObjectType;

struct MsObject {
    MsObjectType type;
    MsObject* next;
    bool isMarked;
};

typedef struct { MsObject base; char* chars; int length; uint32_t hash; } MsString;

/* Forward declarations for later tasks */
typedef struct MsVM MsVM;
typedef MsValue (*MsNativeFn)(MsVM* vm, int argCount, MsValue* args);
typedef struct { MsObject base; MsNativeFn function; MsString* name; int arity; } MsNative;
typedef struct { MsObject base; int arity; int upvalueCount; char* _chunk_placeholder; MsString* name; } MsFunction;
typedef struct MsUpvalue { MsObject base; MsValue* location; MsValue closed; struct MsUpvalue* next; } MsUpvalue;
typedef struct { MsObject base; MsFunction* function; MsUpvalue** upvalues; int upvalueCount; } MsClosure;
typedef struct MsClass MsClass;
typedef struct { MsObject base; MsClass* klass; int _fields_placeholder; } MsInstance;
typedef struct { MsObject base; MsValue receiver; MsClosure* method; } MsBoundMethod;
typedef struct { MsObject base; MsString* name; char* path; int _exports_placeholder; bool isLoaded; } MsModule;
typedef struct { MsObject base; MsValue* elements; int count; int capacity; } MsList;

/* Macros */
#define MS_IS_STRING(v)  (ms_is_obj(v) && ms_as_obj(v)->type == MS_OBJ_STRING)
#define MS_AS_STRING(v)  ((MsString*)ms_as_obj(v))

MsString* ms_string_copy(const char* chars, int length);
MsString* ms_string_take(char* chars, int length);
uint32_t ms_string_hash(const char* key, int length);
MsString* ms_string_concat(MsString* a, MsString* b);
void ms_object_free(MsObject* obj);
void ms_object_print(MsValue value);

#endif
```

Create `src/object.c`:

```c
#include "object.h"
#include "common.h"
#include <string.h>
#include <stdlib.h>

static MsString* ms_alloc_string(void) {
    MsString* str = MS_ALLOCATE(MsString, 1);
    str->base.type = MS_OBJ_STRING;
    str->base.next = NULL;
    str->base.isMarked = false;
    return str;
}

uint32_t ms_string_hash(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 2166136261u;
    }
    return hash;
}

MsString* ms_string_copy(const char* chars, int length) {
    uint32_t hash = ms_string_hash(chars, length);
    MsString* str = ms_alloc_string();
    str->chars = MS_ALLOCATE(char, length + 1);
    memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    str->length = length;
    str->hash = hash;
    return str;
}

void ms_object_free(MsObject* obj) {
    switch (obj->type) {
        case MS_OBJ_STRING: {
            MsString* str = (MsString*)obj;
            MS_FREE(char, str->chars, str->length + 1);
            MS_FREE(MsString, str, 1);
            break;
        }
        default: break;
    }
}
```

All structs defined now (placeholders for unimplemented) → avoid circular deps later. Only string fns implemented here.

Allocate via `MS_ALLOCATE(MsString, 1)`. `base.next = NULL`, `base.isMarked = false`. GC linked-list integration in T15/T17.

**Verify GREEN**: `gcc -I src -o build/test_object_string tests/unit/test_object_string.c src/object.c src/value.c && ./build/test_object_string`

**REFACTOR**: None needed.

---

### Cycle 2: FNV-1a Hash Function

**RED** — Add to `test_object_string.c`:

```c
static void test_string_hash(void) {
    assert(ms_string_hash("", 0) == 2166136261u);

    uint32_t h1 = ms_string_hash("hello", 5);
    uint32_t h2 = ms_string_hash("hello", 5);
    assert(h1 == h2);

    uint32_t h3 = ms_string_hash("world", 5);
    assert(h1 != h3);

    MsString* s = ms_string_copy("hello", 5);
    assert(s->hash == h1);
    ms_object_free((MsObject*)s);

    printf("  test_string_hash PASSED\n");
}
```

**Verify RED**: Should already pass — `ms_string_hash` from Cycle 1. Verification cycle.

**Verify GREEN**: build + run → both tests pass.

**REFACTOR**: None needed.

---

### Cycle 3: String Take (Ownership Transfer)

**RED** — Add to `test_object_string.c`:

```c
static void test_string_take(void) {
    char* buffer = MS_ALLOCATE(char, 6);
    memcpy(buffer, "hello", 5);
    buffer[5] = '\0';

    MsString* s = ms_string_take(buffer, 5);
    assert(s != NULL);
    assert(s->base.type == MS_OBJ_STRING);
    assert(s->length == 5);
    assert(strcmp(s->chars, "hello") == 0);
    assert(s->hash == ms_string_hash("hello", 5));
    assert(s->chars == buffer);

    ms_object_free((MsObject*)s);
    printf("  test_string_take PASSED\n");
}
```

`ms_string_take` undefined → linker error.

**GREEN** — Add to `src/object.c`:

```c
MsString* ms_string_take(char* chars, int length) {
    uint32_t hash = ms_string_hash(chars, length);
    MsString* str = ms_alloc_string();
    str->chars = chars;
    str->length = length;
    str->hash = hash;
    return str;
}
```

Takes ownership of `chars` pointer — no copy.

**Verify GREEN**: build + run → all three tests pass.

**REFACTOR**: None needed.

---

### Cycle 4: String Concatenation

**RED** — Add to `test_object_string.c`:

```c
static void test_string_concat(void) {
    MsString* a = ms_string_copy("foo", 3);
    MsString* b = ms_string_copy("bar", 3);
    MsString* c = ms_string_concat(a, b);

    assert(c != NULL);
    assert(c->length == 6);
    assert(strcmp(c->chars, "foobar") == 0);
    assert(c->hash == ms_string_hash("foobar", 6));

    ms_object_free((MsObject*)c);
    ms_object_free((MsObject*)a);
    ms_object_free((MsObject*)b);
    printf("  test_string_concat PASSED\n");
}
```

`ms_string_concat` undefined → linker error.

**GREEN** — Add to `src/object.c`:

```c
MsString* ms_string_concat(MsString* a, MsString* b) {
    int length = a->length + b->length;
    char* chars = MS_ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';
    return ms_string_take(chars, length);
}
```

Alloc new buffer, copy a then b, take ownership.

**Verify GREEN**: build + run → all four tests pass.

**REFACTOR**: None needed.

---

### Cycle 5: Object Print (String)

**RED** — Add to `test_object_string.c`:

```c
static void test_object_print(void) {
    MsString* s = ms_string_copy("hello world", 11);
    MsValue val = ms_obj_val((MsObject*)s);

    printf("  Expect 'hello world': ");
    ms_object_print(val);
    printf("\n");

    ms_object_free((MsObject*)s);
    printf("  test_object_print PASSED (visual check)\n");
}
```

`ms_object_print` undefined → linker error.

**GREEN** — Add to `src/object.c`:

```c
#include <stdio.h>

void ms_object_print(MsValue value) {
    MsObject* obj = ms_as_obj(value);
    switch (obj->type) {
        case MS_OBJ_STRING: {
            MsString* str = (MsString*)obj;
            printf("%s", str->chars);
            break;
        }
        default:
            printf("<object %d>", obj->type);
            break;
    }
}
```

**Verify GREEN**: build + run → all five tests pass.

**REFACTOR**: None needed.

---

### Cycle 6: Object Free + Hash Consistency

**RED** — Add to `test_object_string.c`:

```c
static void test_object_free_and_consistency(void) {
    for (int i = 0; i < 10; i++) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "str_%d", i);
        MsString* s = ms_string_copy(buf, len);
        assert(s->hash == ms_string_hash(buf, len));
        ms_object_free((MsObject*)s);
    }

    MsString* s1 = ms_string_copy("abc", 3);
    MsString* s2 = ms_string_copy("abcd", 4);
    assert(s1->hash != s2->hash);
    ms_object_free((MsObject*)s1);
    ms_object_free((MsObject*)s2);

    printf("  test_object_free_and_consistency PASSED\n");
}
```

Verification/stress cycle — should already compile and pass.

**Verify GREEN**: build + run → all tests pass. Verify no leaks with ASan:
`gcc -fsanitize=address -I src -o build/test_object_string tests/unit/test_object_string.c src/object.c src/value.c && ./build/test_object_string`

## Acceptance Criteria

- [x] `ms_string_copy("hello", 5)` → valid `MsString` with correct hash
- [x] `ms_string_take(buffer, len)` → ownership transfer, no copy
- [x] `ms_string_hash("", 0)` → FNV-1a offset basis
- [x] `ms_string_concat(a, b)` → `"ab"` contents
- [x] `ms_object_free()` → no crash, no leak
- [x] `ms_object_print(ms_obj_val(str))` → prints string chars
- [x] `MS_IS_STRING` macro works
- [x] Hash deterministic

## Notes

- `ms_alloc_object()` (memory.h) requires `MsVM*` for GC. VM doesn't exist yet → use `MS_ALLOCATE` directly, refactor in T15.
- All object structs defined now (placeholder fields) → avoid circular deps later. Only string ops implemented here.
- `ms_object_free` switch: `MS_OBJ_STRING` handled, `default: break` for others (later tasks).
