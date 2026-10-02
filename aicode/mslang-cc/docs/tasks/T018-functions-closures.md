# T018 — 函数、调用与闭包

- 阶段: P5 函数/类（roadmap 期 5）
- 依赖: T017
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/05-functions.md` §1-§3 §5 §6、`21-bytecode.md` §3.3、`22-object-model.md` §2（FUNCTION/CLOSURE/UPVALUE）

## 1. 目标与范围

实现 MsObjFunction/MsObjClosure/MsObjUpvalue、MAKE_FUNCTION/CLOSURE/CALL/RETURN 全链路、调用帧切换、默认参数求值、rest 参数与 `*` 展开对齐、多返回值（tuple）、函数内省、upvalue 开放/闭合链、循环变量每轮新绑定。出口：fib 递归可运行，闭包计数器语义正确。

不做：生成器（T019）、方法调用 IC（T022）、StackOverflowError 精确防护（帧深上限先用固定计数报错，可增长栈归 T028）。

## 2. 实现要点

- 值类型（22 §2）：FUNCTION（chunk 引用 + 原型：参数名/默认值指令槽/rest 标志）、CLOSURE（FUNCTION + upvalue 数组 FAM）、UPVALUE（开放 = 栈槽指针；闭合 = 值）。
- 调用约定：CALL <argc> —— 实参从左到右压栈；帧切换保存 pc/栈基；RETURN 返回值入调用者栈。栈帧上限（默认 2048 帧，超报"栈溢出（过渡）"运行时错误）。
- 参数装配（05 §1）：位置实参 → 默认参数（缺省时执行默认值指令/取常量）→ `*rest` 收余量为 list；`f(1, *xs)` 展开项排在位置实参之后（03 §4），与默认参数按位对齐；个数/形态不符抛 ArgumentError。
- 多返回值：`return a, b` 编译为 BUILD_TUPLE + RETURN（05 §5）；调用侧解构复用 T014 路径。
- 闭包（05 §3、21 §3.3）：CLOSURE 捕获描述（T009 记录的 upvalue 需求）→ 运行时沿调用链取开放 upvalue 或新建；外层帧返回时其 upvalue 闭合（值拷贝入 UPVALUE 对象）。
- 循环变量每轮新绑定（04 §4）：for 头变量在每轮迭代生成新栈槽（编译器在迭代体入口分配新槽并拷贝），闭包捕获拿到各自轮次的值。
- 内省（05 §6）：`f.__name__`、`f.__params__`（含默认参名，不含 rest 星号）。
- 无重载：后定义覆盖（模块 global 表直接覆盖，行为自然成立，用例锁定）。
- 一等值：赋值/传参/入容器；`xs.map(f)`/`filter`/`reduce`/`each`（T014 桩）以真闭包复跑通过。

## 3. 涉及文件

新增: `src/obj/obj_func.c/.h`、`src/vm/call.c`（调用装配）、`tests/unit/test_func_call.c`、`tests/fixtures/func/*.ms`
修改: `src/compiler/compiler.c`（函数体编译/默认参/rest/闭包捕获编码）、`src/vm/vm.c`（CALL/RETURN/CLOSURE 帧）、`src/gc/gc.c`（FUNCTION/CLOSURE/UPVALUE 标记；帧返回闭合点）、`tests/fixtures/list/methods.ms`（补闭包参数用例）

## 4. 验收标准（DoD）

- [ ] 05 §1-§3 全部示例运行正确
- [ ] `fib(10)` = 55 递归通过；00-overview §3 的 fib 函数可独立运行
- [ ] 默认参/rest/展开组合（含默认参后展开对齐）全过
- [ ] counter 闭包计数 1/2；循环变量每轮新绑定（两 for 形态）通过
- [ ] 多返回值 + 解构接收通过；`f.__name__`/`f.__params__` 正确
- [ ] GC：闭包链可达性（活闭包保活外层局部）单测 + ASan 通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_func_call.c`

| 用例 | 源码 | 期望 |
|------|------|------|
| 递归 | fib(10) | 55 |
| 默认参 | `greet("x")` / `greet("x", "Dr.")` | 前缀默认/覆盖 |
| rest | `varargs(1, 2, 3)` | `[1, [2, 3]]` |
| 展开 | `f(1, *[2, 3])` | 等价 f(1,2,3) |
| 实参不足 | `f()`（需 1 参） | ArgumentError |
| 多返回 | `return 1, 2` + `a, b = f()` | a=1 b=2 |
| 闭包 | counter 双实例独立 | 1,2 / 1,2 |
| 循环绑定 | 闭包装入 list 后逐个调用 | 0,1,2…（非终值） |
| 内省 | `f.__params__` | 名列表 |

### 5.2 ms fixtures `tests/fixtures/func/`

- `closures.ms`：

```ms
func counter() {
    var n = 0
    return func() { n += 1; return n }
}
var c1 = counter()
var c2 = counter()
print(c1(), c1(), c2())        // 1 2 1
var fns = []
for i in range(3) { fns.append(func() { return i }) }
print(fns[0](), fns[1](), fns[2]())   // 0 1 2
```

- `defaults_rest.ms`：默认参 + rest + 展开全组合（golden）
- `multi_return.ms`：交换、`a, *mid, z = f()`
- `fib.ms`：递归 fib（00-overview §3 前半）
- `shadowing.ms`：同名遮蔽与覆盖（后定义覆盖）

## 6. 风险与备注

- upvalue 开放→闭合时机：外层帧 RETURN 时统一闭合该帧全部仍开放 upvalue —— 实现点集中在帧销毁路径，ASan 重点跑闭包逃逸用例。
- T010 的 `.bc` 快照基线因 MAKE_FUNCTION 完整化更新一次（21 §4 示例对齐，属计划内基线迁移）。
