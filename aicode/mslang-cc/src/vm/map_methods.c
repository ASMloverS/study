// map 方法族（T015，10 §4）：args[0] 恒为接收者（D5 机制复用；错误约定同 list_methods）
// GC 纪律：接收者与实参由 CALL_METHOD peek 保栈 root；keys/values/items 快照逐值落栈后
// msVmCollectStackToList 收集（D10）；copy 结果经寄存器返回（put 内部 calloc 无 GC 触发点）
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_tuple.h"
#include "vm/vm.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

static MsObjMap* recvMap(const MsValue* args) {
  return (MsObjMap*)args[0].as.obj;
}

// 不可 hash 键 → TypeError（与 m[k] 索引路径一致，02 §5.2）
static bool keyHashable(MsVM* vm, const MsValue* key, ms_u64* outHash) {
  if (msValueHash(key, outHash) != MS_HASH_OK) {
    methodError(vm, "TypeError: unhashable type: '%s'", msValueTypeName(key));
    return false;
  }
  return true;
}

// ---- 查询族 ----

// get(k[, default])：缺键回 default/nil 永不抛（10 §4）；不可 hash 键仍为类型错
static MsValue mapGet(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2 && argc != 3) {
    methodError(vm, "ArgumentError: get expects 1-2 arguments");
    return msValueNil();
  }
  ms_u64 hash = 0;
  if (!keyHashable(vm, &args[1], &hash)) {
    return msValueNil();
  }
  MsValue out = msValueNil();
  if (msObjMapGet(recvMap(args), &args[1], &out) == MS_MAP_OK) {
    return out;
  }
  return argc == 3 ? args[2] : msValueNil();
}

static MsValue mapHas(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: has expects 1 argument");
    return msValueNil();
  }
  ms_u64 hash = 0;
  if (!keyHashable(vm, &args[1], &hash)) {
    return msValueNil();
  }
  return msValueBool(msObjMapGet(recvMap(args), &args[1], NULL) == MS_MAP_OK);
}

static MsValue mapLen(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: len expects 0 arguments");
    return msValueNil();
  }
  return msValueInt(recvMap(args)->liveCount);
}

// ---- 变更族 ----

// remove(k)：缺键 KeyError；返回 nil（list.remove 先例）
static MsValue mapRemove(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: remove expects 1 argument");
    return msValueNil();
  }
  ms_u64 hash = 0;
  if (!keyHashable(vm, &args[1], &hash)) {
    return msValueNil();
  }
  if (msObjMapRemove(recvMap(args), &args[1], NULL) == MS_MAP_NOENT) {
    methodError(vm, "KeyError: key not found");
  }
  return msValueNil();
}

static MsValue mapClear(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: clear expects 0 arguments");
    return msValueNil();
  }
  msObjMapClear(recvMap(args));
  return msValueNil();
}

// update(other)：other 须为 map；既有键原位改值、新键按 other 插入序追加（22 §4）
static MsValue mapUpdate(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: update expects 1 argument");
    return msValueNil();
  }
  if (!msIsMap(&args[1])) {
    methodError(vm, "ArgumentError: update expects a map argument");
    return msValueNil();
  }
  MsObjMap* dst = recvMap(args);
  const MsObjMap* other = (const MsObjMap*)args[1].as.obj;
  if (dst == other) {
    return msValueNil();  // 自别名：按语义 no-op（put 再哈希会失效遍历下标，obj_map.h 契约）
  }
  for (ms_i64 s = msObjMapFirst(other); s >= 0; s = msObjMapNext(other, s)) {
    // put 内部 calloc 再哈希无 GC 触发点；dst/other 均在 CALL_METHOD 栈
    if (msObjMapPut(vm, dst, msObjMapKeyAt(other, s), msObjMapValueAt(other, s)) == MS_MAP_OOM) {
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
  }
  return msValueNil();
}

// ---- 派生族（新 list/map） ----

// keys()/values()/items()：沿 seq 链快照（10 §4 插入序）；接收者指针先取（push 扩容使 args 失效）
static MsValue mapKeys(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: keys expects 0 arguments");
    return msValueNil();
  }
  const MsObjMap* m = recvMap(args);
  int32_t n = 0;
  for (ms_i64 s = msObjMapFirst(m); s >= 0; s = msObjMapNext(m, s)) {
    if (!msVmPush(vm, *msObjMapKeyAt(m, s))) {
      return msValueNil();  // StackOverflow 槽已置
    }
    n++;
  }
  return msVmCollectStackToList(vm, n);
}

static MsValue mapValues(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: values expects 0 arguments");
    return msValueNil();
  }
  const MsObjMap* m = recvMap(args);
  int32_t n = 0;
  for (ms_i64 s = msObjMapFirst(m); s >= 0; s = msObjMapNext(m, s)) {
    if (!msVmPush(vm, *msObjMapValueAt(m, s))) {
      return msValueNil();
    }
    n++;
  }
  return msVmCollectStackToList(vm, n);
}

// items()：每条目先建 2-tuple 落栈 root 再填充（tuple 分配可触发 GC；键值在 map 内安全，D10）
static MsValue mapItems(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: items expects 0 arguments");
    return msValueNil();
  }
  const MsObjMap* m = recvMap(args);
  int32_t n = 0;
  for (ms_i64 s = msObjMapFirst(m); s >= 0; s = msObjMapNext(m, s)) {
    MsObjTuple* t = msObjTupleNew(vm, 2);
    if (t == NULL) {
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    if (!msVmPush(vm, msValueObj(&t->head))) {
      return msValueNil();
    }
    t->items[0] = *msObjMapKeyAt(m, s);
    t->items[1] = *msObjMapValueAt(m, s);
    n++;
  }
  return msVmCollectStackToList(vm, n);
}

static MsValue mapCopy(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: copy expects 0 arguments");
    return msValueNil();
  }
  const MsObjMap* src = recvMap(args);  // 接收者在 CALL_METHOD 栈 root
  MsObjMap* r = msObjMapNew(vm);
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  for (ms_i64 s = msObjMapFirst(src); s >= 0; s = msObjMapNext(src, s)) {
    if (msObjMapPut(vm, r, msObjMapKeyAt(src, s), msObjMapValueAt(src, s)) == MS_MAP_OOM) {
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
  }
  return msValueObj(&r->head);
}

// ---- 方法表（10 §4 行序） ----

typedef struct MapMethodEntry {
  const char* name;
  MsNativeFn fn;
} MapMethodEntry;

static const MapMethodEntry kMapMethods[] = {
    {"get", mapGet},     {"has", mapHas},       {"remove", mapRemove}, {"clear", mapClear},
    {"keys", mapKeys},   {"values", mapValues}, {"items", mapItems},   {"update", mapUpdate},
    {"copy", mapCopy},   {"len", mapLen},
};

bool msMapMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kMapMethods) / sizeof(kMapMethods[0]); i++) {
    const char* n = kMapMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kMapMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
