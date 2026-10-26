// set 方法族（T015，10 §4）：args[0] 恒为接收者；错误约定同 list_methods.c
// 运算族委托 obj_set 助手（源/结果 GC 契约：源由 CALL_METHOD 栈 root，结果寄存器返回）
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_set.h"
#include "vm/vm.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

static MsObjSet* recvSet(const MsValue* args) {
  return (MsObjSet*)args[0].as.obj;
}

// 不可 hash 元素 → TypeError（与 in 探测路径一致，03 §5）
static bool elemHashable(MsVM* vm, const MsValue* v, ms_u64* outHash) {
  if (msValueHash(v, outHash) != MS_HASH_OK) {
    methodError(vm, "TypeError: unhashable type: '%s'", msValueTypeName(v));
    return false;
  }
  return true;
}

static MsValue setAdd(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: add expects 1 argument");
    return msValueNil();
  }
  switch (msObjSetAdd(vm, recvSet(args), &args[1])) {
    case MS_MAP_OK:
      return msValueNil();
    case MS_MAP_UNHASHABLE:
      methodError(vm, "TypeError: unhashable type: '%s'", msValueTypeName(&args[1]));
      return msValueNil();
    default:
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
  }
}

// remove(x)：缺元素 KeyError；返回 nil（list.remove / map.remove 先例）
static MsValue setRemove(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: remove expects 1 argument");
    return msValueNil();
  }
  ms_u64 hash = 0;
  if (!elemHashable(vm, &args[1], &hash)) {
    return msValueNil();
  }
  if (msObjSetRemove(recvSet(args), &args[1]) == MS_MAP_NOENT) {
    methodError(vm, "KeyError: key not found");
  }
  return msValueNil();
}

static MsValue setHas(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: has expects 1 argument");
    return msValueNil();
  }
  ms_u64 hash = 0;
  if (!elemHashable(vm, &args[1], &hash)) {
    return msValueNil();
  }
  return msValueBool(msObjSetHas(recvSet(args), &args[1]));
}

static MsValue setClear(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: clear expects 0 arguments");
    return msValueNil();
  }
  msObjSetClear(recvSet(args));
  return msValueNil();
}

static MsValue setCopy(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: copy expects 0 arguments");
    return msValueNil();
  }
  MsObjSet* r = msObjSetCopy(vm, recvSet(args));  // 接收者在 CALL_METHOD 栈 root
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static MsValue setToList(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: toList expects 0 arguments");
    return msValueNil();
  }
  MsObjList* r = msObjSetToList(vm, recvSet(args));
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

// 运算族公共骨架：实参须为 set（union/intersect/diff/symmetricDiff，10 §4；等价 | & - ^）
static MsValue setAlgebra(MsVM* vm, int32_t argc, const MsValue* args, const char* mname,
                          MsObjSet* (*op)(MsVM*, const MsObjSet*, const MsObjSet*)) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: %s expects 1 argument", mname);
    return msValueNil();
  }
  if (!msIsSet(&args[1])) {
    methodError(vm, "ArgumentError: %s expects a set argument", mname);
    return msValueNil();
  }
  MsObjSet* r = op(vm, recvSet(args), (const MsObjSet*)args[1].as.obj);  // 双源均在栈
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static MsValue setUnion(MsVM* vm, int32_t argc, const MsValue* args) {
  return setAlgebra(vm, argc, args, "union", msObjSetUnion);
}

static MsValue setIntersect(MsVM* vm, int32_t argc, const MsValue* args) {
  return setAlgebra(vm, argc, args, "intersect", msObjSetIntersect);
}

static MsValue setDiff(MsVM* vm, int32_t argc, const MsValue* args) {
  return setAlgebra(vm, argc, args, "diff", msObjSetDiff);
}

static MsValue setSymDiff(MsVM* vm, int32_t argc, const MsValue* args) {
  return setAlgebra(vm, argc, args, "symmetricDiff", msObjSetSymDiff);
}

// ---- 方法表（10 §4 行序；len() 内置覆盖，不设 set.len） ----

typedef struct SetMethodEntry {
  const char* name;
  MsNativeFn fn;
} SetMethodEntry;

static const SetMethodEntry kSetMethods[] = {
    {"add", setAdd},           {"remove", setRemove},     {"has", setHas},
    {"union", setUnion},       {"intersect", setIntersect}, {"diff", setDiff},
    {"symmetricDiff", setSymDiff}, {"clear", setClear},   {"copy", setCopy},
    {"toList", setToList},
};

bool msSetMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kSetMethods) / sizeof(kSetMethods[0]); i++) {
    const char* n = kSetMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kSetMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
