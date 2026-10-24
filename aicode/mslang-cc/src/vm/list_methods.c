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
    {"append", lstAppend},   {"pop", lstPop},         {"insert", lstInsert},
    {"remove", lstRemove},   {"indexOf", lstIndexOf}, {"contains", lstContains},
    {"sort", lstSort},       {"reverse", lstReverse}, {"concat", lstConcat},
    {"repeat", lstRepeat},   {"map", lstMap},         {"filter", lstFilter},
    {"reduce", lstReduce},   {"each", lstEach},       {"clear", lstClear},
    {"copy", lstCopy},       {"first", lstFirst},     {"last", lstLast},
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
