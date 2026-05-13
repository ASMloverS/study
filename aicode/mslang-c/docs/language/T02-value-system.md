# Task 02: Value System

> **Agentic workers:** Use superpowers:executing-plans.

**Goal:** Impl `MsValue` tagged union (nil/bool/double/int64/object), dynamic array, value ops.
**Deps:** T01
**Produces:** Compilable `ms_support` lib; unit tests for creation, truthiness, equality.

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/value.h` | `MsValue` def, construction/check/extraction macros |
| Create | `src/value.c` | Value op impls |
| Modify | `CMakeLists.txt` | Add `ms_support` static lib |
| Create | `tests/unit/test_value.c` | Value unit tests |

## API

```c
// include/ms/value.h
typedef enum {
    MS_VAL_NIL,
    MS_VAL_BOOL,
    MS_VAL_NUMBER,  // double
    MS_VAL_INT,     // int64_t
    MS_VAL_OBJECT,
} MsValueType;

typedef struct MsObject MsObject;  // forward decl

typedef struct {
    MsValueType type;
    union {
        bool boolean;
        double number;
        ms_i64 integer;
        MsObject* object;
    } as;
} MsValue;

// Construction macros
#define MS_NIL_VAL()     ((MsValue){MS_VAL_NIL,    {.integer = 0}})
#define MS_BOOL_VAL(b)   ((MsValue){MS_VAL_BOOL,   {.boolean = (b)}})
#define MS_NUMBER_VAL(n) ((MsValue){MS_VAL_NUMBER,  {.number = (n)}})
#define MS_INT_VAL(i)    ((MsValue){MS_VAL_INT,     {.integer = (i)}})
#define MS_OBJ_VAL(p)    ((MsValue){MS_VAL_OBJECT,  {.object = (MsObject*)(p)}})

// Type checks
#define MS_IS_NIL(v)     ((v).type == MS_VAL_NIL)
#define MS_IS_BOOL(v)    ((v).type == MS_VAL_BOOL)
#define MS_IS_NUMBER(v)  ((v).type == MS_VAL_NUMBER)
#define MS_IS_INT(v)     ((v).type == MS_VAL_INT)
#define MS_IS_OBJECT(v)  ((v).type == MS_VAL_OBJECT)
#define MS_IS_NUMERIC(v) (MS_IS_NUMBER(v) || MS_IS_INT(v))

// Extraction
#define MS_AS_BOOL(v)    ((v).as.boolean)
#define MS_AS_NUMBER(v)  ((v).as.number)
#define MS_AS_INT(v)     ((v).as.integer)
#define MS_AS_OBJECT(v)  ((v).as.object)

// Numeric coercion
static inline double ms_as_double(MsValue v) {
    return MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
}

// Dynamic array
typedef struct {
    MsValue* data;
    int count;
    int capacity;
} MsValueArray;

// Generic grow-and-push (reusable for any array type)
#define MS_ARRAY_PUSH(arr, item, T) do { \
    if ((arr)->count >= (arr)->capacity) { \
        int _cap = (arr)->capacity < 8 ? 8 : (arr)->capacity * 2; \
        (arr)->data = (T*)realloc((arr)->data, sizeof(T) * (size_t)_cap); \
        (arr)->capacity = _cap; \
    } \
    (arr)->data[(arr)->count++] = (item); \
} while (0)

void ms_value_array_init(MsValueArray* arr);
void ms_value_array_push(MsValueArray* arr, MsValue val);
void ms_value_array_free(MsValueArray* arr);

// Value ops
bool  ms_value_equals(MsValue a, MsValue b);
bool  ms_value_is_truthy(MsValue v);
void  ms_value_print(MsValue v);        // stdout
char* ms_value_to_cstring(MsValue v);   // heap alloc; caller frees
```

## Impl Notes

- **Equality:** nil==nil; bool by val; numeric cross-type (int 5 == double 5.0); object by ptr (strings interned → ptr eq)
- **Truthiness:** nil→false; bool→its val; 0→false; 0.0→false; else→true (empty containers→false, added T21)
- **`ms_value_print`:** int: no decimal; double: `%g`; string: data content
- **`ms_value_to_cstring`:** heap-alloc `char*`; caller frees

## Unit Tests

```c
// tests/unit/test_value.c
#include "test_assert.h"
#include "ms/value.h"

static void test_nil(void) {
    MsValue v = MS_NIL_VAL();
    TEST_ASSERT(MS_IS_NIL(v));
    TEST_ASSERT(!ms_value_is_truthy(v));
}
static void test_bool(void) {
    TEST_ASSERT(ms_value_is_truthy(MS_BOOL_VAL(true)));
    TEST_ASSERT(!ms_value_is_truthy(MS_BOOL_VAL(false)));
}
static void test_number(void) {
    MsValue v = MS_NUMBER_VAL(3.14);
    TEST_ASSERT(MS_IS_NUMBER(v));
    TEST_ASSERT_EQ(MS_AS_NUMBER(v), 3.14);
    TEST_ASSERT(!ms_value_is_truthy(MS_NUMBER_VAL(0.0)));
    TEST_ASSERT(ms_value_is_truthy(MS_NUMBER_VAL(1.0)));
}
static void test_int(void) {
    MsValue v = MS_INT_VAL(42);
    TEST_ASSERT(MS_IS_INT(v));
    TEST_ASSERT(MS_AS_INT(v) == 42);
    TEST_ASSERT(!ms_value_is_truthy(MS_INT_VAL(0)));
}
static void test_equality(void) {
    TEST_ASSERT(ms_value_equals(MS_NIL_VAL(), MS_NIL_VAL()));
    TEST_ASSERT(ms_value_equals(MS_INT_VAL(5), MS_NUMBER_VAL(5.0)));
    TEST_ASSERT(!ms_value_equals(MS_INT_VAL(5), MS_INT_VAL(6)));
    TEST_ASSERT(!ms_value_equals(MS_NIL_VAL(), MS_BOOL_VAL(false)));
}
static void test_array(void) {
    MsValueArray arr;
    ms_value_array_init(&arr);
    ms_value_array_push(&arr, MS_INT_VAL(1));
    ms_value_array_push(&arr, MS_INT_VAL(2));
    TEST_ASSERT_EQ(arr.count, 2);
    TEST_ASSERT(MS_AS_INT(arr.data[0]) == 1);
    ms_value_array_free(&arr);
}

int main(void) {
    test_nil(); test_bool(); test_number();
    test_int(); test_equality(); test_array();
    printf("test_value: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/value_types.ms (run after T13)
print(nil == nil)
// expect: true
print(1 == 1)
// expect: true
print(1 == 1.0)
// expect: true
print(0 == false)
// expect: false
print(nil == false)
// expect: false
print(true)
// expect: true
print(3.14)
// expect: 3.14
print(42)
// expect: 42
```
