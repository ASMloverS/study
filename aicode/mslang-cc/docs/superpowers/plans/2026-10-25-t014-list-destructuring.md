# T014 list 与解构赋值 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 MsObjList 容器（索引/切片/拼接/重复/方法族/版本号迭代器）与 UNPACK_SEQ/UNPACK_STAR 解构赋值。

**Architecture:** 三层推进——obj_list.c 数据面（mutator + 切片 + 稳定归并排序 + 迭代器壳）→ vm.c/list_methods.c 执行面（指令接通 + 10 §3 方法族 + CALL_METHOD/GET_ATTR 分派）→ compiler/binder 解构编译（UNPACK 指令发射 + 目标落盘）。迭代器经首载荷 `kind` 域判别（str/list 共享 MS_OBJ_ITERATOR tag，T017 泛化方向）。

**Tech Stack:** C11（MSVC /W4 /WX 与 GCC/Clang -Wall -Wextra -Werror 双编译面）、CMake、ctest（unit + fixture golden）。

**Worktree:** `.worktrees/t014-list-destructuring`（分支 `t014-list-destructuring`）。全部命令工作目录为 worktree 内 `aicode/mslang-cc`（下记 `$WT`）。

**关键设计决策（偏离/自由裁量记录）：**

- D1 `UNPACK_STAR <u8 slot>` 改为 `<u8 before> <u8 after>`（chunk.c 布局表 OPL_U8 → OPL_U8_U8）：星号任意位需要 before/after 两计数，单 u8 slot 无法承载。
- D2 `UNPACK_SEQ <n>`：TOS list → 弹出；元素数 < n 报 `ValueError`；按序压入 items[0..n)（多余丢弃，04 §2）。非 list 报 `TypeError: cannot unpack`。
- D3 `UNPACK_STAR <before> <after>`：TOS list → 前段逐值 + 中段新 list（[before, count-after)）+ 后段逐值；元素数 < before+after 报 `ValueError`。
- D4 右值多表达式（AST_TUPLE）本任务以 BUILD_LIST 物化为 list 后统一走 UNPACK（任务书"临时栈序列"实现注记；T015 后统一走 tuple）。
- D5 sort 为原地**稳定归并排序**（temp 缓冲，比较失败原序保持）——timsort 的可观察行为等价（稳定 + `<` 序），自适应 runs 优化后置。
- D6 解构目标语义随 AST 形态：AST_ASSIGN 元组目标 = 赋值（IDENT 走 storeIdent，未定义全局报错，同单目标）；AST_DESTRUCTURE 目标 = 声明式（binder T009 已登记新绑定；module 层 DEF_GLOBAL+STORE_GLOBAL 落盘，同 var 声明）。
- D7 binder/ast_dump 对 DESTRUCTURE 非 IDENT 目标（`xs[0]` 等）现按 `as.ident` 联合体硬解（潜在类型混淆）：binder 改为 INDEX/MEMBER 按表达式绑定、dump 改为紧凑 dumpNode。
- D8 深相等：msValueEqual 增 list 逐元素深比较（指针短路）；互引环检测后置（自引用 `xs[0] = xs` 由短路覆盖）。
- D9 str_methods.c 静态 collectToList 抽取为 value.c `msVmCollectStackToList`（split/list map 共用，BUILD_LIST 同款收缩）。
- D10 迭代器 markFn 按 kind 分派（vm.c strIterMark → iterMark）；FOR_ITER 同源判别。

**基线：** main @ ae9e15ccb，104 tests 全绿（worktree 内已验证）。

---

### Task 0: 计划落盘 + 任务状态开工

**Files:**
- Create: `docs/superpowers/plans/2026-10-25-t014-list-destructuring.md`（本文件）
- Modify: `docs/tasks/T014-list-destructuring.md`（头部状态：未开始 → 🚧）
- Modify: `docs/tasks/README.md`（总表 T014 行状态 ⬜ → 🚧）

- [x] **Step 1: 更新任务文档状态**

T014 文档头部 `- 状态: 未开始` → `- 状态: 进行中`；README 总表 T014 行 `⬜` → `🚧`。

- [x] **Step 2: 提交**

```bash
git add docs/tasks/T014-list-destructuring.md docs/tasks/README.md docs/superpowers/plans/
git commit -m "📝 docs(tasks): mark T014 in progress with implementation plan"
```

---

### Task 1: obj_list 数据面——version 字段 + 核心 mutator

**Files:**
- Modify: `src/obj/obj_list.h`（结构 + 新 API）
- Modify: `src/obj/obj_list.c`（实现）
- Create: `tests/unit/test_obj_list.c`（数据面测试 + runSrc 桥，后续任务复用）
- Modify: `tests/CMakeLists.txt`（注册）

- [ ] **Step 1: 写失败测试**

`tests/unit/test_obj_list.c` 完整内容：

```c
// T014：list 数据面 + 执行面单测（docs/tasks/T014 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "gc/gc.h"
#include "obj/obj_list.h"
#include "obj/obj_string.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

// ---------- 数据面（Task 1） ----------

TEST(listGrowthDoubling) {  // §5.1 倍增：append 1000，cap 序列 8→16→…、均摊 O(1)
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  int32_t lastCap = 0;
  int32_t growthSteps = 0;
  for (int i = 0; i < 1000; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i)));
    if (list->cap != lastCap) {
      growthSteps++;
      if (lastCap == 0) {
        EXPECT_EQ(ctx, 8, list->cap);  // 初始 8（22 §2 表）
      } else {
        EXPECT_EQ(ctx, lastCap * 2, list->cap);  // 倍增
      }
      lastCap = list->cap;
    }
  }
  EXPECT_EQ(ctx, 1000, list->count);
  EXPECT(ctx, growthSteps <= 8);  // 8→16→…→1024 共 8 步（均摊 O(1)）
  msVmFree(&vm);
}

TEST(listNegativeIndexAndBounds) {  // §5.1 负索引/越界
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  for (int i = 0; i < 3; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i + 1)));
  }
  MsValue out = msValueNil();
  EXPECT(ctx, msObjListGet(list, -1, &out) && out.as.i == 3);  // xs[-1] → 末元素
  EXPECT(ctx, !msObjListGet(list, 10, &out));                  // 越界 false
  EXPECT(ctx, msObjListSet(list, -1, msValueInt(9)));
  EXPECT(ctx, msObjListGet(list, 2, &out) && out.as.i == 9);
  EXPECT(ctx, !msObjListSet(list, 10, msValueInt(0)));  // 越界写 false
  msVmFree(&vm);
}

TEST(listInsertRemoveClearReverse) {
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  for (int i = 0; i < 3; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i + 1)));  // [1,2,3]
  }
  ms_u32 v0 = list->version;
  EXPECT(ctx, msObjListInsert(list, 1, msValueInt(9)));  // [1,9,2,3]
  EXPECT_EQ(ctx, 4, list->count);
  MsValue mid = msValueNil();
  EXPECT(ctx, msObjListGet(list, 1, &mid) && mid.as.i == 9);
  EXPECT(ctx, list->version != v0);  // 结构修改 → version 递增
  MsValue out = msValueNil();
  EXPECT(ctx, msObjListRemoveAt(list, -1, &out) && out.as.i == 3);  // [1,9,2]
  EXPECT_EQ(ctx, 3, list->count);
  EXPECT(ctx, !msObjListRemoveAt(list, 5, &out));
  msObjListReverse(&vm ? &vm : &vm, list);  // 占位防误用；下一行真调用
  msObjListReverse(list_ptr_fix_placeholder(list));  // 见 Step 3 修正注记
  msVmFree(&vm);
}

TEST(listVersionBumpRules) {  // 22 §4：结构修改失效；SET_INDEX 非结构不失效
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(1)));
  ms_u32 v = list->version;
  EXPECT(ctx, msObjListSet(list, 0, msValueInt(2)));
  EXPECT_EQ(ctx, v, list->version);  // 非结构性
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(3)));
  EXPECT(ctx, list->version != v);
  v = list->version;
  msObjListClear(list);
  EXPECT_EQ(ctx, 0, list->count);
  EXPECT(ctx, list->version != v);
  msVmFree(&vm);
}

TEST(listIndexOf) {
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(5)));
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(6)));
  MsValue needle = msValueInt(6);
  EXPECT_EQ(ctx, 1, msObjListIndexOf(list, &needle));
  needle = msValueInt(9);
  EXPECT_EQ(ctx, -1, msObjListIndexOf(list, &needle));
  msVmFree(&vm);
}

int main(void) {
  TestCtx ctx = {0};
  listGrowthDoubling(&ctx);
  listNegativeIndexAndBounds(&ctx);
  listInsertRemoveClearReverse(&ctx);
  listVersionBumpRules(&ctx);
  listIndexOf(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
```

**注记：** `listInsertRemoveClearReverse` 中两行占位（`msObjListReverse` 签名是 `void msObjListReverse(MsObjList*)`）——写测试时直接用正确签名，删除占位行：

```c
  msObjListReverse(list);  // [2,9,1]
  MsValue first = msValueNil();
  EXPECT(ctx, msObjListGet(list, 0, &first) && first.as.i == 2);
  msObjListClear(list);
  EXPECT_EQ(ctx, 0, list->count);
```

- [ ] **Step 2: 注册测试**

`tests/CMakeLists.txt` 末尾追加：

```cmake
ms_add_test(obj_list ms_obj ms_gc ms_vm ms_compiler ms_binder)
```

- [ ] **Step 3: 运行确认失败**

```bash
cmake --preset msvc 2>&1 | Select-Object -Last 1
cmake --build build --config Debug 2>&1 | Select-Object -Last 3
ctest --test-dir build -C Debug -R obj_list --output-on-failure
```

Expected: 编译失败（version 字段/新 API 未定义）。

- [ ] **Step 4: 实现**

`src/obj/obj_list.h` 全量替换为：

```c
// MsObjList（T014，22 §4）：倍增数组 + 结构版本号 + 切片/拼接/排序数据面 + 迭代器壳
// T013 最小桥（push/reserve/get/mark/free）延续；split/join 收集与 BUILD_LIST 直填路径不变
#ifndef INCLUDE_MS_OBJ_OBJ_LIST_H_
#define INCLUDE_MS_OBJ_OBJ_LIST_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef struct MsObjList {
  MsObj head;
  MsValue* items;  // 倍增（初始 8）
  int32_t count;
  int32_t cap;
  ms_u32 version;  // 结构版本号（22 §4）：结构性修改递增；迭代器快照失配 → IteratorError
} MsObjList;

// 原地稳定排序结果（缺省 < 序；比较失败原序保持）
typedef enum MsListSortResult {
  MS_LIST_SORT_OK = 0,
  MS_LIST_SORT_TYPE,  // 元素两两不可比较
  MS_LIST_SORT_OOM,
} MsListSortResult;

MsObjList* msObjListNew(MsVM* vm);  // 空表（items NULL）
// 尾加（倍增；结构性修改 version++；分配失败返回 false 且列表不变）
bool msObjListPush(MsVM* vm, MsObjList* list, MsValue v);
// 预留容量（容量变化非结构修改，不 bump；失败 false 且列表不变）
bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n);
// 索引读取（负索引换算；越界返回 false）
bool msObjListGet(const MsObjList* list, ms_i64 i, MsValue* out);
// 索引写入（负索引换算；非结构性：不 bump；越界返回 false）
bool msObjListSet(MsObjList* list, ms_i64 i, MsValue v);
// 插入（负索引换算后钳位 [0,count]；version++；分配失败 false 且列表不变）
bool msObjListInsert(MsObjList* list, ms_i64 i, MsValue v);
// 按下标移除并返回（负索引换算；version++；越界返回 false）
bool msObjListRemoveAt(MsObjList* list, ms_i64 i, MsValue* out);
// 清空（count=0，容量保留；version++）
void msObjListClear(MsObjList* list);
// 原地反转（version++）
void msObjListReverse(MsObjList* list);
// 原地稳定排序（缺省 < 序，msValueOrder；成功 version++；失败原序保持）
MsListSortResult msObjListSort(MsObjList* list);
// 按值首查（msValueEqual 深等价）；未命中 -1
int32_t msObjListIndexOf(const MsObjList* list, const MsValue* v);
// 规范切片 → 新 list（源由调用方 root；step==0 返回 NULL 且 *errStepZero 置 true）
MsObjList* msObjListSlice(MsVM* vm, const MsObjList* list, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                          bool hasC, ms_i64 c, bool* errStepZero);
// 拼接 → 新 list（源由调用方 root；溢出/OOM 返回 NULL）
MsObjList* msObjListConcat(MsVM* vm, const MsObjList* a, const MsObjList* b);
// 重复 → 新 list（n<=0 空表；源由调用方 root；溢出/OOM 返回 NULL）
MsObjList* msObjListRepeat(MsVM* vm, const MsObjList* a, ms_i64 n);
// children 标记（gcSetMarkFn 注册转发；进程级表由 vm.c msVmInit 统一注册）
void msObjListMarkChildren(MsVM* vm, MsObj* obj);
// items 数组释放（gcSetFreeFn 注册转发；sweep/heapFree 释放对象前回调）
void msObjListFreeData(MsObj* obj);

// ---- list 迭代器（T014，22 §4 版本号；MS_OBJ_ITERATOR tag，kind 判别见 obj.h） ----

typedef struct MsObjListIter {
  MsObj head;
  ms_u8 kind;       // 恒 MS_ITER_LIST
  MsObjList* list;  // markChildren 标记
  int32_t pos;      // 游标
  ms_u32 version;   // 创建时结构版本快照
} MsObjListIter;

MsObjListIter* msObjListIterNew(MsVM* vm, MsObjList* list);
// 推进一格；耗尽返回 false；版本失配返回 false 且 *errModified 置 true（调用方报 IteratorError）
bool msListIterNext(MsObjListIter* it, MsValue* out, bool* errModified);

static inline bool msIsList(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_LIST;
}

#endif  // INCLUDE_MS_OBJ_OBJ_LIST_H_
```

`src/obj/obj_list.c` 在现有实现上修改（头部注记更新 + push bump + 新函数；get/mark/free/reserve/new 不变）：

```c
// MsObjList 数据面（T014，22 §4）：倍增数组 + 结构版本号 + 切片/拼接/排序 + 迭代器壳
// GC 纪律：push/insert 的 realloc 与 sort 的 temp malloc 均非 GC 分配点（不走 msObjAlloc，无 guard）；
// slice/concat/repeat 内 msObjListNew 为 GC 触发点——源 list 必须由调用方 root
//（VM 侧 peek 纪律 / 单测先落 VM 栈），元素值拷贝窗口内源可达
#include "obj/obj_list.h"

#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_string.h"

MsObjList* msObjListNew(MsVM* vm) {
  MsObjList* list = (MsObjList*)msObjAlloc(vm, MS_OBJ_LIST, sizeof(MsObjList));
  if (list == NULL) {
    return NULL;
  }
  list->items = NULL;
  list->count = 0;
  list->cap = 0;
  list->version = 0;
  return list;
}

bool msObjListPush(MsVM* vm, MsObjList* list, MsValue v) {
  (void)vm;  // realloc 非 GC 分配点（文件头纪律注记）
  if (list->count == list->cap) {
    int32_t cap = list->cap == 0 ? 8 : list->cap * 2;
    MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
    if (items == NULL) {
      return false;  // 原块不动：列表保持不变
    }
    list->items = items;
    list->cap = cap;
  }
  list->items[list->count++] = v;
  list->version++;  // 结构性修改（22 §4）
  return true;
}

bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n) {
  (void)vm;
  if (n <= list->cap) {
    return true;  // 容量已足（n 为 BUILD_LIST u8 计数，恒 >= 0）
  }
  int32_t cap = n < 8 ? 8 : n;
  MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
  if (items == NULL) {
    return false;
  }
  list->items = items;
  list->cap = cap;
  return true;
}

bool msObjListGet(const MsObjList* list, ms_i64 i, MsValue* out) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  *out = list->items[i];
  return true;
}

bool msObjListSet(MsObjList* list, ms_i64 i, MsValue v) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  list->items[i] = v;  // 原位替换非结构性：不 bump（22 §4 仅结构修改失效迭代器）
  return true;
}

bool msObjListInsert(MsObjList* list, ms_i64 i, MsValue v) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0) {
    i = 0;  // 越界钳位（Python insert 语义）
  }
  if (i > list->count) {
    i = list->count;
  }
  if (list->count == list->cap) {
    int32_t cap = list->cap == 0 ? 8 : list->cap * 2;
    MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
    if (items == NULL) {
      return false;
    }
    list->items = items;
    list->cap = cap;
  }
  memmove(&list->items[i + 1], &list->items[i], sizeof(MsValue) * (size_t)(list->count - (int32_t)i));
  list->items[i] = v;
  list->count++;
  list->version++;
  return true;
}

bool msObjListRemoveAt(MsObjList* list, ms_i64 i, MsValue* out) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  int32_t at = (int32_t)i;
  *out = list->items[at];
  memmove(&list->items[at], &list->items[at + 1],
          sizeof(MsValue) * (size_t)(list->count - at - 1));
  list->count--;
  list->version++;
  return true;
}

void msObjListClear(MsObjList* list) {
  list->count = 0;  // 容量保留（复用免再分配）
  list->version++;
}

void msObjListReverse(MsObjList* list) {
  int32_t i = 0;
  int32_t j = list->count - 1;
  while (i < j) {
    MsValue t = list->items[i];
    list->items[i] = list->items[j];
    list->items[j] = t;
    i++;
    j--;
  }
  list->version++;
}

int32_t msObjListIndexOf(const MsObjList* list, const MsValue* v) {
  for (int32_t i = 0; i < list->count; i++) {
    if (msValueEqual(&list->items[i], v)) {
      return i;
    }
  }
  return -1;
}

void msObjListMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjList* list = (MsObjList*)obj;
  for (int32_t i = 0; i < list->count; i++) {
    gcMarkValue(vm, list->items[i]);
  }
}

void msObjListFreeData(MsObj* obj) {
  free(((MsObjList*)obj)->items);  // items 可为 NULL（free(NULL) 无害）
}
```

（slice/concat/repeat/sort/iter 在 Task 2/3 追加，本任务先让文件编译通过——`msObjListSort` 等声明暂不加入头文件，头文件分任务增量扩充。）

**修正：** 本任务头文件只加 `version` 字段与 set/insert/removeAt/clear/reverse/indexOf 的声明与实现；slice/concat/repeat/sort/iter 声明在 Task 2/3 再入头。`msIsList` 内联放本任务（后续 VM 代码即用）。

- [ ] **Step 5: 构建并运行测试**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 3
ctest --test-dir build -C Debug -R obj_list --output-on-failure
```

Expected: PASS（5 tests）。

- [ ] **Step 6: 全量回归**

```bash
ctest --test-dir build -C Debug
```

Expected: 100% tests passed（109 tests）。

- [ ] **Step 7: Commit**

```bash
git add src/obj/obj_list.h src/obj/obj_list.c tests/unit/test_obj_list.c tests/CMakeLists.txt
git commit -m "✨ feat(obj/list): version field with core mutators (set/insert/remove/clear/reverse/indexOf)"
```

---

### Task 2: obj_list 切片/拼接/重复/排序

**Files:**
- Modify: `src/obj/obj_list.h`（追加 4 个 API + 排序结果枚举）
- Modify: `src/obj/obj_list.c`（实现）
- Modify: `tests/unit/test_obj_list.c`（追加测试）

- [ ] **Step 1: 写失败测试**（追加到 `listIndexOf` 之后、main 之前）

```c
// ---------- 数据面（Task 2） ----------

static MsObjList* listFromInts(MsVM* vm, const ms_i64* xs, int32_t n) {
  MsObjList* list = msObjListNew(vm);
  for (int32_t i = 0; i < n; i++) {
    if (!msObjListPush(vm, list, msValueInt(xs[i]))) {
      return NULL;
    }
  }
  msVmPush(vm, msValueObj(&list->head));  // root：slice/concat 内分配点 GC 不误收
  return list;
}

TEST(listSliceRanges) {  // §5.1 切片：[1,2,3,4][1::2] → [2,4]；[::-1] → [4,3,2,1]
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3, 4};
  MsObjList* list = listFromInts(&vm, xs, 4);
  bool zero = true;
  MsObjList* r = msObjListSlice(&vm, list, true, 1, false, 0, true, 2, &zero);
  EXPECT(ctx, r != NULL && !zero && r->count == 2);
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 2);
  EXPECT(ctx, msObjListGet(r, 1, &v) && v.as.i == 4);
  r = msObjListSlice(&vm, list, false, 0, false, 0, true, -1, &zero);  // [::-1]
  EXPECT(ctx, r != NULL && r->count == 4);
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 4);
  EXPECT(ctx, msObjListGet(r, 3, &v) && v.as.i == 1);
  r = msObjListSlice(&vm, list, true, 0, true, 0, false, 0, &zero);  // step 0
  EXPECT(ctx, r == NULL && zero);
  msVmFree(&vm);
}

TEST(listConcatRepeat) {  // §5.1 拼接/重复
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 a[] = {1};
  const ms_i64 b[] = {2};
  MsObjList* la = listFromInts(&vm, a, 1);
  MsObjList* lb = listFromInts(&vm, b, 1);
  MsObjList* r = msObjListConcat(&vm, la, lb);
  EXPECT(ctx, r != NULL && r->count == 2);
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 1);
  EXPECT(ctx, msObjListGet(r, 1, &v) && v.as.i == 2);
  const ms_i64 z[] = {0};
  MsObjList* lz = listFromInts(&vm, z, 1);
  r = msObjListRepeat(&vm, lz, 3);  // [0]*3
  EXPECT(ctx, r != NULL && r->count == 3);
  EXPECT(ctx, msObjListGet(r, 2, &v) && v.as.i == 0);
  r = msObjListRepeat(&vm, lz, -2);  // 负数 → 空表
  EXPECT(ctx, r != NULL && r->count == 0);
  msVmFree(&vm);
}

TEST(listSortStableAndTyped) {  // 缺省 < 序 + 稳定；不可比较失败原序保持
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {3, 1, 2};
  MsObjList* list = listFromInts(&vm, xs, 3);
  EXPECT_EQ(ctx, MS_LIST_SORT_OK, msObjListSort(list));
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(list, 0, &v) && v.as.i == 1);
  EXPECT(ctx, msObjListGet(list, 2, &v) && v.as.i == 3);
  MsObjString* s = msObjStringNew(&vm, "b", 1);
  msVmPush(&vm, msValueObj(&s->head));
  EXPECT(ctx, msObjListPush(&vm, list, msValueObj(&s->head)));  // [1,2,3,"b"]
  EXPECT_EQ(ctx, MS_LIST_SORT_TYPE, msObjListSort(list));  // int vs string 不可比较
  EXPECT(ctx, msObjListGet(list, 0, &v) && v.as.i == 1);   // 原序保持
  msVmFree(&vm);
}
```

main 中追加三行调用：

```c
  listSliceRanges(&ctx);
  listConcatRepeat(&ctx);
  listSortStableAndTyped(&ctx);
```

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 3
ctest --test-dir build -C Debug -R obj_list --output-on-failure
```

Expected: 编译失败（API 未定义）。

- [ ] **Step 3: 实现**

`src/obj/obj_list.c` 追加（含 `#include "obj/obj_string.h"` 已就位；排序用 msValueOrder 来自 vm.h 链）：

```c
// ---- 切片/拼接/重复（msObjListNew 为 GC 触发点：源由调用方 root） ----

static ms_i64 sliceCount(ms_i64 start, ms_i64 stop, ms_i64 step) {
  ms_i64 span = step > 0 ? stop - start : start - stop;
  ms_i64 inc = step > 0 ? step : -step;
  return (span + inc - 1) / inc;  // 规范化后 span >= 0：上取整计元素数
}

MsObjList* msObjListSlice(MsVM* vm, const MsObjList* list, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                          bool hasC, ms_i64 c, bool* errStepZero) {
  *errStepZero = false;
  ms_i64 start = 0;
  ms_i64 stop = 0;
  ms_i64 step = 1;
  if (!msSliceRange(list->count, hasA, a, hasB, b, hasC, c, &start, &stop, &step)) {
    *errStepZero = true;  // 与 string 同一规范切片算法（T013 抽出复用点）
    return NULL;
  }
  ms_i64 n = sliceCount(start, stop, step);
  if (n > INT32_MAX) {
    return NULL;
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：源 list 由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (n > 0 && !msObjListReserve(vm, r, (int32_t)n)) {
    return NULL;  // r 挂堆但不可达，交 GC 回收
  }
  for (ms_i64 k = 0; k < n; k++) {
    r->items[k] = list->items[start + k * step];
  }
  r->count = (int32_t)n;
  return r;
}

MsObjList* msObjListConcat(MsVM* vm, const MsObjList* a, const MsObjList* b) {
  if ((ms_i64)a->count + b->count > INT32_MAX) {
    return NULL;
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：a/b 由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (!msObjListReserve(vm, r, a->count + b->count)) {
    return NULL;
  }
  memcpy(r->items, a->items, sizeof(MsValue) * (size_t)a->count);
  memcpy(r->items + a->count, b->items, sizeof(MsValue) * (size_t)b->count);
  r->count = a->count + b->count;
  return r;
}

MsObjList* msObjListRepeat(MsVM* vm, const MsObjList* a, ms_i64 n) {
  if (n < 0) {
    n = 0;  // 负数重复为空（Python 语义，同 string repeat）
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：源由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (a->count == 0 || n == 0) {
    return r;
  }
  ms_i64 total = (ms_i64)a->count * n;
  if (total > INT32_MAX) {
    return NULL;
  }
  if (!msObjListReserve(vm, r, (int32_t)total)) {
    return NULL;
  }
  for (ms_i64 k = 0; k < total; k++) {
    r->items[k] = a->items[k % a->count];
  }
  r->count = (int32_t)total;
  return r;
}

// ---- 原地稳定归并排序（temp 缓冲 ping-pong；比较失败原序保持） ----

static MsListSortResult mergeHalves(MsValue* dst, const MsValue* src, ms_i64 lo, ms_i64 mid,
                                    ms_i64 hi) {
  ms_i64 i = lo;
  ms_i64 j = mid;
  ms_i64 k = lo;
  while (i < mid && j < hi) {
    int32_t cmp = 0;
    if (!msValueOrder(&src[i], &src[j], &cmp)) {
      return MS_LIST_SORT_TYPE;
    }
    dst[k++] = cmp <= 0 ? src[i++] : src[j++];  // <= 保稳定（相等取左半）
  }
  if (i < mid) {
    memcpy(&dst[k], &src[i], sizeof(MsValue) * (size_t)(mid - i));
  } else if (j < hi) {
    memcpy(&dst[k], &src[j], sizeof(MsValue) * (size_t)(hi - j));
  }
  return MS_LIST_SORT_OK;
}

MsListSortResult msObjListSort(MsObjList* list) {
  int32_t n = list->count;
  if (n < 2) {
    return MS_LIST_SORT_OK;  // 无结构变化：不 bump
  }
  MsValue* tmp = malloc(sizeof(MsValue) * (size_t)n);  // 非 GC 分配点
  if (tmp == NULL) {
    return MS_LIST_SORT_OOM;
  }
  MsValue* src = list->items;
  MsValue* dst = tmp;
  for (ms_i64 width = 1; width < n; width *= 2) {
    for (ms_i64 lo = 0; lo < n; lo += 2 * width) {
      ms_i64 mid = lo + width < n ? lo + width : n;
      ms_i64 hi = lo + 2 * width < n ? lo + 2 * width : n;
      if (mid == hi) {
        memcpy(&dst[lo], &src[lo], sizeof(MsValue) * (size_t)(hi - lo));  // 尾段直通
        continue;
      }
      MsListSortResult r = mergeHalves(dst, src, lo, mid, hi);
      if (r != MS_LIST_SORT_OK) {
        free(tmp);  // list->items 未被触碰：原序保持
        return r;
      }
    }
    MsValue* swap = src;
    src = dst;
    dst = swap;
  }
  if (src != list->items) {
    memcpy(list->items, src, sizeof(MsValue) * (size_t)n);
  }
  free(tmp);
  list->version++;
  return MS_LIST_SORT_OK;
}
```

头文件追加声明（Task 1 头文件模板已含 MsListSortResult 枚举与 slice/concat/repeat/sort/iter 声明——若 Task 1 按修正注记拆分了，此处补齐即可）。

- [ ] **Step 4: 构建、测试、回归、提交**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug -R obj_list --output-on-failure
ctest --test-dir build -C Debug
git add src/obj/obj_list.h src/obj/obj_list.c tests/unit/test_obj_list.c
git commit -m "✨ feat(obj/list): slice/concat/repeat and in-place stable merge sort"
```

Expected: obj_list 8 tests PASS；全量 109 PASS。

---

### Task 3: 迭代器壳——kind 判别 + MsObjListIter

**Files:**
- Modify: `src/obj/obj.h`（MsIterKind + MsObjIterShell）
- Modify: `src/obj/obj_string.h`（MsObjStrIter 增 kind 域）
- Modify: `src/obj/obj_string.c`（构造置 kind）
- Modify: `src/obj/obj_list.h/c`（MsObjListIter + next；头文件 Task 1 模板已含，此处落实现）
- Modify: `tests/unit/test_obj_list.c`（追加迭代器测试）

- [ ] **Step 1: 写失败测试**（追加）

```c
// ---------- 迭代器（Task 3） ----------

TEST(listIterWalkAndExhaust) {
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3};
  MsObjList* list = listFromInts(&vm, xs, 3);
  MsObjListIter* it = msObjListIterNew(&vm, list);
  msVmPush(&vm, msValueObj(&it->head));  // root
  MsValue v = msValueNil();
  bool modified = true;
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 1 && !modified);
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 2);
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 3);
  EXPECT(ctx, !msListIterNext(it, &v, &modified) && !modified);  // 耗尽
  msVmFree(&vm);
}

TEST(listIterVersionInvalidation) {  // §5.1 版本号：迭代中 append 后 next → errModified
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3};
  MsObjList* list = listFromInts(&vm, xs, 3);
  MsObjListIter* it = msObjListIterNew(&vm, list);
  msVmPush(&vm, msValueObj(&it->head));
  MsValue v = msValueNil();
  bool modified = false;
  EXPECT(ctx, msListIterNext(it, &v, &modified) && !modified);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(4)));  // 迭代中修改
  EXPECT(ctx, !msListIterNext(it, &v, &modified) && modified);
  msVmFree(&vm);
}
```

main 追加两行调用。

- [ ] **Step 2: 运行确认失败**（同上，编译失败：MS_ITER_LIST/msObjListIterNew 未定义）

- [ ] **Step 3: 实现**

`src/obj/obj.h` 在 MsObjBoundMethod 声明后追加：

```c
// 迭代器壳种类（T014，22 §4）：str/list 等共享 MS_OBJ_ITERATOR tag，首载荷 kind 判别（T017 泛化）
typedef enum MsIterKind {
  MS_ITER_STR = 1,
  MS_ITER_LIST,
} MsIterKind;

// 迭代器对象公共前缀：kind 紧随 head（MsObjStrIter / MsObjListIter 均以本布局起头）
typedef struct MsObjIterShell {
  MsObj head;
  ms_u8 kind;
} MsObjIterShell;
```

`src/obj/obj_string.h` MsObjStrIter 改为：

```c
typedef struct MsObjStrIter {
  MsObj head;
  ms_u8 kind;        // 恒 MS_ITER_STR（str/list 迭代器共享 tag 判别，obj.h MsObjIterShell）
  MsObjString* str;  // markChildren 标记（D8）
  ms_u32 pos;        // chars：字节游标；bytes：字节下标
  bool byteMode;
} MsObjStrIter;
```

`src/obj/obj_string.c` msObjStrIterNew 构造体内 `it->str = s;` 前加：

```c
  it->kind = MS_ITER_STR;
```

`src/obj/obj_list.c` 追加：

```c
// ---- list 迭代器（22 §4：游标 + 结构版本快照） ----

MsObjListIter* msObjListIterNew(MsVM* vm, MsObjList* list) {
  MsObjListIter* it = (MsObjListIter*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(MsObjListIter));
  if (it == NULL) {
    return NULL;
  }
  it->kind = MS_ITER_LIST;
  it->list = list;  // 分配期间源由调用方 root（GET_ITER peek）
  it->pos = 0;
  it->version = list->version;
  return it;
}

bool msListIterNext(MsObjListIter* it, MsValue* out, bool* errModified) {
  *errModified = false;
  if (it->version != it->list->version) {
    *errModified = true;  // 22 §4：修改即失效，继续 next 抛 IteratorError（调用方翻译）
    return false;
  }
  if (it->pos >= it->list->count) {
    return false;  // 耗尽
  }
  *out = it->list->items[it->pos++];
  return true;
}
```

- [ ] **Step 4: 构建、全量测试（str 迭代器回归关键——layout 变更触及 T013 路径）、提交**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add src/obj/obj.h src/obj/obj_string.h src/obj/obj_string.c src/obj/obj_list.c tests/unit/test_obj_list.c
git commit -m "✨ feat(obj): iterator shell kind dispatch with list cursor iterator"
```

Expected: 全量 111 PASS（含 str 迭代器既有用例不回归）。

---

### Task 4: VM 执行面——索引写/切片/拼接/重复 + 深相等

**Files:**
- Modify: `src/vm/vm.c`（OP_SET_INDEX list 路径、OP_GET_INDEX list 切片、OP_BINARY_ADD/MUL 显式展开）
- Modify: `src/vm/value.c`（msValueEqual list 深相等）
- Modify: `tests/unit/test_obj_list.c`（runSrc 桥 + VM 行为测试）

- [ ] **Step 1: 写失败测试**——先在 test_obj_list.c 顶部（数据面节之前）加入 runSrc 桥（拷自 test_vm_basic.c，同款）：

```c
// ---------- 执行面桥（Task 4+：全管线跑 .ms 源码） ----------

typedef struct RunResult {
  char* out;
  bool compileOk;
  bool hasError;
  char err[240];
  int32_t errLine;
} RunResult;

static char* slurpTmp(FILE* f) {
  fflush(f);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc((size_t)len + 1);
  size_t got = fread(buf, 1, (size_t)len, f);
  buf[got] = '\0';
  fclose(f);
  return buf;
}

static RunResult runSrc(const char* src) {
  RunResult r = {NULL, false, false, "", -1};
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  if (mod != NULL) {
    Binder b;
    binderInit(&b, &arena, "t.ms", &diags);
    binderBind(&b, mod);
    binderDestroy(&b);
    Chunk c;
    if (compilerCompile(mod, &c, "t.ms", &diags)) {
      r.compileOk = true;
      MsVM vm;
      msVmInit(&vm);
      FILE* f = tmpfile();
      vm.out = f;
      msVmRun(&vm, &c);
      r.hasError = vm.hasError;
      snprintf(r.err, sizeof(r.err), "%s", vm.errMsg);
      r.errLine = vm.errLine;
      r.out = slurpTmp(f);
      msVmFree(&vm);
      chunkFree(&c);
    }
  }
  astArenaDestroy(&arena);
  if (r.out == NULL) {
    r.out = calloc(1, 1);
  }
  return r;
}
```

（文件顶部 include 已含所需头。）追加测试：

```c
TEST(runListIndexWriteAndSlice) {  // §5.1：xs[0]=v、xs[a:b:c]、xs[-1]
  RunResult r = runSrc("var xs = [1, 2, 3, 4]\nxs[0] = 9\nprint(xs)\nprint(xs[1::2])\n"
                       "print(xs[::-1])\nprint(xs[1:3])\nprint(xs[-1])\nprint(len(xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[9, 2, 3, 4]\n[2, 4]\n[4, 3, 2, 1]\n[2, 3]\n4\n4\n", r.out);
  free(r.out);
}

TEST(runListIndexWriteErrors) {
  RunResult r = runSrc("var xs = [1]\nxs[5] = 0");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nxs[\"k\"] = 0");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs[::0])");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
}

TEST(runListConcatRepeatOps) {  // §5.1：[1]+[2] / [0]*3 / 3*[0] 不支持
  RunResult r = runSrc("print([1] + [2])\nprint([0] * 3)\nprint([1, 2] * 0)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[1, 2]\n[0, 0, 0]\n[]\n", r.out);
  free(r.out);
  r = runSrc("print([1] + 2)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("print(3 * [0])");  // int * list 不支持（同 string * int 单向）
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(runListDeepEquality) {  // 02 §6：list 逐元素；指针短路
  RunResult r = runSrc("var a = [1, [2, \"x\"]]\nvar b = [1, [2, \"x\"]]\n"
                       "print(a == b)\nprint(a == a)\nprint(a == [1])\nprint(a != b)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true\ntrue\nfalse\nfalse\n", r.out);
  free(r.out);
}
```

main 追加四行调用。

- [ ] **Step 2: 运行确认失败**（SET_INDEX/切片/拼接路径为 RuntimeError 占位或行为缺失）

- [ ] **Step 3: 实现**

3a. `src/vm/vm.c` OP_SET_INDEX 整体替换：

```c
      OPCODE(OP_SET_INDEX) {  // 弹序 val/idx/obj（03 §4 求值序 obj→idx→v 已在编译侧定型）
        MsValue val = pop(vm);
        MsValue idx = pop(vm);
        MsValue obj = pop(vm);
        if (msIsString(&obj)) {
          RUNTIME_ERROR("TypeError: 'string' does not support item assignment");
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {  // T014：负索引/越界
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: list indices must be int");
          }
          if (!msObjListSet((MsObjList*)obj.as.obj, idx.as.i, val)) {
            RUNTIME_ERROR("IndexError: list index out of range");
          }
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' does not support item assignment", msValueTypeName(&obj));
      }
```

3b. OP_GET_INDEX 的 list 分支（现有 `if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {` 内、`idx.kind != MS_VAL_I64` 检查前）插入切片路径：

```c
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {
          if (idx.kind == MS_VAL_SLICE) {  // T014：切片返回新 list（源 peek 保 root）
            bool zero = false;
            MsObjList* r = msObjListSlice(vm, (MsObjList*)obj.as.obj, (idx.as.slice.mask & 1) != 0,
                                          idx.as.slice.start, (idx.as.slice.mask & 2) != 0, idx.as.slice.end,
                                          (idx.as.slice.mask & 4) != 0, idx.as.slice.step, &zero);
            if (r == NULL) {
              if (zero) {
                RUNTIME_ERROR("ValueError: slice step must not be zero");
              }
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            vm->stackTop -= 2;
            msVmPush(vm, msValueObj(&r->head));
            NEXT();
          }
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: list indices must be int");
          }
          ...（原 int 路径不变）
```

3c. `BINARY_ARITH_CASE(OP_BINARY_ADD, BA_ADD)` 与 `(OP_BINARY_MUL, BA_MUL)` 两行删除，宏实例列表上方注释块之后新增两个显式 handler（位置紧邻原处）：

```c
      OPCODE(OP_BINARY_ADD) {  // list+list 拼接 peek（分配窗口 operands 栈 root）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_LIST &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_LIST) {
          MsObjList* r = msObjListConcat(vm, (MsObjList*)vm->stack[vm->stackTop - 2].as.obj,
                                         (MsObjList*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_ADD, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      OPCODE(OP_BINARY_MUL) {  // list*int 重复 peek（02 §5.1 单向，同 string*int）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_LIST &&
            vm->stack[vm->stackTop - 1].kind == MS_VAL_I64) {
          MsObjList* r = msObjListRepeat(vm, (MsObjList*)vm->stack[vm->stackTop - 2].as.obj,
                                         vm->stack[vm->stackTop - 1].as.i);
          if (r == NULL) {
            RUNTIME_ERROR("OverflowError: list repetition result too large");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_MUL, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }
```

3d. `src/vm/value.c` msValueEqual 的 MS_VAL_OBJ 分支中，string 比较后插入：

```c
      if (a->as.obj->type == MS_OBJ_LIST && b->as.obj->type == MS_OBJ_LIST) {  // 02 §6 逐元素深等价
        if (a->as.obj == b->as.obj) {
          return true;  // 同一对象短路（自引用安全；互引环检测归 T021 __eq__ 协议）
        }
        const MsObjList* la = (const MsObjList*)a->as.obj;
        const MsObjList* lb = (const MsObjList*)b->as.obj;
        if (la->count != lb->count) {
          return false;
        }
        for (int32_t i = 0; i < la->count; i++) {
          if (!msValueEqual(&la->items[i], &lb->items[i])) {
            return false;
          }
        }
        return true;
      }
```

同时行尾注释 `// 其余身份（list 值等价 T014）` 改为 `// 其余身份`。

- [ ] **Step 4: 构建、全量测试、提交**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add src/vm/vm.c src/vm/value.c tests/unit/test_obj_list.c
git commit -m "✨ feat(vm): list index write, slicing, concat/repeat operators, deep equality"
```

Expected: 全量 115 PASS。

---

### Task 5: list 方法族 + CALL_METHOD/GET_ATTR 接线 + collectStackToList 抽取

**Files:**
- Create: `src/vm/list_methods.c`
- Modify: `src/vm/vm.h`（msListMethodLookup 声明）
- Modify: `src/vm/value.c`（新增 msVmCollectStackToList）
- Modify: `src/vm/str_methods.c`（静态 collectToList 改调共用版）
- Modify: `CMakeLists.txt`（target_sources ms_vm 增 list_methods.c）
- Modify: `src/vm/vm.c`（CALL_METHOD/GET_ATTR 按 methodRefLookup 分派）
- Modify: `tests/unit/test_obj_list.c`

- [ ] **Step 1: 写失败测试**（追加）

```c
TEST(runListMethodFamily) {  // 10 §3 方法族组合
  RunResult r = runSrc("var xs = [3, 1, 2]\nxs.insert(1, 9)\nprint(xs)\nxs.remove(9)\nprint(xs)\n"
                       "print(xs.indexOf(2))\nprint(xs.contains(5))\nxs.sort()\nprint(xs)\n"
                       "xs.reverse()\nprint(xs)\nprint(xs.concat([7, 8]))\nprint(xs.repeat(2))\n"
                       "print(xs.first())\nprint(xs.last())\nvar ys = xs.copy()\nys.append(99)\n"
                       "print(xs)\nprint(ys)\nprint(xs.pop())\nprint(xs.pop(0))\nprint(xs)\n"
                       "xs.clear()\nprint(xs)\nprint(len(xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[3, 9, 1, 2]\n[3, 1, 2]\n2\nfalse\n[1, 2, 3]\n[3, 2, 1]\n"
                   "[3, 2, 1, 7, 8]\n[3, 2, 1, 3, 2, 1]\n3\n1\n[3, 2, 1]\n[3, 2, 1, 99]\n"
                   "1\n3\n[2]\n[]\n0\n", r.out);
  free(r.out);
}

TEST(runListMethodErrors) {  // remove 缺值 ValueError；pop 越界 IndexError；元数错
  RunResult r = runSrc("var xs = [1]\nxs.remove(5)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs.pop(5))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs.first())\nxs.clear()\nprint(xs.first())");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nxs.append()");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(runListSortTypeError) {  // 混型排序 TypeError 且原序保持
  RunResult r = runSrc("var xs = [1, \"a\"]\nxs.sort()\nprint(xs)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(runListHigherOrderNatives) {  // map/filter/reduce/each：本任务仅原生函数（print 桩）
  RunResult r = runSrc("var xs = [1, 2]\nvar ys = xs.map(str)\nprint(ys)\n"
                       "print(xs.filter(len))\nprint(xs.reduce(str, \"\"))\nxs.each(print)\n"
                       "print(xs)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[\"1\", \"2\"]\n[1, 2]\n\"12\"\n1\n2\n[1, 2]\n", r.out);
  free(r.out);
}
```

main 追加调用。

- [ ] **Step 2: 运行确认失败**（list 无方法 → TypeError: 'list' has no method）

- [ ] **Step 3: 实现**

3a. `src/vm/vm.h` 在 msStringMethodLookup 声明后加：

```c
// list_methods.c：list 实例方法表（T014，10 §3；机制同 D5）
bool msListMethodLookup(const char* name, int32_t len, MsMethodRef* out);  // 未命中 false
// value.c：栈顶 n 个已 root 值 → list（BUILD_LIST 同款；split/map 共用面；错误路径不恢复 stackTop）
MsValue msVmCollectStackToList(MsVM* vm, int32_t n);
```

3b. `src/vm/value.c` 末尾（msValueFromConst 之后）追加：

```c
MsValue msVmCollectStackToList(MsVM* vm, int32_t n) {
  MsObjList* list = msObjListNew(vm);  // 源值均已落栈 root：分配安全
  if (list == NULL) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
    vm->hasError = true;
    return msValueNil();
  }
  msVmPush(vm, msValueObj(&list->head));
  if (vm->hasError) {
    return msValueNil();  // StackOverflow 槽已置
  }
  if (!msObjListReserve(vm, list, n)) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
    vm->hasError = true;
    return msValueNil();
  }
  const MsValue* src = &vm->stack[vm->stackTop - 1 - n];
  for (int32_t i = 0; i < n; i++) {
    list->items[i] = src[i];
  }
  list->count = n;
  MsValue r = vm->stack[vm->stackTop - 1];
  vm->stackTop -= n + 1;  // 弹源值与 list；返回值在寄存器安全（错误路径整栈由调用场景废弃）
  return r;
}
```

3c. `src/vm/str_methods.c`：删除静态 collectToList（含注释块），三处调用点 `collectToList(vm, n)` 改为 `msVmCollectStackToList(vm, n)`（split/splitLines/chars-bytes 等实际调用处以编译错误定位逐一替换）。

3d. `CMakeLists.txt` 在 `target_sources(ms_vm PRIVATE src/vm/str_methods.c)` 后加：

```cmake
target_sources(ms_vm PRIVATE src/vm/list_methods.c)
```

3e. `src/vm/list_methods.c` 全量：

```c
// list 方法族（T014，10 §3）：args[0] 恒为接收者（D5 机制复用）
// 错误约定：置 vm->hasError + errMsg 后返回 nil（OP_CALL_METHOD 检槽统一 unwind）
// GC 纪律：接收者与实参由 CALL_METHOD peek 保栈 root；map/filter 结果逐个落栈后
// msVmCollectStackToList 收集（D10）；callNative 调用窗口实参在栈
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_string.h"
#include "vm/vm.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

static MsObjList* recvList(const MsValue* args) {
  return (MsObjList*)args[0].as.obj;
}

static bool argInt(MsVM* vm, int32_t i, const MsValue* args, const char* mname, ms_i64* out) {
  if (args[i].kind != MS_VAL_I64) {
    methodError(vm, "ArgumentError: %s expects an int argument", mname);
    return false;
  }
  *out = args[i].as.i;
  return true;
}

static bool argList(MsVM* vm, int32_t i, const MsValue* args, const char* mname, const MsObjList** out) {
  if (args[i].kind != MS_VAL_OBJ || args[i].as.obj->type != MS_OBJ_LIST) {
    methodError(vm, "ArgumentError: %s expects a list argument", mname);
    return false;
  }
  *out = (const MsObjList*)args[i].as.obj;
  return true;
}

// 函数实参形态（T014 仅原生函数；脚本函数值 T018、cmp 协议 T021）
static bool checkFnArg(MsVM* vm, const MsValue* f, const char* mname) {
  if (f->kind == MS_VAL_NATIVE) {
    return true;
  }
  if (f->kind == MS_VAL_FUNCTION) {
    methodError(vm, "RuntimeError: script function values execute in T018 (%s)", mname);
    return false;
  }
  methodError(vm, "TypeError: %s expects a function argument", mname);
  return false;
}

// 原生函数调用：argv 值拷贝逐个压栈（调用窗口 GC root）后调用并弹净实参
static bool callNative(MsVM* vm, const MsValue* f, const MsValue* argv, int32_t argc, MsValue* out) {
  for (int32_t i = 0; i < argc; i++) {
    if (!msVmPush(vm, argv[i])) {
      return false;  // StackOverflow 槽已置
    }
  }
  MsValue r = f->as.native(vm, argc, &vm->stack[vm->stackTop - argc]);
  vm->stackTop -= argc;
  if (vm->hasError) {
    return false;  // 错误槽已置（整栈由 unwind 废弃）
  }
  *out = r;
  return true;
}

// ---- 变更族 ----

static MsValue lstAppend(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: append expects 1 argument");
    return msValueNil();
  }
  if (!msObjListPush(vm, recvList(args), args[1])) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueNil();
}

static MsValue lstPop(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1 && argc != 2) {
    methodError(vm, "ArgumentError: pop expects 0-1 arguments");
    return msValueNil();
  }
  MsObjList* list = recvList(args);
  ms_i64 i = list->count - 1;
  if (argc == 2 && !argInt(vm, 1, args, "pop", &i)) {
    return msValueNil();
  }
  MsValue out = msValueNil();
  if (!msObjListRemoveAt(list, i, &out)) {
    methodError(vm, "IndexError: pop index out of range");
    return msValueNil();
  }
  return out;
}

static MsValue lstInsert(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 3) {
    methodError(vm, "ArgumentError: insert expects 2 arguments");
    return msValueNil();
  }
  ms_i64 i = 0;
  if (!argInt(vm, 1, args, "insert", &i)) {
    return msValueNil();
  }
  if (!msObjListInsert(recvList(args), i, args[2])) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueNil();
}

static MsValue lstRemove(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: remove expects 1 argument");
    return msValueNil();
  }
  MsObjList* list = recvList(args);
  int32_t at = msObjListIndexOf(list, &args[1]);
  if (at < 0) {
    methodError(vm, "ValueError: value not in list");
    return msValueNil();
  }
  MsValue out = msValueNil();
  (void)msObjListRemoveAt(list, at, &out);
  return msValueNil();
}

static MsValue lstClear(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: clear expects 0 arguments");
    return msValueNil();
  }
  msObjListClear(recvList(args));
  return msValueNil();
}

static MsValue lstReverse(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: reverse expects 0 arguments");
    return msValueNil();
  }
  msObjListReverse(recvList(args));
  return msValueNil();
}

static MsValue lstSort(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc > 2) {
    methodError(vm, "ArgumentError: sort expects 0-1 arguments");
    return msValueNil();
  }
  if (argc == 2) {
    methodError(vm, "ArgumentError: sort comparator lands in T021");  // cmp 协议归 T021
    return msValueNil();
  }
  switch (msObjListSort(recvList(args))) {
    case MS_LIST_SORT_OK:
      return msValueNil();
    case MS_LIST_SORT_TYPE:
      methodError(vm, "TypeError: '<' not supported between list elements");
      return msValueNil();
    default:
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
  }
}

// ---- 查询族 ----

static MsValue lstIndexOf(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: indexOf expects 1 argument");
    return msValueNil();
  }
  return msValueInt(msObjListIndexOf(recvList(args), &args[1]));
}

static MsValue lstContains(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: contains expects 1 argument");
    return msValueNil();
  }
  return msValueBool(msObjListIndexOf(recvList(args), &args[1]) >= 0);
}

static MsValue lstFirst(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: first expects 0 arguments");
    return msValueNil();
  }
  MsValue out = msValueNil();
  if (!msObjListGet(recvList(args), 0, &out)) {
    methodError(vm, "IndexError: first from empty list");
    return msValueNil();
  }
  return out;
}

static MsValue lstLast(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: last expects 0 arguments");
    return msValueNil();
  }
  MsValue out = msValueNil();
  if (!msObjListGet(recvList(args), -1, &out)) {
    methodError(vm, "IndexError: last from empty list");
    return msValueNil();
  }
  return out;
}

static MsValue lstLen(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: len expects 0 arguments");
    return msValueNil();
  }
  return msValueInt(recvList(args)->count);
}

// ---- 派生族（新 list） ----

static MsValue lstConcat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: concat expects 1 argument");
    return msValueNil();
  }
  const MsObjList* other = NULL;
  if (!argList(vm, 1, args, "concat", &other)) {
    return msValueNil();
  }
  MsObjList* r = msObjListConcat(vm, recvList(args), other);  // 接收者/实参均在 CALL_METHOD 栈
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static MsValue lstCopy(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: copy expects 0 arguments");
    return msValueNil();
  }
  MsObjList* src = recvList(args);
  MsObjList* r = msObjListNew(vm);  // 接收者在 CALL_METHOD 栈 root
  if (r == NULL || !msObjListReserve(vm, r, src->count)) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  memcpy(r->items, src->items, sizeof(MsValue) * (size_t)src->count);
  r->count = src->count;
  return msValueObj(&r->head);
}

static MsValue lstRepeat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: repeat expects 1 argument");
    return msValueNil();
  }
  ms_i64 n = 0;
  if (!argInt(vm, 1, args, "repeat", &n)) {
    return msValueNil();
  }
  MsObjList* r = msObjListRepeat(vm, recvList(args), n);
  if (r == NULL) {
    methodError(vm, "OverflowError: list repetition result too large");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

// ---- 高阶族（本任务仅原生函数实参） ----

static MsValue lstMap(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: map expects 1 argument");
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], "map")) {
    return msValueNil();
  }
  MsValue f = args[1];  // 值拷贝：压栈扩容后 args 指针失效（nativePrint 同纪律）
  MsObjList* list = recvList(args);
  for (int32_t i = 0; i < list->count; i++) {
    MsValue r = msValueNil();
    if (!callNative(vm, &f, &list->items[i], 1, &r)) {
      if (!vm->hasError) {
        methodError(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
    if (!msVmPush(vm, r)) {  // 结果 root（栈溢出槽已置）
      return msValueNil();
    }
  }
  return msVmCollectStackToList(vm, list->count);
}

static MsValue lstFilter(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: filter expects 1 argument");
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], "filter")) {
    return msValueNil();
  }
  MsValue f = args[1];
  MsObjList* list = recvList(args);
  int32_t kept = 0;
  for (int32_t i = 0; i < list->count; i++) {
    MsValue r = msValueNil();
    if (!callNative(vm, &f, &list->items[i], 1, &r)) {
      if (!vm->hasError) {
        methodError(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
    if (msValueIsTruthy(&r)) {
      if (!msVmPush(vm, list->items[i])) {
        return msValueNil();
      }
      kept++;
    }
  }
  return msVmCollectStackToList(vm, kept);
}

static MsValue lstReduce(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2 && argc != 3) {
    methodError(vm, "ArgumentError: reduce expects 1-2 arguments");
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], "reduce")) {
    return msValueNil();
  }
  MsValue f = args[1];
  MsObjList* list = recvList(args);
  int32_t i = 0;
  MsValue acc = msValueNil();
  if (argc == 3) {
    acc = args[2];
  } else {
    if (list->count == 0) {
      methodError(vm, "ArgumentError: reduce of empty list with no initial value");
      return msValueNil();
    }
    acc = list->items[0];
    i = 1;
  }
  for (; i < list->count; i++) {
    MsValue argv[2] = {acc, list->items[i]};
    if (!callNative(vm, &f, argv, 2, &acc)) {
      if (!vm->hasError) {
        methodError(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
  }
  return acc;
}

static MsValue lstEach(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: each expects 1 argument");
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], "each")) {
    return msValueNil();
  }
  MsValue f = args[1];
  MsObjList* list = recvList(args);
  for (int32_t i = 0; i < list->count; i++) {
    MsValue argv[2] = {list->items[i], msValueInt(i)};  // 10 §3：f(v, i)
    MsValue r = msValueNil();
    if (!callNative(vm, &f, argv, 2, &r)) {
      if (!vm->hasError) {
        methodError(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
  }
  return msValueNil();
}

// ---- 方法表（10 §3 表序） ----

typedef struct ListMethodEntry {
  const char* name;
  MsNativeFn fn;
} ListMethodEntry;

static const ListMethodEntry kListMethods[] = {
    {"append", lstAppend},   {"pop", lstPop},           {"insert", lstInsert},
    {"remove", lstRemove},   {"indexOf", lstIndexOf},   {"contains", lstContains},
    {"sort", lstSort},       {"reverse", lstReverse},   {"concat", lstConcat},
    {"repeat", lstRepeat},   {"map", lstMap},           {"filter", lstFilter},
    {"reduce", lstReduce},   {"each", lstEach},         {"clear", lstClear},
    {"copy", lstCopy},       {"first", lstFirst},       {"last", lstLast},
};

bool msListMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kListMethods) / sizeof(kListMethods[0]); i++) {
    const char* n = kListMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kListMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
```

**注：** `lstLen`（`xs.len()`，10 §3 无此条目——表内无 len 方法；builtin `len(xs)` 已有）。**写实现时删去 lstLen**（10 §3 表序为准；YAGNI）。若测试发现 `len` 方法形态被 10 §1 覆盖则走 builtin，不加方法。

3f. `src/vm/vm.c`：dispatch 表两处前加公共查找助手（放 msVmInit 之前）：

```c
// 方法查找公共面（T013 string / T014 list）：命中 0；未命中 -1
static int methodRefLookup(const MsValue* recv, const char* name, int32_t len, MsMethodRef* out) {
  if (msIsString(recv)) {
    return msStringMethodLookup(name, len, out) ? 0 : -1;
  }
  if (recv->kind == MS_VAL_OBJ && recv->as.obj->type == MS_OBJ_LIST) {
    return msListMethodLookup(name, len, out) ? 0 : -1;
  }
  return -1;
}
```

OP_CALL_METHOD 中 `if (msIsString(&args[0]) && msStringMethodLookup(...))` 改为 `if (methodRefLookup(&args[0], consts[k].as.text.start, consts[k].as.text.len, &ref) == 0)`；OP_GET_ATTR 中同理替换条件（物化体不变）。

- [ ] **Step 4: 构建、全量测试、提交**

```bash
cmake --preset msvc 2>&1 | Select-Object -Last 1
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add src/vm/list_methods.c src/vm/vm.h src/vm/value.c src/vm/str_methods.c src/vm/vm.c CMakeLists.txt tests/unit/test_obj_list.c
git commit -m "✨ feat(vm/list): full method family with CALL_METHOD/GET_ATTR wiring"
```

Expected: 全量 119 PASS（str 方法回归不破坏）。

**注意：** `runListHigherOrderNatives` 期望值需与实际核对：`print(ys)` 输出 `[\"1\", \"2\"]`（元素 repr 带引号）；`reduce(str, \"\")` → `\"12\"`（print 直写 → `12` 无引号——print 对 string 直写！修正期望：`print(xs.reduce(str, ""))` 输出 `12` 不是 `"12"`）。写测试时以实际语义核对 golden：
`"[\"1\", \"2\"]\n[1, 2]\n12\n1\n2\n[1, 2]\n"`。

---

### Task 6: 迭代器 VM 接线——GET_ITER/FOR_ITER + IteratorError

**Files:**
- Modify: `src/vm/vm.c`（strIterMark → iterMark 分派；GET_ITER list 路径；FOR_ITER kind 分派）
- Modify: `tests/unit/test_obj_list.c`

- [ ] **Step 1: 写失败测试**（追加）

```c
TEST(runListForIteration) {  // list 迭代 + for 解构铺垫（单目标）
  RunResult r = runSrc("var s = 0\nfor v in [1, 2, 3] { s += v }\nprint(s)\n"
                       "var t = \"\"\nfor c in [\"a\", \"b\"] { t = t + c }\nprint(t)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "6\nab\n", r.out);
  free(r.out);
}

TEST(runListIterConcurrentModify) {  // §5.1 迭代中 append 后 next → IteratorError
  RunResult r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.append(v)\n}\nprint(\"unreachable\")");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.pop()\n}");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.sort()\n}");  // sort 亦失效（任务书 §2）
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nvar n = 0\nfor v in xs {\n  xs[0] = 9\n  n += 1\n}\nprint(n)");  // SET_INDEX 不失效
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

TEST(runListIterExhaustNoError) {  // 耗尽后重新迭代新迭代器
  RunResult r = runSrc("var xs = [1]\nfor v in xs { }\nxs.append(2)\nvar n = 0\n"
                       "for v in xs { n += 1 }\nprint(n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2\n", r.out);
  free(r.out);
}
```

main 追加调用。

- [ ] **Step 2: 运行确认失败**（GET_ITER list → RuntimeError: list iteration executes in T014）

- [ ] **Step 3: 实现**

3a. vm.c 头部 strIterMark 替换 + 注册行更新：

```c
// 迭代器 children 标记（T013 D8 / T014）：str/list 共享 MS_OBJ_ITERATOR tag，按首载荷 kind 分派
static void iterMark(MsVM* vm, MsObj* obj) {
  if (((MsObjIterShell*)obj)->kind == MS_ITER_LIST) {
    gcMarkValue(vm, msValueObj(&((MsObjListIter*)obj)->list->head));
    return;
  }
  gcMarkValue(vm, msValueObj(&((MsObjStrIter*)obj)->str->head));
}
```

msVmInit 内 `gcSetMarkFn(MS_OBJ_ITERATOR, strIterMark);` → `gcSetMarkFn(MS_OBJ_ITERATOR, iterMark);`（注释同步）。

3b. OP_GET_ITER 的 list 占位分支替换：

```c
        if (v.kind == MS_VAL_OBJ && v.as.obj->type == MS_OBJ_LIST) {
          MsObjListIter* it = msObjListIterNew(vm, (MsObjList*)v.as.obj);  // peek：分配期间源在栈
          if (it == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 1] = msValueObj(&it->head);  // 覆盖接收者槽：迭代器持 list
          NEXT();
        }
```

3c. OP_FOR_ITER 头部（range 路径之前）整体替换：

```c
      OPCODE(OP_FOR_ITER) {  // <t exit>：有值压值；耗尽弹迭代器并跳出
        READ_OPERAND(t);
        MsValue* it = &vm->stack[vm->stackTop - 1];
        if (it->kind == MS_VAL_OBJ && it->as.obj->type == MS_OBJ_ITERATOR) {
          ms_u8 ik = ((const MsObjIterShell*)it->as.obj)->kind;  // str/list 首载荷判别（T014）
          MsValue item;
          bool modified = false;
          bool done;
          if (ik == MS_ITER_LIST) {  // 22 §4：版本号防并发修改
            done = !msListIterNext((MsObjListIter*)it->as.obj, &item, &modified);
          } else {
            done = !msStrIterNext(vm, (MsObjStrIter*)it->as.obj, &item);  // D8
          }
          if (done) {
            if (modified) {
              RUNTIME_ERROR("IteratorError: list modified during iteration");
            }
            (void)pop(vm);  // 耗尽弹迭代器（再迭代须重新 GET_ITER）
            ip = t;
            NEXT();
          }
          msVmPush(vm, item);  // 两迭代器 next 均无分配：it 槽位此后不再经缓存指针访问
          NEXT();
        }
        if (it->kind != MS_VAL_RANGE_ITER) {
          RUNTIME_ERROR("TypeError: FOR_ITER over non-iterator '%s'", msValueTypeName(it));
        }
        ...（range 路径原样保留）
```

- [ ] **Step 4: 构建、全量测试、提交**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add src/vm/vm.c tests/unit/test_obj_list.c
git commit -m "✨ feat(vm): list iteration with IteratorError on concurrent modification"
```

Expected: 全量 122 PASS（string for 迭代用例不回归——kind 判别关键）。

---

### Task 7: UNPACK 指令 + 编译器解构 + binder/dump 修复

**Files:**
- Modify: `src/compiler/opcodes.h`（UNPACK_STAR 注释 `<u8 slot>` → `<u8 before> <u8 after>`）
- Modify: `src/compiler/chunk.c`（布局表 UNPACK_STAR OPL_U8 → OPL_U8_U8）
- Modify: `src/vm/vm.c`（两指令 handler + SET_HANDLER）
- Modify: `src/compiler/compiler.c`（storeDeclIdent/storeUnpackTarget/compileTupleAssign/compileDestructure + compileStmt case）
- Modify: `src/binder/binder.c`（AST_DESTRUCTURE 非 IDENT 目标按表达式绑定）
- Modify: `src/parser/ast_dump.c`（DESTRUCTURE 非 IDENT 目标紧凑 dump）
- Modify: `tests/unit/test_obj_list.c`

- [ ] **Step 1: 写失败测试**（追加）

```c
// ---------- 解构（Task 7） ----------

TEST(runUnpackSwapAndFewer) {  // §5.1 交换 / 3 目标收 5 元素多余丢弃
  RunResult r = runSrc("var a = 1\nvar b = 2\na, b = b, a\nprint(a)\nprint(b)\n"
                       "var xs = [1, 2, 3, 4, 5]\nvar p = 0\nvar q = 0\nvar w = 0\n"
                       "p, q, w = xs\nprint(p)\nprint(q)\nprint(w)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "2\n1\n1\n2\n3\n", r.out);
  free(r.out);
}

TEST(runUnpackStarPositions) {  // §5.1 星号任意位：a, *m, z = [1,2,3,4]
  RunResult r = runSrc("var xs = [1, 2, 3, 4]\nx1, *mid, z9 = xs\nprint(x1)\nprint(mid)\nprint(z9)\n"
                       "*head2, tail = xs\nprint(head2)\nprint(tail)\n"
                       "a0, *rest0 = xs\nprint(rest0)\n*all = xs\nprint(all)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1\n[2, 3]\n4\n[1, 2, 3]\n4\n[2, 3, 4]\n[1, 2, 3, 4]\n", r.out);
  free(r.out);
}

TEST(runUnpackStarTupleRhs) {  // 右值多表达式：BUILD_LIST 物化后统一 UNPACK_STAR
  RunResult r = runSrc("x1, *mid, z9 = 1, 2, 3, 4\nprint(x1)\nprint(mid)\nprint(z9)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1\n[2, 3]\n4\n", r.out);
  free(r.out);
}

TEST(runUnpackErrors) {  // 不可解构 TypeError；不足 ValueError；多收编译期错
  RunResult r = runSrc("a, b = 5");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\na, b = xs");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("a, *m, z = 5");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\na, *m, z = xs");  // before+after=2 > 1
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2]\nvar a = 0\nvar b = 0\na, b, c = xs\n");  // c 未声明：binder 按引用 → 编译可过、运行 STORE_GLOBAL 报错？
  free(r.out);  // 该用例语义见 Step 3 注记：IDENT 引用目标落 STORE_GLOBAL；不可达路径不测
}

TEST(runUnpackIndexTargets) {  // 索引目标（04 §2）：元组与星号形态
  RunResult r = runSrc("var xs = [0, 0]\nvar v = 0\nxs[0], v = [7, 8]\nprint(xs)\nprint(v)\n"
                       "var ys = [0]\nxs[1], *tail = [9, 1, 2]\nprint(xs[1])\nprint(tail)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[7, 0]\n8\n9\n[1, 2]\n", r.out);
  free(r.out);
}
```

main 追加调用。

- [ ] **Step 2: 运行确认失败**（statement form is not supported yet → compileOk=false）

- [ ] **Step 3: 实现**

3a. `src/compiler/opcodes.h`：

```c
  OP_UNPACK_SEQ,       // <u8 n>
  OP_UNPACK_STAR,      // <u8 before> <u8 after>
```

`src/compiler/chunk.c` 布局表 `OPL_U8, // UNPACK_STAR` → `OPL_U8_U8,  // UNPACK_STAR`。

3b. `src/vm/vm.c` dispatch 注册：`SET_HANDLER(OP_UNPACK_SEQ);` 与 `SET_HANDLER(OP_UNPACK_STAR);`（OP_BUILD_LIST 行后）。OP_BUILD_LIST handler 之后追加：

```c
      OPCODE(OP_UNPACK_SEQ) {  // <u8 n>：TOS list → 弹源；不足 n 报 ValueError；按序压 items[0..n)（多余丢弃）
        int32_t n = code[ip++];
        MsValue v = vm->stack[vm->stackTop - 1];
        if (v.kind != MS_VAL_OBJ || v.as.obj->type != MS_OBJ_LIST) {  // tuple/string 解构归 T015/T017
          RUNTIME_ERROR("TypeError: cannot unpack '%s' value", msValueTypeName(&v));
        }
        MsObjList* list = (MsObjList*)v.as.obj;
        if (list->count < n) {
          RUNTIME_ERROR("ValueError: not enough values to unpack (expected %d, got %d)", n, list->count);
        }
        vm->stackTop--;  // 弹源：后续纯值拷贝无分配窗口
        for (int32_t i = 0; i < n; i++) {
          msVmPush(vm, list->items[i]);
        }
        NEXT();
      }

      OPCODE(OP_UNPACK_STAR) {  // <u8 before> <u8 after>：前段逐值 + 中段新 list + 后段逐值
        int32_t before = code[ip++];
        int32_t after = code[ip++];
        MsValue v = vm->stack[vm->stackTop - 1];
        if (v.kind != MS_VAL_OBJ || v.as.obj->type != MS_OBJ_LIST) {
          RUNTIME_ERROR("TypeError: cannot unpack '%s' value", msValueTypeName(&v));
        }
        MsObjList* list = (MsObjList*)v.as.obj;
        if (list->count < before + after) {
          RUNTIME_ERROR("ValueError: not enough values to unpack (expected at least %d, got %d)",
                        before + after, list->count);
        }
        // 源保留在栈直至 rest 物化（msObjListNew 为 GC 触发点：源/已压前段均 root）
        int32_t base = vm->stackTop - 1;  // 源槽
        for (int32_t i = 0; i < before; i++) {
          msVmPush(vm, list->items[i]);
          if (vm->hasError) {  // push 溢出：裸槽位运算前必须 unwind
            goto dispatchExit;
          }
        }
        MsObjList* rest = msObjListNew(vm);
        if (rest == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&rest->head));
        if (vm->hasError) {
          goto dispatchExit;
        }
        int32_t mid = list->count - before - after;
        if (mid > 0 && !msObjListReserve(vm, rest, mid)) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        for (int32_t k = 0; k < mid; k++) {
          rest->items[k] = list->items[before + k];
        }
        rest->count = mid;
        for (int32_t j = 0; j < after; j++) {
          msVmPush(vm, list->items[before + mid + j]);
          if (vm->hasError) {
            goto dispatchExit;
          }
        }
        // 移除源槽：[src, p…, rest, s…] → [p…, rest, s…]
        memmove(&vm->stack[base], &vm->stack[base + 1],
                sizeof(MsValue) * (size_t)(before + 1 + after));
        vm->stackTop--;
        NEXT();
      }
```

3c. `src/compiler/compiler.c`：compileAssign 之前插入三个助手 + compileAssign 元组分支 + compileDestructure + compileStmt case：

```c
// 声明式落盘（DESTRUCTURE 目标 = 新绑定）：global DEF+STORE；local STORE（同 finishDeclStore 语义，
// 载荷取 as.ident）
static void storeDeclIdent(FuncState* fs, const AstNode* t) {
  if (t->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_STORE_LOCAL, t->line);
    emitByte(fs, (ms_u8)t->bindSlot);
  } else {
    int32_t k = nameConst(fs, t->as.ident.name, t->as.ident.nameLen);
    emitConstOp(fs, OP_DEF_GLOBAL, k, t->line);
    emitConstOp(fs, OP_STORE_GLOBAL, k, t->line);
  }
}

// 解构落盘：值在栈顶（逆序消费）——IDENT 按声明/引用语义直存；INDEX 先 obj/idx 再 ROT 对齐 SET_INDEX
// 弹序（v obj idx → obj idx v）；MEMBER 目标与单目标赋值同样归 T020
static void storeUnpackTarget(FuncState* fs, AstNode* t, bool declare) {
  if (t->kind == AST_IDENT) {
    if (declare) {
      storeDeclIdent(fs, t);
    } else {
      storeIdent(fs, t);
    }
    return;
  }
  if (t->kind == AST_INDEX) {
    compileExpr(fs, t->as.indexExpr.obj);
    compileExpr(fs, t->as.indexExpr.index);
    emitOp(fs, OP_ROT, t->line);
    emitOp(fs, OP_SET_INDEX, t->line);
    return;
  }
  compErr(fs, t, 1, "assignment target form is not supported yet");
  emitOp(fs, OP_POP, t->line);
}

// 元组目标赋值（04 §2，无星号）：右值多表达式逐个压栈（临时栈序列，T015 统一走 BUILD_TUPLE）；
// 单表达式 UNPACK_SEQ 展开；目标数可少于右值元素（多余丢弃）
static void compileTupleAssign(FuncState* fs, AstNode* n, AstNode* target) {
  int32_t count = target->as.tuple.count;
  const AstNode* value = n->as.assign.value;
  if (count > 255) {
    compErr(fs, n, 1, "too many unpack targets");
    return;
  }
  if (value->kind == AST_TUPLE) {
    int32_t m = value->as.tuple.count;
    for (int32_t i = 0; i < m; i++) {
      compileExpr(fs, value->as.tuple.items[i]);
    }
    if (m != count) {  // 字面计数失配：编译期诊断（发射面保持平衡）
      compErr(fs, n, 1, "unpack target and value counts differ");
      for (int32_t i = 0; i < m; i++) {
        emitOp(fs, OP_POP, n->line);
      }
      return;
    }
  } else {
    compileExpr(fs, value);
    emitOp(fs, OP_UNPACK_SEQ, n->line);
    emitByte(fs, (ms_u8)count);
  }
  for (int32_t i = count - 1; i >= 0; i--) {
    storeUnpackTarget(fs, target->as.tuple.items[i], false);
  }
}

// 含 *rest 的解构赋值（04 §2）：右值物化为 list（多表达式经 BUILD_LIST）后 UNPACK_STAR 展平
static void compileDestructure(FuncState* fs, AstNode* n) {
  int32_t count = n->as.destructure.count;
  int32_t rest = n->as.destructure.restIndex;
  const AstNode* value = n->as.destructure.value;
  if (count > 255) {
    compErr(fs, n, 1, "too many unpack targets");
    return;
  }
  if (value->kind == AST_TUPLE) {
    for (int32_t i = 0; i < value->as.tuple.count; i++) {
      compileExpr(fs, value->as.tuple.items[i]);
    }
    emitOp(fs, OP_BUILD_LIST, n->line);
    emitByte(fs, (ms_u8)value->as.tuple.count);
  } else {
    compileExpr(fs, value);
  }
  emitOp(fs, OP_UNPACK_STAR, n->line);
  emitByte(fs, (ms_u8)rest);
  emitByte(fs, (ms_u8)(count - rest - 1));
  for (int32_t i = count - 1; i >= 0; i--) {  // 目标为 binder 登记的新绑定：声明式落盘
    storeUnpackTarget(fs, n->as.destructure.targets[i], true);
  }
}
```

compileAssign 的 INDEX 分支之后插入：

```c
  if (target->kind == AST_TUPLE && n->as.assign.op == AST_OP_NONE) {
    compileTupleAssign(fs, n, target);
    return;
  }
```

compileStmt switch 的 `case AST_ASSIGN:` 后加：

```c
    case AST_DESTRUCTURE:
      compileDestructure(fs, n);
      break;
```

3d. `src/binder/binder.c` AST_DESTRUCTURE case 替换（修 D7 联合体硬解）：

```c
    case AST_DESTRUCTURE: {  // *rest 形态：IDENT 目标登记新绑定；INDEX/MEMBER 目标按引用绑定（04 §2）
      bindExpr(b, n->as.destructure.value);
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        AstNode* t = n->as.destructure.targets[i];
        if (t == NULL) {
          continue;
        }
        if (t->kind != AST_IDENT) {
          bindExpr(b, t);
          continue;
        }
        Sym* sym = declareStrict(b, t->as.ident.name, t->as.ident.nameLen, false, t);
        if (sym != NULL) {
          annotateDeclNode(t, b->top, sym, false);
        }
      }
      break;
    }
```

3e. `src/parser/ast_dump.c` DESTRUCTURE 目标渲染（`if (t == NULL)` 分支后）改为：

```c
        if (t == NULL) {
          fputc('_', out);
        } else if (t->kind == AST_IDENT) {
          dumpName(out, t->as.ident.name, t->as.ident.nameLen);
          dumpAnn(out, t);
        } else {
          dumpNode(out, t, -1);  // 索引等复合目标：紧凑形态（修联合体硬解）
        }
```

- [ ] **Step 4: 构建、全量测试、提交**

```bash
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add src/compiler/opcodes.h src/compiler/chunk.c src/compiler/compiler.c src/binder/binder.c src/parser/ast_dump.c src/vm/vm.c tests/unit/test_obj_list.c
git commit -m "✨ feat(compiler): UNPACK_SEQ/UNPACK_STAR destructuring assignment"
```

Expected: 全量 127 PASS。

**注记（Step 1 最后用例）：** `a, b, c = xs` 中 `c` 未声明：binder AST_ASSIGN 目标按引用 → GLOBAL → 编译 STORE_GLOBAL → 运行时 "assignment to undefined name"（RuntimeError）。这与单目标赋值语义一致（有意保留），测试断言 `r.hasError && strstr(err, "undefined name")`。写测试时按此断言。

---

### Task 8: fixtures + GC 保活单测

**Files:**
- Create: `tests/fixtures/list/basic.ms` + `basic.out`
- Create: `tests/fixtures/list/methods.ms` + `methods.out`
- Create: `tests/fixtures/list/destructure.ms` + `destructure.out`
- Create: `tests/fixtures/list/iter_err.ms` + `iter_err.out` + `iter_err.err` + `iter_err.code`
- Modify: `tests/unit/test_gc_basic.c`

- [ ] **Step 1: 落盘 fixtures**（T002 契约：新增 .ms+.out 自动注册，reconfigure 触发）

`tests/fixtures/list/basic.ms`（任务书 §5.2 原文）：

```ms
var xs = [1, 2, 3]
xs.append(4)
print(xs)
print(xs[1:3])
print(xs[-1])
xs.reverse()
print(xs)
```

`basic.out`：

```
[1, 2, 3, 4]
[2, 3]
4
[4, 3, 2, 1]
```

`tests/fixtures/list/methods.ms`：

```ms
var xs = [3, 1, 2]
xs.insert(1, 9)
print(xs)
xs.remove(9)
print(xs)
print(xs.indexOf(2))
print(xs.contains(5))
xs.sort()
print(xs)
xs.reverse()
print(xs)
print(xs.concat([7, 8]))
print(xs.repeat(2))
print(xs.first())
print(xs.last())
var ys = xs.copy()
ys.append(99)
print(xs)
print(ys)
print([1] + [2])
print([0] * 3)
```

`methods.out`：

```
[3, 9, 1, 2]
[3, 1, 2]
2
false
[1, 2, 3]
[3, 2, 1]
[3, 2, 1, 7, 8]
[3, 2, 1, 3, 2, 1]
3
1
[3, 2, 1]
[3, 2, 1, 99]
[1, 2]
[0, 0, 0]
```

`tests/fixtures/list/destructure.ms`：

```ms
var a = 1
var b = 2
a, b = b, a
print(a)
print(b)
var xs = [1, 2, 3, 4, 5]
var p = 0
var q = 0
p, q = xs
print(p)
print(q)
x1, *mid, z9 = xs
print(x1)
print(mid)
print(z9)
*head2, tail = xs
print(head2)
print(tail)
for v in xs {
  print(v)
}
```

`destructure.out`：

```
2
1
1
2
1
[2, 3, 4]
5
[1, 2, 3, 4]
5
1
2
3
4
5
```

`tests/fixtures/list/iter_err.ms`：

```ms
var xs = [1, 2, 3]
for v in xs {
  xs.append(v)
}
```

`iter_err.out`：（空文件）
`iter_err.err`：

```
IteratorError: list modified during iteration
```

`iter_err.code`：

```
1
```

- [ ] **Step 2: GC 保活单测**（test_gc_basic.c 追加，文件头 include obj_string.h 不需；注册沿用生产 mark——list/iter 均为生产行为）：

```c
TEST(listChildrenKeepalive) {  // T014：list 子引用标记（markChildren 递归链）
  MsVM vm;
  msVmInit(&vm);
  MsObjList* inner = msObjListNew(&vm);
  msVmPush(&vm, msValueInt(42));
  EXPECT(ctx, msObjListPush(&vm, inner, vm.stack[vm.stackTop - 1]));  // inner 持 int（无堆子）
  vm.stackTop--;
  MsObjList* outer = msObjListNew(&vm);
  msVmPush(&vm, msValueObj(&outer->head));  // 仅 outer 为根：inner 只能经 outer 存活
  EXPECT(ctx, msObjListPush(&vm, outer, msValueObj(&inner->head)));
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 1, outer->count);
  EXPECT(ctx, ((MsObjList*)outer->items[0].as.obj) == inner);  // inner 存活且未被误收
  msVmFree(&vm);
}

TEST(listIterKeepalive) {  // T014：迭代器持 list——iter 栈 root → list 经 iterMark 存活
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(7)));
  MsObjListIter* it = msObjListIterNew(&vm, list);
  msVmPush(&vm, msValueObj(&it->head));  // 仅迭代器为根
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));  // iter + list
  EXPECT_EQ(ctx, 1, it->list->count);
  MsValue v = msValueNil();
  bool modified = false;
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 7 && !modified);
  msVmFree(&vm);
}
```

main 追加调用。

- [ ] **Step 3: 构建、全量测试（新增 4 fixture + 2 unit）、提交**

```bash
cmake --preset msvc 2>&1 | Select-Object -Last 1
cmake --build build --config Debug 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Debug
git add tests/fixtures/list tests/unit/test_gc_basic.c
git commit -m "✅ test(fixtures): list fixtures and GC keepalive coverage"
```

Expected: 全量 133 PASS（127 + 4 fixtures + 2 gc）。

---

### Task 9: 任务状态收尾 + 全量验证

**Files:**
- Modify: `docs/tasks/T014-list-destructuring.md`（状态 → ✅、DoD 勾选）
- Modify: `docs/tasks/README.md`（T014 行 → ✅）

- [ ] **Step 1: DoD 核对（逐条运行验证）**

- `02 §5.1 与 10 §3 全部行为用例`：Task 4/5 测试 + fixtures basic/methods 全绿
- `切片/索引/拼接/重复 fixtures 全绿`：ctest fixture_list_* 4 项
- `迭代中修改抛 IteratorError`：iter_err fixture + runListIterConcurrentModify
- `解构：少收/星号任意位/交换正确；不可解构 TypeError`：runUnpack* 5 用例
- `GC 扩展：list 子引用标记单测`：listChildrenKeepalive/listIterKeepalive（ASan 压力由 CI asan preset 门禁）

- [ ] **Step 2: Release 构建验证**（MSVC /W4 /WX 双配置面）：

```bash
cmake --build build --config Release 2>&1 | Select-Object -Last 2
ctest --test-dir build -C Release
```

- [ ] **Step 3: 状态更新 + 提交**

T014 文档头部 → `- 状态: 已完成`；DoD 五项勾选 `[x]`；README 总表 T014 → ✅。

```bash
git add docs/tasks/T014-list-destructuring.md docs/tasks/README.md
git commit -m "📝 docs(tasks): mark T014 done"
```

- [ ] **Step 4: 分支收尾**

走 finishing-a-development-branch 流程（合并 main / 清理 worktree 由用户定夺后执行）。

---

## Self-Review 记录

1. **Spec 覆盖**：02 §5.1（索引/切片/+/）→ Task 1/2/4；10 §3 方法族 18 方法 → Task 5；22 §4 布局/版本号迭代器 → Task 1/3/6；04 §2 解构四特性 → Task 7；GC markChildren → Task 1（已有）+ Task 8 保活；fixtures §5.2 → Task 8；repr 注记（任务书 §6）→ T013 listForm 已覆盖（`print(xs)` 用例散布各测试）。T018 复跑注记：map/filter/reduce/each 脚本函数用例（任务书 §6）——已在 list_methods 错误信息与计划 D5 注明。
2. **占位符扫描**：Task 1 Step 1 含两行已知占位并即时给出修正体（防拷贝错误）；Task 5 lstLen 明确标注删除；无其他 TBD/TODO。
3. **类型一致性**：msObjListSlice/Concat/Repeat/Sort 签名在 Task 1 头文件模板、Task 2 实现、Task 4/5 调用一致；msListIterNext(it, out, errModified) 三处一致；storeUnpackTarget(fs, t, declare) 两调用点一致；UNPACK_STAR 双 u8 操作数在 opcodes/chunk/vm/compiler 四处一致。
