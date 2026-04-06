# Task 13: VM — Basic Execution

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement the VM dispatch loop — **first milestone where .ms scripts run end-to-end**.
**Dependencies:** T05, T06, T07, T12
**Produces:** `mslang-c` 可执行 .ms 脚本, 支持算术/比较/逻辑/位运算/变量/条件/循环/简单函数

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `include/ms/vm.h` | MsVM 结构, interpret API |
| Create | `src/vm.c` | 分派循环, 基础操作码 |
| Create | `src/vm_call.c` | CALL / RETURN |
| Modify | `src/main.c` | 接入编译+执行管线 |
| Create | `tests/unit/test_vm_basic.c` | VM 基础测试 |

## Key Data Structures / API

```c
// include/ms/vm.h
#define MS_STACK_SIZE (MS_FRAMES_MAX * MS_STACK_MAX)

typedef struct {
    MsObjClosure* closure;
    MsInstruction* ip;
    MsValue* slots;  // 帧的栈基地址
} MsCallFrame;

typedef enum {
    MS_INTERPRET_OK,
    MS_INTERPRET_COMPILE_ERROR,
    MS_INTERPRET_RUNTIME_ERROR,
} MsInterpretResult;

typedef struct MsVM {
    MsValue stack[MS_STACK_SIZE];
    MsValue* stack_top;
    MsCallFrame frames[MS_FRAMES_MAX];
    int frame_count;
    MsTable globals;
    MsTable strings;       // 字符串驻留表
    MsObject* objects;     // GC 全对象链表
    size_t bytes_allocated;
    size_t next_gc;
    MsObjUpvalue* open_upvalues;
    MsObjString* init_string;  // "init"
    struct MsCompiler* compiler; // GC root (编译期间)
    // GC gray stack (T16 填充)
    MsObject** gray_stack;
    int gray_count;
    int gray_capacity;
} MsVM;

void             ms_vm_init(MsVM* vm);
void             ms_vm_free(MsVM* vm);
MsInterpretResult ms_vm_interpret(MsVM* vm, const char* source, const char* path);
MsInterpretResult ms_vm_run(MsVM* vm);
void             ms_vm_runtime_error(MsVM* vm, const char* fmt, ...);
void             ms_vm_define_native(MsVM* vm, const char* name,
                                      MsNativeFn fn, int arity);
```

## Implementation Notes

### 分派循环 (switch-based)

```c
MsInterpretResult ms_vm_run(MsVM* vm) {
    MsCallFrame* frame = &vm->frames[vm->frame_count - 1];
    MsInstruction* ip = frame->ip;

#define READ_INSTR() (*ip++)
#define K(idx) (frame->closure->function->chunk.constants.data[idx])
#define RK(n) (MS_RK_IS_K(n) ? K(MS_RK_TO_K(n)) : frame->slots[n])
#define R(n) (frame->slots[n])

    for (;;) {
        MsInstruction instr = READ_INSTR();
        int A = MS_GET_A(instr);
        switch (MS_GET_OP(instr)) {
        case MS_OP_LOADK:     R(A) = K(MS_GET_Bx(instr)); break;
        case MS_OP_LOADNIL:   /* R(A)..R(B) = nil */ break;
        case MS_OP_LOADTRUE:  R(A) = MS_BOOL_VAL(true); break;
        case MS_OP_LOADFALSE: R(A) = MS_BOOL_VAL(false); break;
        case MS_OP_MOVE:      R(A) = R(MS_GET_B(instr)); break;
        case MS_OP_ADD:       /* RK(B) + RK(C) */ break;
        // ... 其余操作码
        case MS_OP_RETURN:    /* 返回 */ break;
        }
    }
#undef READ_INSTR
#undef K
#undef RK
#undef R
}
```

### 算术

支持 int-int, float-float, int-float 混合 (int 提升为 float):
```c
case MS_OP_ADD: {
    MsValue b = RK(B), c = RK(C);
    if (MS_IS_INT(b) && MS_IS_INT(c))
        R(A) = MS_INT_VAL(MS_AS_INT(b) + MS_AS_INT(c));
    else if (MS_IS_NUMERIC(b) && MS_IS_NUMERIC(c))
        R(A) = MS_NUMBER_VAL(ms_as_double(b) + ms_as_double(c));
    else if (MS_IS_STRING(b) && MS_IS_STRING(c))
        R(A) = MS_OBJ_VAL(ms_obj_string_concat(vm, MS_AS_STRING(b), MS_AS_STRING(c)));
    else runtime_error("Operands must be numbers or strings.");
    break;
}
```

### CALL / RETURN

CALL A B C: 调用 R(A), B-1 个参数 (R(A+1)..R(A+B-1)), C-1 个返回值.
- 检查 R(A) 是 Closure 或 Native
- Closure: push 新 MsCallFrame, slots = &R(A+1), ip = function->chunk.code
- Native: 直接调用 C 函数
- RETURN A B: 返回 B-1 个值从 R(A). pop frame, 将返回值写入调用者的目标寄存器

### ms_vm_interpret 流程

```c
MsInterpretResult ms_vm_interpret(MsVM* vm, const char* src, const char* path) {
    MsObjFunction* fn = ms_compile(vm, src, path, ...);
    if (!fn) return MS_INTERPRET_COMPILE_ERROR;
    MsObjClosure* cl = ms_obj_closure_new(vm, fn);
    // 设置第一帧
    vm->frames[0].closure = cl;
    vm->frames[0].ip = fn->chunk.code;
    vm->frames[0].slots = vm->stack;
    vm->frame_count = 1;
    vm->stack_top = vm->stack + fn->max_stack_size + 1;
    return ms_vm_run(vm);
}
```

## C Unit Tests

```c
// tests/unit/test_vm_basic.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_simple_arithmetic(void) {
    MsVM vm;
    ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm, "print(1 + 2)", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_simple_arithmetic();
    printf("test_vm_basic: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/arithmetic.ms
print(1 + 2)
// expect: 3
print(10 - 4)
// expect: 6
print(3 * 4)
// expect: 12
print(10 / 4)
// expect: 2.5
print(10 % 3)
// expect: 1
print(-7)
// expect: -7
print(1 + 2.5)
// expect: 3.5

// tests/fixtures/comparison.ms
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
print(2 >= 3)
// expect: false

// tests/fixtures/logic.ms
print(true and false)
// expect: false
print(false or true)
// expect: true
print(!true)
// expect: false
print(!nil)
// expect: true

// tests/fixtures/variables_runtime.ms
var x = 10
var y = 20
print(x + y)
// expect: 30
x = x * 2
print(x)
// expect: 20

// tests/fixtures/if_else.ms
if (5 > 3) {
  print("big")
} else {
  print("small")
}
// expect: big

// tests/fixtures/while_loop.ms
var i = 0
while (i < 3) {
  print(i)
  i = i + 1
}
// expect: 0
// expect: 1
// expect: 2

// tests/fixtures/for_loop.ms
for (var j = 0; j < 3; j = j + 1) {
  print(j)
}
// expect: 0
// expect: 1
// expect: 2

// tests/fixtures/functions_basic.ms
fun square(x) {
  return x * x
}
print(square(5))
// expect: 25

fun fib(n) {
  if (n <= 1) return n
  return fib(n - 1) + fib(n - 2)
}
print(fib(8))
// expect: 21

// tests/fixtures/strings_runtime.ms
print("hello" + " " + "world")
// expect: hello world

// tests/fixtures/bitwise.ms
print(1 << 3)
// expect: 8
print(16 >> 2)
// expect: 4
print(0xFF & 0x0F)
// expect: 15
print(0xA0 | 0x0B)
// expect: 171
print(~0)
// expect: -1
```
