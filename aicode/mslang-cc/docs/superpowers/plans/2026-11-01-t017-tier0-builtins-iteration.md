# T017 Tier0 内置函数与迭代协议 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `10-builtins.md` 的 Tier0 内置函数全集、泛化迭代器对象与 `iter()/next()` 协议、range/适配器惰性迭代器、iterator 方法族、三式推导式 + 生成器表达式的编译执行，满足 T017 DoD（roadmap 期 4 收口）。

**Architecture:** 新增 `src/obj/obj_iter.c/.h` 统一迭代器对象 `MsObjIter`（kind 判别：range/map/set/tuple/reverse/enumerate/zip/mapfn/filter/take/drop；str/list 沿用既有壳结构）；VM 的 GET_ITER/FOR_ITER 收敛为 `msIterFromValue`/`msIterNext` 单路径；`builtins_bridge.c` 合并为 `src/vm/builtins.c` 表驱动注册（占位项显式报错）；推导式在编译器内联脱糖为嵌套 GET_ITER/FOR_ITER 循环 + 累加器临时槽（COMPR 伪帧槽位重映射表消解 binder 标注），genexp 物化 list 后 GET_ITER（T019 替换）。

**Tech Stack:** C11（MSVC 19.38 / cl，`/W4 /WX`）、CMake preset `msvc`、ctest。

**Worktree:** `C:\Workspace\Repositories\study\aicode\mslang-cc\.worktrees\t017-tier0\aicode\mslang-cc`（分支 `feature/t017-tier0`）。所有路径相对此根。基线：132 tests 全绿（18 unit + 35 fixture + 79 conformance）。

**规范（必读）:** `docs/language/28-style-c.md`（命名/格式/`//` 注释/禁 Tab）、`docs/language/11-style-ms.md`（fixtures）。所有新文件 UTF-8 无 BOM、LF、无行尾空白。构建唯一入口 `cmake --build build --config Debug` + `ctest --test-dir build -C Debug`。

**与任务文档的偏差（实现前确认，均有依据）:**

1. `sorted(xs, reverse=true)` / `zip(xs, ys, strict=true)` 按位置实参实现（`sorted(xs, key, reverse)`、`zip(xs, ys, strict)`）：`03-expressions.md` §4 调用语法无命名实参，`10-builtins.md` §1 签名本身即位置可选参。`key` 缺省传 `nil`。
2. set 推导式 `print` 输出为插入序（与 T015 set 打印语义一致）；任务文档 §5.2 注释"toList 排序输出"改为用 `sorted(...)` 演示排序。
3. 生成器表达式编译为"物化 list + GET_ITER"（任务文档 §2 明示的语义等价实现；T019 生成器到位后替换为真实惰性管道）。
4. 内置注册采用静态表（`kBuiltins[]` name→fn 数组），非 `msRegisterBuiltin(vm, ...)` 动态 API：当前无运行时注册需求，静态表即注册表且零分配；`msBuiltinLookup` 增加 `vm` 参数以支持内置变量 `__name__`/`__version__`。
5. 迭代协议实现细节：FOR_ITER 耗尽走内部信号（弹迭代器 + 跳转），不压 nil 哨兵值——容器可含 nil 元素，值哨兵会误停（如 `for x in [nil, 1]`）；`next(it)` 耗尽返回 nil 已满足 `05 §4` 可观察语义。
6. range 由过渡值 `MS_VAL_RANGE_ITER` 装箱为 `MsObjIter`（`vm.h` 注释既定归宿 "RANGE_ITER→T017"），FOR_ITER 内联 range 分支删除。
7. `sorted`/`min`/`max`/`sum`/`all`/`set(iterable)` 接受任意可迭代实参（list/tuple/map/set/string/range/iterator），不只 list——统一走 `msIterFromValue`。
8. 函数原型内推导式临时槽基线：编译期对函数体 AST 预扫描 `max(BIND_LOCAL bindSlot)`，`nextTemp = max(paramCount, scan+1)`——零 binder/AST 结构改动（预扫描含推导式目标/嵌套函数的槽位属安全超估）。
9. `it.each(f)` 回调形态取 `f(v)`（单参；`10 §4` 迭代器表未规定索引参，list 的 `f(v, i)` 是 `10 §3` list 专属）。

**任务索引:** Task 1 泛化迭代器对象与 VM 协议重写 → Task 2 Tier0 内置全集（builtins.c 合并桥）→ Task 3 iterator 方法族（惰性链接）→ Task 4 推导式/生成器表达式编译 → Task 5 fixtures 全量 + DoD 复核 + 文档状态。

---

### Task 1: 泛化迭代器对象 MsObjIter 与 GET_ITER/FOR_ITER 重写

**Files:**
- Create: `src/obj/obj_iter.h`
- Create: `src/obj/obj_iter.c`
- Modify: `src/obj/obj.h`（MsIterKind 增补枚举项）
- Modify: `src/vm/vm.h`（删 MS_VAL_RANGE_ITER；声明变化）
- Modify: `src/vm/vm.c`（GET_ITER/FOR_ITER 重写、iterMark 泛化、删 range 分支）
- Modify: `src/vm/value.c`（删 RANGE_ITER case；TypeName/ToStr 增 MS_OBJ_ITERATOR）
- Modify: `src/vm/builtins_bridge.c`（nativeRange 装箱；过渡到 Task 2 合并）
- Modify: `CMakeLists.txt`（ms_obj 增 obj_iter.c）
- Create: `tests/unit/test_builtins.c`（本任务先建迭代器用例骨架 + 注册）
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: 写 `src/obj/obj_iter.h`（完整内容）**

```c
// MsObjIter（T017，22 §4 / 05 §4 迭代协议）：泛化迭代器对象
// str/list 沿用各自壳结构（MsObjStrIter/MsObjListIter），其余种类统一 MsObjIter；
// kind 判别见 obj.h MsIterKind；推进统一入口 msIterNext（耗尽 false，错误置 vm 错误槽）
#ifndef INCLUDE_MS_OBJ_OBJ_ITER_H_
#define INCLUDE_MS_OBJ_OBJ_ITER_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef struct MsObjIter {
  MsObj head;
  ms_u8 kind;      // MS_ITER_RANGE 起（str/list 壳不走本结构）
  MsValue src;     // 源容器（MAP/SET/TUPLE/REVERSE）或源迭代器（ENUMERATE/ZIP/MAPFN/FILTER/TAKE/DROP）
  MsValue src2;    // ZIP 第二源迭代器；其余 nil
  MsValue fn;      // MAPFN/FILTER 原生函数；其余 nil
  ms_i64 cur;      // RANGE 当前值 / ENUMERATE 计数 / TAKE 剩余
  ms_i64 stop;     // RANGE 上界（饱和防 INT64 回绕）
  ms_i64 step;     // RANGE 步长
  ms_i64 i;        // MAP/SET 槽游标（-1 = 未起）/ TUPLE/REVERSE 索引 / DROP 剩余
  ms_u32 version;  // REVERSE over list：创建时结构版本快照
  ms_u8 flags;     // bit0 = ZIP strict；bit1 = DROP 已跳过
} MsObjIter;

// 构造族（失败返回 NULL）
MsObjIter* msObjIterRangeNew(MsVM* vm, ms_i64 start, ms_i64 stop, ms_i64 step);  // step != 0 由调用方保证
MsObjIter* msObjIterMapNew(MsVM* vm, struct MsObjMap* m);       // 键迭代（插入序）
MsObjIter* msObjIterSetNew(MsVM* vm, struct MsObjSet* st);
MsObjIter* msObjIterTupleNew(MsVM* vm, struct MsObjTuple* t);
MsObjIter* msObjIterReverseNew(MsVM* vm, MsValue seq);          // list/string/tuple 反向
MsObjIter* msObjIterEnumerateNew(MsVM* vm, MsValue srcIter, ms_i64 start);
MsObjIter* msObjIterZipNew(MsVM* vm, MsValue a, MsValue b, bool strict);
MsObjIter* msObjIterMapFnNew(MsVM* vm, MsValue srcIter, MsValue fn);
MsObjIter* msObjIterFilterNew(MsVM* vm, MsValue srcIter, MsValue fn);
MsObjIter* msObjIterTakeNew(MsVM* vm, MsValue srcIter, ms_i64 n);
MsObjIter* msObjIterDropNew(MsVM* vm, MsValue srcIter, ms_i64 n);

// 统一推进（it 必为 MS_OBJ_ITERATOR 壳，含 str/list）：true = *out 产出下一值；
// false = 耗尽（无错误）或错误（vm->hasError 已置：IteratorError/ValueError(zip strict)/
// TypeError(fn 非函数或元素不可比) 等）。调用方须保证 it 自身在其调用窗口内为 GC root
bool msIterNext(MsVM* vm, MsValue it, MsValue* out);

// 值 → 迭代器：迭代器原样直通；string/list/map/set/tuple → 新壳；其余 TypeError（errMsg 已置）
// false = 不可迭代 / OOM（errMsg 已置）
bool msIterFromValue(MsVM* vm, MsValue v, MsValue* out);

// 排空 it → 新 list（元素逐个压 VM 栈 root 后 BUILD；sum/sorted/min/max/toList 共用面）
// false = 错误槽已置；out = list
bool msIterCollectList(MsVM* vm, MsValue it, MsValue* out);

// children 标记（gcSetMarkFn 由 vm.c 分派注册：str/list 壳走各自，其余走本函数）
void msObjIterMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsIterator(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_ITERATOR;
}

#endif  // INCLUDE_MS_OBJ_OBJ_ITER_H_
```

- [ ] **Step 1.2: 扩 `src/obj/obj.h` 的 MsIterKind（编辑枚举，MsObjIterShell 注释更新）**

```c
// 迭代器壳种类（T014 起，T017 泛化）：共享 MS_OBJ_ITERATOR tag，首载荷 kind 判别；
// STR/LIST 为各自壳结构（obj_string.h/obj_list.h），其余为统一 MsObjIter（obj_iter.h）
typedef enum MsIterKind {
  MS_ITER_STR = 1,
  MS_ITER_LIST,
  MS_ITER_RANGE,     // 惰性 int 序列（cur/stop/step）
  MS_ITER_MAP,       // map 键游标（插入序快照语义：10 §4 无版本号）
  MS_ITER_SET,       // set 内层 map 键游标
  MS_ITER_TUPLE,     // tuple 下标游标
  MS_ITER_REVERSE,   // list/string/tuple 反向游标
  MS_ITER_ENUMERATE, // (i, v) 适配器
  MS_ITER_ZIP,       // 双源配对适配器（strict 见 flags）
  MS_ITER_MAPFN,     // it.map(f) 惰性映射
  MS_ITER_FILTER,    // it.filter(f) 惰性过滤
  MS_ITER_TAKE,      // it.take(n)
  MS_ITER_DROP,      // it.drop(n)
} MsIterKind;
```

- [ ] **Step 1.3: 写 `src/obj/obj_iter.c`（完整实现，要点如下）**

包含：`vm/vm.h`、`gc/gc.h`、`obj_list.h`、`obj_map.h`、`obj_set.h`、`obj_string.h`、`obj_tuple.h`、`obj_iter.h`。构造族统一 `msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(MsObjIter))` 后填字段（src/src2/fn 语义字段外一律 nil/0；MAP/SET 游标 `i = -1`；TAKE `cur = n`；DROP `i = n`、flags bit1=0）。`msObjIterReverseNew`：src 须 list/string/tuple（否则调用方先验），`i = 元素数-1`（list/tuple 下标；string 为 rune 数 -1，取 `msObjStringCharLen`），list 另存 `version = list->version`。

`msObjIterMarkChildren`：`gcMarkValue(vm, it->src); gcMarkValue(vm, it->src2); gcMarkValue(vm, it->fn);`。

`msIterFromValue`：
```c
bool msIterFromValue(MsVM* vm, MsValue v, MsValue* out) {
  if (msIsIterator(&v)) { *out = v; return true; }  // 迭代器直通（协议：iter(it) == it）
  MsObj* o = msIsObj(&v) ? v.as.obj : NULL;
  if (msIsString(&v)) {
    MsObjStrIter* it = msObjStrIterNew(vm, (MsObjString*)o, false);  // chars 语义（rune 游标）
    if (it == NULL) { snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory"); return false; }
    *out = msValueObj(&it->head); return true;
  }
  if (o != NULL && o->type == MS_OBJ_LIST) { /* 同型：msObjListIterNew */ }
  if (o != NULL && o->type == MS_OBJ_MAP) { /* msObjIterMapNew */ }
  if (o != NULL && o->type == MS_OBJ_SET) { /* msObjIterSetNew */ }
  if (o != NULL && o->type == MS_OBJ_TUPLE) { /* msObjIterTupleNew */ }
  snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: '%s' is not iterable", msValueTypeName(&v));
  return false;
}
```

`msIterNext` 主分派（it 为栈 root 值；适配器递归推进时 src/src2 为 it 的 children，GC 安全）：
```c
static bool advZip(MsVM* vm, MsObjIter* z, MsValue* out);   // 见下
static bool callIterFn(MsVM* vm, MsValue fn, MsValue v, MsValue* out);  // 原生调用（Task 3 抽 value.c；本任务先用本地静态实现，Task 3 迁移）

bool msIterNext(MsVM* vm, MsValue it, MsValue* out) {
  ms_u8 ik = ((const MsObjIterShell*)it.as.obj)->kind;
  if (ik == MS_ITER_STR) { return msStrIterNext(vm, (MsObjStrIter*)it.as.obj, out); }
  if (ik == MS_ITER_LIST) {
    bool modified = false;
    bool ok = msListIterNext((MsObjListIter*)it.as.obj, out, &modified);
    if (!ok && modified) {  // 22 §4 版本号防并发修改
      snprintf(vm->errMsg, sizeof(vm->errMsg), "IteratorError: list modified during iteration");
      vm->hasError = true;
    }
    return ok;
  }
  MsObjIter* o = (MsObjIter*)it.as.obj;
  switch (ik) {
    case MS_ITER_RANGE: {  // 与原 FOR_ITER 内联分支同语义（含饱和推进）
      ms_i64 cur = o->cur, stop = o->stop, step = o->step;
      if (!(step > 0 ? cur < stop : cur > stop)) { return false; }
      *out = msValueInt(cur);
      o->cur = step > 0 ? cur > INT64_MAX - step ? stop : cur + step
                        : cur < INT64_MIN - step ? stop : cur + step;
      return true;
    }
    case MS_ITER_MAP:
    case MS_ITER_SET: {  // 键游标（快照语义：10 §4 无版本号；迭代中修改原 map 不受影响）
      const struct MsObjMap* m = ik == MS_ITER_MAP ? (const struct MsObjMap*)o->src.as.obj
                                                   : ((const struct MsObjSet*)o->src.as.obj)->map;
      o->i = o->i < 0 ? msObjMapFirst(m) : msObjMapNext(m, o->i);
      if (o->i < 0) { return false; }
      const MsValue* k = msObjMapKeyAt(m, o->i);
      if (k == NULL) { return false; }
      *out = *k;
      return true;
    }
    case MS_ITER_TUPLE: {
      const struct MsObjTuple* t = (const struct MsObjTuple*)o->src.as.obj;
      if (o->i >= t->count) { return false; }
      *out = t->items[o->i++];
      return true;
    }
    case MS_ITER_REVERSE: {
      if (o->i < 0) { return false; }
      if (msIsString(&o->src)) {  // 反向 rune 游标
        MsObjString* s = (MsObjString*)o->src.as.obj;
        ms_u32 cp = 0;
        if (!msObjStringRuneAt(vm, s, o->i--, &cp)) { return false; }
        *out = msValueInt((ms_i64)cp);
        return true;
      }
      if (o->src.kind == MS_VAL_OBJ && o->src.as.obj->type == MS_OBJ_LIST) {
        MsObjList* list = (MsObjList*)o->src.as.obj;
        if (list->version != o->version) {  // 与正向 list 迭代器同守版本号
          snprintf(vm->errMsg, sizeof(vm->errMsg), "IteratorError: list modified during iteration");
          vm->hasError = true;
          return false;
        }
        MsValue v2;
        if (!msObjListGet(list, o->i--, &v2)) { return false; }
        *out = v2;
        return true;
      }
      const struct MsObjTuple* t = (const struct MsObjTuple*)o->src.as.obj;
      *out = t->items[o->i--];
      return true;
    }
    case MS_ITER_ENUMERATE: {
      MsValue v;
      if (!msIterNext(vm, o->src, &v)) { return false; }
      struct MsObjTuple* t = msObjTupleNew(vm, 2);  // src 迭代器为 o->src：o 在栈 root，children 可达
      if (t == NULL) { snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory"); vm->hasError = true; return false; }
      t->items[0] = msValueInt(o->cur++);
      t->items[1] = v;
      *out = msValueObj(&t->head);
      return true;
    }
    case MS_ITER_ZIP: { return advZip(vm, o, out); }
    case MS_ITER_MAPFN: {
      MsValue v;
      if (!msIterNext(vm, o->src, &v)) { return false; }
      return callIterFn(vm, o->fn, v, out);  // fn 非原生 → TypeError（script fn T018）
    }
    case MS_ITER_FILTER: {
      MsValue v, keep;
      while (msIterNext(vm, o->src, &v)) {
        if (!callIterFn(vm, o->fn, v, &keep)) { return false; }
        if (msValueIsTruthy(&keep)) { *out = v; return true; }
      }
      return false;
    }
    case MS_ITER_TAKE: {
      if (o->cur <= 0) { return false; }
      MsValue v;
      if (!msIterNext(vm, o->src, &v)) { return false; }
      o->cur--;
      *out = v;
      return true;
    }
    case MS_ITER_DROP: {
      if ((o->flags & 2) == 0) {  // 首次推进：跳过 i 个
        MsValue v;
        while (o->i > 0 && msIterNext(vm, o->src, &v)) { o->i--; }
        o->flags |= 2;
      }
      return msIterNext(vm, o->src, out);
    }
  }
  snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: unknown iterator kind");
  vm->hasError = true;
  return false;
}

// ZIP：双源齐步；strict 长度不等 ValueError（10 §1）
static bool advZip(MsVM* vm, MsObjIter* z, MsValue* out) {
  MsValue a, b;
  bool hasA = msIterNext(vm, z->src, &a);
  if (vm->hasError) { return false; }
  bool hasB = msIterNext(vm, z->src2, &b);
  if (vm->hasError) { return false; }
  if (hasA != hasB) {
    if (z->flags & 1) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: zip() strict: iterables have unequal lengths");
      vm->hasError = true;
      return false;
    }
    return false;  // 短板对齐（任一耗尽即止）
  }
  if (!hasA) { return false; }
  struct MsObjTuple* t = msObjTupleNew(vm, 2);
  if (t == NULL) { snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory"); vm->hasError = true; return false; }
  t->items[0] = a;
  t->items[1] = b;
  *out = msValueObj(&t->head);
  return true;
}
```

`callIterFn`（本任务静态版，Task 3 迁移至 value.c 共享）：实参压 VM 栈 root → `fn.as.native(vm, 1, &vm->stack[vm->stackTop-1])` → 弹净 → hasError 透传；`fn.kind == MS_VAL_FUNCTION` 报 `"RuntimeError: script function values execute in T018"`；其余 `"TypeError: expects a function argument"`。

`msIterCollectList`：`int32_t start = vm->stackTop;` 循环 `msIterNext` 成功即 `msVmPush(vm, item)`（产物逐个栈 root）；错误/溢出路径不恢复 stackTop（调用场景整栈废弃，与 seqForm 同纪律）；结束后 `*out = msVmCollectStackToList(vm, vm->stackTop - start)`。

**GC 关键纪律**：所有适配器递归调用 `msIterNext(vm, o->src, ...)` 前，外层迭代器对象必须可达（FOR_ITER 的 it 在值栈 / 方法的 args[0] 在值栈 / 内层为外层 MsObjIter 的 children）；构造函数 `src` 传参时源对象须已 root（调用方 peek 纪律）。

- [ ] **Step 1.4: VM 接线（vm.h / vm.c / value.c / builtins_bridge.c / CMakeLists.txt）**

`vm.h`：`MsValueKind` 删除 `MS_VAL_RANGE_ITER`（union 中 `range` 成员一并删）；`msBuiltinLookup` 签名改为 `bool msBuiltinLookup(MsVM* vm, const char* name, int32_t len, MsValue* out)`（vm 参数本任务起用于 Task 2 内置变量，调用点同步）；新增声明 `bool msValueAdd(MsVM* vm, const MsValue* a, const MsValue* b, MsValue* out);`（实现见 Step 1.6）。

`vm.c`：
- `iterMark` 泛化：`kind == MS_ITER_LIST → list 标记；kind == MS_ITER_STR → str 标记；其余 → msObjIterMarkChildren(vm, obj)`（include `obj/obj_iter.h`）。
- `OP_GET_ITER` 重写：
```c
OPCODE(OP_GET_ITER) {  // 值 → 迭代器（05 §4 协议；分配期源在栈 root）
  MsValue it;
  if (!msIterFromValue(vm, vm->stack[vm->stackTop - 1], &it)) {
    RUNTIME_ERROR("%s", vm->errMsg);
  }
  vm->stack[vm->stackTop - 1] = it;
  NEXT();
}
```
- `OP_FOR_ITER` 重写（删 `MS_VAL_RANGE_ITER` 内联分支与 str/list kind 判别）：
```c
OPCODE(OP_FOR_ITER) {  // <t exit>：有值压值；耗尽弹迭代器并跳出（协议语义等价 nil 哨兵，见偏差 5）
  READ_OPERAND(t);
  MsValue it = vm->stack[vm->stackTop - 1];
  if (!msIsIterator(&it)) {
    RUNTIME_ERROR("TypeError: FOR_ITER over non-iterator '%s'", msValueTypeName(&it));
  }
  MsValue item;
  if (!msIterNext(vm, it, &item)) {
    if (vm->hasError) { RUNTIME_ERROR("%s", vm->errMsg); }
    (void)pop(vm);  // 耗尽弹迭代器（再迭代须重新 GET_ITER）
    ip = t;
    NEXT();
  }
  msVmPush(vm, item);
  NEXT();
}
```
- `OP_UNPACK_SEQ` 增 string 分支（`var a, b = "hi"` → rune 码点；"string 解构归 T017" 注释兑现）：
```c
if (msIsString(&v)) {  // 解构按 rune（02 §4）：源留栈直至码点全部取毕（runeIndex 惰性构建为分配点）
  MsObjString* s = (MsObjString*)v.as.obj;
  if ((ms_i64)s->charLen < n) {
    RUNTIME_ERROR("ValueError: not enough values to unpack (expected %d, got %d)", n, s->charLen);
  }
  ms_u32 cps[255];  // n ≤ 255（u8 操作数）
  for (int32_t i = 0; i < n; i++) {
    (void)msObjStringRuneAt(vm, s, i, &cps[i]);  // 源在栈 root；越界已由 charLen 前置校验排除
  }
  vm->stackTop--;
  for (int32_t i = 0; i < n; i++) {
    msVmPush(vm, msValueInt((ms_i64)cps[i]));
  }
  NEXT();
}
```

`value.c`：`msValueTypeName` 删 `case MS_VAL_RANGE_ITER`，`MS_VAL_OBJ` 内增 `case MS_OBJ_ITERATOR: return "iterator";`；`msValueToStr` 删 RANGE_ITER case，`MS_VAL_OBJ` 内增 `case MS_OBJ_ITERATOR: snprintf(buf, cap, "<iterator>"); break;`。

`builtins_bridge.c` `nativeRange`：尾部 `MsValue v = {MS_VAL_RANGE_ITER,...}` 三行改为：
```c
MsObjIter* it = msObjIterRangeNew(vm, start, stop, step);  // step != 0 已校验
if (it == NULL) {
  bridgeErr(vm, "RuntimeError: out of memory");
  return msValueNil();
}
return msValueObj(&it->head);
```

`CMakeLists.txt`：`ms_obj` 目标源列表（`obj_string.c intern.c ...` 处）追加 `obj_iter.c`。

- [ ] **Step 1.5: 建测试骨架 `tests/unit/test_builtins.c` + 注册**

测试工具复用 `test_vm_basic.c` 的 `runSrc` 模式（前端→编译→跑→捕获 out/err；照抄其 RunResult/读 tmpfile 逻辑，out 重定向 `vm.out`）。本任务先落迭代协议用例（后续任务向本文件追加）：

| TEST 名 | 源码 | 断言 |
|---------|------|------|
| `iterSixSources` | 六个 for 循环分别迭代 `[1,2]`、`"ab"`、map、`{1,2}`、`(7,8)`、`range(3)` 各打印和/串接 | 输出逐行匹配 |
| `iterNextExhaustNil` | `var it = iter([1,2])\nprint(next(it))\nprint(next(it))\nprint(next(it))` | `1\n2\nnil\n`（随 Task 2 落地：本任务仅写测试函数占位注释，Task 2 补齐） |
| `rangeNegStep` | `for x in range(10, 0, -2) { print(x) }` | `10 8 6 4 2` 逐行 |
| `rangeBoxedIdentity` | `print(type(range(3)))` | `iterator` |
| `unpackString` | `var a, b = "hi"\nprint(a, b)` | `104 105` |
| `forSumAccum` | `var done = 0\nfor x in [1, 2] { done += x }\nprint(done)` | `3` |

注：`iter()`/`next()` 内置在 Task 2 落地；本任务测试仅覆盖 for 协议六源 + range 装箱 + 解构。

`tests/CMakeLists.txt` 追加：`ms_add_test(builtins ms_vm ms_compiler ms_binder)`（`cmake/Tests.cmake` 的 ms_add_test 按测试名取 `tests/unit/test_<name>.c` 为源，照既有条目模式）。

- [ ] **Step 1.6: `msValueAdd` 导出（vm.c 实现，供 Task 2 sum 使用）**

vm.c 在 arithBinary 定义后：
```c
// 通用 '+' 语义（数值塔 + string/list 拼接；sum/init 改类型依赖）：失败置错误槽返回 false
bool msValueAdd(MsVM* vm, const MsValue* a, const MsValue* b, MsValue* out) {
  return arithBinary(vm, BA_ADD, *a, *b, out);
}
```

- [ ] **Step 1.7: 构建 + 全量测试**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
Expected: 全绿（既有 132 项含 range for 用例经新路径通过；新增 builtins 单测通过）。

- [ ] **Step 1.8: Commit**

```bash
git add -A
git commit -m "✨ feat(obj): generalize iterator objects and unify GET_ITER/FOR_ITER protocol"
```

---

### Task 2: Tier0 内置函数全集（builtins.c 合并 builtins_bridge.c）

**Files:**
- Create: `src/vm/builtins.c`（承接 bridge 全部既有 native + 新增全集；表驱动）
- Delete: `src/vm/builtins_bridge.c`
- Modify: `src/vm/vm.c`（LOAD_GLOBAL 调用点传 vm）
- Modify: `CMakeLists.txt`（ms_vm 源列表 builtins_bridge.c → builtins.c）
- Modify: `tests/unit/test_builtins.c`（追加 §5.1 表用例）

- [ ] **Step 2.1: 迁移 + 新增（builtins.c 结构）**

1. 既有 13 个 native（print/len/str/repr/set/type/int/float/bigint/bool/hash/abs/range）从 `builtins_bridge.c` 原样迁入（`bridgeErr` 改名 `bErr`；文件头注释改为 Tier0 全集）。`nativeSet` 扩展可迭代形态：
```c
// set()：空集 / set(iterable)（10 §1 容器节；可迭代形态 T017）
static MsValue nativeSet(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc > 1) { bErr(vm, "ArgumentError: set expects 0-1 arguments"); return msValueNil(); }
  MsObjSet* st = msObjSetNew(vm);
  if (st == NULL) { bErr(vm, "RuntimeError: out of memory"); return msValueNil(); }
  if (argc == 1) {
    MsValue it;
    if (!msIterFromValue(vm, args[0], &it)) { vm->hasError = true; return msValueNil(); }
    if (!msVmPush(vm, it)) { return msValueNil(); }  // 迭代窗口 root（errMsg 已由 FromValue 置）
    MsValue item;
    while (msIterNext(vm, it, &item)) {
      if (!msVmPush(vm, item)) { return msValueNil(); }  // put 前栈 root
      if (msObjSetAdd(vm, st, &item) == MS_MAP_OOM) { bErr(vm, "RuntimeError: out of memory"); return msValueNil(); }
      vm->stackTop--;
    }
    if (vm->hasError) { return msValueNil(); }
    vm->stackTop--;  // 弹迭代器
  }
  return msValueObj(&st->head);
}
```
2. 新增 native 族（每个独立函数，错误消息前缀沿用仓库既有错误类约定）：

| 函数 | 签名/行为 | 错误 |
|------|-----------|------|
| `nativeCap` | 占位：任何调用报 `RuntimeError: cap() lands in T030 (channels)` | — |
| `nativeMake` | 占位：`RuntimeError: make() lands in T030 (channels)` | — |
| `nativeInput` | 占位：`RuntimeError: input() lands in T035 (io stdlib)` | — |
| `nativeRace` | 占位：`RuntimeError: race() lands in T031 (futures)` | — |
| `nativeWeakref` | 占位：`RuntimeError: WeakRef lands in T027 (gc weakrefs)` | — |
| `nativeMin`/`nativeMax` | `min(...xs)`：argc==1 且可迭代 → 容器形态取最值（空 → `ValueError: min() arg is an empty sequence`）；argc≥2 → 逐参比较取最值。比较 `msValueOrder`，不可比 `TypeError: '<' not supported between '%s' and '%s'`。相等保首见 | ArgumentError（0 参） |
| `nativeSum` | `sum(xs[, init])`：init 缺省 `int 0`。迭代累加 `msValueAdd`（数值塔/string/list 拼接，init 改类型）。xs 经 `msIterFromValue` | TypeError 不可迭代 |
| `nativeSorted` | `sorted(xs[, key[, reverse]])`：物化 list（`msIterCollectList`）；key 非 nil 须原生函数（逐元素 `msVmCallNative`），键物化为平行 list；索引稳定归并排序（见 Step 2.2）；reverse=true 反比较（稳定保序）；新 list 返回 | TypeError 元素不可比/键失败 |
| `nativeReversed` | `reversed(xs)`：xs 为 list/string/tuple → `msObjIterReverseNew`；其余 `TypeError: reversed() expects a list, string or tuple` | — |
| `nativeEnumerate` | `enumerate(xs[, start])`：start 缺省 0（须 int）；`msIterFromValue(xs)` → `msObjIterEnumerateNew` | TypeError |
| `nativeZip` | `zip(xs, ys[, strict])`：strict 缺省 false（须 bool）；双源 `msIterFromValue` → `msObjIterZipNew` | ArgumentError/TypeError |
| `nativeIter` | `iter(x)` = `msIterFromValue`（错误透传） | TypeError |
| `nativeNext` | `next(it)`：it 须迭代器（`TypeError: next() expects an iterator`）；`msIterNext` 成功返回值，耗尽返回 `nil`（05 §4 不抛），错误透传 | — |
| `nativeIsinstance` | `isinstance(x, T)`：T 为 string 或 list/tuple of string → 任一匹配 `msValueTypeName(x)` 即 true；T 形态非法 `TypeError: isinstance() expects a type name or a list/tuple of type names` | — |
| `nativeCallable` | `callable(x)`：`MS_VAL_NATIVE`/`MS_VAL_FUNCTION`/bound method 对象 → true（`__call__` 归 T021） | — |
| `nativeId` | `id(x)`：堆对象 → `(ms_i64)(intptr_t)obj`；直接值 → `((ms_i64)kind << 56) ^ 位模式`（bool→b、i64→i、f64→位转换） | — |
| `nativeAssert` | `assert(cond[, msg])`：假 → `AssertionError: <msg>`（msg 缺省 `assertion failed`；非 string 经 `msValueStrForm`） | — |
| `nativeAll` | `all(xs)`：迭代全真（任一假即 false；空/耗尽 true）——本任务序列义；Future 列表并发义 T031 重载 | TypeError 不可迭代 |

GC 纪律统一模式（nativeMin/Max/Sum/All/Set 物化路径相同）：迭代器与逐产物压栈 root，汇总后弹净自压槽；错误路径不恢复（整栈由 unwind 废弃）。

3. 注册表 + 内置变量（替换 memcmp 链）：
```c
typedef struct BuiltinEntry {
  const char* name;
  MsNativeFn fn;
} BuiltinEntry;

static const BuiltinEntry kBuiltins[] = {
    {"print", nativePrint},   {"len", nativeLen},       {"cap", nativeCap},      {"range", nativeRange},
    {"type", nativeType},     {"repr", nativeRepr},     {"str", nativeStr},      {"int", nativeInt},
    {"bigint", nativeBigint}, {"float", nativeFloat},   {"bool", nativeBool},    {"hash", nativeHash},
    {"abs", nativeAbs},       {"min", nativeMin},       {"max", nativeMax},      {"sum", nativeSum},
    {"sorted", nativeSorted}, {"reversed", nativeReversed}, {"enumerate", nativeEnumerate},
    {"zip", nativeZip},       {"iter", nativeIter},     {"next", nativeNext},    {"isinstance", nativeIsinstance},
    {"callable", nativeCallable}, {"make", nativeMake}, {"id", nativeId},       {"input", nativeInput},
    {"assert", nativeAssert}, {"all", nativeAll},       {"race", nativeRace},    {"WeakRef", nativeWeakref},
    {"set", nativeSet},
};

// LOAD_GLOBAL 未命中用户全局时的内置回查（含内置变量 __name__/__version__，10 §5）
bool msBuiltinLookup(MsVM* vm, const char* name, int32_t len, MsValue* out) {
  if (len == 8 && memcmp(name, "__name__", 8) == 0) {
    MsObjString* s = msInternBytes(vm, "__main__", 8);  // 模块名 T024 起动态
    if (s == NULL) { return false; }
    *out = msValueObj(&s->head);
    return true;
  }
  if (len == 11 && memcmp(name, "__version__", 11) == 0) {
    MsObjString* s = msInternBytes(vm, MS_VERSION, (int32_t)strlen(MS_VERSION));
    if (s == NULL) { return false; }
    *out = msValueObj(&s->head);
    return true;
  }
  for (size_t i = 0; i < sizeof(kBuiltins) / sizeof(kBuiltins[0]); i++) {
    const char* n = kBuiltins[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      out->kind = MS_VAL_NATIVE;
      out->as.native = kBuiltins[i].fn;
      return true;
    }
  }
  return false;
}
```
（`__name__`/`__version__` 每次 intern 命中即返回既有永久串，零重复分配。）

- [ ] **Step 2.2: sorted 索引稳定归并排序（builtins.c 内静态实现）**

```c
// 索引稳定归并排序（timsort 语义保序由归并稳定性保证）：按 keys 比较原序索引
// cmp < 0 = a 前；reverse=true 取反（相等保原序 → 稳定，与 Python reverse 语义一致）
typedef struct SortCtx {
  const MsValue* keys;  // 键数组（无 key 时即 items 本身）
  bool reverse;
  bool failed;          // 首个不可比错误（errMsg 已置）
  const char* which;    // 错误消息用（"sorted"/"min"/"max"）
} SortCtx;

static int sortCmp(MsVM* vm, SortCtx* c, int32_t a, int32_t b) {  // 返回 -1/0/1
  int32_t cmp = 0;
  if (!msValueOrder(&c->keys[a], &c->keys[b], &cmp)) {
    if (!c->failed) {
      c->failed = true;
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: '%s' cannot order '%s' and '%s'", c->which,
               msValueTypeName(&c->keys[a]), msValueTypeName(&c->keys[b]));
      vm->hasError = true;
    }
    return 0;
  }
  if (c->reverse) { cmp = -cmp; }
  return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

// 归并：idx 为索引数组（原序 0..n-1），tmp 为等长scratch；失败（failed）即中断返回
static void sortMerge(MsVM* vm, SortCtx* c, int32_t* idx, int32_t* tmp, int32_t lo, int32_t hi);
static void sortRec(MsVM* vm, SortCtx* c, int32_t* idx, int32_t* tmp, int32_t lo, int32_t hi) {
  if (hi - lo < 2) { return; }
  int32_t mid = lo + (hi - lo) / 2;
  sortRec(vm, c, idx, tmp, lo, mid);
  sortRec(vm, c, idx, tmp, mid, hi);
  if (c->failed) { return; }
  sortMerge(vm, c, idx, tmp, lo, hi);  // 经典两路归并：左半 ≤ 右半时保序（稳定）
}
```
`nativeSorted` 流程：物化 items（栈 root）→ 无 key：keys=items；有 key：逐元素调 key（产物栈 root）组成平行 list → idx[0..n) = 0..n-1、tmp malloc → `sortRec` → 失败弹净返 nil → 结果 list 按 idx 序填 items → 弹净自压槽返回。min/max 单容器形态复用 `sortCmp` 线性扫描（无需排序）。

- [ ] **Step 2.3: 调用点与构建接线**

`vm.c` OP_LOAD_GLOBAL：`msBuiltinLookup(name->as.text.start, ...)` → `msBuiltinLookup(vm, ...)`（rg 确认无其他调用点）。`CMakeLists.txt`：`ms_vm` 源列表 `builtins_bridge.c` → `builtins.c`；删除旧文件。

- [ ] **Step 2.4: 单测追加（§5.1 表全落）**

| TEST 名 | 调用 | 期望 |
|---------|------|------|
| `lenAllTypes` | len("中a")/list/map/set/tuple | 4（字节）/元素数 |
| `typeAllTypes` | type 逐项（nil/bool/int/float/string/bigint/list/tuple/map/set/function/iterator） | 名字串 |
| `rangeNegStepSum` | `print(sum(range(10, 0, -2)))` | `30` |
| `enumerateStart` | `for i, v in enumerate(["a", "b"], 1) { print(i, v) }` | `1 a` / `2 b` |
| `zipStrictError` | `runSrc("zip([1], [1, 2], true).toList()")` — 本任务无 toList 方法（Task 3）：改 `for p in zip([1], [1, 2], true) { print(p) }` | err 含 `ValueError: zip() strict` |
| `zipShortest` | `for p in zip([1, 2, 3], ["a"]) { print(p) }` | `(1, "a")` 一行 |
| `sortedKeyStable` | `var xs = ["bb", "a", "cc"]`；`print(sorted(xs, len))` | `["a", "bb", "cc"]`（key=len） |
| `sortedReverse` | `print(sorted(range(5).toList(), nil, true))` — 无 toList：物化经 `var xs = []` 循环填或 `sorted([3,1,2], nil, true)`；`range` 链 Task 3 后补 | `[3, 2, 1]` |
| `minMaxContainer` | `print(min([3, 1, 2]), max([3, 1, 2]))` | `1 3` |
| `minMaxVariadic` | `print(min(4, 2, 9), max(4, 2, 9))` | `2 9` |
| `sumInitString` | `print(sum([1, 2], 10))` → `13`；`print(sum(["a", "b"], ""))` | `ab` |
| `iterNextManual` | `var it = iter([1, 2])`；next 三连 | `1`/`2`/`nil` |
| `hashNumericTower` | `print(hash(1) == hash(1.0))` | `true` |
| `assertFail` | `runSrc("assert(false, \"m\")")` | err 含 `AssertionError: m`，exit 1 |
| `assertPass` | `assert(1 == 1)` | 无输出成功 |
| `isinstanceForms` | `isinstance([], "list")`、`isinstance(1, ["string", "int"])`、非法 T | true/true/err |
| `callableKinds` | `callable(print)`、`callable(len)`、`callable(1)`、`callable("x".toUpper)` | true/true/false/true |
| `builtinVars` | `print(__name__ == "__main__")`、`print(__version__ != "")` | true/true |
| `placeholders` | `runSrc("make(chan)")` / `cap(1)` / `race([])` / `WeakRef(nil)` / `input()` | err 各含 `lands in T0`；exit 1 |
| `idStable` | `var a = []\nvar b = []\nprint(id(a) == id(a), id(a) == id(b))` | `true false` |
| `allSeq` | `print(all([1, true, "x"]), all([1, false]), all([]))` | `true false true` |
| `setFromIterable` | `print(len(set([1, 1, 2])))` | `2` |
| `reversedIter` | `for x in reversed([1, 2, 3]) { print(x) }` | `3 2 1` 逐行 |

- [ ] **Step 2.5: 构建 + 全量测试**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
Expected: 全绿。

- [ ] **Step 2.6: Commit**

```bash
git add -A
git commit -m "✨ feat(vm/builtins): complete Tier0 builtin table with registry"
```

---

### Task 3: iterator 方法族（next/toList/take/drop/map/filter/each 惰性链接）

**Files:**
- Create: `src/vm/iter_methods.c`
- Modify: `src/vm/value.c`（新增公共 `msVmCallNative`）
- Modify: `src/vm/vm.h`（声明 `msVmCallNative`、`msIterMethodLookup`）
- Modify: `src/vm/vm.c`（methodRefLookup 增 iterator 分支）
- Modify: `src/vm/list_methods.c`（静态 callNative 换用共享 msVmCallNative）
- Modify: `src/obj/obj_iter.c`（callIterFn 换用 msVmCallNative，删本地实现）
- Modify: `CMakeLists.txt`（ms_vm 增 iter_methods.c）
- Modify: `tests/unit/test_builtins.c`（方法族用例）

- [ ] **Step 3.1: value.c 公共调用助手（照搬 list_methods.c:58-71 callNative）**

```c
// 原生函数调用公共面（list_methods D5 / obj_iter / iter_methods 共用）：
// argv 值拷贝逐个压栈（调用窗口 GC root）后调用并弹净实参
bool msVmCallNative(MsVM* vm, const MsValue* f, const MsValue* argv, int32_t argc, MsValue* out) {
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
```
`list_methods.c` 删静态 `callNative`，调用点改 `msVmCallNative`；`obj_iter.c` 删静态 `callIterFn`，`msIterNext` 的 MAPFN/FILTER 分支改（fn 形态校验先行）：
```c
if (o->fn.kind != MS_VAL_NATIVE && o->fn.kind != MS_VAL_FUNCTION) {
  snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: expects a function argument");
  vm->hasError = true;
  return false;
}
if (o->fn.kind == MS_VAL_FUNCTION) {
  snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: script function values execute in T018");
  vm->hasError = true;
  return false;
}
```

- [ ] **Step 3.2: 写 `src/vm/iter_methods.c`（方法表 + 实现）**

模式照 `list_methods.c`（methodError/argc 检查/args[0] = receiver）。receiver 须迭代器（`iterRecv` 助手校验）。方法表（10 §4）：

```c
static const IterMethodEntry kIterMethods[] = {
    {"next", itNext},   {"toList", itToList}, {"take", itTake},
    {"drop", itDrop},   {"map", itMap},       {"filter", itFilter},
    {"each", itEach},
};
```

| 方法 | 行为 |
|------|------|
| `it.next()` | 同 `next(it)`：`msIterNext`（耗尽 nil；错误透传） |
| `it.toList()` | `msIterCollectList(it)` → 新 list |
| `it.take(n)` | n 须非负 int → `msObjIterTakeNew(vm, it, n)`（it 直通为 src，返回新迭代器） |
| `it.drop(n)` | 同上 → `msObjIterDropNew` |
| `it.map(f)` | f 原生函数 → `msObjIterMapFnNew`（script fn 报 T018，同 checkFnArg 消息） |
| `it.filter(f)` | 同上 → `msObjIterFilterNew` |
| `it.each(f)` | 终结操作：迭代逐项 `msVmCallNative(f, v)`（丢弃返回值；f 形态校验同上），返回 nil |

`msIterMethodLookup` 表线性查找（同 list 模式）。

- [ ] **Step 3.3: 接线**

`vm.c` `methodRefLookup` 增分支（iterator 放在数值前）：
```c
if (msIsIterator(recv)) {
  return msIterMethodLookup(name, len, out) ? 0 : -1;
}
```
`vm.h` 增声明；`CMakeLists.txt` ms_vm 增 `iter_methods.c`。

- [ ] **Step 3.4: 单测追加**

| TEST 名 | 源码要点 | 期望 |
|---------|----------|------|
| `iterChainTakeDrop` | `range(10).take(7).drop(2).toList()` 打印 | `[2, 3, 4, 5, 6]` |
| `iterChainMapFilter` | `range(6).filter(f).map(g).toList()`（filter x%2、map x*x，经 print） | `[0, 4, 16]` |
| `iterEachSum` | `var s = 0`；`[1,2,3].map(f).each(g)`（map ×2、each 累加 print s） | `12` |
| `rangeToListSorted` | `print(sorted(range(5).toList(), nil, true))`（DoD 组合） | `[4, 3, 2, 1, 0]` |
| `iterLazyNoOvershoot` | take 后源未过度推进：`var it = range(100)\nit.take(2).toList()` 打印后 `print(next(it))` | 先 `[0, 1]` 后 `2`（take 只推进源两次） |
| `nextMethodForm` | `var it = iter("ab")`；`it.next()` 三连 | `97`/`98`/`nil` |

- [ ] **Step 3.5: 构建 + 全量测试 + Commit**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure` → 全绿。

```bash
git add -A
git commit -m "✨ feat(vm): iterator method family with lazy chaining"
```

---

### Task 4: 推导式 / 生成器表达式编译（内联脱糖 + 帧槽重映射）

**Files:**
- Modify: `src/compiler/chunk.h`（Chunk 增 `localCount`）
- Modify: `src/compiler/chunk.c`（chunkInit 置 0）
- Modify: `src/compiler/compiler.c`（FuncState 扩展 + compileCompr + 标识符重映射 + localOp）
- Modify: `src/vm/vm.c`（msVmRun 保留局部槽区；OP_RETURN 归位 stackTop）
- Modify: `tests/unit/test_compiler_basic.c`（推导式编译形状用例）
- Modify: `tests/unit/test_vm_basic.c`（推导式/嵌套/多 for/变量不泄漏/局部区保留回归）

原理（偏差 8）：binder 把推导式当伪函数帧（`SCOPE_COMPR`，槽位独立从 0 编号；跨伪帧引用标 `AST_BIND_UPVALUE(depth)`，depth = 跨越的函数边界数）。内联编译时维护推导式帧栈：

- `AST_BIND_LOCAL`（comprDepth>0）→ 内层帧映射表 `map[slot]`
- `AST_BIND_UPVALUE`：`depth == comprDepth` → 定义于真实外层函数 → `bindSlot` 即本帧槽直用；`depth < comprDepth` → 第 `depth` 层外推导式帧的 `map[bindSlot]`；`depth > comprDepth` → 跨真实函数边界（T018，维持既有报错）
- 推导式不产生 break/continue/return（纯表达式），无需 LoopCtx；累加器与目标全部落临时槽（GC root 于帧局部区）

- [ ] **Step 4.1: chunk.h/chunk.c 局部槽数字段**

`struct Chunk` 增 `int32_t localCount;  // 帧局部槽位数（含参数与推导式临时槽；VM 入帧保留 nil 区）`；`chunkInit` 置 0。

- [ ] **Step 4.2: compiler.c FuncState 扩展与 localOp 助手**

```c
// 推导式内联编译帧：binder COMPR 伪帧槽位 → 真实帧槽位映射（按需物化）
typedef struct ComprFrame {
  int32_t* map;
  int32_t mapLen, mapCap;
} ComprFrame;

typedef struct FuncState {
  Chunk* chunk;
  ChunkJump* jumps;
  int32_t jumpCount, jumpCap;
  LoopCtx* loops;
  int32_t loopCount, loopCap;
  MsDiagSink* diags;
  const char* file;
  int32_t localCount;           // 本帧最大槽位号 + 1（收尾写 chunk->localCount）
  int32_t nextTemp;             // 下一临时槽（推导式累加器/目标）
  ComprFrame* comprStack;
  int32_t comprDepth, comprCap;
} FuncState;
```

所有 `FuncState` 字面量初始化处（compilerCompile/compileFuncProto）补 `0, 0, NULL, 0, 0`。局部槽发射助手（替换全部裸 `emitOp(OP_LOAD_LOCAL/OP_STORE_LOCAL)` 发射点：loadIdent/storeIdent/finishDeclStore/storeDeclIdent/storeIterTarget/storeCaseBinding）：

```c
// 局部槽访问统一入口：槽位域校验（u8 操作数）+ localCount 跟踪
static void localOp(FuncState* fs, ms_u8 op, int32_t slot, int32_t line) {
  if (slot > 255) {
    compErr(fs, NULL, 1, "too many local variables");
    slot = 0;  // 占位维持发射面
  }
  if (slot + 1 > fs->localCount) {
    fs->localCount = slot + 1;
  }
  emitOp(fs, op, line);
  emitByte(fs, (ms_u8)slot);
}
```
（compErr 首参声明为 `const AstNode*`——传 NULL 需将 compErr 签名的解引用防御；直接沿用现有 compErr 但传一个安全节点：改为先判空。若不愿动 compErr，slot>255 分支用 `compErr(fs, (const AstNode*)(intptr_t)0, ...)` 不可取——正确做法：compErr 增 NULL 判断 `if (n == NULL) { ...只入 sink... }`，注意 msDiagAdd 需要 line/col，取 0。）

- [ ] **Step 4.3: 标识符重映射（loadIdent/storeIdent 重写）**

```c
// 推导式帧栈内解析槽位：BIND_LOCAL → 内层帧 map；BIND_UPVALUE(depth) → 跨 depth 层伪帧或直通外层函数
// 返回 -1 = 不可解析（槽位未声明/跨真实函数边界）
static int32_t comprResolveSlot(const FuncState* fs, int32_t slot, int32_t hops) {
  if (fs->comprDepth == 0) {
    return slot;  // 帧局部直通
  }
  const ComprFrame* inner = &fs->comprStack[fs->comprDepth - 1];
  if (hops < 0) {  // BIND_LOCAL：属内层推导式伪帧（模块/外层函数局部不会以 LOCAL 形态出现在推导式内）
    return slot < inner->mapLen ? inner->map[slot] : -1;
  }
  if (hops == fs->comprDepth) {
    return slot;  // 定义于真实外层函数：bindSlot 即本帧槽
  }
  if (hops > fs->comprDepth) {
    return -1;  // 跨真实函数边界：upvalue 归 T018
  }
  const ComprFrame* def = &fs->comprStack[fs->comprDepth - 1 - hops];
  return slot < def->mapLen ? def->map[slot] : -1;
}

static void loadIdent(FuncState* fs, const AstNode* n) {
  if (n->bindKind == AST_BIND_LOCAL) {
    int32_t slot = comprResolveSlot(fs, n->bindSlot, -1);
    if (slot < 0) {
      compErr(fs, n, n->as.ident.nameLen, "comprehension variable used before declaration");
      emitOp(fs, OP_LOAD_NIL, n->line);
      return;
    }
    localOp(fs, OP_LOAD_LOCAL, slot, n->line);
    return;
  }
  if (n->bindKind == AST_BIND_GLOBAL) {
    int32_t k = nameConst(fs, n->as.ident.name, n->as.ident.nameLen);
    emitConstOp(fs, OP_LOAD_GLOBAL, k, n->line);
    return;
  }
  if (fs->comprDepth > 0 && n->bindUpvalDepth <= fs->comprDepth) {  // 推导式捕获：同帧消解
    int32_t slot = comprResolveSlot(fs, n->bindSlot, n->bindUpvalDepth);
    if (slot >= 0) {
      localOp(fs, OP_LOAD_LOCAL, slot, n->line);
      return;
    }
  }
  compErr(fs, n, n->as.ident.nameLen, "upvalue capture is not supported yet");
  emitOp(fs, OP_LOAD_NIL, n->line);
}
```
`storeIdent` 同构（`OP_STORE_LOCAL`；UPVALUE 分支保留既有 T018 报错即可——推导式值表达式不含赋值语句，UPVALUE store 不可达，防御即可）。

- [ ] **Step 4.4: compileCompr（compileExpr 增 case）**

```c
// 推导式帧栈操作
static void comprPush(FuncState* fs) {
  if (fs->comprDepth == fs->comprCap) {
    fs->comprCap = fs->comprCap == 0 ? 4 : fs->comprCap * 2;
    fs->comprStack = realloc(fs->comprStack, sizeof(ComprFrame) * (size_t)fs->comprCap);
    MS_ASSERT(fs->comprStack != NULL);
  }
  ComprFrame* f = &fs->comprStack[fs->comprDepth++];
  f->map = NULL;
  f->mapLen = f->mapCap = 0;
}

static void comprPop(FuncState* fs) {
  ComprFrame* f = &fs->comprStack[--fs->comprDepth];
  free(f->map);
  f->map = NULL;
}

// binder COMPR 伪帧槽 → 真实帧槽（按需物化；映射表随声明顺序增长）
static int32_t comprSlot(FuncState* fs, int32_t slot) {
  ComprFrame* f = &fs->comprStack[fs->comprDepth - 1];
  while (f->mapLen <= slot) {  // 稀疏防御：中间槽顺序占位（binder 槽位致密，正常不触发）
    MS_ARRAY_PUSH(f->map, f->mapLen, f->mapCap, fs->nextTemp++);
  }
  return f->map[slot];
}
```
（`MS_ARRAY_PUSH` 见 `src/common/common.h`，扩容语义同既有用法。）

```c
// 推导式目标落盘（remap 版 storeIterTarget）：IDENT 经 comprSlot；TUPLE → UNPACK_SEQ 逆序递归
static void storeComprTarget(FuncState* fs, AstNode* t) {
  if (t->kind == AST_TUPLE) {
    if (t->as.tuple.count > 255) {
      compErr(fs, t, 1, "too many iteration targets");
      return;
    }
    emitOp(fs, OP_UNPACK_SEQ, t->line);
    emitByte(fs, (ms_u8)t->as.tuple.count);
    for (int32_t i = t->as.tuple.count - 1; i >= 0; i--) {
      storeComprTarget(fs, t->as.tuple.items[i]);
    }
    return;
  }
  if (t->kind != AST_IDENT) {  // BAD_TARGET 残留防御
    compErr(fs, t, 1, "iteration target form is not supported yet");
    emitOp(fs, OP_POP, t->line);
    return;
  }
  localOp(fs, OP_STORE_LOCAL, comprSlot(fs, t->bindSlot), t->line);
}

// 脱糖主体：嵌套 GET_ITER/FOR_ITER 循环 + 条件 + 元素追加（03 §2）
// 累加器驻帧槽（acc）；list/set 经 CALL_METHOD append/add（零新指令），map 经 SET_INDEX
static void compileComprBody(FuncState* fs, const AstNode* n, int32_t acc, int32_t forIdx, int32_t lTopInner) {
  if (forIdx == n->as.compr.forCount) {
    for (int32_t i = 0; i < n->as.compr.condCount; i++) {  // 多 if 合取：假 → 内层循环头
      compileExpr(fs, n->as.compr.conds[i]);
      int32_t j = emitJump(fs, OP_JMP_IF_FALSE, n->as.compr.conds[i]->line);
      fs->jumps[j].target = lTopInner;
    }
    if (n->as.compr.compKind == AST_COMP_MAP) {
      localOp(fs, OP_LOAD_LOCAL, acc, n->line);
      compileExpr(fs, n->as.compr.key);
      compileExpr(fs, n->as.compr.value);
      emitOp(fs, OP_SET_INDEX, n->line);  // m[k] = v：弹三无产出，栈平衡
      return;
    }
    localOp(fs, OP_LOAD_LOCAL, acc, n->line);
    compileExpr(fs, n->as.compr.value);
    int32_t k = nameConst(fs, n->as.compr.compKind == AST_COMP_SET ? "add" : "append", 3);
    emitConstOp(fs, OP_CALL_METHOD, k, n->line);
    emitByte(fs, 0);  // icSlot（T022）
    emitByte(fs, 1);  // argc = 1（接收者之外仅元素）
    emitOp(fs, OP_POP, n->line);  // append/add 返回 nil 弹净
    return;
  }
  compileExpr(fs, n->as.compr.fors[forIdx].iter);  // 迭代源在第 i 层子句域求值（03 §2）
  emitOp(fs, OP_GET_ITER, n->line);
  int32_t lTop = fs->chunk->codeLen;
  int32_t jExit = emitJump(fs, OP_FOR_ITER, n->line);
  storeComprTarget(fs, n->as.compr.fors[forIdx].target);
  compileComprBody(fs, n, acc, forIdx + 1, forIdx == n->as.compr.forCount - 1 ? lTop : lTopInner);
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  fs->jumps[jExit].target = fs->chunk->codeLen;
}

// AST_COMPREHENSION / AST_GEN_EXPR 共用（03 §1-§2）：genexp = 物化 list + GET_ITER（T019 替换）
static void compileCompr(FuncState* fs, const AstNode* n) {
  comprPush(fs);
  int32_t acc = fs->nextTemp++;  // 累加器临时槽（帧局部区 root）
  ms_u8 build = n->as.compr.compKind == AST_COMP_MAP ? OP_BUILD_MAP
                : n->as.compr.compKind == AST_COMP_SET ? OP_BUILD_SET
                                                       : OP_BUILD_LIST;
  emitOp(fs, build, n->line);
  emitByte(fs, 0);
  localOp(fs, OP_STORE_LOCAL, acc, n->line);
  compileComprBody(fs, n, acc, 0, 0);
  comprPop(fs);
  localOp(fs, OP_LOAD_LOCAL, acc, n->line);
  if (n->kind == AST_GEN_EXPR) {
    emitOp(fs, OP_GET_ITER, n->line);
  }
}
```
`compileExpr` switch 增：
```c
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      compileCompr(fs, n);
      break;
```

- [ ] **Step 4.5: 函数原型槽位基线（compileFuncProto / compilerCompile）**

```c
// 通用 AST 遍历：收集 BIND_LOCAL bindSlot 最大值（含推导式目标/嵌套函数——安全超估）
static void scanMaxLocalSlot(const AstNode* n, int32_t* maxSlot);
```
遍历覆盖全部子节点（MODULE/BLOCK/LIST/SET/TUPLE、VAR_DECL/ASSIGN/DESTRUCTURE/EXPR_STMT/IF/FOR/WHILE/SWITCH+CASE/RETURN/THROW/YIELD/DEFER/SEND/SELECT+CASE/IMPORT/FUNC_DECL/CLASS_DECL/TRY、IDENT/STRING 段/BINARY/LOGIC/UNARY/RECV/GO/AWAIT/TERNARY/CALL/INDEX/MEMBER/SLICE/MAP/COMPREHENSION/GEN_EXPR/FUNC_EXPR；decl 节点与 IDENT 检查 `bindKind == AST_BIND_LOCAL` 时取 max）。

`compilerCompile`：`fs.nextTemp` 基线 = `scanMaxLocalSlot(module) + 1`（模块层无参数；scan 为 -1 时为 0）。`compileFuncProto`：`int32_t base = n->as.funcDecl.paramCount; int32_t scan = -1; scanMaxLocalSlot(body, &scan); if (scan + 1 > base) base = scan + 1; inner.nextTemp = base;`。两者收尾（finishChunk 后）`fs->chunk->localCount` 赋 `fs.localCount`（函数原型经 `*protoOut` 已持有）。

- [ ] **Step 4.6: VM 局部槽区保留（vm.c）**

`msVmRun` 推帧后：
```c
  for (int32_t i = 0; i < chunk->localCount; i++) {  // 帧局部区（推导式临时槽/参数区）：入帧即 root
    if (!msVmPush(vm, msValueNil())) {
      vm->frameCount--;
      return;
    }
  }
```
`OP_RETURN`：
```c
      OPCODE(OP_RETURN) {
        (void)pop(vm);  // 返回值（模块帧丢弃；帧切换 T018）
        vm->stackTop = vm->frames[vm->frameCount - 1].base;  // 归位：连局部区一并退栈
        vm->frameCount--;
        goto dispatchExit;
      }
```

- [ ] **Step 4.7: 测试（compiler_basic + vm_basic）**

`test_compiler_basic.c` 追加：
| TEST 名 | 断言 |
|---------|------|
| `listCompCompiles` | `[x * x for x in xs]` 编译通过，无诊断；反汇编含 `GET_ITER`/`FOR_ITER`/`CALL_METHOD` |
| `genExprCompiles` | `(x for x in xs)` 通过且以 `GET_ITER` 收尾（disasm 尾部指令） |
| `mapCompCompiles` | `{k: v for k, v in m.items()}` 含 `SET_INDEX` |
| `localCountReserved` | 含推导式的 chunk `localCount >= 累加器+目标数`；无推导式 chunk `localCount == 0` |

`test_vm_basic.c` 追加（runSrc 黄金）：
| TEST 名 | 源码 | 期望 |
|---------|------|------|
| `listCompBasic` | `print([x * x for x in [1, 2, 3]])` | `[1, 4, 9]` |
| `listCompIf` | `print([x for x in range(6) if x % 2 == 0 if x > 0])` | `[2, 4]` |
| `compTwoFors` | `print([x + y for x in [1, 2] for y in [10, 20]])` | `[11, 21, 12, 22]` |
| `compNoLeak` | `var xs = [1, 2]\nvar ys = [x for x in xs]\nprint(ys)` + 编译后引用 `x` → runSrc `"print(x)"` 追加段报 undefined（拆两用例：正例输出 + 反例 `RuntimeError: undefined name 'x'`） | 泄漏拒绝 |
| `mapComp` | `{k: len(v) for k, v in {"a": [1], "b": [2, 3]}.items()}` 打印 | `{"a": 1, "b": 2}` |
| `setComp` | `{x % 3 for x in [1, 2, 3, 4, 5, 6]}` 打印 | `{1, 2, 0}`（插入序） |
| `nestedComp` | `print([[y * 2 for y in row] for row in [[1], [2, 3]]])` | `[[2], [4, 6]]` |
| `genExprSum` | `print(sum(x * x for x in range(5)))` | `30` |
| `genExprAssign` | `var it = (x + 1 for x in [1, 2])\nfor v in it { print(v) }` | `2`/`3` 逐行 |
| `compCaptureOuter` | `var n = 3\nprint([x * n for x in [1, 2]])` | `[3, 6]`（UPVALUE d==comprDepth 直通路径） |
| `overviewExample` | `print([x * x for x in range(1, 6)])`（00 §3） | `[1, 4, 9, 16, 25]` |
| `comprShadowOuter` | `var x = 9\nvar ys = [x for x in [1]]\nprint(ys[0], x)` | `1 9`（目标遮蔽外层全局） |

- [ ] **Step 4.8: 构建 + 全量测试（含 .bc conformance 回归——既有结构发射不变，快照应保持）+ Commit**

```bash
git add -A
git commit -m "✨ feat(compiler): compile comprehensions and generator expressions inline"
```

---

### Task 5: fixtures 全量、DoD 复核与文档状态

**Files:**
- Create: `tests/fixtures/builtin/comprehension.ms` + `.out`
- Create: `tests/fixtures/builtin/iter_protocol.ms` + `.out`
- Create: `tests/fixtures/builtin/conv.ms` + `.out`
- Create: `tests/fixtures/builtin/genexp_pre.ms` + `.out`
- Create: `tests/fixtures/builtin/tier0.ms` + `.out`
- Create: `tests/fixtures/builtin/range_enumerate.ms` + `.out`
- Create: `tests/fixtures/builtin/assert_fail.{ms,out,err,code}`（err 首行为匹配 needle）
- Create: `tests/fixtures/builtin/zip_strict.{ms,out,err,code}`
- Create: `tests/fixtures/builtin/iter_modify.{ms,out,err,code}`（迭代中改 list → IteratorError）
- Modify: `docs/tasks/T017-tier0-builtins-iteration.md`（头部状态 + DoD 勾选）
- Modify: `docs/tasks/README.md`（总表 T017 状态 ⬜ → ✅）

- [ ] **Step 5.1: fixtures 内容（golden 以实际输出为准落盘；下方为源与预期）**

`comprehension.ms`（11-style-ms 规范）：
```ms
var xs = [1, 2, 3, 4, 5, 6]
print([x * x for x in xs if x % 2 == 0])
print({x % 3 for x in xs})
print(sorted({x % 3 for x in xs}))
var m = {"a": [1], "b": [2, 3]}
print({k: len(v) for k, v in m.items()})
var flat = [x + y for x in [1, 2] for y in [10, 20]]
print(flat)
print([x * x for x in range(1, 6)])
```
预期 `.out`：
```
[4, 16, 36]
{1, 2, 0}
[0, 1, 2]
{"a": 1, "b": 2}
[11, 21, 12, 22]
[1, 4, 9, 16, 25]
```

`iter_protocol.ms`：
```ms
var it = iter([1, 2, 3])
print(next(it))
print(next(it))
print(it.next())
print(next(it), next(it))
for v in iter("ab") {
  print(v)
}
for v in range(10).take(7).drop(2).toList() {
  print(v)
}
var doubled = range(4).map(abs).filter(abs).toList()
print(doubled)
var total = 0
range(5).each(abs)
print(sum(range(5)))
for k in {"a": 1, "b": 2} {
  print(k)
}
```
预期 `.out`（逐行）：`1`、`2`、`3`、`nil nil`、`97`、`98`、`2..6` 各行、`[0, 1, 2, 3]`、`10`、`a`、`b`。

`conv.ms`（golden）：
```ms
print(int("42"), int("2a", 16), int(3.9), int(-3.9))
print(float("2.5"), float(2))
print(bool(0), bool(""), bool(nil), bool(1))
print(str(12), str([1, "a"]), repr("a\"b"))
print(bigint("123456789012345678901234567890") + 1)
print(type(1), type(1.0), type(""), type([]), type({}), type(()), type(range(1)))
```
预期：`42 42 3 -3`、`2.5 2.0`、`true true false true`（02 §2 真值：仅 nil/false 为假 → bool(0)/bool("")/bool(1) 为 true、bool(nil) 为 false）、`12 [1, "a"] "a\"b"`、`123456789012345678901234567891`、`int float string list map tuple iterator`。

`genexp_pre.ms`：`print(sum(x * x for x in range(5)))` → `.out`：`30`（T019 后复跑同文件）。

`tier0.ms`：
```ms
print(__name__, __version__ != "")
print(len("hello"), len([1, 2]), len({"a": 1}), len({1, 2}), len((1, 2, 3)))
print(hash(1) == hash(1.0), hash("x") == hash("x"))
print(abs(-3), abs(-2.5), abs(-(1 << 70)))
print(min([3, 1, 2]), max(3, 1, 2), min("b", "a"))
print(sum([1, 2, 3]), sum([1, 2], 10), sum(["a", "b"], ""))
print(isinstance([], "list"), isinstance(1, ["string", "int"]))
print(callable(print), callable(len), callable(1))
print(sorted([3, 1, 2]), sorted(["bb", "a", "ccc"], len, false), sorted([1, 2, 3], nil, true))
print(reversed([1, 2, 3]).toList())
print(zip([1, 2], ["a", "b"]).toList())
print(enumerate(["a", "b"], 1).toList())
print(all([1, true]), any_placeholder_note())
```
（末行占位函数不存在会编译失败——删除该行；`any` 非 Tier0 表内函数，勿引入。以实际语义写完并按运行输出落 `.out`。）

`range_enumerate.ms`：`range(10,0,-2)` 求和与逐项、`enumerate` start=1 元组打印、`for i, v in enumerate(...)` 解构。

错误 fixtures（`.err` 首行 = stderr needle，`.code` = 1）：
- `assert_fail.ms`：`assert(1 == 2, "boom")` → needle `AssertionError: boom`
- `zip_strict.ms`：`for p in zip([1], [1, 2], true) { print(p) }` → needle `ValueError: zip() strict`
- `iter_modify.ms`：`var xs = [1, 2, 3]\nfor x in xs { xs.append(x) }` → needle `IteratorError: list modified during iteration`

- [ ] **Step 5.2: 运行验证 fixtures（新 ctest 用例自动注册，重跑 cmake configure）**

Run: `cmake --preset msvc; cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
Expected: 全绿（132 + builtins 单测 + 新 fixtures）。

- [ ] **Step 5.3: DoD 复核（逐条）**

- [ ] 10 §1 表可现函数全过（占位项报错信息明确）——tier0/placeholders 单测 + fixtures
- [ ] `for x in <list/string/map/tuple/set/range>` 六源全过——`iterSixSources` + iter_protocol
- [ ] 推导式三式 + 多 for/if 与嵌套推导式输出正确，变量不泄漏——comprehension fixtures + compNoLeak
- [ ] `sorted(range(5).toList(), nil, true)`（位置实参形态）、zip strict、sum init 组合正确——单测 + fixtures
- [ ] `next` 耗尽 nil、for 停止；it.take/drop/map/filter 惰性链正确——iter_protocol + iterLazyNoOvershoot
- [ ] 00-overview §3 示例 `[x * x for x in range(1, 6)]` → `[1, 4, 9, 16, 25]`——overviewExample
- [ ] genexp_pre.ms 语义等价落地（T019 复核标注已在文件名 _pre 体现）

- [ ] **Step 5.4: 文档状态更新 + 最终提交**

`docs/tasks/T017-tier0-builtins-iteration.md`：头部"状态: 未开始"→"已完成（2026-11-01）"，DoD 清单全勾并注明偏差（位置实参、set 插入序打印）。`docs/tasks/README.md`：T017 行 ⬜ → ✅。

```bash
git add -A
git commit -m "🧪 test(fixtures/builtin): tier0 builtins and iteration end-to-end cases"
git commit -m "📝 docs(tasks): mark T017 done"   # 与上一提交分开
```

---

## Self-Review 记录

- **覆盖核对**：10 §1 表 31 项 → print/len/cap/range/type/repr/str/int/bigint/float/bool/hash/abs/min/max/sum/sorted/reversed/enumerate/zip/iter/next/isinstance/callable/make/id/input/assert/all/race/WeakRef 全部落 Task 2（set 构造器随容器族保留）；迭代协议六源（Task 1）；iterator 方法 7 项（Task 3）；推导式三式 + 多 for/if + 嵌套 + 变量不泄漏（Task 4）；genexp（Task 4）；FORMAT_STRING 已端到端（fixtures 覆盖）；__name__/__version__（Task 2）；DoD 六条（Task 5 复核）。
- **类型一致性**：`msIterNext(vm, MsValue, MsValue*)`、`msIterFromValue(vm, MsValue, MsValue*)`、`msIterCollectList(vm, MsValue, MsValue*)`、`msVmCallNative(vm, f, argv, argc, out)`、`msValueAdd(vm, a, b, out)`、`msBuiltinLookup(vm, name, len, out)` 跨任务签名一致；`localOp/comprSlot/comprPush/comprPop/comprResolveSlot/storeComprTarget/compileComprBody/compileCompr/scanMaxLocalSlot` 命名唯一。
- **零新指令**：全部复用既有 opcode（GET_ITER/FOR_ITER/CALL_METHOD/SET_INDEX/LOAD|STORE_LOCAL/BUILD_*），.bc 快照理论不受扰（Task 4.8 显式回归）。
- **已知风险**：sorted 键物化内存 O(n)；map 迭代快照语义与 list 版本号语义差异由 fixtures 分别锁定（任务书 §6）。





