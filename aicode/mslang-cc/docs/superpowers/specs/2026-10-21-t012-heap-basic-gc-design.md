# T012 堆与对象基础 + 临时标记清除 GC — 设计文档

- 日期: 2026-10-21
- 任务: docs/tasks/T012-heap-basic-gc.md（P4, 依赖 T011, 规模 M）
- 分支: feat/t012-heap-basic-gc
- 设计文档: docs/language/22-object-model.md §1 §2 §9、23-gc.md（过渡形态：整堆 STW 标记清除）

## 1. 目标

实现 `MsValue` 完整 tagged union（五 tag）、`MsObj` 对象头与 `MsObjType` 枚举全集、全堆链表分配器、过渡版 GC（整堆 STW 标记清除，root = 栈 + 全局表）。为 T013-T017 各容器铺路。

不做：分代/并发（T026/T027）、对象池 slab（T027）、NaN-boxing（占位编译开关，评估归 T044）。

## 2. 方案选型

| 决策点 | 选型 | 否决项与理由 |
|--------|------|--------------|
| MsValue 过渡形态 | 五 tag 并存：`MS_VAL_OBJ` 新增；`MS_VAL_INT/FLOAT` 重命名为 `MS_VAL_I64/F64` 对齐 22 §1；`STRING/FUNCTION/NATIVE/RANGE_ITER` 保留为过渡 kind，注释标明归宿 | 全部迁为对象：前置 T013（MsObjString 布局 22 §3）、T017/T018 的结构设计，vm.c 大范围无谓改动 |
| markChildren 分派 | `gcSetMarkFn(type, fn)` 运行时注册 + gc.c 内 `static MarkChildrenFn g_markFns[MS_OBJ_COUNT]` 表 | 编译期内表：T012 所有类型 children 为空，链式引用/自引用测试无法构造；注册 API 同时是 T013+ 容器任务的统一扩展点 |
| 标记算法 | 迭代 worklist（动态数组灰队列） | 递归标记：深链（容器嵌套）有爆栈风险；worklist 对齐 23-gc §4 灰队列方向 |
| guard 位置 | `msVmAllocGuard(vm)` 由 `msObjAlloc` 内部在 malloc 前调用 | 各分配点手动调用：T012 无生产分配点（ms 代码尚不能构造对象），统一 choke point 免遗漏 |
| ASan 验证 | 本机 clang（C:\Tools\LLVM 18）+ Ninja + `MS_SANITIZERS=ON` 重配置 build/ 跑单测，验毕恢复 MSVC 配置 | T001 先例按"本地无工具链延后 CI"；现已确认本机 clang ASan 可用（探针验证 UAF 检出正常），DoD 本地闭环 |

## 3. 已批准的偏差

| 偏差 | 任务文档建议 | 实际设计 | 理由 |
|------|--------------|----------|------|
| A | MsValue 完整五 tag | `STRING/FUNCTION/NATIVE/RANGE_ITER` 过渡 kind 并存，判型族先行（`msIsNil/msIsBool/msIsI64/msIsF64/msIsObj`） | 22 §3 string 布局归 T013、迭代协议归 T017、函数对象归 T018；不前置设计 |
| B | gc.c 内静态 markFn 表 | 表保留在 gc.c，暴露 `gcSetMarkFn` 注册 API | 链式引用/自引用测试需要带 children 的对象；编译期表无法支撑 |
| C | keepalive.ms fixture | 按任务书 §5.2 延后至 T013（最小 string 形态补跑） | ms 代码本任务无法构造堆对象；任务书已注明"本任务 C 单测为主" |

## 4. 详细设计

### 4.1 MsValue（vm.h）

```c
typedef enum MsValueKind {
  MS_VAL_NIL, MS_VAL_BOOL, MS_VAL_I64, MS_VAL_F64, MS_VAL_OBJ,  // 22 §1 五 tag
  // 过渡 kind（T011 形态）：STRING→T013、FUNCTION/NATIVE→T018、RANGE_ITER→T017 迁入 MS_VAL_OBJ
  MS_VAL_STRING, MS_VAL_FUNCTION, MS_VAL_NATIVE, MS_VAL_RANGE_ITER,
} MsValueKind;

typedef struct MsValue {
  MsValueKind kind;
  union {
    bool b; ms_i64 i; double f; MsObj* obj;
    struct { const char* bytes; int32_t len; } str;   // 过渡：借用常量池文本
    const Chunk* func; MsNativeFn native;             // 过渡
    struct { ms_i64 cur, stop, step; } range;          // 过渡
  } as;
} MsValue;
```

- 判型族 static inline（28 §1 命名）：`msIsNil/msIsBool/msIsI64/msIsF64/msIsObj`。
- 构造器 `msValueObj(MsObj*)`；`msValueInt/msValueFloat` 名义保留（内部 tag 换 I64/F64）。
- 重命名波及：vm.h/vm.c/value.c/builtins_bridge.c/test_vm_basic.c（机械替换 ~25 处）。
- `msValueEqual`：`MS_VAL_OBJ` 分支按指针同一性（身份语义；值等价随容器任务接入）。

### 4.2 对象头与分配器（src/obj/obj.h、obj.c）

```c
typedef enum MsObjType {
  MS_OBJ_STRING, MS_OBJ_BIGINT, MS_OBJ_FUNCTION, MS_OBJ_NATIVE, MS_OBJ_CLOSURE,
  MS_OBJ_UPVALUE, MS_OBJ_CLASS, MS_OBJ_INSTANCE, MS_OBJ_BOUND_METHOD, MS_OBJ_LIST,
  MS_OBJ_MAP, MS_OBJ_SET, MS_OBJ_TUPLE, MS_OBJ_MODULE, MS_OBJ_COROUTINE,
  MS_OBJ_CHANNEL, MS_OBJ_FUTURE, MS_OBJ_ITERATOR, MS_OBJ_WEAKREF, MS_OBJ_COUNT,
} MsObjType;  // 22 §2 表全集；本任务均为 NULL 实现（无行为）

#define MS_OBJ_MARK      ((ms_u8)(1u << 0))  // 标记位（白/黑）
#define MS_OBJ_PERMANENT ((ms_u8)(1u << 1))  // 永久代（intern/常量池，跳过回收）

typedef struct MsObj {
  ms_u8 type;          // MsObjType
  ms_u8 flags;         // mark | permanent | 杂项
  struct MsObj* next;  // 全堆链表（GC 枚举）
} MsObj;

MsObj* msObjAlloc(MsVM* vm, MsObjType type, size_t size);
// msVmAllocGuard(vm) → malloc(size) → 头初始化 → 头插全堆链表 → bytes/count 累加
```

### 4.3 GC（src/gc/gc.h、gc.c）

```c
typedef struct GcHeap {
  MsObj* objects;      // 全堆链表头
  size_t bytes;        // 累计分配字节数
  int32_t count;       // 在堆对象数
  size_t threshold;    // 触发阈值（MS_GC_HEAP_MAX 或默认 8MB）
} GcHeap;

typedef void (*MarkChildrenFn)(MsVM* vm, MsObj* obj);
void gcSetMarkFn(MsObjType type, MarkChildrenFn fn);  // 容器任务/测试扩展点
void gcCollectAll(MsVM* vm);      // 整堆 STW 标记清除；保留为 runtime.gc() 全量路径
void msVmAllocGuard(MsVM* vm);    // bytes >= threshold → gcCollectAll
size_t gcHeapBytes(const MsVM* vm);
int32_t gcHeapObjCount(const MsVM* vm);
```

- 生命周期：`gcHeapInit(GcHeap*)`（解析 `MS_GC_HEAP_MAX`，十进制字节数，缺省 `MS_GC_HEAP_MAX_DEFAULT` = 8MB）/ `gcHeapFree(GcHeap*)`（释放全部对象含 permanent）。
- `gcCollectAll` 流程：
  1. 标记：worklist 初值 = 栈 `[0, stackTop)` + `globals[i].value` 中的 `MS_VAL_OBJ`（过渡 kind 无堆指针跳过）；pop → 置 `MS_OBJ_MARK` → 调 `g_markFns[type]`（未注册视为无 children）。
  2. 清除：全堆链表遍历；`MARK|PERMANENT` → 清 MARK 保留；白 → 摘链 `free`，bytes/count 递减。
- permanent 对象 children 仍经 markFn 遍历（intern 表为 root 的语义由 T013 接入；本任务 permanent 类型均无 children）。
- `MS_VAL_OBJ` 在 `msValueIsTruthy` 中为真、`msValueTypeName` 返回 "object"（容器任务后续细分）。

### 4.4 VM 接线（src/vm/vm.c）

- `msVmInit`：`vm->heap = malloc(sizeof(GcHeap)); gcHeapInit(...)`。
- `msVmFree`：`gcHeapFree` + `free(heap)`。
- vm.h 的 `struct GcHeap` 前向声明保留（opaque；vm.c include gc.h）。

### 4.5 CMake

- `ms_obj`：obj.c 实装（占位 typedef 移除），`target_link_libraries(ms_obj PRIVATE ms_gc)`（msObjAlloc 调 guard）。
- `ms_vm`：追加 `PRIVATE ms_gc`（vm.c 调 gcHeapInit/Free）。
- 无符号级循环：gc.c 仅使用 vm.h 类型与自身符号。
- 测试注册：`ms_add_test(gc_basic ms_obj ms_gc ms_vm ms_compiler ms_binder)`。

## 5. 测试计划

### 5.1 C 单测 tests/unit/test_gc_basic.c

| 用例 | 输入 | 期望 |
|------|------|------|
| 值直存 | nil/bool/i64/f64 构造 + 判型族 | kind 正确；堆 bytes/count 不变（无分配） |
| 分配回收 | 循环 alloc 死对象 + `gcCollectAll` | count/bytes 回落基线 |
| 自动触发 | `MS_GC_HEAP_MAX` 置小值后循环 alloc | 堆字节数稳定 < threshold + 单对象尺寸 |
| 栈保活 | 局部 MsValue 引用对象过 GC | count 不减、magic 字段不变 |
| 全局保活 | globals 引用对象过 GC | 同上 |
| 链式引用 | A→B→C（测试注册 markFn 标 child），仅 A 在栈 | B C 存活；A 不可达则全收 |
| 自引用 | obj.child = self，不可达 | 正确回收 |
| permanent | 置 MS_OBJ_PERMANENT 后不可达 | 不被回收；vmFree 后无泄漏 |
| 统计接口 | gcHeapBytes/gcHeapObjCount | 与分配/回收一致 |

- 保活判定：count 断言为主 + magic 字段读（ASan 下误收即 UAF 报错）。
- 测试负载对象：`typedef struct { MsObj head; MsValue child; ms_u64 magic; } TestNode;`，以 NULL 行为类型（如 MS_OBJ_ITERATOR）alloc。

### 5.2 ASan 验证

- `clang -G Ninja -DMS_SANITIZERS=ON` 重配置 build/ → 构建 → ctest unit 标签全跑 → 恢复 MSVC 配置（build/ 单目录顺序复用，符合 AGENTS.md）。

### 5.3 ms 用例

keepalive.ms 延后至 T013（偏差 C）。

## 6. 风险与备注

- `g_markFns` 运行时可变全局：单线程 v1 无保护问题；T026 并发化时随 GC 状态一并处理。
- 深链 worklist 内存：分配失败按 malloc 失败处理（v1 简化，OOM 策略归 T026）。
- 过渡 kind 迁移：T013/T017/T018 各自将对应 kind 换为 MS_VAL_OBJ 时需同步 msValueEqual/msValueTypeName/msValueIsTruthy 与 GC markValue（DoD 统一含"GC 单测扩展"）。
