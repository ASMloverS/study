# Task 11: Compiler — Variables and Scoping

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Add variable declarations, local/global resolution, scoping with block statements, and compound assignment.
**Dependencies:** T10
**Produces:** 编译器支持 `var` 声明, 块作用域, 赋值和复合赋值

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Modify | `src/compiler.c` | 添加 var 声明, 作用域管理, 赋值 |
| Modify | `src/compiler_expr.c` | 添加标识符解析 (local/global) |
| Create | `tests/unit/test_compiler_vars.c` | 变量编译测试 |

## Key Data Structures / API

无新公共 API — 扩展 MsCompiler 内部行为。

## Implementation Notes

### 全局变量

```
var x = expr    // 顶层 (scope_depth == 0)
```
1. 编译 expr → 寄存器 reg
2. 发射 `DEFGLOBAL reg, K(name_idx)` — 其中 name_idx 是变量名字符串在常量池的索引

读全局: GETGLOBAL A Bx — `R(A) = globals[K(Bx)]`
写全局: SETGLOBAL A Bx — `globals[K(Bx)] = R(A)`

### 局部变量

```
{ var x = expr }    // scope_depth > 0
```
1. `alloc_reg()` 为 x 分配寄存器 slot
2. 编译 expr → 该寄存器
3. 记录到 `locals[]`: name=token, depth=scope_depth, slot=reg, is_captured=false

**解析标识符**: 从 `locals[]` 尾部向前扫描, 匹配 token 文本 → 返回 `EDESC_LOCAL(slot)`. 未找到 → 返回 `EDESC_GLOBAL(name_const_idx)`.

### 作用域管理

```c
static void begin_scope(MsCompiler* c) { c->scope_depth++; }
static void end_scope(MsCompiler* c) {
    c->scope_depth--;
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        MsLocal* local = &c->locals[c->local_count - 1];
        if (local->is_captured) {
            emit(c, ms_enc_ABC(MS_OP_CLOSE, local->slot, 0, 0));
        }
        c->local_count--;
        free_reg(c, local->slot);
    }
}
```

### 赋值和复合赋值

赋值在 Pratt parser 中通过 `can_assign` 参数控制:
- 解析标识符时, 若 `can_assign && match(EQUAL)` → 编译右值, 发射 SET 指令
- 复合赋值 `x += expr` 等价于 `x = x + expr`: 读取 x → 编译 expr → 发射 ADD → 发射 SET

### 字符串常量去重

```c
static int add_string_constant(MsCompiler* c, MsObjString* s) {
    MsValue existing;
    if (ms_table_get(&c->string_cache, s, &existing)) {
        return (int)MS_AS_INT(existing);
    }
    int idx = ms_chunk_add_constant(current_chunk(c), MS_OBJ_VAL(s));
    ms_table_set(&c->string_cache, s, MS_INT_VAL(idx));
    return idx;
}
```

## C Unit Tests

```c
// tests/unit/test_compiler_vars.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_global_var(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "var x = 10\nprint(x)", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "global_var");
    // Expect: LOADK R(0) K(10), DEFGLOBAL R(0) K("x"), GETGLOBAL R(0) K("print"), ...
    ms_vm_free(&vm);
}

static void test_local_scope(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int dc = 0;
    MsObjFunction* fn = ms_compile(&vm, "{ var a = 1\n var b = 2 }", "<test>",
                                    diags, &dc, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "local_scope");
    ms_vm_free(&vm);
}

int main(void) {
    test_global_var();
    test_local_scope();
    printf("test_compiler_vars: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/variables.ms (run after T13)
var x = 10
print(x)
// expect: 10
x = x + 1
print(x)
// expect: 11

var greeting = "hello"
print(greeting)
// expect: hello

{
  var inner = 42
  print(inner)
  // expect: 42
}

var a = 1
var b = 2
var c = a + b
print(c)
// expect: 3

// Compound assignment
var n = 10
n += 5
print(n)
// expect: 15
n -= 3
print(n)
// expect: 12
n *= 2
print(n)
// expect: 24
n /= 4
print(n)
// expect: 6
n %= 4
print(n)
// expect: 2

// Shadowing in nested scope
var s = "outer"
{
  var s = "inner"
  print(s)
  // expect: inner
}
print(s)
// expect: outer
```
