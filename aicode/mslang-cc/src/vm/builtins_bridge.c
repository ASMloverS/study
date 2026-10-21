// 内置桥（T011）：print / range 最小实现（10-builtins §1；完整 Tier0 归 T017）
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

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

// print(...xs)：空格连接 + 换行（Python 语义）；string 直写裸文本（转义解码归 T013）
static MsValue nativePrint(MsVM* vm, int32_t argc, const MsValue* args) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) {
      putc(' ', vm->out);
    }
    if (args[i].kind == MS_VAL_STRING) {
      fwrite(args[i].as.str.bytes, 1, (size_t)args[i].as.str.len, vm->out);
    } else {
      char buf[64];
      msValueToStr(&args[i], buf, sizeof(buf));
      fputs(buf, vm->out);
    }
  }
  putc('\n', vm->out);
  return msValueNil();
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
  return false;
}
