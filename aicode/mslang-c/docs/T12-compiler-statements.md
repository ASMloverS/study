# Task 12: Compiler — Statements and Control Flow

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Add all control-flow statements and function declarations to compiler: if/else, while, for, break/continue, switch/case, fun.
**Dependencies:** T11
**Produces:** 编译器支持完整控制流和函数声明

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | 添加 if/while/for/switch/fun/return |
| Create | `tests/unit/test_compiler_stmts.c` | 控制流编译测试 |

## Implementation Notes

### if / else

```
if (cond) { then } else { alt }
```
1. 编译 cond → 寄存器
2. 发射 `TEST A 0` + `JMP exit_then` (若 false 跳过 then)
3. 编译 then-body
4. 发射 `JMP exit_else` (跳过 else)
5. 回填 exit_then 目标
6. 编译 else-body
7. 回填 exit_else 目标

### while

```
while (cond) { body }
```
1. `loop_start = current_ip`
2. 编译 cond → `TEST + JMP(exit)`
3. 编译 body
4. 发射 `JMP(loop_start)`
5. 回填 exit 目标

### for (C-style)

```
for (init; cond; post) { body }
```
1. begin_scope
2. 编译 init (var 声明或表达式)
3. `loop_start = current_ip`
4. 编译 cond → `TEST + JMP(exit)`
5. 编译 body (break/continue 注册到 LoopCtx)
6. 编译 post
7. `JMP(loop_start)`
8. 回填 exit + break list
9. end_scope

### break / continue

- `break`: 发射 `JMP(placeholder)`, 加入 `loop->break_list` 补丁链
- `continue`: 发射 `JMP(placeholder)`, 需跳到 post 表达式之前 (或 loop_start)

补丁链使用 JMP 指令的 sBx 字段临时存储前一个补丁的偏移 (linked list in-place):
```c
static void patch_list(MsCompiler* c, int list, int target) {
    while (list != NO_JUMP) {
        int next = get_jump_target(c, list);  // 读旧 sBx
        patch_jump(c, list, target);
        list = next;
    }
}
```

### switch / case

```
switch (expr) { case v1: ... case v2: ... default: ... }
```
编译为线性比较链:
1. 编译 switch expr → 寄存器 reg
2. 对每个 case: 编译 case value → 常量, `EQ reg K(value)`, `JMP(next_case)`, 编译 case body
3. default: 直接编译 body
4. 回填所有 case 的 JMP

### 函数声明

```
fun foo(a, b) { body }
```
1. 创建嵌套 MsCompiler (enclosing = 当前编译器)
2. 参数成为 locals[0], locals[1], ...
3. 编译函数体
4. 结束编译 → 返回 MsObjFunction* (作为 proto)
5. 外层发射 `CLOSURE A Bx` (Bx = proto 在常量池的索引)

**return 语句**: 编译 expr → reg, 发射 `RETURN reg 2` (1 个返回值). 无表达式: `RETURN 0 1` (nil).

### 表达式语句和 print

- `print(expr)` 暂时作为内置语句处理, 也可编译为全局函数调用
- 表达式语句: 编译表达式, 丢弃结果 (free_reg)

## C Unit Tests

```c
// tests/unit/test_compiler_stmts.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_if_else(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "if (true) { print(1) } else { print(2) }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "if_else");
    ms_vm_free(&vm);
}

static void test_while_loop(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "var i = 0\nwhile (i < 3) { i = i + 1 }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "while");
    ms_vm_free(&vm);
}

static void test_function_decl(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8]; int dc = 0;
    MsObjFunction* fn = ms_compile(&vm,
        "fun add(a, b) { return a + b }",
        "<test>", diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "fun_decl");
    ms_vm_free(&vm);
}

int main(void) {
    test_if_else();
    test_while_loop();
    test_function_decl();
    printf("test_compiler_stmts: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/control_flow.ms (run after T13)
if (true) {
  print("yes")
}
// expect: yes

if (false) {
  print("no")
} else {
  print("else")
}
// expect: else

var i = 0
while (i < 3) {
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

for (var j = 10; j < 13; j = j + 1) {
  print(j)
}
// expect: 10
// expect: 11
// expect: 12

fun add(a, b) {
  return a + b
}
print(add(3, 4))
// expect: 7

fun fib(n) {
  if (n <= 1) return n
  return fib(n - 1) + fib(n - 2)
}
print(fib(8))
// expect: 21

switch (2) {
  case 1: print("one")
  case 2: print("two")
  default: print("other")
}
// expect: two

// Nested if
var x = 15
if (x > 10) {
  if (x > 20) {
    print("very big")
  } else {
    print("medium")
  }
} else {
  print("small")
}
// expect: medium
```
