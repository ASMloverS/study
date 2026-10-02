# T023 — 异常系统与 defer

- 阶段: P6 异常/模块（roadmap 期 6）
- 依赖: T020
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/07-errors.md` 全文、`21-bytecode.md` §3.7

## 1. 目标与范围

实现内置异常类树（07 §2 全树）、throw/try/catch/finally（PUSH_TRY/POP_TRY/THROW/MATCH_CATCH/RERAISE）、defer（PUSH_DEFER/RUN_DEFERS）、unwind 语义、异常对象与 traceback、uncaught 处理（彩色 traceback + 退出码 1）。出口：roadmap 期 6 前半——07 文档用例全过。

不做：异常跨 goroutine 语义（T029，本任务单执行流）、`errors` 模块工具（T034）。

## 2. 实现要点

- 类树（07 §2）：Error 为根的 18 类层级（ValueError/TypeError/KeyError/IndexError/ArithmeticError>ZeroDivisionError/OverflowError/ArgumentError/AttributeError/RuntimeError>StackOverflowError+IteratorError/ImportError/IOError>FileNotFoundError/ChannelError/CancelledError + AssertionError）；内置无需 import；`e.msg` 字段、`str(e)` = `"TypeName: msg"`；构造 `ValueError("msg")`。
- try 编译：PUSH_TRY <catchT> <finallyT> 建 unwinder 记录（catch 链表头 + defer 链）；POP_TRY 弹出；THROW 取栈顶值沿当前帧 unwinder 搜索：先 RUN_DEFERS（LIFO），再首个类型匹配 catch（MATCH_CATCH instanceof 语义，多类型子句任一）。
- catch 匹配（07 §1）：类型子句自上而下首个匹配；`catch e` 裸绑定须在最后（解析已保证）；绑定 e 进 catch 块作用域；`catch V { }` 不绑形式。
- finally（07 §3）：正常退出/捕获/再抛出/取消均执行；finally 内 return/throw 覆盖进行中的退出值；实现为退出路径上的受保护执行段 + 覆盖语义状态机。
- RERAISE：catch 块内裸 `throw` 重抛当前异常并保留原 traceback。
- traceback：异常对象携 `e.trace`（帧列表：file:line:fn），throw 点展开填充；uncaught 打印彩色 traceback（含源码行 + `^`）退出码 1；颜色可 `NO_COLOR` 关闭。
- defer（04 §7、07 §4）：PUSH_DEFER 注册（调用目标 + 立即求值的实参快照）；RUN_DEFERS <n> 在函数退出（正常 return / 异常 unwind / 取消）LIFO 执行；defer 内异常按普通异常处理且不阻止其余 defer（逐个尽力）；defer 中 throw 的异常替换进行中的退出。
- 既有"错误槽最小路径"（除零/越界等 T011-T017 引入点）全部切换为真实异常对象抛出——语义统一回归。

## 3. 涉及文件

新增: `src/vm/errors.c/.h`（类树构建 + unwind）、`src/vm/exceptions_data.c`（内置异常注册）、`tests/unit/test_exceptions.c`、`tests/fixtures/exception/*.ms`
修改: `src/compiler/compiler.c`（try/catch/finally/defer/throw 编码）、`src/vm/vm.c`（THROW/TRY 族/DEFER 族指令、错误槽切换）、`src/vm/vm.c`（全部内置错误点改抛异常）、`tools/ms.c`（uncaught 打印与退出码）

## 4. 验收标准（DoD）

- [ ] 07 §1-§5 全部示例输出逐字匹配（含 defer→catch 顺序例）
- [ ] finally 四路径（正常/捕获/再抛/嵌套）全过；finally 覆盖 return/throw
- [ ] 裸 throw 重抛保留 traceback（行号断言）
- [ ] 自定义异常类（`class AppError : Error`）可抛可捕
- [ ] 既有 fixtures 中"stderr TypeError/退出码 1"的用例在真实异常下行为不变
- [ ] uncaught traceback 输出格式（文件:行 + 源码行 + 指示）符合 20 §1 风格

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_exceptions.c`

| 用例 | 源码 | 期望 |
|------|------|------|
| 按类型捕获 | throw TypeError → catch TypeError, Error | 首个匹配 |
| 多类型子句 | `catch ValueError, TypeError` | 任一命中 |
| 未捕获传播 | 内层 throw 外层 catch | e.msg 保持 |
| finally 必执行 | 正常/异常双路径 | 标记输出顺序 |
| finally 覆盖 | finally 内 return | 覆盖值生效 |
| defer LIFO | 注册 a,b,c | 输出 c,b,a |
| defer 异常链 | defer 内再 throw | 后续 defer 仍执行 |
| 07 §4 示例 | work() defer+throw | `always` → `ValueError(...)` |
| RERAISE | catch 内裸 throw | 外层再捕获、trace 行号保留 |
| 子类匹配 | ZeroDivisionError → catch ArithmeticError | 命中 |

### 5.2 ms fixtures `tests/fixtures/exception/`

- `basics.ms`：

```ms
try {
    throw ValueError("bad input: ${-1}")
} catch e {
    print(e.str())          // ValueError: bad input: -1
}
try { risky() } catch ValueError, TypeError { print("vt") } catch e { print("all") }
```

- `defer_unwind.ms`（07 §4 原例 + 资源惯用法 io 伪句柄版本）
- `finally_paths.ms`：四路径 + finally 覆盖
- `traceback.ms`：深层调用 uncaught → `.code`=1、stderr 含 traceback 行（`.err` 前缀匹配）
- `custom_error.ms`：AppError 双字段构造与捕获
- `err_mapping.ms`：除零/缺键/越界/迭代违约 → 对应类型捕获

## 6. 风险与备注

- unwind 与闭包闭合点的顺序：帧销毁时先 RUN_DEFERS 再闭合 upvalue——顺序错误将导致 defer 内闭包读已闭合值，单测专设此场景。
- 彩色输出在 Windows Terminal 旧控制台的兼容：启用 VT 处理失败时降级纯文本（plat 层一次性探测）。
