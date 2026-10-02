# T011 — VM 基础：dispatch 循环与控制流

- 阶段: P3 编译 + VM 基础（roadmap 期 3）
- 依赖: T010
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/21-bytecode.md` §6、`20-architecture.md` §1 §7、`04-statements.md` §3 §4 §5

## 1. 目标与范围

实现栈式 VM 核心：dispatch 循环（computed goto / MSVC switch 双路径）、值栈与调用帧（本任务单帧执行模块体）、装载/存储/运算/比较指令执行、print 等最小内置、if/for/while/break/continue/switch（值/类型）控制流。出口：roadmap 期 3 门禁 `print 1+2` → `3`，fixtures 起步。

不做：函数调用帧切换（T018）、堆对象类型（T012 起）、异常与并发指令。

## 2. 实现要点

- VM 结构与主循环：

```c
typedef struct MsVM {
  MsValue* stack; int32_t stackTop, stackCap;
  MsFrame* frames; int32_t frameCount;  // 本任务仅模块帧
  MsValue* globals;                     // 全局表（简单数组+名字索引，T015 换 map）
  struct GcHeap* heap;                  // T012 接入，本任务 NULL 路径
} MsVM;
void msVmRun(MsVM* vm, Chunk* chunk);
```

- dispatch：`MS_USE_COMPUTED_GOTO`（T001 探测）两套展开，语义一致；`CHECK_SAFEPOINT` 先实现为 NOP 预算递减（抢占逻辑 T029）。
- 运算执行：int/int 与 float/float 快路径；int 溢出暂发运行时错误占位（bigint 归 T016）；除零/模零抛 ArithmeticError 族（本任务以"错误槽 + 打印 + 退出码 1"最小实现，完整异常对象归 T023）。
- print：空格连接 + 换行（Python 语义）；本任务支持 nil/bool/int/float 的 str 形态。
- 控制流：JMP 族全部接通；值 switch 编译为比较链（SWITCH_TABLE 跳转表优化允许后置，语义用例先行）；类型 switch 依赖 isinstance 语义（本任务仅内置类型名匹配，class 归 T020 后补）。
- for 三段式/条件式/无限 + break/continue（跳转回填）；`for x in xs` 迭代协议等 GET_ITER/FOR_ITER —— 本任务仅支持 range（内置 range 先以最小 C 迭代器形态提供，完整迭代协议归 T017）。
- 退出码与错误输出按 27 §10：编译错 3、运行时错误 1。
- `--trace` 逐步执行日志（20 §6）。

## 3. 涉及文件

新增: `src/vm/vm.h`、`src/vm/vm.c`（dispatch）、`src/vm/value.c`（值操作助手）、`src/vm/builtins_bridge.c`（print/range 最小桥）、`tests/unit/test_vm_basic.c`、`tests/fixtures/vm-basic/*.ms`
修改: `tools/ms.c`（`ms run`/直接 `ms x.ms` 执行路径 + `--trace`）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] `ms tests/fixtures/smoke/hello.ms` 输出 `hello`（T002 占位用例激活）
- [ ] `print 1+2` → `3`（roadmap 期 3 出口）
- [ ] if/else 链、三段式 for、while、无限 for + break/continue、switch 值式/类型式（内置类型）fixtures 全绿
- [ ] computed goto 与 switch 两路径 fixtures 输出一致（CI 双跑或本地双构建验证）
- [ ] `--trace` 输出指令序列与 `ms dump` 一致
- [ ] 除零/模零输出错误并退出码 1

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_vm_basic.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 算术 | `print(1 + 2 * 3)` | stdout `7` |
| 浮点 | `print(1 / 2.0)` | `0.5` |
| 除零 | `print(1 / 0)` | 错误 + 退出码 1 |
| 真值 | `if 0 { print("t") }` | 输出 `t`（0 为真） |
| 短路值 | `print(nil \|\| "d")` | `d` |
| for 三段 | `var s=0; for i:=0;i<5;i++ { s+=i }; print(s)` | `10` |
| 迭代 range | `var s=0; for x in range(5) { s+=x }; print(s)` | `10` |
| 嵌套 break | 双层循环内层 break | 外层完整执行 |
| switch 值 | `switch 2 { case 1,2: print("a") default: print("b") }` | `a` |
| switch 类型 | `switch type(1) { case int: print("i") default: print("?") }` | `i` |

### 5.2 ms fixtures `tests/fixtures/vm-basic/`

`arith.ms`（四则/优先级/括号） → golden；`truthy.ms`（真值规则 02 §2）；`control_flow.ms`（if/for/while/switch 综合样例，覆盖 04 §3-§5 全示例）；`trace_dump.ms`（`--trace` 与 dump 一致性，特殊比对模式）。

## 6. 风险与备注

- 本任务是语义回归基线的起点：此后所有任务的 fixtures 只增不改，出错优先怀疑新变更。
- int 溢出在 T016 前为运行时错误属已知临时语义，fixtures 避免使用（用注释标注）。
- `&& / ||` 返回操作数本身（非 bool 化）——值栈直接传值即可，用例锁定。
