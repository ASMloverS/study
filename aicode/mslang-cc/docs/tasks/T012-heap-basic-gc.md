# T012 — 堆与对象基础 + 临时标记清除 GC

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T011
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/22-object-model.md` §1 §2 §9、`23-gc.md`（本章仅取"追踪式全堆 STW"过渡形态）

## 1. 目标与范围

实现 `MsValue` 完整 tagged union、`MsObj` 对象头与类型枚举全集、全堆链表分配器、过渡版 GC（整堆 STW 标记清除，root = 栈 + 全局表）。为 T013-T017 各容器铺路。

不做：分代/并发（T026/T027）、对象池 slab（T027）、NaN-boxing（仅保留编译开关占位，评估归 T044）。

## 2. 实现要点

- MsValue（22 §1）：`MS_VAL_NIL/BOOL/I64/F64/OBJ` 五 tag；`msIsNil/msIsBool/…` 判型内联函数族。
- 对象头（22 §2）：`type/flags/next`；`MsObjType` 枚举全集（STRING…WEAKREF，22 §2 表）；本任务落地 NULL 实现（分配即挂链表，类型行为由各容器任务补）。
- 分配器：`msObjAlloc(vm, MsObjType type, size_t size)` —— malloc + 头初始化 + 挂全堆链表；大对象策略本阶段不区分（分代归 T026）。
- 过渡 GC `gcCollectAll(vm)`：
  - root：VM 栈（frame map 枚举每帧 MsValue 槽——本任务模块帧即整个栈）、全局表、（后续任务追加：intern 表、常量池标记为永久）。
  - 标记：按 MsObjType 分派的 `markChildren` 函数指针表（本任务多数类型 children 为空）。
  - 清除：全堆链表摘白、free。
  - 触发：`msVmAllocGuard(vm)` 在分配点调用，堆字节数超阈值（默认 8MB，`MS_GC_HEAP_MAX` 先行尊重）触发；`runtime.gc()` 归 T036。
- 永久代雏形：intern 字符串表（T013 建立）与常量池对象标记 `MS_OBJ_PERMANENT` flag 跳过回收。
- 帧内精确扫描：模块帧 value 栈即 MsValue 数组，直接全扫（无 native 帧）。

```c
typedef void (*MarkChildrenFn)(MsVM* vm, MsObj* obj);
// gc.c 内表: static MarkChildrenFn g_markFns[MS_OBJ_COUNT];
```

## 3. 涉及文件

新增: `src/obj/obj.h`、`src/obj/obj.c`、`src/gc/gc.h`、`src/gc/gc.c`、`tests/unit/test_gc_basic.c`
修改: `src/vm/vm.c`（分配点接 guard）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 分配/全回收冒烟：循环分配临时对象，堆不超阈值稳定（泄漏检测）
- [ ] 存活对象不被误收（栈/全局引用保持可达）
- [ ] ASan 构建下 GC 单测无 UAF/泄漏
- [ ] MsValue 判型族与 nil/bool/i64/f64 直存正确
- [ ] 堆统计接口（对象数/字节数）可用于测试断言

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_gc_basic.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 值直存 | i64/f64/bool/nil | 无堆分配、判型正确 |
| 分配回收 | 循环 alloc 触发 gc | 堆字节回落、无泄漏（ASan） |
| 栈保活 | 局部引用对象过 GC | 对象仍可用（magic 字段不变） |
| 全局保活 | 全局表引用过 GC | 同上 |
| 链式引用 | A→B→C 仅 A 可达 | B C 同收；A 活则全活 |
| 误删防护 | 自引用对象 | 正确回收（追踪式天然支持） |

### 5.2 ms 用例

`tests/fixtures/gc-basic/keepalive.ms`（构造对象图 + 循环，验证程序输出不变，依赖后续任务的容器语法时以最小 string 形态在 T013 后补跑；本任务 C 单测为主）。

## 6. 风险与备注

- `markChildren` 分派表是后续所有容器任务的必经扩展点：每个容器任务必须同步补标记函数与对应测试，DoD 统一含"GC 单测扩展"。
- 过渡 GC 与最终分代版共存策略：`gcCollectAll` 保留为 `runtime.gc()` 强制全量路径，不删除。
