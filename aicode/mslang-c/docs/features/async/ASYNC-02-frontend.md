# ASYNC-02: async/await 前端（Scanner + Compiler）

## Scanner 修改（`src/scanner.c`, `include/ms/token.h`）

### 新增 Token

`include/ms/token.h:29`（`yield` 所在行附近）添加：

```c
TK_ASYNC,   // async
TK_AWAIT,   // await
TK_SPAWN,   // spawn（可选，v1 先加 token，语义可后实现）
```

### 关键字识别（`src/scanner.c`）

在 `scan_identifier` 的关键字查找表（`scanner.c:190` 附近）中补：

```c
case 'a':
    if (match_keyword(s, "sync"))  return TK_ASYNC;
    if (match_keyword(s, "wait"))  return TK_AWAIT;
    break;
case 's':
    // 现有 s 分支（super/self 等）
    if (match_keyword(s, "pawn"))  return TK_SPAWN;
    break;
```

`async`/`await` 可能与现有标识符冲突——检查测试套件里是否有变量名为 `async`/`await`，  
若有则迁移（与关键字升级的通常做法相同）。

## Compiler 修改（`src/compiler.c`, `src/compiler_expr.c`）

### async fun 声明

`parse_function_decl`（或 `compile_fun`，grep `fun` 关键字解析点）：

```c
// 检测 async 修饰符
bool is_async = false;
if (check(TK_ASYNC)) {
    advance();
    is_async = true;
}
expect(TK_FUN, "expected 'fun'");
// ...编译函数体...
function->is_async = is_async;  // ObjFunction 新增 bit flag
```

`MsObjFunction`（`include/ms/object.h`）在现有 flag/arity 字段旁加：

```c
bool is_async;   // 或折入现有 flags bitmask
```

`is_async` 函数在调用时不直接执行，而是生成一个 `ObjFuture`（见 ASYNC-03）。

### async fun 调用路径

async 函数可通过四种路径调用，统一抽出 helper：

```c
// src/vm_call.c
static MsObjFuture* ms_make_async_future(MsVM* vm, MsObjClosure* closure,
                                          int argc, MsValue* argv) {
    if (!vm->loop_inited) { ms_loop_init(&vm->event_loop, vm); vm->loop_inited = true; }
    MsObjFuture* fut = ms_obj_future_from_async(vm, closure, argc, argv);
    ms_loop_call_soon(&vm->event_loop, fut->coro);
    return fut;
}
```

**路径 1：直接 closure 调用**（`call_value`，`src/vm.c:490`）：

```c
if (closure->function->is_async) {
    MsObjFuture* fut = ms_make_async_future(vm, closure, argc, argv);
    frame->slots[A] = MS_OBJECT_VAL((MsObject*)fut);
    break;
}
```

**路径 2：bound method 调用**（`OP_INVOKE` 中 `invoke_from_class`）：

```c
// 取出 method closure 后与路径 1 相同
if (method->function->is_async) {
    // 把 receiver 追加为 argv[0]，再调 ms_make_async_future
    MsObjFuture* fut = ms_make_async_future(vm, method, argc, argv);
    frame->slots[A] = MS_OBJECT_VAL((MsObject*)fut);
    return;
}
```

**路径 3：super 方法调用**（`OP_SUPER_INVOKE`）：同路径 2，调 `ms_make_async_future`。

**路径 4：原生函数返回 Future**：原生函数本身同步返回一个 `MsObjFuture*`（已处于 PENDING 或 RESOLVED 状态），调用方直接 `await` 即可，无需 EventLoop 介入创建协程。例：`tcp_connect` 返回 PENDING future，IO 就绪时由 Reactor 回调 resolve。

```c
// 不需要 is_async 标志；原生函数返回值若为 Future，await 路径自动处理
```

### await 表达式

`parse_await`（`src/compiler_expr.c`）遵循 Pratt + ExprDesc 模型：

```c
static void parse_await(MsCompiler* c, ExprDesc* ed) {
    if (!c->in_async_fun)
        error(c, "'await' can only be used inside an async function");

    ExprDesc futured;
    parse_expr_prec(c, &futured, PREC_UNARY);   // 编译 awaited 表达式
    int fr = discharge_to_reg(c, &futured);      // future 落到寄存器 fr
    int rr = alloc_reg(c);                       // 分配结果寄存器 rr
    emit_abc(c, MS_OP_AWAIT, rr, fr, 0);
    // 结果以寄存器形式返回给上层表达式
    ed->kind = EXPR_REG;
    ed->reg  = rr;
}
```

此函数作为 `TK_AWAIT` 的 prefix parse rule 注册到 Pratt 表，无需全局 `result_reg`/`future_reg` 变量。

同时，编译器在 `parse_yield`（处理 `OP_YIELD`）中加检查：

```c
if (c->in_async_fun)
    error(c, "'yield' inside async function");
```

`OP_AWAIT` 语义（`src/vm.c` dispatch loop）：

1. 取 `future_reg` 里的 `ObjFuture`
2. 若 `future->state == RESOLVED`：`result_reg = future->value`，继续执行
3. 若 `future->state == PENDING`：把当前协程加入 `future->waiters`，挂起（类似 `yield`）
4. 若 `future->state == REJECTED`：抛出异常

### 编译器状态追踪

`MsCompiler`（`include/ms/compiler_impl.h`）加字段：

```c
bool in_async_fun;   // 用于 await 合法性检查
```

进入 `async fun` 编译时置 `true`，退出时恢复。

## 生成的字节码形态

```ms
async fun add(a, b) {
    var x = await get_value()
    return x + b
}
```

编译输出（伪码）：

```
CLOSURE    r0, <add>        ; async flag set on function
...
; 在 add 函数体内：
CALL       r1, get_value, 0
OP_AWAIT   r2, r1           ; 若 r1(future) pending 则挂起
ADD        r3, r2, b
RETURN     r3
```

## 验证

- `tests/unit/test_scanner.c`：扫描 `async`/`await` 得正确 token
- `tests/unit/test_compiler.c`：
  - `async fun` 生成 `is_async=true` 的函数对象
  - `await` 在 non-async fun 中产生编译期报错
  - `await` 在 async fun 中生成 `OP_AWAIT`
- conformance：已有测试不受影响（`async`/`await` 原本是标识符，需确认无命名冲突）
