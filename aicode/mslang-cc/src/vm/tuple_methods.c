// tuple 方法族（T015，10 §4）：args[0] 恒为接收者；错误约定同 list_methods.c
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_tuple.h"
#include "vm/vm.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

static const MsObjTuple* recvTuple(const MsValue* args) {
  return (const MsObjTuple*)args[0].as.obj;
}

static MsValue tupCount(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: count expects 1 argument");
    return msValueNil();
  }
  const MsObjTuple* t = recvTuple(args);
  int32_t n = 0;
  for (int32_t i = 0; i < t->count; i++) {
    if (msValueEqual(&t->items[i], &args[1])) {
      n++;
    }
  }
  return msValueInt(n);
}

// indexOf(x)：未命中 -1（同 list.indexOf 约定）
static MsValue tupIndexOf(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: indexOf expects 1 argument");
    return msValueNil();
  }
  const MsObjTuple* t = recvTuple(args);
  for (int32_t i = 0; i < t->count; i++) {
    if (msValueEqual(&t->items[i], &args[1])) {
      return msValueInt(i);
    }
  }
  return msValueInt(-1);
}

static MsValue tupContains(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: contains expects 1 argument");
    return msValueNil();
  }
  const MsObjTuple* t = recvTuple(args);
  for (int32_t i = 0; i < t->count; i++) {
    if (msValueEqual(&t->items[i], &args[1])) {
      return msValueBool(true);
    }
  }
  return msValueBool(false);
}

static MsValue tupToList(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: toList expects 0 arguments");
    return msValueNil();
  }
  const MsObjTuple* t = recvTuple(args);  // 接收者在 CALL_METHOD 栈 root
  MsObjList* r = msObjListNew(vm);
  if (r == NULL || !msObjListReserve(vm, r, t->count)) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  if (t->count > 0) {
    memcpy(r->items, t->items, sizeof(MsValue) * (size_t)t->count);
  }
  r->count = t->count;
  return msValueObj(&r->head);
}

// ---- 方法表（10 §4 行序） ----

typedef struct TupleMethodEntry {
  const char* name;
  MsNativeFn fn;
} TupleMethodEntry;

static const TupleMethodEntry kTupleMethods[] = {
    {"count", tupCount}, {"indexOf", tupIndexOf}, {"contains", tupContains}, {"toList", tupToList},
};

bool msTupleMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kTupleMethods) / sizeof(kTupleMethods[0]); i++) {
    const char* n = kTupleMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kTupleMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
