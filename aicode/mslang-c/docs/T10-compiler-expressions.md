# Task 10: Compiler — Expressions

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement single-pass Pratt parser for expressions: literals, arithmetic, comparisons, logical and/or, unary, bitwise, grouping. Uses ExprDesc to minimize register moves.
**Dependencies:** T05, T06, T08
**Produces:** `ms_compile()` 可编译表达式为字节码; 反汇编可验证输出

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/compiler.h` | 公共编译接口 |
| Create | `src/compiler_impl.h` | 内部结构 (MsCompiler, ExprDesc, Local 等) |
| Create | `src/compiler.c` | 核心编译逻辑, advance/consume/emit |
| Create | `src/compiler_expr.c` | 表达式解析函数 |
| Modify | `CMakeLists.txt` | 添加 ms_frontend 库 |
| Create | `tests/unit/test_compiler_expr.c` | 表达式编译测试 |

## Key Data Structures / API

```c
// include/ms/compiler.h
#pragma once
#include "ms/object.h"

typedef struct {
    int line, column;
    char message[256];
} MsDiagnostic;

typedef struct MsVM MsVM;

// 编译源码为顶层函数; 失败返回 NULL
MsObjFunction* ms_compile(MsVM* vm, const char* source, const char* path,
                           MsDiagnostic* diags, int* diag_count, int max_diags);
```

```c
// src/compiler_impl.h (内部头文件)
typedef enum {
    EDESC_VOID, EDESC_NIL, EDESC_TRUE, EDESC_FALSE,
    EDESC_NUMBER, EDESC_INT, EDESC_CONST,
    EDESC_REG, EDESC_GLOBAL, EDESC_LOCAL, EDESC_UPVAL, EDESC_PROP,
} MsExprKind;

typedef struct {
    MsExprKind kind;
    union {
        double number;
        ms_i64 integer;
        int idx;
        struct { int obj_reg; int key_const; } prop;
    };
    int true_list;   // JMP patch chain for true
    int false_list;  // JMP patch chain for false
} MsExprDesc;

#define NO_JUMP (-1)

typedef struct {
    MsToken name;
    int depth;
    bool is_captured;
    int slot;
} MsLocal;

typedef struct {
    bool is_local;
    int index;
} MsUpvalueDesc;

typedef struct MsLoopCtx {
    int start;
    int break_list;
    int depth;
    struct MsLoopCtx* enclosing;
} MsLoopCtx;

typedef struct MsClassCompiler {
    struct MsClassCompiler* enclosing;
    bool has_superclass;
} MsClassCompiler;

typedef void (*MsParseFn)(struct MsCompiler*, bool can_assign);

typedef enum {
    PREC_NONE = 0, PREC_ASSIGNMENT, PREC_TERNARY, PREC_OR, PREC_AND,
    PREC_EQUALITY, PREC_COMPARISON, PREC_BIT_OR, PREC_BIT_XOR,
    PREC_BIT_AND, PREC_SHIFT, PREC_TERM, PREC_FACTOR,
    PREC_UNARY, PREC_CALL, PREC_PRIMARY,
} MsPrecedence;

typedef struct {
    MsParseFn prefix;
    MsParseFn infix;
    MsPrecedence precedence;
} MsParseRule;

typedef struct MsCompiler {
    MsScanner scanner;
    MsToken current;
    MsToken previous;
    bool had_error;
    bool panic_mode;
    MsDiagnostic* diags;
    int* diag_count;
    int max_diags;
    MsVM* vm;
    MsObjFunction* function;
    struct MsCompiler* enclosing;
    MsLocal locals[256];
    int local_count;
    int scope_depth;
    MsUpvalueDesc upvalues[MS_MAX_UPVALUES];
    int next_reg;
    int max_reg;
    MsLoopCtx* loop;
    MsClassCompiler* klass;
    MsTable string_cache;  // string dedup: ObjString* → int(const idx)
} MsCompiler;
```

## Implementation Notes

- **Pratt Parser**: `parse_precedence(c, min_prec)` 先调用 prefix 函数, 然后循环调用 infix 函数 (只要当前 token 的优先级 >= min_prec)
- **ExprDesc**: 延迟求值描述符. `EDESC_NUMBER/INT` 不立即发射 LOADK, 直到需要放入寄存器时才发射. 这允许常量折叠
- **expr_to_reg(c, e)**: 将 ExprDesc 物化为寄存器: NIL→LOADNIL, TRUE→LOADTRUE, NUMBER→LOADK, REG→无操作, GLOBAL→GETGLOBAL, etc.
- **常量折叠**: binary(+,-,*,/,%) 若两侧均为 EDESC_NUMBER/INT, 直接计算返回新 EDESC. 例: `1 + 2` → EDESC_INT(3)
- **逻辑 and/or**: 使用 TEST + JMP 短路. and: 若左侧 false → 跳过右侧; or: 若左侧 true → 跳过右侧

核心函数:
```c
static void advance(MsCompiler* c);
static void consume(MsCompiler* c, MsTokenType type, const char* msg);
static int  emit(MsCompiler* c, MsInstruction instr);
static int  alloc_reg(MsCompiler* c);
static void free_reg(MsCompiler* c, int reg);
static int  expr_to_reg(MsCompiler* c, MsExprDesc* e);
static int  expr_to_any_reg(MsCompiler* c, MsExprDesc* e);
static int  add_constant(MsCompiler* c, MsValue val);
static MsExprDesc parse_precedence(MsCompiler* c, MsPrecedence min);
static MsExprDesc expression(MsCompiler* c);
```

## C Unit Tests

```c
// tests/unit/test_compiler_expr.c
#include "test_assert.h"
#include "ms/compiler.h"
#include "ms/debug.h"
#include "ms/vm.h"

static void test_constant_folding(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(1 + 2)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    // 1+2 should be folded to constant 3
    // Expect: LOADK (for 3), then GETGLOBAL/CALL for print
    ms_disasm_chunk(&fn->chunk, "constant_fold");
    ms_vm_free(&vm);
}

static void test_unary(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(&vm, "print(-5)", "<test>",
                                    diags, &diag_count, 8);
    TEST_ASSERT(fn != NULL);
    ms_disasm_chunk(&fn->chunk, "unary");
    ms_vm_free(&vm);
}

int main(void) {
    test_constant_folding();
    test_unary();
    printf("test_compiler_expr: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/expressions.ms (run after T13)
print(1 + 2)
// expect: 3
print(10 - 3)
// expect: 7
print(2 * 6)
// expect: 12
print(10 / 4)
// expect: 2.5
print(10 % 3)
// expect: 1
print(-5)
// expect: -5
print(!true)
// expect: false
print(!nil)
// expect: true
print(1 < 2)
// expect: true
print(2 > 1)
// expect: true
print(1 == 1)
// expect: true
print(1 != 2)
// expect: true
print(1 <= 1)
// expect: true
print(true and false)
// expect: false
print(false or true)
// expect: true
print(nil or "default")
// expect: default
print("a" + "b")
// expect: ab
print(1 << 3)
// expect: 8
print(15 & 9)
// expect: 9
print(~0)
// expect: -1
```
