# Task 27: Native Functions

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Register all built-in native functions and implement ASCII character cache for fast single-char string allocation.
**Dependencies:** T13, T21
**Produces:** clock(), type(), str(), num(), len(), input(), assert(), int(), float() 等可用

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `src/vm_natives.c` | 原生函数注册和实现 |
| Modify | `src/vm.c` | 在 ms_vm_init 中注册原生函数 |
| Modify | `include/ms/vm.h` | ASCII char cache 字段 |
| Create | `tests/unit/test_natives.c` | 原生函数测试 |

## Key Data Structures / API

```c
// 追加到 MsVM:
MsObjString* ascii_cache[128];  // 单字符 ASCII 字符串缓存

// 注册所有原生函数
void ms_register_natives(MsVM* vm);
```

## Implementation Notes

### 原生函数列表

| 函数 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `clock()` | 0 | number | 当前时间 (秒, 浮点) |
| `type(val)` | 1 | string | 值类型名 ("nil"/"bool"/"number"/"int"/"string"/"list"/"map"/"function"/"class"/"instance") |
| `str(val)` | 1 | string | 值转字符串 |
| `num(val)` | 1 | number | 值转数字 |
| `int(val)` | 1 | int | 值转整数 |
| `float(val)` | 1 | number | 值转浮点 |
| `len(val)` | 1 | int | 长度 (string/list/map/tuple) |
| `input(prompt?)` | 0-1 | string | 读取一行输入 |
| `assert(cond, msg?)` | 1-2 | nil | 断言, 失败则 runtime error |
| `hasattr(obj, name)` | 2 | bool | 对象是否有属性 |
| `getattr(obj, name)` | 2 | value | 获取属性值 |
| `setattr(obj, name, val)` | 3 | nil | 设置属性值 |
| `print(val)` | 1 | nil | 打印值 + 换行 (也可作为全局函数而非语句) |

### 实现示例

```c
static MsValue native_clock(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(vm); MS_UNUSED(argc); MS_UNUSED(argv);
    return MS_NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static MsValue native_type(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(argc);
    MsValue val = argv[0];
    const char* name;
    if (MS_IS_NIL(val))         name = "nil";
    else if (MS_IS_BOOL(val))   name = "bool";
    else if (MS_IS_INT(val))    name = "int";
    else if (MS_IS_NUMBER(val)) name = "number";
    else if (MS_IS_STRING(val)) name = "string";
    else if (MS_IS_LIST(val))   name = "list";
    else if (MS_IS_MAP(val))    name = "map";
    else if (MS_IS_FUNCTION(val) || MS_IS_CLOSURE(val) || MS_IS_NATIVE(val))
        name = "function";
    else if (MS_IS_CLASS(val))  name = "class";
    else if (MS_IS_INSTANCE(val)) name = "instance";
    else name = "object";
    return MS_OBJ_VAL(ms_obj_string_copy(vm, name, (int)strlen(name)));
}

static MsValue native_len(MsVM* vm, int argc, MsValue* argv) {
    MS_UNUSED(argc);
    MsValue val = argv[0];
    if (MS_IS_STRING(val)) return MS_INT_VAL(MS_AS_STRING(val)->length);
    if (MS_IS_LIST(val))   return MS_INT_VAL(MS_AS_LIST(val)->items.count);
    if (MS_IS_MAP(val))    return MS_INT_VAL(MS_AS_MAP(val)->table.count);
    if (MS_IS_TUPLE(val))  return MS_INT_VAL(MS_AS_TUPLE(val)->count);
    ms_vm_runtime_error(vm, "Cannot get length of %s.", "value");
    return MS_NIL_VAL();
}
```

### 注册

```c
void ms_register_natives(MsVM* vm) {
    ms_vm_define_native(vm, "clock",   native_clock,   0);
    ms_vm_define_native(vm, "type",    native_type,    1);
    ms_vm_define_native(vm, "str",     native_str,     1);
    ms_vm_define_native(vm, "num",     native_num,     1);
    ms_vm_define_native(vm, "int",     native_int,     1);
    ms_vm_define_native(vm, "float",   native_float,   1);
    ms_vm_define_native(vm, "len",     native_len,     1);
    ms_vm_define_native(vm, "input",   native_input,  -1);
    ms_vm_define_native(vm, "assert",  native_assert, -1);
    ms_vm_define_native(vm, "hasattr", native_hasattr, 2);
    ms_vm_define_native(vm, "getattr", native_getattr, 2);
    ms_vm_define_native(vm, "setattr", native_setattr, 3);
    ms_vm_define_native(vm, "print",   native_print,   1);
}
```

### ASCII Character Cache

```c
// 在 ms_vm_init 中初始化
for (int i = 0; i < 128; i++) {
    char c = (char)i;
    vm->ascii_cache[i] = ms_obj_string_copy(vm, &c, 1);
}
```

单字符 string 分配时直接返回缓存 (在 ms_obj_string_copy 中检查 length==1 && chars[0] < 128).

## C Unit Tests

```c
// tests/unit/test_natives.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_type_function(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "print(type(42))\nprint(type(\"hi\"))\nprint(type(nil))", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_type_function();
    printf("test_natives: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/native_type.ms
print(type(42))
// expect: int
print(type(3.14))
// expect: number
print(type("hello"))
// expect: string
print(type(true))
// expect: bool
print(type(nil))
// expect: nil
print(type([1,2]))
// expect: list
print(type({"a":1}))
// expect: map

// tests/fixtures/native_conversions.ms
print(str(42))
// expect: 42
print(str(true))
// expect: true
print(num("3.14"))
// expect: 3.14
print(int(3.7))
// expect: 3
print(float(42))
// expect: 42

// tests/fixtures/native_len.ms
print(len("hello"))
// expect: 5
print(len([1,2,3]))
// expect: 3
print(len({"a":1, "b":2}))
// expect: 2
print(len((1,2,3,4)))
// expect: 4

// tests/fixtures/native_clock.ms
var t = clock()
print(type(t))
// expect: number

// tests/fixtures/native_assert.ms
assert(true)
assert(1 == 1)
assert(1 < 2, "math is broken")
print("assertions passed")
// expect: assertions passed
```
