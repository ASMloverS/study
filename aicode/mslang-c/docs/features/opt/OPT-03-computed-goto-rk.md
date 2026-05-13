# OPT-03: Computed-Goto 派发 + RK 专化 opcode

## 背景

主派发循环（`src/vm.c:731-749`）使用 `switch(op)`，在 GCC/Clang 下通常编译为间接跳转，  
但 CPU 分支预测器对 `switch` 的效果不如 **computed-goto 跳转表**（dispatch table）——  
后者每条指令末尾直接跳入下一条指令处理器，消除回到 switch 顶部的跳转。  
文档 T29 承诺此优化但未实现。

`RK(n)` 宏（`vm.c:736`）在运行时区分"寄存器"vs"常量"，产生一次分支。  
对常见 `reg op const` 模式（如 `a + 1`、`a < 10`），可在编译期发射专化 opcode 消除这一分支。

## Part 1 — Computed-Goto 派发

### 实现结构

```c
// src/vm.c 主循环顶部
#if defined(__GNUC__) || defined(__clang__)
#define USE_COMPUTED_GOTO 1
#endif

#ifdef USE_COMPUTED_GOTO
    // 跳转表——顺序必须与 OpCode 枚举完全一致
    static const void* const dispatch_table[MS_OP_COUNT] = {
        &&L_OP_LOAD_NIL,
        &&L_OP_LOAD_TRUE,
        // ... 所有 opcode 按枚举顺序
        &&L_OP_RETURN,
    };
    #define DISPATCH()  goto *dispatch_table[READ_INSTR() & 0xFF]
    #define CASE(op)    L_##op:
    #define DEFAULT()   L_UNKNOWN:
    DISPATCH();  // 进入第一条指令
#else
    #define DISPATCH()  continue
    #define CASE(op)    case op:
    #define DEFAULT()   default:
#endif

for (;;) {
#ifndef USE_COMPUTED_GOTO
    MsInstruction instr = READ_INSTR();
    uint8_t op = MS_OP(instr);
    switch (op) {
#endif

    CASE(MS_OP_LOAD_NIL) {
        // ...
        DISPATCH();
    }
    // ...

#ifndef USE_COMPUTED_GOTO
    DEFAULT() { runtime_error(vm, "unknown opcode"); break; }
    }  // switch
#endif
}
```

### 关键点

- `READ_INSTR()` 在 `DISPATCH()` 内调用，不在循环顶部——每条指令自己 fetch 下一条。
- MSVC 保持原 `switch`，`DISPATCH()` = `continue`，无任何语义变化。
- `__GNUC__` 分支需要 `&&label` 扩展（GCC 4.x+ / Clang 均支持）。
- A/B/C 解码移到各 `CASE` 内部，只解码当前 op 需要的字段（iABC 的 B/C，iABx 的 Bx）。

### 文件：`src/vm.c:731-749`（主循环改造）

## Part 2 — RK 专化 opcode

### 原理

编译期已知某操作数是常量时（编译器发射 `OP_ADD a, b, K(c)` 中 RK 位置），  
可改发 `OP_ADD_RK a, b, c`——`c` 直接是常量池索引，dispatch 处无 `RK_IS_K` 分支。

### 新增 opcode（`include/ms/opcode.h`）

```c
// 在现有特化 op 之后追加
MS_OP_ADD_RK,    // ADD a, reg, K  — C 是常量索引
MS_OP_SUB_RK,
MS_OP_MUL_RK,
MS_OP_DIV_RK,
MS_OP_LT_RK,
MS_OP_LE_RK,
MS_OP_EQ_RK,
MS_OP_GETGLOBAL_CACHED,  // GETGLOBAL 带 shape/hash 缓存（后续 quickening 用）
```

### 编译器发射规则（`src/compiler_expr.c`）

在 `emit_binary` 或对应的 `parse_*` 路径中：

```c
// 当右操作数是常量折叠结果（已在常量池）时
if (right_is_const) {
    emit(MS_OP_ADD_RK, dest, left_reg, const_idx);
} else {
    emit(MS_OP_ADD, dest, left_reg, right_reg);
}
```

> 现有 quickening（`vm.c:798+`）已有 `ADD` → `ADD_II/FF` 的自重写；  
> `ADD_RK` 可作为一个中间层：类型未知时先用 `ADD_RK`，quicken 后再变成 `ADD_RK_II` 等。  
> v1 可以先只做 `ADD_RK`（免 RK 分支），不做进一步类型特化，留给 quickening 路径。

### VM 实现（`src/vm.c`）

```c
CASE(MS_OP_ADD_RK) {
    MsValue left  = frame->slots[B];
    MsValue right = frame->closure->function->chunk.constants.data[C]; // 直接索引，无分支
    // ... 同 ADD 但跳过 RK_IS_K 检查
    DISPATCH();
}
```

### .msc 兼容性

新增 opcode 改变枚举值，`.msc` 缓存需 version bump（见 OPT-04，两者可合并为一次 bump）。

## 预期收益

| Benchmark | 预期提速 |
|---|---|
| `arith_loop.ms` | 25–40% |
| `fib_recursive.ms` | 20–35% |
| `quickening_deopt.ms` | 验证退优化路径不崩溃 |

## 验证

```bash
cmake --build build && cd build && ctest --output-on-failure
python ../benchmarks/run_all.py --compare baseline.json --runs 5
```

编译器单元测试：`tests/unit/test_compiler.c` 验证 `ADD_RK` 被正确发射。  
conformance 100% 通过。
