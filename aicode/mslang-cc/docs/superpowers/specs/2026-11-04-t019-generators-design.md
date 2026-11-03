# T019 生成器 — 设计规格

- 日期: 2026-11-04
- 任务: docs/tasks/T019-generators.md（P5，依赖 T018）
- 分支: feature/t019-generators（worktree .worktrees/t019-generators）
- 状态: 已评审通过（用户确认两项决策：跨 yield 别名语义、parser 后置脱糖）

## 1. 决策记录

| 决策点 | 选择 | 备选（否决原因） |
|--------|------|------------------|
| 跨 yield 闭包别名 | relink 方案：YIELD 关闭 upvalue 并记录 (槽位, upvalue) 对；RESUME 回拷 closed 值、重指 location、按序插回开放链 | 仅关闭（值语义）：挂起前创建的闭包看到旧值、恢复后再捕获产生重复 upvalue，静默语义错误 |
| genexp 脱糖位置 | parseModule 末尾一趟 AST 重写（GEN_EXPR → 匿名 func* + 即时调用） | 编译器内综合：需复刻 binder 自由变量分析/upvalue 建表，重复递归多 |
| 挂起帧存储 | 协程对象持有独立 MsValue 数组快照（copy-out/copy-in） | 驻留共享栈：挂起生成器钉住 VM 栈区，与栈复用纪律冲突 |
| 入口挂起 | 生成器调用走正常 msVmSetupCall 后立即转协程（快照帧槽、弹帧） | 复刻 setupCall 写协程数组：重复实参/默认值/rest 处理逻辑 |

## 2. 组件设计

### 2.1 Chunk 状态位（src/compiler/chunk.h）

- 新增 `ms_u8 flags`；`#define MS_CHUNK_GENERATOR 1u`、`MS_CHUNK_ASYNC 2u`（T031 预留，本任务不置位）。

### 2.2 协程对象（新增 src/obj/obj_coroutine.c/.h，用已预留 MS_OBJ_COROUTINE 标签）

```c
typedef struct MsUpvalLink {   // YIELD 时被关闭的 upvalue 重联记录
  int32_t slotOff;             // 相对帧 base 的槽位偏移
  MsObjUpvalue* upv;
} MsUpvalLink;

typedef struct MsObjCoroutine {
  MsObj head;
  const Chunk* chunk;
  MsObjClosure* closure;       // 生成器自身闭包（LOAD_UPVAL 依赖）
  MsValue* stack;              // 挂起帧槽快照（[0, stackTop)）
  int32_t stackTop, stackCap;  // 槽数/容量（yield 时可增长；无 upvalue 指入，增长安全）
  int32_t ip;                  // 恢复点
  uint8_t state;               // CORO_START / CORO_RUNNING / CORO_SUSPENDED / CORO_DONE
  MsUpvalLink* links;          // relink 表
  int32_t linkCount, linkCap;
} MsObjCoroutine;
```

- `msObjCoroutineMarkChildren`：标记 closure、stack 逐值（经 gcMarkValueRange）、links 中 upvalue。
- freeFn：释放 stack 与 links 数组。
- 不可达协程被回收时其 upvalue 均为 closed（自包含），无悬垂；协程经 links 持有 upvalue 引用保活。

### 2.3 GC（src/gc/gc.c）

- 新增助手 `gcMarkValueRange(MsVM*, const MsValue*, int32_t n)`：标记 n 个值（任务 §6 的 T028 复用面，届时可按 chunk+pc 精确化）。
- msVmInit 注册 `gcSetMarkFn(MS_OBJ_COROUTINE, ...)` 与 `gcSetFreeFn`。

### 2.4 VM（src/vm/vm.c、call.c、新增 src/vm/generator.c）

- OP_CALL 脚本函数分支：setupCall 后若 `chunk->flags & MS_CHUNK_GENERATOR` → 立即入口挂起：
  创建协程（state=START, ip=0），快照 `[base, stackTop)`，弹帧，协程写入被调槽且 stackTop=base（同 OP_RETURN 纪律）。
  实参/默认值/rest/元数错误均经 setupCall 在调用时暴露。
- OP_YIELD 处理器：值出栈 → 记恢复点 ip → 快照 `[base, stackTop)`（按需扩容）→ 遍历开放链记录 location≥base 项入 links → `msVmCloseUpvalues(&stack[base])` → 弹帧 → 值写 `stack[base-1]` 且 stackTop=base（与 OP_RETURN 同款：结果保持为活跃栈顶）→ state=SUSPENDED → 经 entry 机制退出嵌套 runFrames。
- `msVmResumeGenerator(vm, coro, out)`（generator.c，恢复公共面）：
  DONE → false；否则压协程入栈（被调槽）、回拷 stack、置 RUNNING、relink（closed 值回拷槽位、location 重指、按地址降序插回开放链）、压帧 `{chunk, ip, closure}`、callDepth 守卫下嵌套 msVmRunFrames。
  返回后：hasError → 协程置 DONE、false；state==SUSPENDED → `*out` 取被调槽值、true；否则（RUNNING，自然 RETURN）→ 置 DONE、false（返回值按 05 §4 丢弃）。
- OP_RESUME 处理器：TOS 协程 → 调 msVmResumeGenerator，结果或 nil 替换 TOS（编译侧不发射，指令完整可用）。
- upvalue 不变量零改动：挂起期间开放链内全部指向 VM 栈或已闭合；msVmPush 扩容修复、msVmCloseUpvalues 均无需修改。

### 2.5 迭代协议对接（src/obj/obj_iter.c、src/vm/builtins.c、iter_methods.c、value.c）

- `msIterFromValue`：MS_OBJ_COROUTINE 透传（GET_ITER 返回生成器自身）。
- `msIsIterator` 纳入协程；`msIterNext` 增协程分支（调 msVmResumeGenerator）。
- `next(g)` builtin 与 `g.next()/take/drop/map/filter/each/toList`：经 msIsIterator/迭代方法表泛化生效；TAKE/DROP/MAPFN/FILTER 适配器持有协程源值，惰性管道成立。
- typeName → "generator"；formatValue → "<generator>"。

### 2.6 编译器（src/compiler/compiler.c）

- compileStmt 新增 AST_YIELD：编译表达式（缺省 nil）+ OP_YIELD。
- compileFuncProto：isGen = `flags & AST_FUNC_GENERATOR` 或体内直层 AST_YIELD 浅扫描（不进嵌套函数，不改 AST）；置 chunk flags。
- compileCompr 删除 AST_GEN_EXPR 特化（parser 重写后该 kind 不再到达编译器）。

### 2.7 parser 后置脱糖（src/parser/parser.c）

- parseModule 末尾单趟递归重写：`AST_GEN_EXPR` → `AST_CALL(AST_FUNC_EXPR{GENERATOR, 参数 $gN}, [首个 iterable])`；
  体 = 嵌套 for-in（FRESH 目标）包裹内层条件 if 与 `yield 元素`；多 for 子句：首子句 iterable 提为实参（急求值），其余子句在体内逐层求值。
- 合成参数名 `$gN`：arena 分配、Parser 计数器保证唯一；用户源码不可写 `$`，无捕获冲突。
- 位置限制在解析期以原 kind 校验，重写仅替换节点；所有 frontend 调用方统一生效。
- binder 对合成函数走既有匿名函数路径（T018 闭包/upvalue/FRESH 绑定全复用）。

## 3. 测试计划

- tests/unit/test_generator.c（任务 §5.1 全七例）+ 闭包跨 yield 别名锁定 + 嵌套 func 内 yield 归属判定 + GC 压力（挂起可达：MS_GC_HEAP_MAX 强制回收后 next 正常；不可达：回收无崩溃）。
- fixtures tests/fixtures/gen/：fib_gen.ms、lazy_chain.ms、side_effect.ms（含 .out）。
- conformance：05-func-04-generator-fib 保留 .ast；再生成 03-expr-13-gen-expr.ast（脱糖形态，经 tools/gen_ast_snapshots.py）。
- ASan 预设为非 Windows CI；本地 MSVC 以 GC 压力单测覆盖同一场景。

## 4. 错误与边界

- 恢复中运行时错误：hasError 槽位机制退出，协程置 DONE，后续 next 恒 nil；沿用"错误路径不恢复 stackTop"纪律。
- 生成器内显式 return：按耗尽处理，返回值丢弃。
- for-in 内 yield、双生成器交错、默认参数/rest 生成器：快照/relink 机制天然覆盖。
- 嵌套生成器（FOR_ITER 于生成器体内）：resume 嵌套 runFrames，callDepth 复用守卫。
