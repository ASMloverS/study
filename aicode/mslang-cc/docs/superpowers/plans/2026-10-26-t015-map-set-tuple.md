# T015 map / set / tuple Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement MsObjMap (open addressing + insertion-order seq chain), MsObjSet (map with placeholder values), MsObjTuple (FAM fixed-length + lazy hash cache); wire BUILD_MAP/BUILD_TUPLE/BUILD_SET literals, map indexing, `in`, set operators `| & - ^`, method families, and switch VM globals to a MsObjMap.

**Architecture:** Three new object types in `src/obj/` reusing the T013/T014 patterns (msObjAlloc + gcSetMarkFn/gcSetFreeFn registration in msVmInit, D6/D10 GC stack-root discipline). A shared value-hash function `msValueHash` in obj_map.c normalizes the numeric tower (int/integral-float share one hash domain) and caches tuple hashes. Method families follow list_methods.c (MsMethodRef tables wired into methodRefLookup in vm.c).

**Tech Stack:** C11, CMake (msvc preset), ctest golden fixtures (`tests/fixtures/container/`).

**Spec:** `docs/tasks/T015-map-set-tuple.md`; design docs `02-types.md` §5.2-§5.4/§6/§7, `10-builtins.md` §4, `22-object-model.md` §4, `03-expressions.md` §5, `04-statements.md` §4 (destructuring for/in).

**Worktree:** `C:\Workspace\Repositories\study\aicode\mslang-cc\.worktrees\t015-map-set-tuple\aicode\mslang-cc` on branch `feature/t015-map-set-tuple`. All commands below run from that directory.

---

## Design decisions (from task doc + design docs)

- **Map:** capacity 2^n (initial 8), open addressing + linear probe; slot states empty/active/tombstone; load factor 0.75 counts live+tombstones (`(used+1)*4 > cap*3` triggers rehash; grow x2 when `live*2 >= cap`, else same-size tombstone purge). Key hash cached per slot. Insertion order kept by a doubly-linked slot-index chain (`seqPrev/seqNext`, head/tail in map); rehash rebuilds the chain in old order (walk old chain, re-probe into new array, relink sequentially). Remove marks tombstone and unlinks from chain.
- **Numeric tower keys:** `1 == 1.0` are one key (02 §6). `msValueHash`: i64 → mix64(i); float integral-and-in-i64-range → mix64((i64)f) (covers -0.0 == 0); other floats → mix64(bitcast). NaN keys: msValueEqual(NaN,NaN) is false (IEEE) so probes never match → each insert adds a key (02 §6). bool never equals int (02 §2) — hash domains may collide, equality gates.
- **Tuple:** FAM `MsValue items[]`, single allocation, items pre-filled nil before return (GC-safe); `hash`/`hashValid` lazy cache computed on first map/set use; composite = FNV-1a-64 fold over element hashes (order-sensitive). Unhashable element → tuple unhashable (no cache).
- **Set:** `MsObjSet { head; MsObjMap* map; }` — separate heap object (22 §2 lists both types). Constructor temporarily roots the set on the VM stack while allocating the inner map (D6). Placeholder value = `true`.
- **Equality (02 §6):** tuple element-wise deep; map same-count + per-key lookup with value equality; set same-count + membership. All depth-capped (100, same as list) against cycles via nested lists. `==` between different container kinds is false.
- **repr/str forms (same for both, containers use element repr):** map `{"a": 1, "b": 2}`, empty `{}`; set `{1, 2}`, empty `set()`; tuple `(1, "a")`, single `(1,)`, empty `()`. Depth cap renders `{...}` / `(...)`.
- **Errors:** `m[k]` missing → `KeyError: key not found`; `m.remove(k)`/`st.remove(x)` missing → KeyError; unhashable key (list/map/set/instance) → `TypeError: unhashable type: '<typename>'`; `m.get(k, d)` never raises (default nil). Set indexing → TypeError not indexable.
- **`in` (03 §5):** string = substring (both operands string); list/tuple = element scan; map = key lookup; set = membership. Unhashable probe key on map/set → TypeError.
- **Set ops:** `| & - ^` and union/intersect/diff/symmetricDiff produce new sets; result order = left operand order, then new elements from right (union), left order filtered (intersect/diff), left-only then right-only (symdiff).
- **Multi-var for-in** (`for k, v in m.items()`, 04 §4): compile target tuple as `UNPACK_SEQ n` + per-target store (right-to-left, nested tuples recurse); binder already annotates these as fresh bindings (binder.c declareIterTargets — no binder change).
- **Globals:** `vm->globals` becomes `MsObjMap*` (heap object). Keys = interned MsObjString via `msInternBytes` (PERMANENT, survives GC; miss-alloc is GC-safe: new string becomes permanent before any further allocation). LOAD/STORE/DEF_GLOBAL rewritten to map get/put. `markRoots` (gc.c) marks the globals map object itself.
- **`set()` constructor** (02 §5.4 `{}` is map; empty set needs `set()`) and minimal `type()` builtin (needed by task test table "{} vs set() type") added to builtins_bridge.c; `len()` extended to map/set/tuple (10 §1). `hash()` builtin stays T017.
- **Not in scope (later tasks):** map/set/tuple comprehension execution (AST_COMPREHENSION still unsupported), direct iteration `for x in m`/`for x in t` (GET_ITER stays TypeError; T017 iterator shell), bigint keys (T016), `__eq__/__hash__` instances (T021), chan len/cap (T030), frozenset (not in 22 §2 table — skip entirely).

## File structure

- Create `src/obj/obj_map.h` / `src/obj/obj_map.c` — MsObjMap + `msValueHash` shared hash.
- Create `src/obj/obj_set.h` / `src/obj/obj_set.c` — MsObjSet wrapper + set-algebra helpers.
- Create `src/obj/obj_tuple.h` / `src/obj/obj_tuple.c` — MsObjTuple.
- Create `src/vm/map_methods.c`, `src/vm/set_methods.c`, `src/vm/tuple_methods.c` — method tables (10 §4).
- Create `tests/unit/test_obj_map.c`, `tests/unit/test_obj_tuple.c`.
- Create `tests/fixtures/container/{map_basic,set_ops,tuple,key_err,unhashable_key}.{ms,out,...}`.
- Modify `src/vm/vm.h` — globals field type, method-lookup decls, remove MsGlobalVar.
- Modify `src/vm/vm.c` — BUILD_MAP/TUPLE/SET, GET/SET_INDEX map/tuple, IN, set operators, UNPACK_SEQ tuple, globals map, mark/free registration, methodRefLookup wiring.
- Modify `src/vm/value.c` — type names, deep equality (tuple/map/set), repr forms.
- Modify `src/vm/builtins_bridge.c` — `set()`, `type()`, len() extension.
- Modify `src/gc/gc.c` — markRoots marks globals map.
- Modify `src/compiler/compiler.c` — AST_MAP/AST_SET/AST_TUPLE emission, for-in tuple target.
- Modify `CMakeLists.txt`, `tests/CMakeLists.txt` — sources + tests.
- Modify `docs/tasks/T015-map-set-tuple.md` (status/DoD) + `docs/tasks/README.md` (status column) at the end.

Build/verify commands (run in worktree project root):

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

---

### Task 1: MsObjTuple data plane + equality + repr

**Files:**
- Create: `src/obj/obj_tuple.h`, `src/obj/obj_tuple.c`
- Modify: `CMakeLists.txt` (add `target_sources(ms_obj PRIVATE src/obj/obj_tuple.c)`), `src/vm/vm.c` (register mark fn), `src/vm/value.c` (type name, equality branch, repr forms)
- Test: `tests/unit/test_obj_tuple.c`, `tests/CMakeLists.txt`

- [ ] **Step 1.1: Write failing tests** `tests/unit/test_obj_tuple.c` (copy runSrc bridge from test_obj_list.c lines 20-74 verbatim):

```c
// T015：tuple 数据面 + 相等/repr/hash 缓存单测（docs/tasks/T015 §5.1）
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
#include "obj/obj_tuple.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

// runSrc 桥：与 test_obj_list.c 相同（parse → bind → compile → run，捕获 stdout/错误槽）
typedef struct RunResult {
  char* out;
  bool compileOk;
  bool hasError;
  char err[240];
  int32_t errLine;
} RunResult;
// ...（照抄 test_obj_list.c 的 slurpTmp/runSrc，两文件各自独立编译）

TEST(tupleAllocItemsNil) {  // FAM 定长；分配即全 nil（GC 窗口安全）
  MsVM vm;
  msVmInit(&vm);
  msVmPush(&vm, msValueNil());  // 占位防优化；真实 root 在下
  MsObjTuple* t = msObjTupleNew(&vm, 3);
  EXPECT(ctx, t != NULL);
  msVmPush(&vm, msValueObj(&t->head));  // root
  EXPECT_EQ(ctx, 3, t->count);
  for (int32_t i = 0; i < 3; i++) {
    EXPECT(ctx, msIsNil(&t->items[i]));
  }
  t->items[0] = msValueInt(1);
  const MsValue* v = msObjTupleGet(t, -1);
  EXPECT(ctx, v != NULL && msIsNil(v));
  EXPECT(ctx, msObjTupleGet(t, 3) == NULL);
  msVmFree(&vm);
}

TEST(tupleEqualRepr) {  // (1,"a") == (1,"a") true；repr 形态
  MsVM vm;
  msVmInit(&vm);
  MsObjTuple* a = msObjTupleNew(&vm, 2);
  msVmPush(&vm, msValueObj(&a->head));
  MsObjTuple* b = msObjTupleNew(&vm, 2);
  msVmPush(&vm, msValueObj(&b->head));
  MsObjString* s = msObjStringNew(&vm, "a", 1);
  msVmPush(&vm, msValueObj(&s->head));
  a->items[0] = msValueInt(1); a->items[1] = msValueObj(&s->head);
  b->items[0] = msValueInt(1); b->items[1] = msValueObj(&s->head);
  MsValue va = msValueObj(&a->head), vb = msValueObj(&b->head);
  EXPECT(ctx, msValueEqual(&va, &vb));
  MsValue one = msValueInt(1);
  EXPECT(ctx, !msValueEqual(&va, &one));
  MsObjString* ra = msValueReprForm(&vm, &va);
  EXPECT(ctx, ra != NULL);
  EXPECT(ctx, ra != NULL && ra->byteLen == 8 && memcmp(ra->bytes, "(1, \"a\")", 8) == 0);
  msVmFree(&vm);
}
```

Main function follows the test_obj_list.c pattern (`main` runs all TESTs, returns ctx.failures).

- [ ] **Step 1.2: Register test** in `tests/CMakeLists.txt`:

```cmake
ms_add_test(obj_tuple ms_obj ms_gc ms_vm ms_compiler ms_binder)
```

- [ ] **Step 1.3: Run** `cmake --build build --config Debug` → link error (obj_tuple.h missing). Expected FAIL (file not found).

- [ ] **Step 1.4: Implement** `src/obj/obj_tuple.h`:

```c
// MsObjTuple（T015，22 §4）：FAM 定长不可变序列 + 惰性 hash 缓存
#ifndef INCLUDE_MS_OBJ_OBJ_TUPLE_H_
#define INCLUDE_MS_OBJ_OBJ_TUPLE_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjTuple {
  MsObj head;
  ms_u64 hash;   // 惰性缓存：首次入 map/set 时计算（obj_map.c msValueHash 写入）
  bool hashValid;
  int32_t count;
  MsValue items[];  // FAM；创建即全 nil，调用方覆写
} MsObjTuple;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 创建 count 元组（items 全 nil——分配点 GC 安全）；失败 NULL
MsObjTuple* msObjTupleNew(MsVM* vm, int32_t count);
// 索引读取（负索引换算；越界 NULL）
const MsValue* msObjTupleGet(const MsObjTuple* t, ms_i64 i);
// children 标记（gcSetMarkFn 注册转发）
void msObjTupleMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsTuple(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_TUPLE;
}

#endif  // INCLUDE_MS_OBJ_OBJ_TUPLE_H_
```

`src/obj/obj_tuple.c`:

```c
// MsObjTuple 数据面（T015，22 §4）：FAM 单块分配；markChildren 遍历元素
#include "obj/obj_tuple.h"

#include <string.h>

#include "gc/gc.h"

MsObjTuple* msObjTupleNew(MsVM* vm, int32_t count) {
  if (count < 0 || (size_t)count > (SIZE_MAX - sizeof(MsObjTuple)) / sizeof(MsValue)) {
    return NULL;
  }
  MsObjTuple* t = (MsObjTuple*)msObjAlloc(vm, MS_OBJ_TUPLE, sizeof(MsObjTuple) + (size_t)count * sizeof(MsValue));
  if (t == NULL) {
    return NULL;
  }
  t->hash = 0;
  t->hashValid = false;
  t->count = count;
  for (int32_t i = 0; i < count; i++) {
    t->items[i] = msValueNil();  // 分配点后即达 GC 安全态（调用方覆写前可见全 nil）
  }
  return t;
}

const MsValue* msObjTupleGet(const MsObjTuple* t, ms_i64 i) {
  if (i < 0) {
    i += t->count;
  }
  if (i < 0 || i >= t->count) {
    return NULL;
  }
  return &t->items[i];
}

void msObjTupleMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjTuple* t = (MsObjTuple*)obj;
  for (int32_t i = 0; i < t->count; i++) {
    gcMarkValue(vm, t->items[i]);
  }
}
```

- [ ] **Step 1.5: Wire in** — `CMakeLists.txt` after the obj_list line: `target_sources(ms_obj PRIVATE src/obj/obj_tuple.c)`; `src/vm/vm.c` msVmInit after list registration: `gcSetMarkFn(MS_OBJ_TUPLE, msObjTupleMarkChildren);  // T015：FAM 元素（单块分配无 freeFn）` + include `obj/obj_tuple.h`.

- [ ] **Step 1.6: value.c** — type name: in `msValueTypeName` MS_VAL_OBJ switch add `case MS_OBJ_TUPLE: return "tuple";`; equality: rename existing statics to depth-family (see Task 2 for map/set — this task only adds tuple):

```c
#define MS_EQ_MAX_DEPTH 100  // 深嵌套/互引环截断（保守判不等；__eq__ 协议归 T021）

static bool valueEqualDepth(const MsValue* a, const MsValue* b, int32_t depth);

static bool tupleEqualDepth(const MsValue* a, const MsValue* b, int32_t depth) {
  if (a->as.obj == b->as.obj) {
    return true;
  }
  const MsObjTuple* ta = (const MsObjTuple*)a->as.obj;
  const MsObjTuple* tb = (const MsObjTuple*)b->as.obj;
  if (ta->count != tb->count) {
    return false;
  }
  if (depth >= MS_EQ_MAX_DEPTH) {
    return false;
  }
  for (int32_t i = 0; i < ta->count; i++) {
    if (!valueEqualDepth(&ta->items[i], &tb->items[i], depth + 1)) {
      return false;
    }
  }
  return true;
}
```

Restructure `msValueEqual` → `static bool valueEqualDepth(a, b, depth)` where listEqualDepth/list branch calls recurse with depth+1 (replace `MS_LIST_EQ_MAX_DEPTH` with `MS_EQ_MAX_DEPTH`); public `msValueEqual(a,b) { return valueEqualDepth(a, b, 0); }`. Add in the MS_VAL_OBJ section: `if (a->as.obj->type == MS_OBJ_TUPLE && b->as.obj->type == MS_OBJ_TUPLE) { return tupleEqualDepth(a, b, depth); }`.

- [ ] **Step 1.7: value.c repr** — generalize `listForm` into `seqForm(vm, v, depth, open, close, trailingComma)` (list passes `"["`, `"]"`, false; tuple passes `"("`, `")"`, count==1); add tuple dispatch in `strFormDepth`/`reprFormDepth` (both render element repr). Tuple depth-cap renders `(...)`.

- [ ] **Step 1.8: Build + run** `ctest --test-dir build -C Debug -R obj_tuple --output-on-failure` → PASS. Full suite green.

- [ ] **Step 1.9: Commit** `✨ feat(obj): tuple object with FAM storage, deep equality and repr form`

---

### Task 2: msValueHash + MsObjMap data plane

**Files:**
- Create: `src/obj/obj_map.h`, `src/obj/obj_map.c`
- Modify: `CMakeLists.txt`, `src/vm/vm.c` (mark/free registration), `src/vm/value.c` (type name "map", map deep equality, map repr)
- Test: `tests/unit/test_obj_map.c`, `tests/CMakeLists.txt`

- [ ] **Step 2.1: Write failing tests** `tests/unit/test_obj_map.c` (same runSrc bridge as test_obj_list.c). Key cases (task doc §5.1 table):

```c
TEST(mapInsertionOrder) {  // put b,a,c 删 a 再 put a → 遍历 b,c,a
  MsVM vm; msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue kb = msValueObj(&msInternBytes(&vm, "b", 1)->head);  // intern：键串永久存活
  MsValue ka = msValueObj(&msInternBytes(&vm, "a", 1)->head);
  MsValue kc = msValueObj(&msInternBytes(&vm, "c", 1)->head);
  msVmPush(&vm, kb); msVmPush(&vm, ka); msVmPush(&vm, kc);  // root
  EXPECT(ctx, msObjMapPut(&vm, m, &kb, msValueInt(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &ka, msValueInt(2)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &kc, msValueInt(3)) == MS_MAP_OK);
  MsValue old;
  EXPECT(ctx, msObjMapRemove(m, &ka, &old) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &ka, msValueInt(4)) == MS_MAP_OK);
  const char* order = "bca";
  ms_i64 it = msObjMapFirst(m);
  for (int32_t i = 0; i < 3; i++) {
    EXPECT(ctx, it >= 0);
    const MsValue* k = msObjMapKeyAt(m, it);
    EXPECT(ctx, ((const MsObjString*)k->as.obj)->bytes[0] == order[i]);
    it = msObjMapNext(m, it);
  }
  EXPECT_EQ(ctx, -1, it);
  EXPECT_EQ(ctx, 3, m->liveCount);
  msVmFree(&vm);
}

TEST(mapNumericTowerKeys) {  // m[1]=1; m[1.0]=2 → 单键值 2；1 == 1.0 键等价
  MsVM vm; msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue k1 = msValueInt(1), k1f = msValueFloat(1.0);
  EXPECT(ctx, msObjMapPut(&vm, m, &k1, msValueInt(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &k1f, msValueInt(2)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 1, m->liveCount);
  MsValue out;
  EXPECT(ctx, msObjMapGet(m, &k1, &out) == MS_MAP_OK && out.as.i == 2);
  msVmFree(&vm);
}

TEST(mapUnhashableKey) {  // list 作键 → MS_MAP_UNHASHABLE；bool/int 键区分
  MsVM vm; msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&m->head));
  MsObjList* l = msObjListNew(&vm);
  msVmPush(&vm, msValueObj(&l->head));
  MsValue kl = msValueObj(&l->head);
  EXPECT(ctx, msObjMapPut(&vm, m, &kl, msValueInt(1)) == MS_MAP_UNHASHABLE);
  EXPECT(ctx, msObjMapGet(m, &kl, NULL) == MS_MAP_UNHASHABLE);
  MsValue bt = msValueBool(true), one = msValueInt(1);
  EXPECT(ctx, msObjMapPut(&vm, m, &bt, msValueInt(9)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &one, msValueInt(8)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 2, m->liveCount);  // true 与 1 不同键（02 §2）
  msVmFree(&vm);
}

TEST(mapRehashStress) {  // 1000 增删密布：全键值保留、序不变（DoD 墓碑/再哈希）
  MsVM vm; msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&m->head));
  for (int32_t i = 0; i < 1000; i++) {  // 偶数键先入
    MsValue k = msValueInt(i * 2);
    EXPECT(ctx, msObjMapPut(&vm, m, &k, msValueInt(i * 2 + 1)) == MS_MAP_OK);
  }
  for (int32_t i = 0; i < 500; i++) {  // 删一半（奇数槽位墓碑化）
    MsValue k = msValueInt(i * 2);
    EXPECT(ctx, msObjMapRemove(m, &k, NULL) == MS_MAP_OK);
  }
  for (int32_t i = 0; i < 500; i++) {  // 奇数键补入
    MsValue k = msValueInt(i * 2 + 1);
    EXPECT(ctx, msObjMapPut(&vm, m, &k, msValueInt(i * 2 + 10000)) == MS_MAP_OK);
  }
  EXPECT_EQ(ctx, 1000, m->liveCount);
  for (int32_t i = 0; i < 500; i++) {  // 值抽查
    MsValue k = msValueInt(i * 2 + 1), out;
    EXPECT(ctx, msObjMapGet(m, &k, &out) == MS_MAP_OK && out.as.i == i * 2 + 10000);
  }
  ms_i64 it = msObjMapFirst(m);  // 序：1000 个奇数键（i*2+1 升序）
  for (int32_t i = 0; i < 1000; i++) {
    EXPECT(ctx, it >= 0);
    EXPECT(ctx, msObjMapKeyAt(m, it)->as.i == i * 2 + 1);
    it = msObjMapNext(m, it);
  }
  msVmFree(&vm);
}

TEST(mapNaNKeyIEEE) {  // NaN != NaN → 每次插入新键（02 §6 IEEE）
  MsVM vm; msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&m->head));
  MsValue kn = msValueFloat(0.0 / 0.0);
  EXPECT(ctx, msObjMapPut(&vm, m, &kn, msValueInt(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, m, &kn, msValueInt(2)) == MS_MAP_OK);
  EXPECT_EQ(ctx, 2, m->liveCount);  // 不等价 → 两键
  msVmFree(&vm);
}

TEST(mapDeepEqualAndRepr) {  // 02 §6 逐键值；repr 插入序
  MsVM vm; msVmInit(&vm);
  MsObjMap* a = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&a->head));
  MsObjMap* b = msObjMapNew(&vm);
  msVmPush(&vm, msValueObj(&b->head));
  MsValue ka = msValueObj(&msInternBytes(&vm, "a", 1)->head);
  MsValue kb = msValueObj(&msInternBytes(&vm, "b", 1)->head);
  // 插入序不同（a: a,b  b: b,a）但内容同 → 深相等
  EXPECT(ctx, msObjMapPut(&vm, a, &ka, msValueInt(1)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, a, &kb, msValueInt(2)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, b, &kb, msValueInt(2)) == MS_MAP_OK);
  EXPECT(ctx, msObjMapPut(&vm, b, &ka, msValueInt(1)) == MS_MAP_OK);
  MsValue va = msValueObj(&a->head), vb = msValueObj(&b->head);
  EXPECT(ctx, msValueEqual(&va, &vb));
  MsObjString* r = msValueReprForm(&vm, &va);
  EXPECT(ctx, r != NULL && r->byteLen == 12 && memcmp(r->bytes, "{\"a\": 1, \"b\": 2}", 14) == 0);
  msVmFree(&vm);
}
```

Fix the expected repr length in the last assertion while writing (it is 14, fix `r->byteLen == 14`). Register: `ms_add_test(obj_map ms_obj ms_gc ms_vm ms_compiler ms_binder)`.

- [ ] **Step 2.2: Run** → FAIL (headers missing / link errors).

- [ ] **Step 2.3: Implement** `src/obj/obj_map.h`:

```c
// MsObjMap（T015，22 §4）：开放寻址线性探测 + 墓碑 + 插入序 seq 链 + 键 hash 缓存
// 共享值 hash（msValueHash）：数值塔统一（02 §7 hash(1)==hash(1.0)）
#ifndef INCLUDE_MS_OBJ_OBJ_MAP_H_
#define INCLUDE_MS_OBJ_OBJ_MAP_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef enum MsMapSlotState {
  MS_MSS_EMPTY = 0,
  MS_MSS_ACTIVE = 1,
  MS_MSS_TOMBSTONE = 2,
} MsMapSlotState;

typedef struct MsMapSlot {
  MsValue key;
  MsValue value;
  ms_u64 hash;             // 键 hash 缓存（22 §4）
  ms_i64 seqPrev, seqNext; // 插入序双向链（slot 下标；-1 端哨兵）
  ms_u8 state;             // MsMapSlotState
} MsMapSlot;

typedef struct MsObjMap {
  MsObj head;
  MsMapSlot* slots;  // cap 2^n（初始 8；负载 0.75 再哈希）
  int32_t cap;
  int32_t liveCount;  // active 数
  int32_t usedCount;  // active + tombstone（探测/扩容依据）
  ms_i64 seqHead, seqTail;
} MsObjMap;

typedef enum MsMapOp {
  MS_MAP_OK = 0,
  MS_MAP_NOENT,       // get/remove 未命中
  MS_MAP_UNHASHABLE,  // 键不可 hash（调用方翻译 TypeError）
  MS_MAP_OOM,
} MsMapOp;

typedef enum MsHashStatus {
  MS_HASH_OK = 0,
  MS_HASH_UNHASHABLE,  // list/map/set/实例等（02 §7；bigint 键 T016、__hash__ T021）
} MsHashStatus;

// 值 hash（nil/bool/int/float/string/tuple；tuple 首次计算后写缓存）
MsHashStatus msValueHash(const MsValue* v, ms_u64* out);

MsObjMap* msObjMapNew(MsVM* vm);
MsMapOp msObjMapGet(const MsObjMap* m, const MsValue* key, MsValue* outValue);  // outValue 可 NULL
MsMapOp msObjMapPut(MsVM* vm, MsObjMap* m, const MsValue* key, const MsValue* value);
MsMapOp msObjMapRemove(MsObjMap* m, const MsValue* key, MsValue* outOld);  // outOld 可 NULL
void msObjMapClear(MsObjMap* m);
// 插入序遍历：First 返回首 slot 或 -1；Next 返回后继或 -1（slot 失效勿跨变更使用）
ms_i64 msObjMapFirst(const MsObjMap* m);
ms_i64 msObjMapNext(const MsObjMap* m, ms_i64 slot);
const MsValue* msObjMapKeyAt(const MsObjMap* m, ms_i64 slot);
const MsValue* msObjMapValueAt(const MsObjMap* m, ms_i64 slot);
void msObjMapMarkChildren(MsVM* vm, MsObj* obj);
void msObjMapFreeData(MsObj* obj);

static inline bool msIsMap(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_MAP;
}

#endif  // INCLUDE_MS_OBJ_OBJ_MAP_H_
```

`src/obj/obj_map.c` core (complete file; ~230 lines):

```c
// MsObjMap 数据面（T015，22 §4）：线性探测/墓碑/seq 链/再哈希；msValueHash 数值塔统一
// GC 纪律：msObjMapNew 为触发点（调用方 root 后再 put）；put 内再哈希仅 malloc 非 GC 路径
#include "obj/obj_map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"

#define MS_MAP_INIT_CAP 8

static ms_u64 mix64(ms_u64 x) {  // splitmix64 finalizer（int/float 域混叠充分）
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

// 数值塔归一（02 §6/§7）：[-2^63, 2^63) 整值与同值 int 同域（含 -2^63 ↔ INT64_MIN、-0.0 ↔ 0）；
// NaN/inf/±2^63 及以上走位型（不等于任何 i64，仅自一致；相等门控在 msValueEqual）
static ms_u64 hashNumeric(double f) {
  if (f >= -9223372036854775808.0 && f < 9223372036854775808.0 && f == floor(f)) {
    return mix64((ms_u64)(ms_i64)f);
  }
  ms_u64 bits;
  memcpy(&bits, &f, sizeof(bits));
  return mix64(bits);
}

MsHashStatus msValueHash(const MsValue* v, ms_u64* out) {
  switch (v->kind) {
    case MS_VAL_NIL:
      *out = mix64(0x9E3779B97F4A7C15ULL);
      return MS_HASH_OK;
    case MS_VAL_BOOL:
      *out = mix64(v->as.b ? 0x2 : 0x1);  // 与 int 域分离（bool 不与 1/0 等价，02 §2）
      return MS_HASH_OK;
    case MS_VAL_I64:
      *out = mix64((ms_u64)v->as.i);
      return MS_HASH_OK;
    case MS_VAL_F64:
      *out = hashNumeric(v->as.f);
      return MS_HASH_OK;
    case MS_VAL_OBJ:
      switch (v->as.obj->type) {
        case MS_OBJ_STRING:
          *out = ((const MsObjString*)v->as.obj)->hash64;  // FNV-1a-64（创建即定）
          return MS_HASH_OK;
        case MS_OBJ_TUPLE: {  // 惰性缓存（22 §4）：FNV-1a-64 复合元素 hash（序敏感）
          MsObjTuple* t = (MsObjTuple*)v->as.obj;
          if (t->hashValid) {
            *out = t->hash;
            return MS_HASH_OK;
          }
          ms_u64 h = 0xcbf29ce484222325ULL;
          for (int32_t i = 0; i < t->count; i++) {
            ms_u64 eh = 0;
            if (msValueHash(&t->items[i], &eh) != MS_HASH_OK) {
              return MS_HASH_UNHASHABLE;  // 元素不可 hash → 整体不可 hash（不缓存）
            }
            h = (h ^ eh) * 0x100000001b3ULL;
          }
          t->hash = h;
          t->hashValid = true;
          *out = h;
          return MS_HASH_OK;
        }
        default:
          return MS_HASH_UNHASHABLE;  // list/map/set/实例等（bigint T016、function T018+）
      }
    default:
      return MS_HASH_UNHASHABLE;
  }
}
```

Then allocation/probe/put/remove/rehash/iteration/mark/free:

```c
MsObjMap* msObjMapNew(MsVM* vm) {
  MsObjMap* m = (MsObjMap*)msObjAlloc(vm, MS_OBJ_MAP, sizeof(MsObjMap));
  if (m == NULL) {
    return NULL;
  }
  m->slots = NULL;  // 惰性建表（首 put 时 calloc）
  m->cap = 0;
  m->liveCount = m->usedCount = 0;
  m->seqHead = m->seqTail = -1;
  return m;
}

// 探测：返回命中 active 槽；未命中 *outInsert = 首个空/墓碑槽（复用墓碑），*outFound = false
static ms_i64 mapProbe(const MsObjMap* m, ms_u64 hash, const MsValue* key, ms_i64* outInsert, bool* outFound) {
  ms_i64 mask = m->cap - 1;
  ms_i64 idx = (ms_i64)(hash & (ms_u64)mask);
  ms_i64 insert = -1;
  for (ms_i64 n = 0; n < m->cap; n++) {
    MsMapSlot* s = &m->slots[idx];
    if (s->state == MS_MSS_EMPTY) {
      *outFound = false;
      *outInsert = insert >= 0 ? insert : idx;  // 优先复用墓碑
      return -1;
    }
    if (s->state == MS_MSS_TOMBSTONE) {
      if (insert < 0) {
        insert = idx;
      }
    } else if (s->hash == hash && msValueEqual(key, &s->key)) {
      *outFound = true;
      return idx;
    }
    idx = (idx + 1) & mask;
  }
  *outFound = false;  // 满表（无空槽无墓碑）——再哈希后重试
  *outInsert = -1;
  return -1;
}

// 再哈希：沿旧 seq 链序重探入新表并重建链（插入序保持；墓碑丢弃）
static bool mapRehash(MsObjMap* m, int32_t newCap) {
  ms_i64 oldHead = m->seqHead;
  MsMapSlot* old = m->slots;
  MsMapSlot* slots = calloc((size_t)newCap, sizeof(MsMapSlot));
  if (slots == NULL) {
    return false;
  }
  m->slots = slots;
  m->cap = newCap;
  m->liveCount = m->usedCount = 0;
  m->seqHead = m->seqTail = -1;
  for (ms_i64 cur = oldHead; cur >= 0; cur = old[cur].seqNext) {
    ms_i64 idx = (ms_i64)(old[cur].hash & (ms_u64)(newCap - 1));
    while (slots[idx].state == MS_MSS_ACTIVE) {
      idx = (idx + 1) & (newCap - 1);
    }
    slots[idx] = old[cur];  // 拷贝 key/value/hash/state(ACTIVE)
    slots[idx].seqPrev = m->seqTail;
    slots[idx].seqNext = -1;
    if (m->seqTail >= 0) {
      slots[m->seqTail].seqNext = idx;
    } else {
      m->seqHead = idx;
    }
    m->seqTail = idx;
    m->liveCount++;
    m->usedCount++;
  }
  free(old);
  return true;
}

static bool mapEnsureRoom(MsVM* vm, MsObjMap* m) {
  (void)vm;  // 再哈希为 malloc 路径（非 GC 触发点）
  if (m->cap == 0) {  // 惰性建表
    m->slots = calloc(MS_MAP_INIT_CAP, sizeof(MsMapSlot));
    if (m->slots == NULL) {
      return false;
    }
    m->cap = MS_MAP_INIT_CAP;
    return true;
  }
  if ((m->usedCount + 1) * 4 > m->cap * 3) {  // 负载 0.75（含墓碑）
    int32_t newCap = m->liveCount * 2 >= m->cap ? m->cap * 2 : m->cap;  // 密集倍容/墓碑净化
    return mapRehash(m, newCap);
  }
  return true;
}

MsMapOp msObjMapPut(MsVM* vm, MsObjMap* m, const MsValue* key, const MsValue* value) {
  ms_u64 hash = 0;
  if (msValueHash(key, &hash) != MS_HASH_OK) {
    return MS_MAP_UNHASHABLE;
  }
  if (!mapEnsureRoom(vm, m)) {
    return MS_MAP_OOM;
  }
  for (;;) {
    ms_i64 insert = -1;
    bool found = false;
    ms_i64 hit = mapProbe(m, hash, key, &insert, &found);
    if (found) {  // 既有键：改值不动序
      m->slots[hit].value = *value;
      return MS_MAP_OK;
    }
    if (insert >= 0) {
      bool reuseTomb = m->slots[insert].state == MS_MSS_TOMBSTONE;
      MsMapSlot* s = &m->slots[insert];
      s->key = *key;
      s->value = *value;
      s->hash = hash;
      s->state = MS_MSS_ACTIVE;
      s->seqPrev = m->seqTail;  // 序链尾接
      s->seqNext = -1;
      if (m->seqTail >= 0) {
        m->slots[m->seqTail].seqNext = insert;
      } else {
        m->seqHead = insert;
      }
      m->seqTail = insert;
      m->liveCount++;
      if (!reuseTomb) {
        m->usedCount++;  // 墓碑复用不增 used（原已计入）
      }
      return MS_MAP_OK;
    }
    if (!mapRehash(m, m->cap * 2)) {  // 满表（无空槽无墓碑）：倍容重探
      return MS_MAP_OOM;
    }
  }
}
```

`msObjMapGet`: hash → cap==0 → `MS_MAP_NOENT`; probe; hit copies `*outValue`. `msObjMapRemove`: probe; hit → outOld, state=tombstone, unlink seq chain (head/tail fix), `liveCount--`; miss → `MS_MAP_NOENT`; unhashable → `MS_MAP_UNHASHABLE`. `msObjMapClear`: memset slots, counters 0, chain -1. `First/Next/KeyAt/ValueAt` trivial. `MarkChildren` walks seq chain marking key+value (avoids stale tombstone payloads). `FreeData`: `free(slots)`.

- [ ] **Step 2.4: Wire in** — CMake `target_sources(ms_obj PRIVATE src/obj/obj_map.c)`; vm.c: `gcSetMarkFn(MS_OBJ_MAP, msObjMapMarkChildren); gcSetFreeFn(MS_OBJ_MAP, msObjMapFreeData);` + include.

- [ ] **Step 2.5: value.c** — type name `case MS_OBJ_MAP: return "map";`; equality: `mapEqualDepth` (count equal → for each key via seq chain, `msObjMapGet` other, compare values with `valueEqualDepth(depth+1)`); repr: `mapForm` — `{"k": v, ...}` over seq chain (push each rendered key+value repr onto VM stack D10-style, then assemble; empty `{}`; depth cap `{...}`).

- [ ] **Step 2.6: Build + tests** → obj_map suite PASS; full suite green.

- [ ] **Step 2.7: Commit** `✨ feat(obj): map with open addressing, insertion order chain and numeric-tower hashing`

---

### Task 3: MsObjSet data plane + algebra

**Files:**
- Create: `src/obj/obj_set.h`, `src/obj/obj_set.c`
- Modify: `CMakeLists.txt`, `src/vm/vm.c` (mark registration), `src/vm/value.c` (type name "set", set equality, set repr)
- Test: extend `tests/unit/test_obj_map.c`

- [ ] **Step 3.1: Failing tests** (append to test_obj_map.c):

```c
TEST(setBasicsAndOrder) {  // add 去重、has、remove；插入序遍历
  MsVM vm; msVmInit(&vm);
  MsObjSet* st = msObjSetNew(&vm);
  msVmPush(&vm, msValueObj(&st->head));  // root（构造器内部已临时 root，此处供后续操作）
  MsValue k1 = msValueInt(1), k2 = msValueInt(2), k1f = msValueFloat(1.0);
  EXPECT(ctx, msObjSetAdd(&vm, st, &k1) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, st, &k2) == MS_MAP_OK);
  EXPECT(ctx, msObjSetAdd(&vm, st, &k1f) == MS_MAP_OK);  // 1.0 与 1 同元素
  EXPECT(ctx, msObjSetHas(st, &k1f));
  EXPECT_EQ(ctx, 2, msObjSetCount(st));
  MsObjList* l = msObjSetToList(&vm, st);
  msVmPush(&vm, msValueObj(&l->head));
  EXPECT(ctx, l->count == 2 && l->items[0].as.i == 1 && l->items[1].as.i == 2);
  EXPECT(ctx, msObjSetRemove(st, &k1) == MS_MAP_OK);
  EXPECT(ctx, !msObjSetHas(st, &k1));
  EXPECT(ctx, msObjSetRemove(st, &k1) == MS_MAP_NOENT);
  msVmFree(&vm);
}

TEST(setAlgebra) {  // {1,2} | {2,3} → {1,2,3}；& → {2}；- → {1}；^ → {1,3}（任务 §5.1 表）
  MsVM vm; msVmInit(&vm);
  MsObjSet* a = msObjSetNew(&vm); msVmPush(&vm, msValueObj(&a->head));
  MsObjSet* b = msObjSetNew(&vm); msVmPush(&vm, msValueObj(&b->head));
  for (int32_t i = 1; i <= 2; i++) { MsValue k = msValueInt(i); EXPECT(ctx, msObjSetAdd(&vm, a, &k) == MS_MAP_OK); }
  for (int32_t i = 2; i <= 3; i++) { MsValue k = msValueInt(i); EXPECT(ctx, msObjSetAdd(&vm, b, &k) == MS_MAP_OK); }
  MsObjSet* u = msObjSetUnion(&vm, a, b); msVmPush(&vm, msValueObj(&u->head));
  MsObjSet* i2 = msObjSetIntersect(&vm, a, b); msVmPush(&vm, msValueObj(&i2->head));
  MsObjSet* d = msObjSetDiff(&vm, a, b); msVmPush(&vm, msValueObj(&d->head));
  MsObjSet* x = msObjSetSymDiff(&vm, a, b); msVmPush(&vm, msValueObj(&x->head));
  EXPECT(ctx, u != NULL && i2 != NULL && d != NULL && x != NULL);
  MsValue probe;
  EXPECT_EQ(ctx, 3, msObjSetCount(u));
  EXPECT_EQ(ctx, 1, msObjSetCount(i2) && msObjSetHas(i2, &(probe = msValueInt(2), &probe)));
  EXPECT_EQ(ctx, 1, msObjSetCount(d) && msObjSetHas(d, &(probe = msValueInt(1), &probe)));
  EXPECT_EQ(ctx, 2, msObjSetCount(x) && msObjSetHas(x, &(probe = msValueInt(1), &probe)) &&
                   msObjSetHas(x, &(probe = msValueInt(3), &probe)));
  MsValue va = msValueObj(&a->head), vu = msValueObj(&u->head);
  EXPECT(ctx, msValueEqual(&va, &vu) == false);  // 02 §6 深相等：{1,2} != {1,2,3}
  MsObjString* r = msValueReprForm(&vm, &va);
  EXPECT(ctx, r != NULL && r->byteLen == 5 && memcmp(r->bytes, "{1, 2}", 6) == 0);  // len 6
  msVmFree(&vm);
}
```

(Fix literal lengths when writing: `"{1, 2}"` length 6.)

- [ ] **Step 3.2: Implement** `src/obj/obj_set.h`:

```c
// MsObjSet（T015，22 §4）：map 特例（值占位 true）；运算族返回新 set
#ifndef INCLUDE_MS_OBJ_OBJ_SET_H_
#define INCLUDE_MS_OBJ_OBJ_SET_H_

#include "obj/obj_map.h"

typedef struct MsObjSet {
  MsObj head;
  MsObjMap* map;  // 值恒占位 true
} MsObjSet;

// 构造：分配 set 后借 VM 栈临时 root 再建内层 map（D6：两次 GC 触发点间不失根）
MsObjSet* msObjSetNew(MsVM* vm);
MsMapOp msObjSetAdd(MsVM* vm, MsObjSet* st, const MsValue* v);
bool msObjSetHas(const MsObjSet* st, const MsValue* v);       // 不可 hash 一律 false
MsMapOp msObjSetRemove(MsObjSet* st, const MsValue* v);
void msObjSetClear(MsObjSet* st);
int32_t msObjSetCount(const MsObjSet* st);
// toList/拷贝/运算族：分配点（结果内部先落 VM 栈 root）；OOM NULL；元素已 hashable 无 UNHASHABLE 路径
MsObjList* msObjSetToList(MsVM* vm, const MsObjSet* st);      // 插入序快照
MsObjSet* msObjSetCopy(MsVM* vm, const MsObjSet* st);
MsObjSet* msObjSetUnion(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetIntersect(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetSymDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
void msObjSetMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsSet(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_SET;
}

#endif  // INCLUDE_MS_OBJ_OBJ_SET_H_
```

`obj_set.c` implementation notes (complete, follow obj_list.c style):
- `msObjSetNew`: `set = msObjAlloc(MS_OBJ_SET)`; `if (set == NULL) return NULL;` then root via `msVmPush(vm, msValueObj(&set->head))` (on push failure `return NULL` — stack overflow slot set), `set->map = msObjMapNew(vm);` `MS_ASSERT(map != NULL);` pop; return. (MS_ASSERT per existing style — intern init uses it.)
- add/has/remove/count/clear delegate to `st->map` with placeholder `msValueBool(true)` (has: `msObjMapGet(...) == MS_MAP_OK`).
- `toList`: `msObjListNew` + reserve count + walk seq chain copying keys (list rooted by caller? list must be rooted before... copy loop has no allocation → safe to build then return; but `msObjListNew` itself may trigger GC → map (source) is rooted by caller, the fresh list pointer is in register and then filled without further allocation — same pattern as lstCopy. OK).
- `copy`: new set (self-rooting), walk source chain adding keys.
- `union`: copy of a, then walk b chain adding. `intersect`: new set, walk a chain, add if `msObjSetHas(b, k)`. `diff`: new set, walk a chain, add if NOT has(b,k). `symDiff`: diff(a,b) then walk b chain add if NOT has(a,k). Order notes documented in header.
- `msObjSetMarkChildren`: `gcMarkValue(vm, msValueObj(&set->map->head));`

- [ ] **Step 3.3: Wire** — CMake source, `gcSetMarkFn(MS_OBJ_SET, msObjSetMarkChildren);` (no freeFn — inner map is its own heap object). value.c: `case MS_OBJ_SET: return "set";`, `setEqualDepth` (count equal + membership via Has), set repr `{1, 2}` / empty `set()` (both str/repr; depth cap `{...}`).

- [ ] **Step 3.4: Build + tests** → PASS, full suite green.

- [ ] **Step 3.5: Commit** `✨ feat(obj): set as map specialization with algebra operations`

---

### Task 4: Compiler literals + BUILD_MAP/TUPLE/SET + multi-var for-in

**Files:**
- Modify: `src/compiler/compiler.c` (AST_MAP/AST_SET/AST_TUPLE, for-in tuple targets), `src/vm/vm.c` (three BUILD handlers, UNPACK_SEQ tuple, SET_HANDLER rows)
- Test: extend `tests/unit/test_obj_map.c` / `test_obj_tuple.c` with runSrc cases

- [ ] **Step 4.1: Failing tests** (runSrc-based; append):

```c
TEST(execLiterals) {  // {} 空 map / set() 区分、字面量构造（任务 §5.1 空 literal 行）
  RunResult r = runSrc(
      "var m = {\"a\": 1, \"b\": 2}\n"
      "m[\"c\"] = 3\n"
      "print(m)\n"
      "print(type({}))\n"
      "print(type(set()))\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "{\"a\": 1, \"b\": 2, \"c\": 3}\nmap\nset\n") == 0);
}
```

Note: `m["c"] = 3` (SET_INDEX map) and `type()` land in Task 6 — write this test in Task 6, or here without those lines. Split: this task asserts `print({"a": 1, "b": 2})`, `print({1, 2})`, `print((1, "a"))`, `print((1,))`, `print(())`; multi-var: `for k, v in [[1, 2], [3, 4]] { print(k + v) }` → `3\n7\n`; `var a, b = 1, 2` still works; tuple unpack `var t = (1, 2)\nvar x, y = t` → x=1 y=2 (UNPACK_SEQ tuple branch).

```c
TEST(execBuildTupleUnpack) {
  RunResult r = runSrc("var t = (1, \"a\")\nprint(t)\nvar x, y = t\nprint(x)\nprint(y)\nprint((1,))\nprint(())\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "(1, \"a\")\n1\n\"a\"\n(1,)\n()\n") == 0);
}

TEST(execForInMultiVar) {
  RunResult r = runSrc("for k, v in [[1, 2], [3, 4]] {\n  print(k + v)\n}\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "3\n7\n") == 0);
}
```

- [ ] **Step 4.2: compiler.c** — in `compileExpr` add three cases before default:

```c
case AST_MAP: {
  int32_t count = n->as.mapLit.count;
  if (count > 127) {  // BUILD_MAP u8 操作数 = 2*count
    compErr(fs, n, 1, "map literal has too many entries");
    return;  // 上层按表达式处理中断（与既有 compErr 发射面差异：本分支不发占位——保持栈平衡由诊断拒绝保证）
  }
  for (int32_t i = 0; i < count; i++) {
    compileExpr(fs, n->as.mapLit.keys[i]);
    compileExpr(fs, n->as.mapLit.values[i]);
  }
  emitOp(fs, OP_BUILD_MAP, n->line);
  emitByte(fs, (ms_u8)(count * 2));
  break;
}
case AST_SET: {
  int32_t count = n->as.list.count;
  if (count > 255) {
    compErr(fs, n, 1, "set literal has too many elements");
    return;
  }
  for (int32_t i = 0; i < count; i++) {
    compileExpr(fs, n->as.list.items[i]);
  }
  emitOp(fs, OP_BUILD_SET, n->line);
  emitByte(fs, (ms_u8)count);
  break;
}
case AST_TUPLE: {
  int32_t count = n->as.tuple.count;
  if (count > 255) {
    compErr(fs, n, 1, "tuple literal has too many elements");
    return;
  }
  for (int32_t i = 0; i < count; i++) {
    compileExpr(fs, n->as.tuple.items[i]);
  }
  emitOp(fs, OP_BUILD_TUPLE, n->line);
  emitByte(fs, (ms_u8)count);
  break;
}
```

Note: `compErr` + `return` without placeholder emission matches `compileTupleAssign`'s over-count path (compile aborts on error anyway).

- [ ] **Step 4.3: compiler.c for-in tuple target** — add helper + replace the error branch in `compileFor` (AST_FOR_IN):

```c
// for-in 目标落盘（binder 已按 fresh 声明标注）：IDENT 直存；TUPLE 先 UNPACK_SEQ 再逆序递归
static void storeIterTarget(FuncState* fs, AstNode* t) {
  if (t->kind == AST_TUPLE) {
    if (t->as.tuple.count > 255) {
      compErr(fs: t, 1, "too many iteration targets");
      return;
    }
    emitOp(fs, OP_UNPACK_SEQ, t->line);
    emitByte(fs, (ms_u8)t->as.tuple.count);
    for (int32_t i = t->as.tuple.count - 1; i >= 0; i--) {  // 逆序消费栈顶
      storeIterTarget(fs, t->as.tuple.items[i]);
    }
    return;
  }
  if (t->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_STORE_LOCAL, t->line);
    emitByte(fs, (ms_u8)t->bindSlot);
    return;
  }
  int32_t k = nameConst(fs, t->as.ident.name, t->as.ident.nameLen);
  emitConstOp(fs, OP_DEF_GLOBAL, k, t->line);
  emitConstOp(fs, OP_STORE_GLOBAL, k, t->line);
}
```

(Fix `compErr(fs: t, ...)` → `compErr(fs, t, ...)` when writing.) In compileFor, replace the `compErr(... "multi-variable iteration lands in T017")` branch with `storeIterTarget(fs, target);`.

- [ ] **Step 4.4: vm.c handlers** — register `SET_HANDLER(OP_BUILD_MAP); SET_HANDLER(OP_BUILD_TUPLE); SET_HANDLER(OP_BUILD_SET);` and implement (peek discipline identical to OP_BUILD_LIST):

```c
OPCODE(OP_BUILD_MAP) {  // <u8 2n>：k1 v1 … kn vn → map（重键后者胜，序保持首插位）
  int32_t n = code[ip++] / 2;
  MsObjMap* map = msObjMapNew(vm);
  if (map == NULL) { RUNTIME_ERROR("RuntimeError: out of memory"); }
  msVmPush(vm, msValueObj(&map->head));
  if (vm->hasError) { goto dispatchExit; }
  const MsValue* src = &vm->stack[vm->stackTop - 1 - 2 * n];
  for (int32_t i = 0; i < n; i++) {  // put 内再哈希为 malloc 路径：无 GC 窗口
    switch (msObjMapPut(vm, map, &src[2 * i], &src[2 * i + 1])) {
      case MS_MAP_UNHASHABLE:
        RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&src[2 * i]));
      case MS_MAP_OOM:
        RUNTIME_ERROR("RuntimeError: out of memory");
      default:
        break;
    }
  }
  vm->stack[vm->stackTop - 2 * n - 1] = vm->stack[vm->stackTop - 1];
  vm->stackTop -= 2 * n;
  NEXT();
}

OPCODE(OP_BUILD_SET) {  // <u8 n>：构造器自 root（两次分配间）；add 无 GC 窗口
  int32_t n = code[ip++];
  MsObjSet* set = msObjSetNew(vm);
  if (set == NULL) { RUNTIME_ERROR("RuntimeError: out of memory"); }
  msVmPush(vm, msValueObj(&set->head));
  if (vm->hasError) { goto dispatchExit; }
  const MsValue* src = &vm->stack[vm->stackTop - 1 - n];
  for (int32_t i = 0; i < n; i++) {
    switch (msObjSetAdd(vm, set, &src[i])) {
      case MS_MAP_UNHASHABLE:
        RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&src[i]));
      case MS_MAP_OOM:
        RUNTIME_ERROR("RuntimeError: out of memory");
      default:
        break;
    }
  }
  vm->stack[vm->stackTop - n - 1] = vm->stack[vm->stackTop - 1];
  vm->stackTop -= n;
  NEXT();
}

OPCODE(OP_BUILD_TUPLE) {  // <u8 n>：单块 FAM；items 预置 nil 后直填
  int32_t n = code[ip++];
  MsObjTuple* t = msObjTupleNew(vm, n);
  if (t == NULL) { RUNTIME_ERROR("RuntimeError: out of memory"); }
  msVmPush(vm, msValueObj(&t->head));
  if (vm->hasError) { goto dispatchExit; }
  const MsValue* src = &vm->stack[vm->stackTop - 1 - n];
  memcpy(t->items, src, sizeof(MsValue) * (size_t)n);
  vm->stack[vm->stackTop - n - 1] = vm->stack[vm->stackTop - 1];
  vm->stackTop -= n;
  NEXT();
}
```

UNPACK_SEQ: add tuple branch before the list-only error — `if (v.as.obj->type == MS_OBJ_TUPLE) { MsObjTuple* t = ...; if (t->count < n) RUNTIME_ERROR("ValueError: not enough values to unpack (expected %d, got %d)", n, t->count); vm->stackTop--; for (i) msVmPush(t->items[i]); NEXT(); }` (restructure the existing list check into a shared shape).

- [ ] **Step 4.5: Build + tests** → PASS (this task's tests only need BUILD + UNPACK + for-in; index/set-op/methods come next). Full suite green (OP_IN etc. untouched).

- [ ] **Step 4.6: Commit** `✨ feat(compiler): map/set/tuple literals and destructuring for-in targets`

---

### Task 5: VM index / in / set operators

**Files:**
- Modify: `src/vm/vm.c` (GET_INDEX map/tuple, SET_INDEX map, OP_IN, set `| & - ^` in BINARY ops, UNPACK done in Task 4)
- Test: extend `tests/unit/test_obj_map.c`

- [ ] **Step 5.1: Failing tests**:

```c
TEST(execMapIndexErrors) {  // 缺键 KeyError；list 键 TypeError（任务 §5.1）
  RunResult r = runSrc("var m = {\"a\": 1}\nprint(m[\"x\"])\n");
  EXPECT(ctx, r.compileOk && r.hasError);
  EXPECT(ctx, strstr(r.err, "KeyError") != NULL);
  RunResult r2 = runSrc("var m = {}\nm[[1]] = 1\n");
  EXPECT(ctx, r2.compileOk && r2.hasError);
  EXPECT(ctx, strstr(r2.err, "TypeError: unhashable type: 'list'") != NULL);
  RunResult r3 = runSrc("var m = {1: 9}\nm[1.0] = 8\nprint(m[1])\n");
  EXPECT(ctx, r3.compileOk && !r3.hasError && strcmp(r3.out, "8\n") == 0);  // 数值塔同键
}

TEST(execSetOperators) {
  RunResult r = runSrc(
      "var a = {1, 2}\n"
      "var b = {2, 3}\n"
      "print(a | b)\nprint(a & b)\nprint(a - b)\nprint(a ^ b)\n"
      "print(2 in a)\nprint(5 in a)\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "{1, 2, 3}\n{2}\n{1}\n{1, 3}\ntrue\nfalse\n") == 0);
}

TEST(execInContainers) {
  RunResult r = runSrc(
      "print(\"ell\" in \"hello\")\n"
      "print(2 in [1, 2])\n"
      "print(2 in (1, 2))\n"
      "print(\"k\" in {\"k\": 1})\n"
      "print(\"z\" in {\"k\": 1})\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "true\ntrue\ntrue\ntrue\nfalse\n") == 0);
}
```

- [ ] **Step 5.2: GET_INDEX** — after the list branch, before the final TypeError:

```c
if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_MAP) {  // 任意 hashable 键
  MsValue out;
  switch (msObjMapGet((MsObjMap*)obj.as.obj, &idx, &out)) {
    case MS_MAP_OK:
      vm->stackTop -= 2;
      msVmPush(vm, out);
      NEXT();
    case MS_MAP_NOENT:
      RUNTIME_ERROR("KeyError: key not found");
    default:
      RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&idx));
  }
}
if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_TUPLE) {
  if (idx.kind != MS_VAL_I64) {
    RUNTIME_ERROR("TypeError: tuple indices must be int");
  }
  const MsValue* item = msObjTupleGet((MsObjTuple*)obj.as.obj, idx.as.i);
  if (item == NULL) {
    RUNTIME_ERROR("IndexError: tuple index out of range");
  }
  vm->stackTop -= 2;
  msVmPush(vm, *item);
  NEXT();
}
// set：不可索引（成员判断走 in / has）
```

(keep the existing final `TypeError: '%s' is not indexable` for set/tuple-slice etc.)

- [ ] **Step 5.3: SET_INDEX** — map branch after pops is safe (put never allocates GC). In OP_SET_INDEX after the three pops, before string check:

```c
if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_MAP) {  // put 无 GC 分配窗口：局部引用安全
  switch (msObjMapPut(vm, (MsObjMap*)obj.as.obj, &idx, &val)) {
    case MS_MAP_UNHASHABLE:
      RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&idx));
    case MS_MAP_OOM:
      RUNTIME_ERROR("RuntimeError: out of memory");
    default:
      NEXT();
  }
}
```

- [ ] **Step 5.4: OP_IN** — replace trap with: pop b(container), pop a(probe); switch on b type:
  - string: a must be string (else `TypeError: 'in' on string expects a string operand`); byte-substring scan (local `findBytes`-style loop, h=b bytes, n=a bytes; empty needle true).
  - list: `msObjListIndexOf(...) >= 0`.
  - tuple: loop `msValueEqual`.
  - map: `msObjMapGet` OK → true; NOENT → false; UNHASHABLE → `TypeError: unhashable type: '%s'` (probe).
  - set: `msObjSetHas` (unhashable → TypeError same as map).
  - other: `TypeError: '%s' is not a container` — pick message `TypeError: 'in' not supported for '%s'`.
  - Push bool result.

- [ ] **Step 5.5: set operators** — extend OP_BINARY_OR / OP_BINARY_AND / OP_BINARY_XOR / OP_BINARY_SUB with set-set peek fast paths before the generic pop/arithBinary (mirroring the list+list pattern in OP_BINARY_ADD):

```c
if (vm->stack[stackTop-1] and [stackTop-2] are MS_OBJ_SET) {
  MsObjSet* r = <msObjSetUnion|Intersect|Diff|SymDiff>(vm, a, b);  // 操作数 peek 保 root
  if (r == NULL) { RUNTIME_ERROR("RuntimeError: out of memory"); }
  vm->stackTop -= 2;
  msVmPush(vm, msValueObj(&r->head));
  NEXT();
}
```

Add to OP_BINARY_SUB (after list*int check) for `-`, and OP_BINARY_OR/AND/XOR (before BINARY_ARITH_CASE macro uses). Mixed set/non-set falls through to arithBinary → its TypeError message (`unsupported operand types`) is correct.

- [ ] **Step 5.6: Build + tests** → PASS; full suite green.

- [ ] **Step 5.7: Commit** `✨ feat(vm): map/tuple indexing, KeyError paths, in operator and set algebra`

---

### Task 6: Method families + set()/type()/len() builtins

**Files:**
- Create: `src/vm/map_methods.c`, `src/vm/set_methods.c`, `src/vm/tuple_methods.c`
- Modify: `CMakeLists.txt` (target_sources ms_vm), `src/vm/vm.h` (3 lookup decls), `src/vm/vm.c` (methodRefLookup + includes), `src/vm/builtins_bridge.c` (set/type/len)
- Test: extend both unit test files

- [ ] **Step 6.1: Failing tests** (task doc §5.1 table rows: get 默认、keys/items、方法族全走查):

```c
TEST(execMapMethods) {
  RunResult r = runSrc(
      "var m = {\"a\": 1, \"b\": 2}\n"
      "print(m.get(\"x\", 7))\nprint(m.get(\"a\"))\nprint(m.has(\"b\"))\n"
      "print(m.keys())\nprint(m.values())\nprint(len(m))\n"
      "var n = m.copy()\nn.update({\"c\": 3})\nprint(n)\nprint(m.remove(\"a\"))\nprint(m)\n"
      "for k, v in m.items() { print(\"${k}=${v}\") }\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out,
      "7\n1\ntrue\n[\"a\", \"b\"]\n[1, 2]\n2\n"
      "{\"a\": 1, \"b\": 2, \"c\": 3}\n"
      "1\n{\"b\": 2}\nb=2\n") == 0);
}

TEST(execSetTupleMethods) {
  RunResult r = runSrc(
      "var s = set()\ns.add(1)\ns.add(1)\ns.add(2)\nprint(s)\nprint(s.toList())\n"
      "var u = s.union({3})\nprint(u.intersect({2, 3}))\nprint(u.diff({1}))\nprint(u.symmetricDiff({9}))\n"
      "var t = (1, \"a\", 1)\nprint(t.count(1))\nprint(t.indexOf(\"a\"))\nprint(t.contains(1))\nprint(t.toList())\n"
      "var m = {}\nm[(1, 2)] = \"pair\"\nprint(m[(1, 2)])\nprint((1, \"a\") == (1, \"a\"))\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out,
      "{1, 2}\n[1, 2]\n{2, 3}\n{2, 3}\n{1, 2, 9}\n"
      "2\n1\ntrue\n[1, \"a\", 1]\n"
      "pair\ntrue\n") == 0);
}

TEST(tupleHashCachedOnce) {  // DoD：两次 hash 值一致且仅计算一次（hashValid 计数断言）
  MsVM vm; msVmInit(&vm);
  MsObjTuple* t = msObjTupleNew(&vm, 2);
  msVmPush(&vm, msValueObj(&t->head));
  MsObjString* s = msObjStringNew(&vm, "x", 1);
  msVmPush(&vm, msValueObj(&s->head));
  t->items[0] = msValueInt(5);
  t->items[1] = msValueObj(&s->head);
  MsValue vt = msValueObj(&t->head);
  ms_u64 h1 = 0, h2 = 0;
  EXPECT(ctx, msValueHash(&vt, &h1) == MS_HASH_OK);
  EXPECT(ctx, t->hashValid);          // 首次计算即缓存
  EXPECT(ctx, msValueHash(&vt, &h2) == MS_HASH_OK);
  EXPECT(ctx, h1 == h2);              // 二次取缓存值
  EXPECT(ctx, t->hash == h1);
  EXPECT(ctx, msValueHash(&vt, &h2) == MS_HASH_OK && h2 == h1);
  msVmFree(&vm);
}
```

- [ ] **Step 6.2: map_methods.c** — follow list_methods.c exactly (methodError/recv helpers, static MsValue fns, `kMapMethods[]`, `msMapMethodLookup`). Methods: get(1-2 args; missing → default or nil), has, remove (missing → `KeyError: key not found`), clear, keys/values/items (snapshot lists via chain walk; items pushes 2-tuples onto VM stack then `msVmCollectStackToList` — D10), update (arg must be map; walk other chain put), copy (new map + puts), len. GC notes: receiver+args rooted by CALL_METHOD peek; `msObjListNew`/tuple allocs happen with receiver rooted; snapshot results pushed progressively.

- [ ] **Step 6.3: set_methods.c** — add/remove (missing → KeyError)/has/union/intersect/diff/symmetricDiff (arg must be set; delegate to obj_set helpers)/clear/copy/toList.

- [ ] **Step 6.4: tuple_methods.c** — count/indexOf/contains (element scan via msValueEqual)/toList.

- [ ] **Step 6.5: Wire** — vm.h declarations next to msListMethodLookup:

```c
// map/set/tuple 实例方法表（T015，10 §4）
bool msMapMethodLookup(const char* name, int32_t len, MsMethodRef* out);
bool msSetMethodLookup(const char* name, int32_t len, MsMethodRef* out);
bool msTupleMethodLookup(const char* name, int32_t len, MsMethodRef* out);
```

vm.c methodRefLookup adds three type checks; CMake adds three sources.

- [ ] **Step 6.6: builtins_bridge.c** — `nativeSet` (0 args → new set; internal push-root while inner map allocates; argc>0 → `ArgumentError: set expects 0 arguments (iterable forms land in T017)`); `nativeType` (1 arg → `msValueTypeName` wrapped via `msObjStringNew`); extend `nativeLen` with map/set (`liveCount` via msObjSetCount) / tuple (`count`); register names `set`, `type` in `msBuiltinLookup`.

- [ ] **Step 6.7: Build + tests** → PASS; full suite green.

- [ ] **Step 6.8: Commit** `✨ feat(vm): map/set/tuple method families, set() and type() builtins`

---

### Task 7: VM globals on MsObjMap

**Files:**
- Modify: `src/vm/vm.h` (field + remove MsGlobalVar), `src/vm/vm.c` (init/free/LOAD/STORE/DEF_GLOBAL, delete findGlobal/defGlobal), `src/gc/gc.c` (markRoots)
- Test: extend `tests/unit/test_obj_map.c`; existing gc/vm suites must stay green

- [ ] **Step 7.1: Failing test** (globals keepalive via map + basic load/store):

```c
TEST(globalsMapKeepalive) {  // 全局值经 map 承载仍为 GC root（对齐 test_gc_basic globalKeepalive）
  MsVM vm;
  msVmInit(&vm);
  const Chunk* keep = NULL;  // 借 chunk 常量不必要——直接构造：
  // def x = list；强制 GC；x 仍活
  // 简化：直接操作 API 层——put 后 gcCollectAll 再 get
  MsObjList* l = msObjListNew(&vm);
  msVmPush(&vm, msValueObj(&l->head));  // 暂存源
  MsObjString* k = msInternBytes(&vm, "gx", 2);
  MsValue kv = msValueObj(&k->head);
  EXPECT(ctx, msObjMapPut(&vm, vm.globals, &kv, msValueObj(&l->head)) == MS_MAP_OK);
  vm.stackTop = 0;  // 清栈：仅剩 globals root
  gcCollectAll(&vm);
  MsValue out;
  EXPECT(ctx, msObjMapGet(vm.globals, &kv, &out) == MS_MAP_OK);
  EXPECT(ctx, out.kind == MS_VAL_OBJ && out.as.obj->type == MS_OBJ_LIST);
  EXPECT(ctx, gcHeapObjCount(&vm) >= 2);  // map + list 至少
  msVmFree(&vm);
}
```

Also runSrc smoke: `var g = 41\ng = g + 1\nprint(g)` → `42` (covered by existing suites once field switches).

- [ ] **Step 7.2: vm.h** — delete `MsGlobalVar` struct; replace fields:

```c
struct MsObjMap;  // obj/obj_map.h；按指针持有避免头环
struct MsVM {
  ...
  struct MsObjMap* globals;  // 全局表（T015 起 map 承载；键 = intern 名）
  ...
```

- [ ] **Step 7.3: vm.c** — include obj_map.h; msVmInit: `vm->globals = msObjMapNew(vm); MS_ASSERT(vm->globals != NULL);`; msVmFree: remove `free(vm->globals)` (map object freed by gcHeapFree); delete findGlobal/defGlobal; new helpers:

```c
// 全局名 → intern 键（miss 建 PERMANENT：跨 GC 存活；分配窗口内新串在入表前不可达亦安全，
// 见 intern.c 消费式契约）
static MsValue globalKey(MsVM* vm, const char* name, int32_t len) {
  return msValueObj(&msInternBytes(vm, name, len)->head);
}
```

LOAD_GLOBAL: `MsValue key = globalKey(...); MsValue v; if (msObjMapGet(vm->globals, &key, &v) == MS_MAP_OK) { push; NEXT; }` then builtin fallback (unchanged), else undefined error. STORE_GLOBAL: get → miss → `RuntimeError: assignment to undefined name '%.*s'`; hit → `msObjMapPut` (overwrite). DEF_GLOBAL: peek TOS → `msObjMapPut`. (Keys are always hashable strings; put OOM → `RUNTIME_ERROR("RuntimeError: out of memory")`.)

- [ ] **Step 7.4: gc.c** — include `obj/obj_map.h`; markRoots replaces the array loop with:

```c
gcMarkValue(vm, msValueObj(&vm->globals->head));  // 全局表 map 对象自身（T015 map 承载）
```

- [ ] **Step 7.5: Build + full suite** → all green (test_vm_basic/test_gc_basic/globalKeepalive exercise globals through the new path).

- [ ] **Step 7.6: Commit** `✨ feat(vm): carry globals in MsObjMap with interned name keys`

---

### Task 8: Fixtures, GC stress, docs, final verification

**Files:**
- Create: `tests/fixtures/container/map_basic.{ms,out}`, `set_ops.{ms,out}`, `tuple.{ms,out}`, `key_err.{ms,out,err,code}`, `unhashable_key.{ms,out,err,code}`
- Modify: `docs/tasks/T015-map-set-tuple.md`, `docs/tasks/README.md`
- Test: extend `tests/unit/test_obj_map.c` with GC pressure test

- [ ] **Step 8.1: map_basic.ms** (task doc §5.2 verbatim):

```ms
var m = {"a": 1, "b": 2}
m["c"] = 3
print(m)
print(m.keys())
for k, v in m.items() {
  print("${k}=${v}")
}
```

map_basic.out:

```
{"a": 1, "b": 2, "c": 3}
["a", "b", "c"]
a=1
b=2
c=3
```

- [ ] **Step 8.2: set_ops.ms** (insertion order is deterministic — golden without sort):

```ms
var a = {1, 2}
var b = {2, 3}
print(a | b)
print(a & b)
print(a - b)
print(a ^ b)
print(a.union(b).toList())
print(a.toList())
var e = set()
e.add(7)
print(e)
print(e.has(7))
print(e.copy())
e.clear()
print(e)
```

set_ops.out:

```
{1, 2, 3}
{2}
{1}
{1, 3}
[1, 2, 3]
[1, 2]
{7}
true
{7}
set()
```

- [ ] **Step 8.3: tuple.ms**:

```ms
var unit = (1,)
var empty = ()
var t = (1, "a", 1)
print(unit)
print(empty)
print(t.count(1))
print(t.indexOf("a"))
print(t.contains("a"))
print(t.toList())
var m = {}
m[(1, 2)] = "pair"
print(m[(1, 2)])
print((1, "a") == (1, "a"))
print((1,) == (2,))
var x, y = (10, 20)
print(x + y)
for k, v in {1: "one"}.items() {
  print("${k}:${v}")
}
```

tuple.out:

```
(1,)
()
2
1
true
[1, "a", 1]
pair
true
false
30
1:one
```

- [ ] **Step 8.4: key_err.ms** (`KeyError`, exit 1) / **unhashable_key.ms** (list key, `TypeError: unhashable type: 'list'`, exit 1):

```ms
var m = {"a": 1}
print(m["x"])
```

key_err.out: empty file; key_err.err: `KeyError: key not found`; key_err.code: `1`.

```ms
var m = {}
m[[1]] = 1
```

unhashable_key.out: empty; unhashable_key.err: `TypeError: unhashable type: 'list'`; unhashable_key.code: `1`.

- [ ] **Step 8.5: GC pressure unit test** (extend test_obj_map.c; follow test_gc_basic allocGuardAutoTrigger `_putenv`/`setenv` pattern):

```c
TEST(gcPressureMapsSetsTuples) {  // DoD：GC 扩展 + 压力（墓碑/再哈希窗口下无失根/悬垂）
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=65536");
#else
  setenv("MS_GC_HEAP_MAX", "65536", 1);
#endif
  MsVM vm;
  msVmInit(&vm);
  for (int32_t round = 0; round < 200; round++) {
    MsObjMap* m = msObjMapNew(&vm);
    if (!msVmPush(&vm, msValueObj(&m->head))) { break; }
    for (int32_t i = 0; i < 20; i++) {
      MsValue k = msValueInt(i);
      EXPECT(ctx, msObjMapPut(&vm, m, &k, msValueInt(i)) == MS_MAP_OK);
    }
    MsObjSet* st = msObjSetNew(&vm);
    if (!msVmPush(&vm, msValueObj(&st->head))) { break; }
    MsValue probe = msValueInt(3);
    EXPECT(ctx, msObjSetAdd(&vm, st, &probe) == MS_MAP_OK);
    MsObjTuple* t = msObjTupleNew(&vm, 2);
    if (!msVmPush(&vm, msValueObj(&t->head))) { break; }
    t->items[0] = msValueObj(&st->head);  // tuple 持 set（跨对象标记）
    t->items[1] = msValueInt(round);
    vm.stackTop -= 3;  // 本轮产物不可达：交回收
  }
  gcCollectAll(&vm);
  msVmFree(&vm);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}
```

- [ ] **Step 8.6: Full verification** — `cmake --build build --config Debug` (treat warnings as errors is default via /W4 /WX) then `ctest --test-dir build -C Debug` → 100% pass (109 baseline + ~12 new). Optionally verify ASan preset if a GCC/Clang toolchain exists (`cmake --preset asan` may fail on MSVC-only machines — if unavailable, note in final report; MSVC Debug + full suite is the delivery gate here, ASan gate runs on CI/Linux per repo presets).

- [ ] **Step 8.7: Docs** — `docs/tasks/T015-map-set-tuple.md`: header 状态 → 已完成, tick all DoD checkboxes; `docs/tasks/README.md`: T015 row ⬜ → ✅.

- [ ] **Step 8.8: Commit** `✅ test(fixtures): container fixtures and GC pressure for map/set/tuple` then `📝 docs(tasks): mark T015 done`.

- [ ] **Step 8.9: Review** — run superpowers requesting-code-review (code-reviewer) on the branch diff; fix findings with follow-up commits.

- [ ] **Step 8.10: Finish** — superpowers finishing-a-development-branch: merge `feature/t015-map-set-tuple` → `main` (user preference at that point), remove worktree.

## Self-review notes

- Spec coverage vs task doc §2: map structure ✓(T2) set+ops ✓(T3/T5) 键规则/数值塔 ✓(T2) 错误 ✓(T5) tuple+FAM+hash 缓存 ✓(T1/T6) BUILD 族 ✓(T4) 方法族 ✓(T6) GC ✓(T1-T3/T8) globals map ✓(T7) 多返回值铺垫（return tuple→BUILD_TUPLE via AST_TUPLE in AST_RETURN, UNPACK 复用）✓(T4).
- §5.1 C 单测表全覆盖（插入序/缺键/get 默认/数值塔/不可 hash/再哈希/set 运算/空 literal/tuple hash 缓存/tuple 相等）；§5.2 fixtures 五件齐。
- Type consistency: MsMapOp/MsHashStatus shared by map+set APIs; method tables match vm.h decls; obj includes follow existing layers (obj → vm forward-decl only).




