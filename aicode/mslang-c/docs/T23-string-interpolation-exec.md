# Task 23: String Interpolation Execution

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Complete string interpolation end-to-end: compile `"a ${expr} b"` into concatenation bytecode, execute at runtime.
**Dependencies:** T09, T13, T22
**Produces:** `"hello ${name}!"` 在运行时正确拼接

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler_expr.c` | 编译字符串插值 token 序列 |
| Modify | `src/vm.c` | 确保 STR (tostring) + ADD (concat) 正确工作 |
| Create | `tests/unit/test_interp_exec.c` | 插值执行测试 |

## Implementation Notes

### 编译策略

Scanner 产出的 token 序列 (来自 T09):
```
"hello "     → STRING_INTERP
name         → IDENTIFIER
"!"          → STRING_INTERP_END
```

编译器将此转化为拼接操作:
```
LOADK   R(0) K("hello ")
GETLOCAL R(1) slot(name)   ; 或其他 expr
STR     R(1) R(1)          ; tostring (若非字符串)
ADD     R(0) R(0) R(1)     ; concat
LOADK   R(1) K("!")
ADD     R(0) R(0) R(1)     ; concat
```

具体步骤:
1. 遇到 `STRING_INTERP` token → 将前缀文本加载为字符串常量
2. 编译插值表达式 → 得到寄存器 reg
3. 发射 `STR dest, reg` (将值转为字符串)
4. 发射 `ADD dest, prefix_reg, str_reg` (拼接)
5. 若后续是 `STRING_INTERP_END` → 加载后缀, 再次拼接
6. 若后续是另一个 `STRING_INTERP` → 重复步骤 1-4

### MS_OP_STR 实现

```c
case MS_OP_STR: {
    MsValue val = R(MS_GET_B(instr));
    if (MS_IS_STRING(val)) {
        R(A) = val;  // 已经是字符串
    } else {
        char* s = ms_value_to_cstring(val);
        R(A) = MS_OBJ_VAL(ms_obj_string_take(vm, s, (int)strlen(s)));
    }
    break;
}
```

### 优化考虑

多段拼接 `"a ${x} b ${y} c"` 可使用 StringBuilder 避免大量中间字符串:
- 简化版: 直接用多次 ADD (足够初版使用)
- 高级版: 编译为 NEWSTR_BUILDER → APPEND × N → TO_STRING (延迟到 T28+)

## C Unit Tests

```c
// tests/unit/test_interp_exec.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_simple_interp(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var name = \"world\"\nprint(\"hello ${name}!\")", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

static void test_expr_interp(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "print(\"${1 + 2} items\")", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_simple_interp();
    test_expr_interp();
    printf("test_interp_exec: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/string_interp_runtime.ms
var name = "world"
print("hello ${name}!")
// expect: hello world!

print("${1 + 2} items")
// expect: 3 items

var x = 42
print("x = ${x}")
// expect: x = 42

print("${true} or ${false}")
// expect: true or false

print("nested: ${"inner ${1}"}")
// expect: nested: inner 1

// Empty interpolation prefix/suffix
print("${100}")
// expect: 100

// Multiple interpolations
var a = "A"
var b = "B"
print("${a} and ${b}")
// expect: A and B

// Interpolation with method call
print("len = ${"hello".len()}")
// expect: len = 5

// Interpolation with nil
var n = nil
print("value is ${n}")
// expect: value is nil
```
