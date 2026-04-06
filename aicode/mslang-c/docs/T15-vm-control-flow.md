# Task 15: VM — Break, Continue, Switch, Lambda

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Complete VM support for break/continue in loops, switch/case dispatch, and anonymous functions (lambda).
**Dependencies:** T14
**Produces:** 循环中的 break/continue, switch 分派, 匿名函数

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/vm.c` | 确保 JMP/TEST 正确支持循环控制 |
| Modify | `src/compiler.c` | 完善 break/continue 补丁, lambda 语法 |
| Create | `tests/unit/test_vm_control.c` | 控制流测试 |

## Implementation Notes

### break / continue

break/continue 在编译器侧已处理 (T12): 发射 JMP 并链入 LoopCtx 的 break_list. VM 侧只需正确执行 JMP (已在 T13 实现). 本任务确保:
- 嵌套循环中 break 只跳出最内层
- continue 跳到循环的 post 部分 (for) 或条件判断 (while)
- break 在 switch 中跳出 switch (不影响外层循环)

### Lambda (匿名函数)

语法: `fun(x) { return x * 2 }` 或 `fun(x, y) { return x + y }`

编译方式与命名函数相同, 但 ObjFunction.name = NULL 或 `<lambda>`. 通过 CLOSURE 指令创建.

### switch 执行

switch 在编译器侧生成线性比较链 (EQ + JMP). VM 只需正确执行 EQ 比较和条件跳转.

## C Unit Tests

```c
// tests/unit/test_vm_control.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_break(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "var i = 0\n"
        "while (true) {\n"
        "  if (i >= 3) break\n"
        "  print(i)\n"
        "  i = i + 1\n"
        "}", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_break();
    printf("test_vm_control: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/break.ms
var i = 0
while (true) {
  if (i >= 3) break
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

// tests/fixtures/continue.ms
for (var i = 0; i < 5; i = i + 1) {
  if (i == 2) continue
  if (i == 4) continue
  print(i)
}
// expect: 0
// expect: 1
// expect: 3

// tests/fixtures/nested_break.ms
for (var i = 0; i < 3; i = i + 1) {
  for (var j = 0; j < 3; j = j + 1) {
    if (j == 1) break
    print(i * 10 + j)
  }
}
// expect: 0
// expect: 10
// expect: 20

// tests/fixtures/switch.ms
fun classify(n) {
  switch (n) {
    case 1: return "one"
    case 2: return "two"
    case 3: return "three"
    default: return "other"
  }
}
print(classify(1))
// expect: one
print(classify(2))
// expect: two
print(classify(5))
// expect: other

// tests/fixtures/lambda.ms
var double_it = fun(x) { return x * 2 }
print(double_it(5))
// expect: 10

fun apply(f, x) {
  return f(x)
}
print(apply(fun(n) { return n + 100 }, 42))
// expect: 142

// tests/fixtures/higher_order.ms
fun make_multiplier(factor) {
  return fun(x) { return x * factor }
}
var triple = make_multiplier(3)
print(triple(7))
// expect: 21
```
