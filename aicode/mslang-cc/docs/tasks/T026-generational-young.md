# T026 — 分代 young GC

- 阶段: P7 GC（roadmap 期 7）
- 依赖: T023
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/23-gc.md` §1 §3 §5 §9、`22-object-model.md` §2

## 1. 目标与范围

实现堆分代布局与 young 复制式 GC：per-worker（当前 per-VM）双半空间 nursery（256KB 起步）、Cheney 复制、年龄晋升（存活 2 次入 old）、old 区 128KB bump block 链表、large >32KB 直接 old 独立块、card marking 记忆集（old→young）。minor STW 执行。

不做：并发标记（T027）、多 worker 分区 young（T028 后按 worker 拆分）、对象池（T027）。

## 2. 实现要点

- 堆布局（23 §1）：young = 双 semispace（from/to）；old = block 链表 bump 分配；large = >32KB 独立块入 old；永久 = intern/代码/内置类（flag 直挂）。
- 分配（23 §1）：新对象一律 young bump（仅顶点越界检查）；large 直接 old；对象头 flags 增设年龄位（22 §2）。
- minor（23 §3）： Cheney——roots（栈/Globals/挂起生成器栈/handle 注册表占位）+ 脏 card 记忆集出发，BFS 复制存活到 to-space，年龄 +1；≥2 晋升 old（bump 分配 + 写屏障占位接 T027）；结束交换半空间、清 card。
- 对象移动：MsValue 内 obj 指针更新——forwarding 指针复用对象头（from-space 副本首字写转发地址）；native/iterator/句柄等间接引用统一经一次"修正表"重定向（对象池对象不移动：T022 slab 池对象分配于 old，天然不复制）。
- card marking（23 §5）：old 块按 512B 分 card；old 对象写字段的屏障点（SET_ATTR/list set/map put 等容器写路径）标脏——本任务实现"屏障桩 + 标脏"，并发标记消费在 T027。
- 晋升写屏障（23 §3）：晋升对象引用的 young 对象需一并可达——复制根遍历时整图复制已覆盖（Cheney 闭包），屏障仅在 T027 并发标记激活后启用完整混合屏障。
- 触发（23 §2）：young 满触发 minor；old 活跃估算 ≥ heapGoal 触发 major（本任务仅记账 + 调 gcCollectAll 兜底，真并发 T027）。
- 参数（23 §9）：`MS_GC_YOUNG_KB`（默认 256）、`MS_GC_HEAP_MAX`（4G 硬上限 → full GC + OOMError）。

## 3. 涉及文件

新增: `src/gc/gc_young.c/.h`、`src/gc/gc_old.c/.h`（block 分配器）、`src/gc/card_table.c/.h`、`tests/unit/test_gc_young.c`、`tests/fixtures/gc/*.ms`
修改: `src/gc/gc.c`（分配入口改分代路由）、`src/obj/*.c`（容器写路径挂屏障桩）、`src/vm/vm.c`（CHECK_SAFEPOINT 接 minor 触发点）

## 4. 验收标准（DoD）

- [ ] 短命对象 minor 后 young 占用回落、old 不增长
- [ ] 存活 2 次 minor 的对象晋升 old（年龄位断言）
- [ ] large 对象直接 old 不复制
- [ ] 对象移动后全部引用有效（含 upvalue 开放槽、迭代器、绑定方法）——ASan 全绿
- [ ] old→young 引用经脏 card 被扫描（构造 old 持 new 场景）
- [ ] 环形引用 young 正常回收
- [ ] minor P99 < 1ms（256KB 上限，基准统计输出，29 §4 指标口径）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_gc_young.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| bump 分配 | 连续分配 | 指针递增、越界触发 minor |
| 复制保活 | young 对象栈可达过 minor | 副本可用、旧地址转发 |
| 晋升 | 两轮 minor 存活 | 年龄 2、位于 old |
| 跨代引用 | old.list.append(youngObj) | card 脏、minor 后引用有效 |
| 大对象 | 40KB string/buffer | 直接 old、不进 nursery |
| 移动修正 | 闭包捕获局部过 minor | upvalue 值正确 |
| 环回收 | A⇄B young 不可达 | 双双回收 |
| 硬上限 | 注入小 HEAP_MAX | full GC 后仍超 → OOMError |

### 5.2 ms fixtures `tests/fixtures/gc/`

- `young_churn.ms`：循环构造短命 list/map（10 万次），输出稳定结果 + MS_VM_STATS 的 minor 次数/耗时行（golden 含统计段）
- `promotion.ms`：全局容器长期持有对象，分代正确（输出值验证）

## 6. 风险与备注

- 对象移动 × 开放 upvalue：开放 upvalue 指向栈槽而非堆，不受移动影响；闭合 upvalue 是堆对象由复制处理——两态各自用例覆盖。
- IC 槽缓存 Shape 指针：Shape 永久代（类创建播种，不回收不移动）——本任务将 Shape 分配入永久代，消除 IC 失效维度（文档注记设计决定）。
