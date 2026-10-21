# T012 堆与对象基础 + 临时标记清除 GC — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 MsValue 五 tag（含 MS_VAL_OBJ）、MsObj 对象头与类型枚举全集、全堆链表分配器、过渡版整堆 STW 标记清除 GC（root = 栈 + 全局表），为 T013-T017 容器铺路。

**Architecture:** MsValue 过渡并存——新增 `MS_VAL_OBJ` 与判型族，`MS_VAL_INT/FLOAT` 重命名为 `MS_VAL_I64/F64`，`STRING/FUNCTION/NATIVE/RANGE_ITER` 保留为过渡 kind（归宿 T013/T017/T018）。对象经 `msObjAlloc` malloc + 头插全堆链表；`gcCollectAll` 以迭代 worklist 标记（root = 值栈 + 全局表），sweep 摘白释放、permanent 保留；`msVmAllocGuard` 在分配点按 `MS_GC_HEAP_MAX` 阈值（默认 8MB）自动触发。设计文档：`docs/superpowers/specs/2026-10-21-t012-heap-basic-gc-design.md`。

**Tech Stack:** C11（MSVC /W4 /WX + GCC/Clang -Wall -Wextra -Werror -Wshadow）、CMake ≥3.16、ctest、本机 clang ASan（C:\Tools\LLVM）。

**分支:** `feat/t012-heap-basic-gc`（已创建）。构建：`cmake --build build --config Debug`；测试：`ctest --test-dir build -C Debug`。

**关键语义决定（设计文档依据）:**

- markValue 仅追踪 `MS_VAL_OBJ`；过渡 kind 无堆指针（NIL/BOOL/I64/F64 直存，STRING/FUNCTION/NATIVE/RANGE_ITER 借用非堆存储）。
- `MS_VAL_OBJ` 相等为指针同一性（身份语义；值等价随容器任务接入）；类型名 "object"；真值为真。
- 标记迭代 worklist（灰队列方向，23-gc §4），禁递归防爆栈。
- sweep：`MARK|PERMANENT` 清 MARK 保留；白对象 `free` 且 `bytes -= obj->size`、`count--`。
- permanent 对象 children 仍经 markFn 遍历；本任务 permanent 类型均无 children（intern 表 root 归 T013）。
- guard：`bytes >= threshold`（先检查后分配）；`MS_GC_HEAP_MAX` 十进制字节数，非法/0 值回落默认 8MB。
- MsObj 头含过渡 `size_t size` 字段（偏差 D，sweep 字节核算）。
- malloc 失败上抛 NULL（v1 简化，OOM 策略归 T026）。

**文件结构:**

- 修改 `src/vm/vm.h` — MsValueKind 重命名/新增 OBJ、MsObj 前向声明、判型族、msValueObj 声明
- 修改 `src/vm/value.c` — 重命名 + msValueObj + OBJ 的 equal/typename/toStr
- 修改 `src/vm/vm.c` — 重命名 + heap 生命周期接线
- 修改 `src/vm/builtins_bridge.c` — 重命名（1 处）
- 新增 `src/obj/obj.h` — MsObjType 全集、flags、MsObj 头、msObjAlloc
- 覆盖 `src/obj/obj.c` — 分配器实装（占位移除）
- 新增 `src/gc/gc.h` — GcHeap、gcCollectAll/guard/注册/统计公共面
- 覆盖 `src/gc/gc.c` — 标记清除实装（占位移除）
- 修改 `CMakeLists.txt` — ms_obj/ms_vm 链接 ms_gc（T12 注释）
- 修改 `tests/CMakeLists.txt` — 注册 test_gc_basic
- 修改 `tests/unit/test_vm_basic.c` — 重命名 + 判型族用例
- 新增 `tests/unit/test_gc_basic.c` — 任务书 §5.1 全表
- 修改 `docs/tasks/T012-heap-basic-gc.md` + `docs/tasks/README.md` — 状态 ✅

---

### Task 1: MsValue 五 tag + 判型族

**Files:**
- Modify: `src/vm/vm.h:12-44`
- Modify: `src/vm/value.c`
- Modify: `src/vm/vm.c`（仅 kind 重命名）
- Modify: `src/vm/builtins_bridge.c:13`
- Modify: `tests/unit/test_vm_basic.c`

- [ ] **Step 1.1: 写失败测试** — `tests/unit/test_vm_basic.c`：全部 `MS_VAL_INT` → `MS_VAL_I64`、`MS_VAL_FLOAT` → `MS_VAL_F64`（仅 valueConstructors 一处），并追加判型族用例（插在 valueOrder 之后）：

```c
TEST(valuePredicateFamily) {  // T012：五 tag 判型族（22 §1）+ OBJ 身份语义
  MsValue n = msValueNil();
  MsValue b = msValueBool(false);
  MsValue i = msValueInt(7);
  MsValue f = msValueFloat(0.5);
  EXPECT(ctx, msIsNil(&n) && !msIsBool(&n) && !msIsI64(&n) && !msIsF64(&n) && !msIsObj(&n));
  EXPECT(ctx, msIsBool(&b) && !msIsNil(&b) && !msIsI64(&b));
  EXPECT(ctx, msIsI64(&i) && !msIsF64(&i) && !msIsBool(&i));
  EXPECT(ctx, msIsF64(&f) && !msIsI64(&f));
  MsValue o = msValueObj((MsObj*)(uintptr_t)0x10);  // 判型不 deref
  EXPECT(ctx, msIsObj(&o) && !msIsI64(&o) && !msIsNil(&o));
  EXPECT(ctx, msValueEqual(&o, &o));  // OBJ 相等 = 指针同一
  EXPECT_STREQ(ctx, "object", msValueTypeName(&o));
  EXPECT(ctx, msValueIsTruthy(&o));
  MsValue o2 = msValueObj((MsObj*)(uintptr_t)0x20);
  EXPECT(ctx, !msValueEqual(&o, &o2));
}
```

main() 的 tests 数组 `valueOrder` 之后插入 `valuePredicateFamily,`；文件头 include 区无新增（stdint/stdbool 已有，uintptr_t 在 stdint.h）。

- [ ] **Step 1.2: 跑测试确认编译失败** — `cmake --build build --config Debug --target test_vm_basic` 预期 MSVC C2065（MS_VAL_I64/msIsNil/msValueObj 未定义）。

- [ ] **Step 1.3: 实现 vm.h** — 枚举与 union 改为：

```c
typedef struct MsObj MsObj;  // 对象头（obj/obj.h，T012）

// 值类别（22 §1 五 tag + T011 过渡 kind；过渡项归宿：STRING→T013、FUNCTION/NATIVE→T018、RANGE_ITER→T017）
typedef enum MsValueKind {
  MS_VAL_NIL,
  MS_VAL_BOOL,
  MS_VAL_I64,
  MS_VAL_F64,
  MS_VAL_OBJ,         // 堆对象指针（T012）
  MS_VAL_STRING,      // 过渡：借用常量池文本（转义原文；解码归 T013）
  MS_VAL_FUNCTION,    // 过渡：函数 chunk 引用（MsObjFunction 归 T018）
  MS_VAL_NATIVE,      // 过渡：C 内置函数（MsObjNative 归 T018）
  MS_VAL_RANGE_ITER,  // 过渡：range 惰性迭代器（MsObjIter 归 T017）
} MsValueKind;
```

union 在 `double f;` 后加 `MsObj* obj;`；MsValue 定义后加判型族与构造声明：

```c
// 判型内联族（22 §1；28 §1 bool 谓词命名）
static inline bool msIsNil(const MsValue* v) { return v->kind == MS_VAL_NIL; }
static inline bool msIsBool(const MsValue* v) { return v->kind == MS_VAL_BOOL; }
static inline bool msIsI64(const MsValue* v) { return v->kind == MS_VAL_I64; }
static inline bool msIsF64(const MsValue* v) { return v->kind == MS_VAL_F64; }
static inline bool msIsObj(const MsValue* v) { return v->kind == MS_VAL_OBJ; }
```

（vm.h 的 `// ---- value.c：值操作助手 ----` 声明区追加：`MsValue msValueObj(MsObj* obj);`）

- [ ] **Step 1.4: 实现 value.c** — 全文件 `MS_VAL_INT` → `MS_VAL_I64`、`MS_VAL_FLOAT` → `MS_VAL_F64`；追加构造器；equal 补 OBJ 分支；typeName/toStr 补 OBJ：

```c
MsValue msValueObj(MsObj* obj) {
  MsValue v = {MS_VAL_OBJ, {0}};
  v.as.obj = obj;
  return v;
}
```

equal 的 switch 中 `case MS_VAL_NATIVE:` 之后加：

```c
    case MS_VAL_OBJ:  // 身份语义：值等价随容器任务接入
      return b->kind == MS_VAL_OBJ && a->as.obj == b->as.obj;
```

typeName 的 `case MS_VAL_RANGE_ITER:` 前加 `case MS_VAL_OBJ: return "object";`（switch case 形态对齐现有写法）；toStr 同位置加 `case MS_VAL_OBJ: snprintf(buf, cap, "<object>"); break;`。

- [ ] **Step 1.5: 机械重命名** — `src/vm/vm.c` 与 `src/vm/builtins_bridge.c` 内全部 `MS_VAL_INT` → `MS_VAL_I64`、`MS_VAL_FLOAT` → `MS_VAL_F64`（vm.c 13 处、builtins_bridge.c 1 处）。

- [ ] **Step 1.6: 全量构建 + 测试** — `cmake --build build --config Debug` 零警告；`ctest --test-dir build -C Debug` 全绿（98 项含 test_vm_basic）。

- [ ] **Step 1.7: Commit** — `git add -A; git commit -m "✨ feat(vm): MsValue five-tag union with MS_VAL_OBJ and predicate family (T012 task 1)"`

---

### Task 2: 对象头/分配器 + GcHeap 生命周期与统计

**Files:**
- Create: `src/obj/obj.h`
- Modify: `src/obj/obj.c`（占位覆盖）
- Create: `src/gc/gc.h`
- Modify: `src/gc/gc.c`（占位覆盖，本任务仅 heap 生命周期/统计）
- Modify: `src/vm/vm.c`（heap 接线）
- Modify: `src/vm/vm.h:60`（heap 注释更新）
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/unit/test_gc_basic.c`

- [ ] **Step 2.1: 写失败测试** — `tests/unit/test_gc_basic.c` 首批：

```c
// T012：堆/GC 单测（docs/tasks/T012 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj.h"
#include "test_harness.h"
#include "vm/vm.h"

// 测试负载：NULL 行为类型 + magic 载荷（链式引用用 child；MS_OBJ_ITERATOR 行为归 T017）
typedef struct TestNode {
  MsObj head;
  MsValue child;
  ms_u64 magic;
} TestNode;

static TestNode* allocNode(MsVM* vm, ms_u64 magic) {
  TestNode* n = (TestNode*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(TestNode));
  n->child = msValueNil();
  n->magic = magic;
  return n;
}

TEST(allocAndStats) {
  MsVM vm;
  msVmInit(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, gcHeapBytes(&vm));
  TestNode* a = allocNode(&vm, 1);
  TestNode* b = allocNode(&vm, 2);
  EXPECT(ctx, a != NULL && b != NULL);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 2 * (long long)sizeof(TestNode), (long long)gcHeapBytes(&vm));
  EXPECT_EQ(ctx, MS_OBJ_ITERATOR, (MsObjType)a->head.type);
  EXPECT_EQ(ctx, 0, a->head.flags);
  EXPECT(ctx, vm.heap->objects == &b->head && b->head.next == &a->head);  // 头插
  msVmFree(&vm);
}

TEST(heapFreeReleasesAll) {  // vmFree 全释放（泄漏由 ASan 门禁验证）
  MsVM vm;
  msVmInit(&vm);
  for (int i = 0; i < 100; i++) {
    EXPECT(ctx, allocNode(&vm, (ms_u64)i) != NULL);
  }
  EXPECT_EQ(ctx, 100, gcHeapObjCount(&vm));
  msVmFree(&vm);
}
```

main()（文件尾，沿用 test_vm_basic 跑器模式）：

```c
int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      allocAndStats,
      heapFreeReleasesAll,
  };
  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  for (int i = 0; i < total; i++) {
    if (only < 0 || only == i) {
      tests[i](&ctx);
    }
  }
  if (ctx.failures == 0) {
    printf("all tests passed\n");
  }
  return ctx.failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2.2: 注册测试** — `tests/CMakeLists.txt` 追加 `ms_add_test(gc_basic ms_obj ms_gc ms_vm ms_compiler ms_binder)`。构建确认失败：C1083 找不到 gc/gc.h、obj/obj.h。

- [ ] **Step 2.3: 实现 obj.h**：

```c
// 对象头与全堆分配器（T012，22 §2/§9）：类型枚举全集 + 头部 + 链表分配；类型行为由 T013+ 各容器任务补
#ifndef INCLUDE_MS_OBJ_OBJ_H_
#define INCLUDE_MS_OBJ_OBJ_H_

#include <stddef.h>

#include "common/common.h"
#include "vm/vm.h"

// 对象类型全集（22 §2 表）；T012 均为 NULL 实现（仅分配挂链，行为归各容器任务）
typedef enum MsObjType {
  MS_OBJ_STRING,
  MS_OBJ_BIGINT,
  MS_OBJ_FUNCTION,
  MS_OBJ_NATIVE,
  MS_OBJ_CLOSURE,
  MS_OBJ_UPVALUE,
  MS_OBJ_CLASS,
  MS_OBJ_INSTANCE,
  MS_OBJ_BOUND_METHOD,
  MS_OBJ_LIST,
  MS_OBJ_MAP,
  MS_OBJ_SET,
  MS_OBJ_TUPLE,
  MS_OBJ_MODULE,
  MS_OBJ_COROUTINE,
  MS_OBJ_CHANNEL,
  MS_OBJ_FUTURE,
  MS_OBJ_ITERATOR,
  MS_OBJ_WEAKREF,
  MS_OBJ_COUNT,
} MsObjType;

#define MS_OBJ_MARK ((ms_u8)(1u << 0))       // 标记清除黑位
#define MS_OBJ_PERMANENT ((ms_u8)(1u << 1))  // 永久代（intern/常量池雏形）：跳过回收

typedef struct MsObj {
  ms_u8 type;          // MsObjType
  ms_u8 flags;         // MS_OBJ_* 位集
  struct MsObj* next;  // 全堆链表（GC 枚举）
  size_t size;         // 过渡：malloc 尺寸（sweep 按 size 递减 bytes；T026 块分配器重整头部）
} MsObj;

// 分配整体 size 字节（含 MsObj 头；调用方传 sizeof(具体对象类型)）；失败返回 NULL
MsObj* msObjAlloc(MsVM* vm, MsObjType type, size_t size);

#endif  // INCLUDE_MS_OBJ_OBJ_H_
```

- [ ] **Step 2.4: 实现 obj.c**（占位 typedef 移除；guard 调用 Task 3 接入）：

```c
// 全堆链表分配器（T012，22 §2）：malloc + 头初始化 + 头插链表 + 统计累加
#include "obj/obj.h"

#include <stdlib.h>

MsObj* msObjAlloc(MsVM* vm, MsObjType type, size_t size) {
  MsObj* obj = malloc(size);
  if (obj == NULL) {
    return NULL;  // v1 简化：分配失败上抛 NULL，OOM 策略归 T026
  }
  obj->type = (ms_u8)type;
  obj->flags = 0;
  obj->size = size;
  GcHeap* heap = vm->heap;
  obj->next = heap->objects;
  heap->objects = obj;
  heap->bytes += size;
  heap->count++;
  return obj;
}
```

- [ ] **Step 2.5: 实现 gc.h**（完整公共面一次落齐；未实装符号 Task 3/4 定义）：

```c
// 过渡版整堆 STW 标记清除 GC（T012，23-gc.md 过渡形态；分代/并发归 T026/T027）
#ifndef INCLUDE_MS_GC_GC_H_
#define INCLUDE_MS_GC_GC_H_

#include <stddef.h>
#include <stdint.h>

#include "obj/obj.h"
#include "vm/vm.h"

typedef struct GcHeap {
  MsObj* objects;             // 全堆链表头（头插）
  size_t bytes;               // 在堆字节数（分配量 - 回收量）
  int32_t count;              // 在堆对象数
  size_t threshold;           // 触发全量回收阈值（MS_GC_HEAP_MAX 或默认 8MB）
  MsObj** gray;               // 标记 worklist（容量跨次复用；23-gc §4 灰队列方向）
  int32_t grayCount, grayCap;
} GcHeap;

typedef void (*MarkChildrenFn)(MsVM* vm, MsObj* obj);
// markChildren 注册扩展点：每个容器任务为自身类型补标记并同步 GC 单测（任务书 T012 §6）
void gcSetMarkFn(MsObjType type, MarkChildrenFn fn);
// 标记入口：root 扫描与已注册 markFn 共用（仅追踪 MS_VAL_OBJ）
void gcMarkValue(MsVM* vm, MsValue v);
// 整堆 STW 标记清除（root = 栈 + 全局表）；保留为 runtime.gc() 全量路径（归 T036）
void gcCollectAll(MsVM* vm);
// 分配点 guard：bytes >= threshold → gcCollectAll（msObjAlloc 调用）
void msVmAllocGuard(MsVM* vm);

void gcHeapInit(GcHeap* heap);  // 阈值解析 MS_GC_HEAP_MAX（十进制字节），缺省 8MB
void gcHeapFree(GcHeap* heap);  // 释放全部对象（含 permanent）与 worklist
size_t gcHeapBytes(const MsVM* vm);
int32_t gcHeapObjCount(const MsVM* vm);

#endif  // INCLUDE_MS_GC_GC_H_
```

- [ ] **Step 2.6: 实现 gc.c**（占位 typedef 移除；本步仅生命周期/统计/env，标记清除 Task 3 追加）：

```c
// 过渡版整堆 STW 标记清除（T012，23-gc.md §1 过渡形态）
#include "gc/gc.h"

#include <stdlib.h>

#include "common/common.h"

#define MS_GC_HEAP_MAX_DEFAULT (8u * 1024u * 1024u)  // 任务书 T012 §2：默认 8MB

static MarkChildrenFn g_markFns[MS_OBJ_COUNT];  // 未注册 = 无 children

void gcSetMarkFn(MsObjType type, MarkChildrenFn fn) { g_markFns[type] = fn; }

void gcHeapInit(GcHeap* heap) {
  heap->objects = NULL;
  heap->bytes = 0;
  heap->count = 0;
  heap->threshold = MS_GC_HEAP_MAX_DEFAULT;
  const char* env = getenv("MS_GC_HEAP_MAX");  // 先行尊重（T012 §2）：十进制字节数
  if (env != NULL) {
    char* end = NULL;
    unsigned long long v = strtoull(env, &end, 10);
    if (end != env && *end == '\0' && v > 0) {
      heap->threshold = (size_t)v;
    }
  }
  heap->gray = NULL;
  heap->grayCount = heap->grayCap = 0;
}

void gcHeapFree(GcHeap* heap) {
  MsObj* obj = heap->objects;
  while (obj != NULL) {
    MsObj* next = obj->next;
    free(obj);
    obj = next;
  }
  free(heap->gray);
  heap->objects = NULL;
  heap->bytes = 0;
  heap->count = 0;
  heap->gray = NULL;
  heap->grayCount = heap->grayCap = 0;
}

size_t gcHeapBytes(const MsVM* vm) { return vm->heap->bytes; }

int32_t gcHeapObjCount(const MsVM* vm) { return vm->heap->count; }
```

- [ ] **Step 2.7: vm 接线** — vm.c：include 区（string.h 后）加 `#include "gc/gc.h"`；msVmInit 的 `vm->heap = NULL;` 改为：

```c
  vm->heap = malloc(sizeof(GcHeap));
  MS_ASSERT(vm->heap != NULL);
  gcHeapInit(vm->heap);
```

msVmFree 开头加：

```c
  if (vm->heap != NULL) {
    gcHeapFree(vm->heap);
    free(vm->heap);
    vm->heap = NULL;
  }
```

（msVmFree 末尾 msVmInit 重置会重建空 heap，VM 可复用。）vm.h:60 注释改 `struct GcHeap;  // gc/gc.h；msVmInit 创建、msVmFree 销毁`。

- [ ] **Step 2.8: CMake** — CMakeLists.txt 在 T011 块后加：

```cmake
# T012：对象头/分配器 + 过渡 GC；obj 分配经 guard（gc）、vm 持有 heap 生命周期
target_link_libraries(ms_obj PRIVATE ms_gc)
target_link_libraries(ms_vm PRIVATE ms_gc)
```

- [ ] **Step 2.9: 构建 + 测试** — `cmake --build build --config Debug` 零警告；`ctest --test-dir build -C Debug -R gc_basic` 2 用例通过；全量回归绿。

- [ ] **Step 2.10: Commit** — `git add -A; git commit -m "✨ feat(obj): MsObj header, type enum and heap-list allocator with stats (T012 task 2)"`

---

### Task 3: gcCollectAll 标记清除 + 分配 guard

**Files:**
- Modify: `src/gc/gc.c`（标记清除 + guard 追加）
- Modify: `src/obj/obj.c:8`（guard 接入）
- Modify: `tests/unit/test_gc_basic.c`

- [ ] **Step 3.1: 写失败测试** — test_gc_basic.c 追加 4 用例（插在 heapFreeReleasesAll 后，tests 数组同步）：

```c
TEST(valueDirectStorageNoHeap) {  // 值直存：nil/bool/i64/f64 无堆分配（DoD）
  MsVM vm;
  msVmInit(&vm);
  (void)msValueNil();
  (void)msValueBool(true);
  (void)msValueInt(9);
  (void)msValueFloat(1.5);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, gcHeapBytes(&vm));
  msVmFree(&vm);
}

TEST(collectDeadObjects) {  // 分配回收：死对象全收、字节回落（DoD）
  MsVM vm;
  msVmInit(&vm);
  for (int i = 0; i < 50; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 无任何引用 → 全死
  }
  EXPECT_EQ(ctx, 50, gcHeapObjCount(&vm));
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, (long long)gcHeapBytes(&vm));
  msVmFree(&vm);
}

TEST(stackKeepalive) {  // 栈保活：模块帧值栈即整个栈（23-gc §1 过渡）
  MsVM vm;
  msVmInit(&vm);
  TestNode* live = allocNode(&vm, 0xC0FFEE);
  for (int i = 0; i < 10; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 噪声死对象
  }
  vm.stack = malloc(sizeof(MsValue));  // 直接构造值栈槽
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&live->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0xC0FFEE, (long long)live->magic);  // 误收则 ASan UAF
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  msVmFree(&vm);
}

TEST(globalKeepalive) {  // 全局表保活
  MsVM vm;
  msVmInit(&vm);
  TestNode* live = allocNode(&vm, 0xABCD);
  vm.globals = malloc(sizeof(MsGlobalVar));
  vm.globalCount = vm.globalCap = 1;
  vm.globals[0].name = "keep";
  vm.globals[0].len = 4;
  vm.globals[0].value = msValueObj(&live->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0xABCD, (long long)live->magic);
  msVmFree(&vm);  // globals 由 msVmFree 释放
}
```

- [ ] **Step 3.2: 确认失败** — `cmake --build build --config Debug --target test_gc_basic` 预期 LNK2019（gcCollectAll 未定义）。

- [ ] **Step 3.3: 实现 gc.c 标记清除 + guard**（gcHeapObjCount 后追加）：

```c
// ---- 标记清除（root = 栈 + 全局表；模块帧值栈即整个栈，无 native 帧）----

void gcMarkValue(MsVM* vm, MsValue v) {
  if (v.kind != MS_VAL_OBJ) {
    return;  // NIL/BOOL/I64/F64 直存；过渡 kind 借用非堆存储
  }
  GcHeap* heap = vm->heap;
  if (v.as.obj->flags & MS_OBJ_MARK) {
    return;
  }
  v.as.obj->flags |= MS_OBJ_MARK;
  MS_ARRAY_PUSH(heap->gray, heap->grayCount, heap->grayCap, v.as.obj);
}

static void markRoots(MsVM* vm) {
  for (int32_t i = 0; i < vm->stackTop; i++) {
    gcMarkValue(vm, vm->stack[i]);
  }
  for (int32_t i = 0; i < vm->globalCount; i++) {
    gcMarkValue(vm, vm->globals[i].value);
  }
}

void gcCollectAll(MsVM* vm) {
  GcHeap* heap = vm->heap;
  markRoots(vm);
  while (heap->grayCount > 0) {  // 迭代 worklist（灰队列方向；防爆栈）
    MsObj* obj = heap->gray[--heap->grayCount];
    MarkChildrenFn fn = g_markFns[obj->type];
    if (fn != NULL) {
      fn(vm, obj);
    }
  }
  MsObj** link = &heap->objects;
  while (*link != NULL) {  // sweep：摘白释放，黑/永久保留
    MsObj* obj = *link;
    if (obj->flags & (MS_OBJ_MARK | MS_OBJ_PERMANENT)) {
      obj->flags &= (ms_u8)~MS_OBJ_MARK;
      link = &obj->next;
      continue;
    }
    *link = obj->next;
    heap->bytes -= obj->size;
    heap->count--;
    free(obj);
  }
}

void msVmAllocGuard(MsVM* vm) {
  if (vm->heap->bytes >= vm->heap->threshold) {
    gcCollectAll(vm);
  }
}
```

- [ ] **Step 3.4: obj.c 接 guard** — msObjAlloc 开头（malloc 前）加：

```c
  msVmAllocGuard(vm);
```

文件 include 区加 `#include "gc/gc.h"`（stdlib.h 后）。

- [ ] **Step 3.5: 构建 + 测试** — `ctest --test-dir build -C Debug -R gc_basic` 6 用例通过；全量回归绿。

- [ ] **Step 3.6: Commit** — `git add -A; git commit -m "✨ feat(gc): transitional whole-heap mark-sweep collector and alloc guard (T012 task 3)"`

---

### Task 4: markFn 注册 + 链式/自引用

**Files:**
- Modify: `tests/unit/test_gc_basic.c`（gcSetMarkFn/gcMarkValue 自 Task 2/3 已可用，本任务纯测试扩展）

- [ ] **Step 4.1: 写失败测试** — test_gc_basic.c 追加（文件头 TestNode 定义之后加 markFn，用例插在 globalKeepalive 后）：

```c
static void testMarkNode(MsVM* vm, MsObj* obj) {  // 标记 TestNode.child（容器任务 markFn 同构）
  gcMarkValue(vm, ((TestNode*)obj)->child);
}

TEST(chainReference) {  // A→B→C：A 可达全活；不可达则同收
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);
  MsVM vm;
  msVmInit(&vm);
  TestNode* a = allocNode(&vm, 1);
  TestNode* b = allocNode(&vm, 2);
  TestNode* c = allocNode(&vm, 3);
  a->child = msValueObj(&b->head);
  b->child = msValueObj(&c->head);
  vm.stack = malloc(sizeof(MsValue));
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&a->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 3, gcHeapObjCount(&vm));  // A 活则 B C 活
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));  // A 失根后 B C 同收
  msVmFree(&vm);
}

TEST(selfReferenceCycle) {  // 自引用：追踪式天然回收（无误删/漏删）
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);
  MsVM vm;
  msVmInit(&vm);
  TestNode* a = allocNode(&vm, 7);
  a->child = msValueObj(&a->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  msVmFree(&vm);
}
```

tests 数组追加 `chainReference, selfReferenceCycle,`。

- [ ] **Step 4.2: 跑测试** — 预期直接通过（gcSetMarkFn/gcMarkValue 已实装）；若失败则修复 gc.c 分派/标记逻辑。`ctest --test-dir build -C Debug -R gc_basic` 8 用例绿。

- [ ] **Step 4.3: Commit** — `git add -A; git commit -m "✅ test(gc): chain reference and self-cycle collection via markFn registration (T012 task 4)"`

---

### Task 5: permanent 永久代 + 阈值自动触发

**Files:**
- Modify: `tests/unit/test_gc_basic.c`

- [ ] **Step 5.1: 写失败测试** — 追加 2 用例（插在 selfReferenceCycle 后）：

```c
TEST(permanentSurvives) {  // permanent：无 root 引用也不回收；heapFree 释放
  MsVM vm;
  msVmInit(&vm);
  MsObj* perm = &allocNode(&vm, 0x1234)->head;
  perm->flags |= MS_OBJ_PERMANENT;  // intern/常量池雏形（intern 表归 T013）
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  msVmFree(&vm);  // 含 permanent 全释放（ASan 无泄漏）
}

TEST(allocGuardAutoTrigger) {  // MS_GC_HEAP_MAX 小阈值：循环分配堆稳定（DoD）
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=4096");
#else
  setenv("MS_GC_HEAP_MAX", "4096", 1);
#endif
  MsVM vm;
  msVmInit(&vm);  // gcHeapInit 读 env
  size_t maxSeen = 0;
  for (int i = 0; i < 10000; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 死对象：guard 触发全量回收
    size_t now = gcHeapBytes(&vm);
    if (now > maxSeen) {
      maxSeen = now;
    }
  }
  EXPECT(ctx, maxSeen < 4096 + 2 * sizeof(TestNode));  // 阈值 + 单次分配余量
  EXPECT(ctx, gcHeapObjCount(&vm) < 200);
  msVmFree(&vm);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}
```

tests 数组追加 `permanentSurvives, allocGuardAutoTrigger,`。

- [ ] **Step 5.2: 跑测试** — 预期通过（env 解析/guard/sweep permanent 分支已实装）；失败则修 gcHeapInit env 路径或 guard 阈值比较。`ctest --test-dir build -C Debug -R gc_basic` 10 用例绿。

- [ ] **Step 5.3: Commit** — `git add -A; git commit -m "✅ test(gc): permanent survival and MS_GC_HEAP_MAX auto-trigger stability (T012 task 5)"`

---

### Task 6: ASan 门禁 + 文档收尾

**Files:**
- Modify: `docs/tasks/T012-heap-basic-gc.md`（状态 ✅ + DoD 勾选）
- Modify: `docs/tasks/README.md:33`（T012 行 ⬜→✅）

- [ ] **Step 6.1: ASan 构建** — build/ 单目录顺序复用（AGENTS.md 合规）：

```powershell
Remove-Item build -Recurse -Force
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DMS_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

预期：98+10 项全绿、无 UAF/泄漏报告（gc_basic 10 用例即 DoD "ASan 构建下 GC 单测无 UAF/泄漏"）。若 UBSan 报既有代码问题（非 T012 引入），记录并仅修 T012 范围内问题。

- [ ] **Step 6.2: 恢复 MSVC 基线** — `Remove-Item build -Recurse -Force; cmake --preset msvc; cmake --build build --config Debug; ctest --test-dir build -C Debug` 全绿。

- [ ] **Step 6.3: 文档更新** — T012-heap-basic-gc.md：`- 状态: 未开始` → `- 状态: ✅`；§4 DoD 五项 `- [ ]` → `- [x]`。README.md T012 行 `⬜` → `✅`。

- [ ] **Step 6.4: 终验** — `ctest --test-dir build -C Debug` 全绿；`git status` 无遗漏文件；编码检查（新文件 UTF-8 无 BOM、LF、无尾随空白）。

- [ ] **Step 6.5: Commit** — `git add -A; git commit -m "📝 docs(tasks): mark T012 done"`

- [ ] **Step 6.6: 收尾报告** — 汇报 DoD 五项逐条达成证据（测试数、ASan 输出、提交列表），等待用户审查后按 finishing-a-development-branch 决定合并方式。

---

## 自审记录（计划 vs spec 覆盖）

| Spec 条目 | 任务 |
|-----------|------|
| §4.1 五 tag/判型族/重命名/OBJ equal-typename-truthy | Task 1 |
| §4.2 MsObj/MsObjType 全集/flags/msObjAlloc | Task 2 |
| §4.3 GcHeap/gcHeapInit-Free/env/gcCollectAll/worklist/gcSetMarkFn/gcMarkValue/msVmAllocGuard/统计 | Task 2/3/4 |
| §4.4 vm 接线 | Task 2 |
| §4.5 CMake/测试注册 | Task 2 |
| §5.1 用例表 9 项 | Task 2（alloc/stats、heapFree）/ Task 3（直存、回收、栈、全局）/ Task 4（链式、自引用）/ Task 5（permanent、自动触发） |
| §5.2 ASan 本地 clang | Task 6 |
| 偏差 C keepalive.ms 延后 | 不做（spec 注明） |
| 类型一致性 | gcSetMarkFn(MsObjType, MarkChildrenFn) 与 gc.c g_markFns 表一致；TestNode/allocNode 前置定义供后续任务复用 |





