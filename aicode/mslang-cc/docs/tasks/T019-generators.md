# T019 — 生成器

- 阶段: P5 函数/类（roadmap 期 5）
- 依赖: T018
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/05-functions.md` §4、`21-bytecode.md` §3.6、`24-scheduler.md` §3（async/生成器共协程机制的编译侧铺垫）

## 1. 目标与范围

实现生成器函数（`func*` 显式声明或体内出现 `yield` 隐式判定）、YIELD/RESUME 指令、惰性 iterator 语义（调用不执行体）、`next()` 推进、`for x in gen()` 直接迭代、耗尽 nil；生成器表达式替换 T017 的物化实现为真实惰性管道。

不做：async 函数（T031，但本任务的状态机骨架需为其预留 `chunk 状态位` 设计）。

## 2. 实现要点

- 判定：`func*` 声明或函数体内任一 `yield`（二者其一即生成器；推荐 func\*——linter 提示在 T038）。async 内 `yield` 非法、生成器内 `await` 非法（Binder 已拒，运行前防线）。
- 执行模型（21 §3.6）：生成器调用 → 分配 COROUTINE 轻量执行态（复用 MsObjCoroutine 结构的"暂停帧"子集：chunk + pc + 栈快照 + upvalue）→ 立即返回 iterator，不执行体。
  - `next(it)`：RESUME 恢复栈与 pc → 执行至 YIELD（值入调用者栈，状态置挂起）或 RETURN（置耗尽，后续 next 恒 nil）。
  - 挂起帧的栈保活：COROUTINE 对象持有 MsValue 栈数组，作为 GC root 的一部分（挂起生成器可达则其栈可扫——markChildren 枚举）。
- 迭代协议对接：GET_ITER 对生成器返回自身；FOR_ITER 内部即 next 语义（nil 跳出）。
- 生成器表达式：编译为匿名生成器函数 + 即时调用（`(x * x for x in xs if c)` ≡ `func*(xs) { for x in xs { if c { yield x * x } } }(xs)` 脱糖），真惰性：`it.take(3)` 只计算 3 个。
- `yield` 只能直层出现在生成器体内（闭包内 yield 属于闭包自身判定，不影响外层）——单测锁定。

## 3. 涉及文件

新增: `src/obj/obj_coroutine.c/.h`（生成器态，T028 扩展为完整 G）、`src/vm/generator.c`、`tests/unit/test_generator.c`、`tests/fixtures/gen/*.ms`
修改: `src/compiler/compiler.c`（func* 编译、genexp 脱糖替换）、`src/vm/vm.c`（YIELD/RESUME）、`src/gc/gc.c`（COROUTINE 挂起栈标记）

## 4. 验收标准（DoD）

- [ ] 05 §4 fib 生成器示例输出 0 1 1 2 3…正确
- [ ] 调用不执行体（副作用计数为 0）单测锁定
- [ ] `for v in fib()` 直接迭代；耗尽后 next 恒 nil
- [ ] genexp 惰性：`range(10).toList()` 大源 + take(3) 仅计算 3 次（计数器断言）
- [ ] 生成器内闭包/解构/多 for 与 T017 推导式语义等价（同输出对照）
- [ ] GC：挂起生成器可达/不可达场景 ASan 通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_generator.c`

| 用例 | 源码 | 期望 |
|------|------|------|
| 惰性 | gen 体内 print 计数 | 调用后 0，next 后 1 |
| fib 生成器 | next ×6 | 0 1 1 2 3 5 |
| 耗尽 | 小 finite 生成器 | 先值后 nil nil |
| 双生成器独立 | 两个实例交错 next | 各自序列 |
| yield 表达式 | `yield a; a, b = b, a + b` | 状态跨恢复保持 |
| genexp 惰性 | `take(3)` 于 range(1e6) | 仅推进 3 次 |
| for in | `for v in gen() {}` | 全值后停 |

### 5.2 ms fixtures `tests/fixtures/gen/`

- `fib_gen.ms`：

```ms
func* fib() {
    var a, b = 0, 1
    for { yield a; a, b = b, a + b }
}
var g = fib()
print(next(g), next(g), next(g), next(g), next(g))   // 0 1 1 2 3
for v in fib() {
    if v > 50 { break }
    print(v)
}
```

- `lazy_chain.ms`：`range(100).toList()` genexp 过滤 + take/drop 组合（输出前 N 项）
- `side_effect.ms`：惰性证明（无 next 无输出）

## 6. 风险与备注

- 挂起帧栈是 GC 新 root 形态：T012 的 markFns 表此处扩展 COROUTINE——后续 T028 的完整 G 栈扫描复用同一枚举逻辑，设计时抽 `markValueStack(chunk, pc, stack)` 供复用。
- RESUME 重入点正确性用 `--trace` 对照 dump 人工复核一次（快照用例固化输出）。
