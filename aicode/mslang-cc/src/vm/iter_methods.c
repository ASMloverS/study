// iterator 方法族（T017，10 §4）：args[0] 恒为接收者（MS_OBJ_ITERATOR 壳，D5 机制复用）
// take/drop/map/filter 惰性链接（返回新适配器），终结于 next/toList/each/for；
// 错误约定同 list_methods.c：置 vm->hasError + errMsg 后返回 nil
// GC 纪律：接收者与实参由 CALL_METHOD peek 保栈 root；each 推进值经 msVmCallValue
// 首压即 root（压栈前无分配窗口，C 局部实参安全）
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "obj/obj_func.h"
#include "obj/obj_iter.h"
#include "vm/call.h"
#include "vm/vm.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

// 接收者防御（methodRefLookup 已保证 iterator；num_methods.c char 同纪律）
static bool iterRecv(MsVM* vm, const MsValue* args, const char* mname) {
  if (!msIsIterator(&args[0])) {
    methodError(vm, "TypeError: %s requires an iterator receiver", mname);
    return false;
  }
  return true;
}

// 函数实参形态（同 list_methods.c：T018 起脚本函数值可执行；cmp 协议 T021）
static bool checkFnArg(MsVM* vm, const MsValue* f, const char* mname) {
  if (f->kind == MS_VAL_NATIVE || msIsCallableObj(f)) {
    return true;
  }
  methodError(vm, "TypeError: %s expects a function argument", mname);
  return false;
}

// next()：同 next(it)（05 §4）；耗尽 nil
static MsValue itNext(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: next expects 0 arguments");
    return msValueNil();
  }
  if (!iterRecv(vm, args, "next")) {
    return msValueNil();
  }
  MsValue out;
  if (msIterNext(vm, args[0], &out)) {
    return out;
  }
  return msValueNil();  // 耗尽 → nil；错误路径槽已置
}

// toList()：排干收集（终结形态）
static MsValue itToList(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: toList expects 0 arguments");
    return msValueNil();
  }
  if (!iterRecv(vm, args, "toList")) {
    return msValueNil();
  }
  MsValue out;
  if (!msIterCollectList(vm, args[0], &out)) {
    return msValueNil();  // 错误槽已置（迭代错误/栈溢出/OOM）
  }
  return out;
}

// take(n)/drop(n) 共用骨架：n 非负 int；返回适配器（惰性，源即接收者）
static MsValue itTakeDrop(MsVM* vm, int32_t argc, const MsValue* args, const char* mname,
                          MsObjIter* (*ctor)(MsVM*, MsValue, ms_i64)) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: %s expects 1 argument", mname);
    return msValueNil();
  }
  if (!iterRecv(vm, args, mname)) {
    return msValueNil();
  }
  if (args[1].kind != MS_VAL_I64) {
    methodError(vm, "TypeError: %s expects a non-negative int", mname);
    return msValueNil();
  }
  if (args[1].as.i < 0) {
    methodError(vm, "ValueError: %s count must not be negative", mname);
    return msValueNil();
  }
  MsObjIter* it = ctor(vm, args[0], args[1].as.i);  // 接收者在 CALL_METHOD 栈 root
  if (it == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

static MsValue itTake(MsVM* vm, int32_t argc, const MsValue* args) {
  return itTakeDrop(vm, argc, args, "take", msObjIterTakeNew);
}

static MsValue itDrop(MsVM* vm, int32_t argc, const MsValue* args) {
  return itTakeDrop(vm, argc, args, "drop", msObjIterDropNew);
}

// map(f)/filter(f) 共用骨架：f 须函数（原生/脚本）；返回适配器（惰性）
static MsValue itMapFilter(MsVM* vm, int32_t argc, const MsValue* args, const char* mname,
                           MsObjIter* (*ctor)(MsVM*, MsValue, MsValue)) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: %s expects 1 argument", mname);
    return msValueNil();
  }
  if (!iterRecv(vm, args, mname)) {
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], mname)) {
    return msValueNil();
  }
  MsObjIter* it = ctor(vm, args[0], args[1]);  // 接收者/实参均在 CALL_METHOD 栈 root
  if (it == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

static MsValue itMap(MsVM* vm, int32_t argc, const MsValue* args) {
  return itMapFilter(vm, argc, args, "map", msObjIterMapFnNew);
}

static MsValue itFilter(MsVM* vm, int32_t argc, const MsValue* args) {
  return itMapFilter(vm, argc, args, "filter", msObjIterFilterNew);
}

// each(f)：逐值 f(v)（ratified 单参形态；list.each 为 f(v, i)，10 §3）弃置结果（终结）
static MsValue itEach(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: each expects 1 argument");
    return msValueNil();
  }
  if (!iterRecv(vm, args, "each")) {
    return msValueNil();
  }
  if (!checkFnArg(vm, &args[1], "each")) {
    return msValueNil();
  }
  MsValue it = args[0];  // 值拷贝：msVmCallNative 压栈扩容后 args 指针失效（nativePrint 同纪律）
  MsValue f = args[1];   // it/f 引用对象仍由 CALL_METHOD 栈槽 root
  for (;;) {
    MsValue v;
    if (!msIterNext(vm, it, &v)) {
      if (vm->hasError) {
        return msValueNil();
      }
      break;  // 耗尽
    }
    MsValue r;
    if (!msVmCallValue(vm, &f, &v, 1, &r)) {
      return msValueNil();  // 错误槽已置（fn 报错/栈溢出）
    }
  }
  return msValueNil();
}

// ---- 方法表（10 §4 表序） ----

typedef struct IterMethodEntry {
  const char* name;
  MsNativeFn fn;
} IterMethodEntry;

static const IterMethodEntry kIterMethods[] = {
    {"next", itNext}, {"toList", itToList}, {"take", itTake}, {"drop", itDrop},
    {"map", itMap},   {"filter", itFilter}, {"each", itEach},
};

bool msIterMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kIterMethods) / sizeof(kIterMethods[0]); i++) {
    const char* n = kIterMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kIterMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
