# Task 05: Base Objects — Function, Native, Upvalue, Closure

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement remaining base object types needed by compiler and VM: ObjFunction, ObjNative, ObjUpvalue, ObjClosure (FAM).
**Dependencies:** T04
**Produces:** 可创建函数/闭包对象; Chunk 拥有权归 ObjFunction

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `include/ms/object.h` | 添加 Function/Native/Upvalue/Closure 结构和宏 |
| Modify | `src/object.c` | 创建/销毁/打印函数 |
| Create | `tests/unit/test_base_objects.c` | 基础对象测试 |

## Key Data Structures / API

```c
// 追加到 include/ms/object.h

// Forward decl
typedef struct MsChunk MsChunk;
typedef struct MsObjUpvalue MsObjUpvalue;
typedef struct MsObjClosure MsObjClosure;
typedef struct MsInlineCache MsInlineCache;

// 原生函数签名
typedef MsValue (*MsNativeFn)(struct MsVM* vm, int argc, MsValue* argv);

typedef struct {
    MsObject obj;
    int arity;           // 参数数量
    int min_arity;       // 最少参数 (支持默认参数时), -1=无默认参数
    int upvalue_count;
    int max_stack_size;  // 编译器计算的最大寄存器使用量
    bool is_generator;
    MsChunk chunk;       // 拥有的字节码块 (内联存储)
    MsObjString* name;   // NULL = anonymous / top-level script
    MsInlineCache* ic;   // 内联缓存数组 (T20 填充)
    int ic_count;
} MsObjFunction;

typedef struct {
    MsObject obj;
    MsNativeFn function;
    MsObjString* name;
    int arity;           // -1 = variadic
} MsObjNative;

struct MsObjUpvalue {
    MsObject obj;
    MsValue* location;   // open: 指向栈; closed: 指向 &closed
    MsValue closed;      // closed 时存储值
    struct MsObjUpvalue* next;  // open upvalue 链
};

struct MsObjClosure {
    MsObject obj;
    MsObjFunction* function;
    int upvalue_count;
    MsObjUpvalue* upvalues[];  // FAM: count = upvalue_count
};

// --- IS / AS macros ---
#define MS_IS_FUNCTION(v)    MS_IS_OBJ_TYPE(v, MS_OBJ_FUNCTION)
#define MS_AS_FUNCTION(v)    ((MsObjFunction*)MS_AS_OBJECT(v))
#define MS_IS_NATIVE(v)      MS_IS_OBJ_TYPE(v, MS_OBJ_NATIVE)
#define MS_AS_NATIVE(v)      ((MsObjNative*)MS_AS_OBJECT(v))
#define MS_IS_CLOSURE(v)     MS_IS_OBJ_TYPE(v, MS_OBJ_CLOSURE)
#define MS_AS_CLOSURE(v)     ((MsObjClosure*)MS_AS_OBJECT(v))
#define MS_IS_UPVALUE(v)     MS_IS_OBJ_TYPE(v, MS_OBJ_UPVALUE)

// --- Constructors ---
MsObjFunction* ms_obj_function_new(struct MsVM* vm);
MsObjNative*   ms_obj_native_new(struct MsVM* vm, MsNativeFn fn,
                                   const char* name, int arity);
MsObjUpvalue*  ms_obj_upvalue_new(struct MsVM* vm, MsValue* slot);
MsObjClosure*  ms_obj_closure_new(struct MsVM* vm, MsObjFunction* fn);
```

## Implementation Notes

- **ObjFunction**: 内联 `MsChunk chunk` (非指针), `ms_obj_function_new` 中调用 `ms_chunk_init(&fn->chunk)`. 析构时调用 `ms_chunk_free(&fn->chunk)`
- **ObjClosure FAM**: 分配大小 = `sizeof(MsObjClosure) + sizeof(MsObjUpvalue*) * fn->upvalue_count`. 初始化所有 upvalue 指针为 NULL
- **ObjUpvalue**: 创建时 `location = slot`, `closed = MS_NIL_VAL()`, `next = NULL`
- **ObjNative**: arity=-1 表示可变参数
- **ms_object_free 扩展**: 添加 MS_OBJ_FUNCTION/NATIVE/UPVALUE/CLOSURE 分支
- **ms_object_print 扩展**: function 打印 `<fn name>`, native 打印 `<native name>`, closure 打印 `<fn name>`

## C Unit Tests

```c
// tests/unit/test_base_objects.c
#include "test_assert.h"
#include "ms/object.h"
#include "ms/chunk.h"
#include "ms/vm.h"

static void test_function(MsVM* vm) {
    MsObjFunction* fn = ms_obj_function_new(vm);
    TEST_ASSERT(fn->obj.type == MS_OBJ_FUNCTION);
    TEST_ASSERT(fn->arity == 0);
    TEST_ASSERT(fn->name == NULL);
    fn->name = ms_obj_string_copy(vm, "add", 3);
    TEST_ASSERT_STR_EQ(fn->name->data, "add");
}

static void test_closure(MsVM* vm) {
    MsObjFunction* fn = ms_obj_function_new(vm);
    fn->upvalue_count = 3;
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    TEST_ASSERT(cl->function == fn);
    TEST_ASSERT(cl->upvalue_count == 3);
    // FAM slots should be NULL-initialized
    for (int i = 0; i < 3; i++)
        TEST_ASSERT(cl->upvalues[i] == NULL);
}

static void test_upvalue(MsVM* vm) {
    MsValue slot = MS_INT_VAL(99);
    MsObjUpvalue* uv = ms_obj_upvalue_new(vm, &slot);
    TEST_ASSERT(*uv->location == slot.as.integer); // points to slot
    // Close upvalue
    uv->closed = *uv->location;
    uv->location = &uv->closed;
    TEST_ASSERT(MS_AS_INT(*uv->location) == 99);
}

static MsValue native_add(MsVM* vm, int argc, MsValue* argv) {
    (void)vm;
    if (argc == 2) return MS_INT_VAL(MS_AS_INT(argv[0]) + MS_AS_INT(argv[1]));
    return MS_NIL_VAL();
}

static void test_native(MsVM* vm) {
    MsObjNative* n = ms_obj_native_new(vm, native_add, "add", 2);
    TEST_ASSERT(n->arity == 2);
    MsValue args[] = {MS_INT_VAL(3), MS_INT_VAL(4)};
    MsValue result = n->function(vm, 2, args);
    TEST_ASSERT(MS_AS_INT(result) == 7);
}

int main(void) {
    MsVM vm;
    ms_vm_init(&vm);
    test_function(&vm);
    test_closure(&vm);
    test_upvalue(&vm);
    test_native(&vm);
    ms_vm_free(&vm);
    printf("test_base_objects: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/functions_basic.ms (run after T13)
fun greet(name) {
  return "hello " + name
}
print(greet("world"))
// expect: hello world

fun factorial(n) {
  if (n <= 1) return 1
  return n * factorial(n - 1)
}
print(factorial(5))
// expect: 120
```
