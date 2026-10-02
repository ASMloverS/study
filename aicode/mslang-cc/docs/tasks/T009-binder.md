# T009 — Binder：作用域分析与 conformance 快照

- 阶段: P2 语法（roadmap 期 2）
- 依赖: T008
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/20-architecture.md` §1（Binder 职责）、`04-statements.md` §1 §6、`05-functions.md` §3

## 1. 目标与范围

实现 Binder（AST 标注遍历）：作用域栈与名称解析（local/global/upvalue 分类）、控制流位置合法性、常量折叠。同时建立 AST 快照 dump 与 conformance 基线——roadmap 期 2 出口（全部 conformance 语法树快照匹配）。

不做：字节码生成（T010）、重复声明的运行时语义（`const` 再赋值是运行时 TypeError）。

## 2. 实现要点

- 遍历产出"标注 AST"：每个标识符节点标注解析类别与槽位序号（`local slot n / global / upvalue d`），供编译器直接消费。

```c
typedef struct BinderScope { /* 函数/块/推导式/类体作用域 */ } BinderScope;
// 规则：
// - 函数参数与 var/const/:= 进入当前作用域，分配槽位
// - := 要求当前作用域内新建（已存在则报重复声明）
// - 内层引用外层函数局部 → 记 upvalue 需求（捕获描述，T018 消费）
// - 模块层标识符 → global
// - for 迭代变量/推导式变量每轮新绑定：为循环体生成子作用域（语义标注，编译期落实）
```

- 位置合法性（04 §6）：break/continue 仅循环体内；return 顶层非法；`await` 仅 async 函数体与模块顶层；`<-` recv/send 不在顶层之外受限（channel 语句任意函数内合法，模块顶层仅 main goroutine 合法——v1 全部合法，不检查）。
- 解构目标、`case … as`、`catch e` 均在各自作用域注册绑定。
- 常量折叠（20 §1）：纯字面量运算在 int/float/bool/nil 范围内折叠（`1 + 2` → `3`；`"a" + "b"` 不折叠——字符串拼接非折叠目标，留给插值优化）；溢出 int 折叠结果升 bigint 字面量。
- AST dump：规范化文本输出（节点 + 关键域 + 标注，忽略纯位置差异），`ms` 调试开关 `--dump-ast`。

## 3. 涉及文件

新增: `src/binder/binder.h`、`src/binder/binder.c`、`src/parser/ast_dump.c`、`tests/unit/test_binder.c`、`tests/conformance/`（快照基线集，~60 用例）
修改: `tools/ms.c`（`--dump-ast` 开关）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 快照 dump 对 03/04/05/06 文档全部示例输出稳定（两次运行 diff 为空）
- [ ] conformance 语法树快照用例 ≥60 条全绿（roadmap 期 2 出口）
- [ ] 位置非法（顶层 return、循环外 break、普通函数 await）各发 MSE1xxx
- [ ] 常量折叠用例（含溢出升 bigint）通过
- [ ] 标注槽位/类别正确（单测断言局部/upvalue 链）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_binder.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 局部分类 | `func f(){ var x = 1; return x }` | x → local slot 0 |
| 遮蔽 | 同名内外层 | 内层新槽位，外层不被改写 |
| upvalue 需求 | counter 闭包示例（05 §3） | 内层函数捕获 n → upvalue |
| := 重复 | `var x` 后 `x := 1` | MSE1xxx |
| 顶层 return | `return 1`（模块层） | MSE1xxx |
| 普通函数 await | `func f(){ await g() }` | MSE1xxx |
| async 内 await | `async func f(){ await g() }` | 通过 |
| 顶层 await | `await f()`（模块层） | 通过 |
| 折叠 | `1+2*3` | 字面量 7 |
| 折叠溢出 | `9223372036854775807 + 1` | BIGINT 字面量 |
| 循环新绑定标注 | `for x in xs {}` | 循环体子作用域存在 |

### 5.2 ms 快照 conformance `tests/conformance/`

从 03/04/05/06 文档示例逐条转写（命名 `<doc>-<n>-<slug>.ms` + 同名 `.ast` 快照文件），示例清单：03 §2 三式推导、§5 短路返回、§9 插值；04 §1-§9 全语句示例；05 §1-§4 函数/闭包/生成器；06 §1 §2 §4 类。跑器扩展：`--dump-ast` 输出与 `.ast` 比对（T002 目录扫描自动注册）。

## 6. 风险与备注

- 快照基线一经建立不可手改：语义变更须走"改实现→审查 diff→重新生成"流程并在提交说明。
- upvalue 捕获描述在本阶段只"记录需求"（哪个外层槽被哪层内层引用），真实 open/closed 链在 T018。
