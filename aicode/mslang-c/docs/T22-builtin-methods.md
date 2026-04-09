# Task 22: Built-in Type Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement built-in methods for string, list, map, tuple types, plus ObjStringBuilder.
**Dependencies:** T21
**Produces:** `"hello".len()`, `list.push()`, `map.keys()` and other built-in methods available

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `src/vm_builtins.c` | Built-in method dispatch |
| Modify | `include/ms/object.h` | ObjStringBuilder struct |
| Modify | `src/object.c` | StringBuilder create/destroy |
| Modify | `src/vm.c` | INVOKE dispatches to builtins |
| Create | `tests/unit/test_builtins.c` | Built-in method tests |

## Key Data Structures / API

```c
// Built-in method dispatch entry (called from INVOKE / GETPROP)
// Returns true if handled; result placed in *out
bool ms_builtin_invoke(MsVM* vm, MsValue receiver, MsObjString* method,
                        int argc, MsValue* argv, MsValue* out);

// ObjStringBuilder
typedef struct {
    MsObject obj;
    char* buffer;
    int length;
    int capacity;
} MsObjStringBuilder;

MsObjStringBuilder* ms_obj_sb_new(MsVM* vm);
void ms_obj_sb_append(MsObjStringBuilder* sb, const char* str, int len);
MsObjString* ms_obj_sb_to_string(MsVM* vm, MsObjStringBuilder* sb);
```

## Implementation Notes

### String Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `len()` | → int | string length |
| `upper()` | → string | to uppercase |
| `lower()` | → string | to lowercase |
| `contains(s)` | → bool | substring check |
| `starts_with(s)` | → bool | prefix match |
| `ends_with(s)` | → bool | suffix match |
| `index_of(s)` | → int | substring position, -1 if not found |
| `split(sep)` | → list | split by separator |
| `trim()` | → string | strip leading/trailing whitespace |
| `replace(old, new)` | → string | replace substring |
| `slice(start, end)` | → string | substring slice |

### List Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `len()` | → int | list length |
| `push(val)` | → nil | append element |
| `pop()` | → value | remove and return last element |
| `contains(val)` | → bool | membership check |
| `index_of(val)` | → int | element position |
| `remove(idx)` | → value | remove by index |
| `sort()` | → list | sort in-place (numeric/string) |
| `reverse()` | → list | reverse in-place |
| `map(fn)` | → list | apply function to each element |
| `filter(fn)` | → list | filter by predicate |
| `join(sep)` | → string | join with separator |
| `slice(start, end)` | → list | slice |

### Map Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `len()` | → int | entry count |
| `keys()` | → list | all keys |
| `values()` | → list | all values |
| `has(key)` | → bool | key existence |
| `remove(key)` | → bool | delete entry |

### Tuple Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `len()` | → int | length |
| `contains(val)` | → bool | membership check |

### Dispatch Logic

In `INVOKE`, if receiver is not `ObjInstance`, try `ms_builtin_invoke`:
```c
if (MS_IS_STRING(receiver)) {
    return string_invoke(vm, MS_AS_STRING(receiver), method, argc, argv, out);
} else if (MS_IS_LIST(receiver)) {
    return list_invoke(vm, MS_AS_LIST(receiver), method, argc, argv, out);
}
// ...
```

## C Unit Tests

```c
// tests/unit/test_builtins.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_string_len(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "print(\"hello\".len())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_string_len();
    printf("test_builtins: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/string_methods.ms
print("hello".len())
// expect: 5
print("Hello".upper())
// expect: HELLO
print("Hello".lower())
// expect: hello
print("hello world".contains("world"))
// expect: true
print("hello".starts_with("hel"))
// expect: true
print("hello".ends_with("llo"))
// expect: true
print("hello".index_of("ll"))
// expect: 2
print("a,b,c".split(",").len())
// expect: 3
print("  hello  ".trim())
// expect: hello
print("hello".replace("ll", "r"))
// expect: hero
print("hello".slice(1, 3))
// expect: el

// tests/fixtures/list_methods.ms
var a = [3, 1, 2]
print(a.len())
// expect: 3
a.push(4)
print(a.len())
// expect: 4
print(a.pop())
// expect: 4
print(a.contains(2))
// expect: true
print(a.index_of(1))
// expect: 1
a.sort()
print(a[0])
// expect: 1
a.reverse()
print(a[0])
// expect: 3

var doubled = [1,2,3].map(fun(x) { return x * 2 })
print(doubled[0])
// expect: 2
print(doubled[2])
// expect: 6

var evens = [1,2,3,4,5].filter(fun(x) { return x % 2 == 0 })
print(evens.len())
// expect: 2

print([1,2,3].join("-"))
// expect: 1-2-3

// tests/fixtures/map_methods.ms
var m = {"a": 1, "b": 2}
print(m.len())
// expect: 2
print(m.has("a"))
// expect: true
print(m.has("c"))
// expect: false
m.remove("a")
print(m.len())
// expect: 1

// tests/fixtures/tuple_methods.ms
var t = (1, 2, 3)
print(t.len())
// expect: 3
print(t.contains(2))
// expect: true
print(t.contains(5))
// expect: false
```
