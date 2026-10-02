# T027 — 并发标记 major + WeakRef

- 阶段: P7 GC（roadmap 期 7）
- 依赖: T026
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/23-gc.md` §2 §4 §6-§9 §11、`22-object-model.md` §8 §9、`25-capi.md` §4（handle 注册表前置）

## 1. 目标与范围

实现 old 区并发三色标记清除：mark setup/并发 mark/termination/并发 sweep 四阶段、混合写屏障（Dijkstra+Yuasa）、safepoint 握手（精确栈图）、mutator assist、heapGoal；WeakRef 弱表；handle 注册表（C API 前置）；对象池 slab 完善；`runtime.gcStats()` 数据埋点（模块归 T036）。出口：roadmap 期 7——GC 压力单测（含 ASan）长时间随机脚本无崩溃。

不做：标记并行化（多 GC 线程，23 §10 后续）、old 压缩（23 §11 明确不做）。

## 2. 实现要点

- 四阶段（23 §4）：
  1. setup STW#1：开屏障 → 扫固定 roots（全局/内置/parked 栈快照）置灰入队 → 唤醒 GC 线程 → 恢复 mutator
  2. concurrent mark：GC 线程 drain 灰队列；mutator 按分配量 assist（23 §8 配额）；新分配直接黑
  3. termination STW#2：排空残余灰 → 关屏障 → 存活统计
  4. concurrent sweep：old 块逐块回收白（free-list 归还）；分配惰性 assist
- 混合屏障（23 §4）：`old.f = new` 时（标记激活期间）：old 灰则 shade(new) + shade(旧值)；栈帧 STW#2 统一保守按灰一次处理（免重扫栈，23 §4）。
- safepoint 握手（23 §6）：CHECK_SAFEPOINT 预算尽 → 读全局 GC 状态字（atomic）→ setup 阶段各 worker 自旋交栈快照；精确栈图 = frame map（chunk 内建：每帧 MsValue 槽位表）逐帧枚举。
- 触发与目标（23 §2 §8）：old 活跃 ≥ heapGoal（存活×2.0，MS_GC_GOAL）；assist 比例动态；堆硬上限 OOMError 路径。
- WeakRef（23 §7、22 §8）：全局弱表（对象→弱节点链）；标记结束白对象置 nil；弱节点本身黑保活；`WeakRef(o).get()` 返回对象或 nil（10 §1）。
- handle 注册表（25 §4）：稳定槽索引数组，GC 扫描为 root——T033 C API 直接消费，本任务交付内部表 + root 扫描。
- 对象池（22 §9）：Upvalue/BoundMethod/Iterator per-worker slab（本阶段 per-VM）；池对象入全堆链表。
- 模型（23 §10）：当前 1 worker（主线程）+ 1 GC 标记线程；多 worker 分区在 T028 扩展（card/young per-worker 拆分复核点）。

## 3. 涉及文件

新增: `src/gc/gc_mark.c/.h`（三色/屏障/assist）、`src/gc/gc_sweep.c`、`src/gc/weakref.c/.h`、`src/gc/handles.c/.h`、`src/gc/gc_stats.c`、`tests/unit/test_gc_mark.c`、`tests/unit/test_gc_weakref.c`、`tests/fixtures/gc/major*.ms`、`tools/fuzz_gc.py`（随机脚本生成器）
修改: `src/plat/plat_thread.c`（GC 线程）、`src/vm/vm.c`（CHECK_SAFEPOINT 状态机）、`src/gc/gc.c`（阶段调度）、`src/obj/*.c`（写屏障完整接入）

## 4. 验收标准（DoD）

- [ ] major 全阶段状态机单测通过（强制触发 + 统计断言）
- [ ] 写屏障：并发期间修改 old 图，无丢失引用（压力随机对照）
- [ ] safepoint：STW 两段时长统计输出（P99 < 500µs 目标口径，29 §4）
- [ ] WeakRef：对象回收后 get() 为 nil；存活期非 nil；弱表不阻碍回收
- [ ] handle：root 保活跨 full GC
- [ ] 随机脚本模糊（fuzz_gc.py 生成 ≥1000 脚本）ASan 构建全量跑无崩溃（roadmap 期 7 出口）
- [ ] 24h 压力抽样（CI 夜跑，本任务交付脚本）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_gc_mark.c`、`test_gc_weakref.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 三色不变式 | 并发修改图 | 终态无白可达（内部断言模式） |
| 删除屏障 | 覆盖 old 引用 | 旧值不失联（标记期） |
| assist 配额 | 分配风暴 | mutator 分摊标记（计数断言） |
| sweep | 混合活死 old | 白回收、黑保留、free-list 复用 |
| WeakRef 存活 | 有强引用 | get() 非 nil |
| WeakRef 死亡 | 无强引用过 major | get() nil |
| handle root | 注册后 full GC | 对象可达 |

### 5.2 ms fixtures `tests/fixtures/gc/`

- `major_cycle.ms`：分代+major 混合压力（旧对象图随机断链，输出校验和）
- `weakref.ms`：

```ms
var o = {"k": 1}
var w = WeakRef(o)
print(w.get() is o)      // true
o = nil
runtime.gc()             // 强制 full（T036 前以 MS_FORCE_GC=1 环境变量代）
print(w.get())           // nil
```

## 6. 风险与备注

- roadmap 期 7 的"24h 随机脚本"在 CI 以抽样夜跑形式落地（fuzz 生成器入库，全量跑按周任务）；本任务 DoD 以 ≥1000 脚本 ASan 通过为准。
- 混合屏障正确性依赖"屏障桩全覆盖容器写路径"——T026 的桩点清单作为 checklist 逐点复核（SET_ATTR/SET_INDEX/map put/set add/upvalue 闭合/字段溢出数组写）。
