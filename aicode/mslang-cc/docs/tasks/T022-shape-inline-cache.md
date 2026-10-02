# T022 — Shape 与内联缓存

- 阶段: P5 函数/类（roadmap 期 5）
- 依赖: T021
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/22-object-model.md` §6 §9、`21-bytecode.md` §5

## 1. 目标与范围

实现 Shape 隐藏类（转移链共享、SBO 8 内联字段 + 溢出数组）与三处 IC（GET_ATTR / CALL_METHOD / LOAD_GLOBAL_IC）：4 路 PIC、megamorphic 回退。纯性能任务，语义快照必须不变。

不做：CALL_METHOD 指令的引入（方法调用本已由 GET_ATTR + CALL 完成；本任务引入 CALL_METHOD 合并形态并启用 IC 槽）——语义等价由快照保证。

## 2. 实现要点

- Shape（22 §6）：`{父指针, 字段名→槽位, 字段数, 迁移表}`；实例加字段 = Shape 转移（未命中迁移表则新建子 Shape，同构对象共享）；内联字段 8（SBO），超出溢出堆数组；类创建时播种空 Shape。
- IC 槽（21 §5）：chunk 内 `nIcSlots` 数组，槽 = `{Shape 指针或 Class 指针, 缓存槽位/方法}`；GET_ATTR 命中走缓存读写；miss 填充。
- 4 路 PIC（22 §6）：每 IC 槽 4 路环形替换；满 4 路不同 Shape → megamorphic 标志，退化为每次直查（不再污染缓存）。
- CALL_METHOD <nameIdx> <icSlot> <argc>：方法查找 + 调用一体化（省一次 GET_ATTR 装载）；编译器在 `obj.m(args)` 形态优先生成 CALL_METHOD。
- LOAD_GLOBAL_IC <name> <icSlot>：全局名查缓存（globals map 命中内联；T015 的 map 查找退化为缓存槽直达）。
- 语义透明保证（21 §5）：IC 只缓存"查什么"（Shape→槽位、类→方法、名→全局槽），不缓存"值"；缓存失效路径 = Shape 迁移/类方法表变更（v1 类体执行完成后方法表稳定，动态修改方法表不提供——文档注记）。
- 对象池（22 §9）：Upvalue/BoundMethod/Iterator 三类高频短命对象 per-worker slab free-list 池（单线程阶段即 per-VM）；池对象仍入全堆链表（GC 兼容）。

## 3. 涉及文件

新增: `src/vm/ic.c/.h`、`src/obj/slab.c/.h`、`tests/unit/test_shape_ic.c`、`tests/fixtures/ic/*.ms`
修改: `src/obj/obj_class.c`（Shape 转移链替换基础版字段表）、`src/compiler/compiler.c`（CALL_METHOD/LOAD_GLOBAL_IC 生成、IC 槽分配）、`src/vm/vm.c`（三指令缓存路径）、`src/obj/obj_func.c`（Upvalue 池化）

## 4. 验收标准（DoD）

- [ ] 全部既有 fixtures/conformance 快照不变（语义零变化的强门禁）
- [ ] Shape 共享：同构对象 Shape 指针相等（单测断言）；转移链不重复建
- [ ] PIC：4 形态循环命中率统计 >0（VM_STATS 观测），第 5 形态后 megamorphic
- [ ] CALL_METHOD 与 GET_ATTR+CALL 输出一致（同源双编译对照用例）
- [ ] 溢出字段：>8 字段实例读写正确；Shape 链深度可控
- [ ] slab 池：分配/回收统计与 ASan 通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_shape_ic.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| Shape 共享 | 两实例同序加同名字段 | Shape 指针相同 |
| Shape 分叉 | 同序不同名字段 | 两个子 Shape |
| 溢出字段 | 12 个字段 | 8 内联 + 4 溢出读写正确 |
| IC 命中 | 循环同型访问 | icHit 计数增长 |
| PIC 满 4 路 | 5 形态轮询 | 第 5 形态后 megamorphic（计数断言） |
| 语义等价 | CALL_METHOD vs GET_ATTR+CALL | 输出一致 |

### 5.2 ms fixtures `tests/fixtures/ic/`

- `polymorphic.ms`：4 类多态方法调用循环（输出正确性 + `MS_VM_STATS=1` 下 ic 命中率行）
- `field_growth.ms`：同构构造 100 实例、逐步加字段至 12（输出字段值全验证）

## 6. 风险与备注

- 本任务为纯优化：review 重点 = 缓存失效正确性（Shape 迁移后旧 IC 槽永不命中错误槽位）；模糊对照跑全部既有用例是硬门禁。
- `MS_VM_STATS=1`（20 §6）本任务引入编译期开关：指令计数 + IC 命中率先行，GC/调度器指标后续任务补。
