// 内置桥（T011）：print / range / len / str / repr 最小实现（10-builtins §1；完整 Tier0 归 T017）
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_string.h"

static void bridgeErr(MsVM* vm, const char* msg) {
  vm->hasError = true;
  snprintf(vm->errMsg, sizeof(vm->errMsg), "%s", msg);
}

static bool needInt(MsVM* vm, const MsValue* v, ms_i64* out) {
  if (v->kind != MS_VAL_I64) {
    bridgeErr(vm, "TypeError: range arguments must be int");
    return false;
  }
  *out = v->as.i;
  return true;
}

// print(...xs)：空格连接 + 换行（Python 语义）；string 直写解码后字节（T013 堆对象）
// 非 string 经 msValueStrForm（list 等堆形态递归构建）；args 指向 VM 栈槽（OP_CALL peek 纪律），
// StrForm 内部 push 可能扩容搬移栈数组——按栈基下标逐参重取
static MsValue nativePrint(MsVM* vm, int32_t argc, const MsValue* args) {
  int32_t base = (int32_t)(args - vm->stack);
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) {
      putc(' ', vm->out);
    }
    const MsValue* v = &vm->stack[base + i];
    if (msIsString(v)) {
      const MsObjString* s = (const MsObjString*)v->as.obj;
      fwrite(s->bytes, 1, s->byteLen, vm->out);
      continue;
    }
    MsObjString* form = msValueStrForm(vm, v);  // 返回值在寄存器安全：fwrite 前无分配窗口
    if (form == NULL) {
      if (!vm->hasError) {
        bridgeErr(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
    fwrite(form->bytes, 1, form->byteLen, vm->out);
  }
  putc('\n', vm->out);
  return msValueNil();
}

// len(x)：string 字节数 / list 元素数（10 §1；其余容器 T014+）
static MsValue nativeLen(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: len expects 1 argument");
    return msValueNil();
  }
  if (msIsString(&args[0])) {
    return msValueInt((ms_i64)((const MsObjString*)args[0].as.obj)->byteLen);
  }
  if (args[0].kind == MS_VAL_OBJ && args[0].as.obj->type == MS_OBJ_LIST) {
    return msValueInt((ms_i64)((const MsObjList*)args[0].as.obj)->count);
  }
  bridgeErr(vm, "TypeError: len expects a string or list");
  return msValueNil();
}

// str(x)：str 形态转换（02 §8 显式转换；__str__ 归 T021）
static MsValue nativeStr(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: str expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];  // 值拷贝：StrForm 内部 push 可能搬移栈，args 指针不跨分配复用
  MsObjString* s = msValueStrForm(vm, &a);
  if (s == NULL) {
    if (!vm->hasError) {
      bridgeErr(vm, "RuntimeError: out of memory");
    }
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// repr(x)：repr 形态转换（string 带引号转义；__repr__ 归 T021）
static MsValue nativeRepr(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: repr expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];
  MsObjString* s = msValueReprForm(vm, &a);
  if (s == NULL) {
    if (!vm->hasError) {
      bridgeErr(vm, "RuntimeError: out of memory");
    }
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// range([start,] stop[, step])：惰性 int 迭代器
static MsValue nativeRange(MsVM* vm, int32_t argc, const MsValue* args) {
  ms_i64 start = 0;
  ms_i64 stop = 0;
  ms_i64 step = 1;
  if (argc < 1 || argc > 3) {
    bridgeErr(vm, "ArgumentError: range expects 1-3 arguments");
    return msValueNil();
  }
  if (argc >= 2 ? (!needInt(vm, &args[0], &start) || !needInt(vm, &args[1], &stop))
                : !needInt(vm, &args[0], &stop)) {
    return msValueNil();
  }
  if (argc == 3 && !needInt(vm, &args[2], &step)) {
    return msValueNil();
  }
  if (step == 0) {
    bridgeErr(vm, "ValueError: range step must not be zero");
    return msValueNil();
  }
  MsValue v = {MS_VAL_RANGE_ITER, {0}};
  v.as.range.cur = start;
  v.as.range.stop = stop;
  v.as.range.step = step;
  return v;
}

bool msBuiltinLookup(const char* name, int32_t len, MsValue* out) {
  if (len == 5 && memcmp(name, "print", 5) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativePrint;
    return true;
  }
  if (len == 5 && memcmp(name, "range", 5) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeRange;
    return true;
  }
  if (len == 3 && memcmp(name, "len", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeLen;
    return true;
  }
  if (len == 3 && memcmp(name, "str", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeStr;
    return true;
  }
  if (len == 4 && memcmp(name, "repr", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeRepr;
    return true;
  }
  return false;
}
