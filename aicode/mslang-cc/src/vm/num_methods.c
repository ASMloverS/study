// int/bigint 实例方法表（T016，10 §4）：toString([base]) / char / pow；错误约定同 tuple_methods.c
#include "vm/vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "obj/obj_bigint.h"
#include "obj/obj_string.h"

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

// i64 → base 字符串（2/8/10/16；负数 '-'）
static MsValue intToStringBase(MsVM* vm, ms_i64 v, ms_i64 base) {
  char buf[80];
  int32_t n = 0;
  if (base == 10) {
    n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
  } else {
    ms_u64 m = v < 0 ? (ms_u64)(-(v + 1)) + 1 : (ms_u64)v;
    const char* digits = "0123456789abcdef";
    do {
      buf[n++] = digits[m % (ms_u64)base];
      m /= (ms_u64)base;
    } while (m != 0);
    if (v < 0) {
      buf[n++] = '-';
    }
    for (int32_t x = 0, y = n - 1; x < y; x++, y--) {
      char t = buf[x];
      buf[x] = buf[y];
      buf[y] = t;
    }
  }
  MsObjString* s = msObjStringNew(vm, buf, n);
  if (s == NULL) {
    methodError(vm, "RuntimeError: out of memory");  // OOM 置槽（照 tuple_methods 形态）
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// toString([base])：int/bigint 共用；base ∈ {2, 8, 10, 16}
static MsValue numToString(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    methodError(vm, "ArgumentError: toString expects 0-1 arguments");
    return msValueNil();
  }
  ms_i64 base = 10;
  if (argc == 2) {
    if (args[1].kind != MS_VAL_I64) {
      methodError(vm, "TypeError: toString base must be int");
      return msValueNil();
    }
    base = args[1].as.i;
  }
  if (base != 2 && base != 8 && base != 10 && base != 16) {
    methodError(vm, "ValueError: toString base must be one of 2, 8, 10 or 16");
    return msValueNil();
  }
  if (args[0].kind == MS_VAL_I64) {
    return intToStringBase(vm, args[0].as.i, base);
  }
  MsObjString* s = msObjBigintToString(vm, (const MsObjBigInt*)args[0].as.obj, (int)base);
  if (s == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// char()：合法码点 → 单字符 UTF-8 string（int 接收者专属）
static MsValue numChar(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: char expects 0 arguments");
    return msValueNil();
  }
  if (args[0].kind != MS_VAL_I64) {
    methodError(vm, "TypeError: char requires an int receiver");
    return msValueNil();
  }
  ms_i64 cp = args[0].as.i;
  if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    methodError(vm, "ValueError: invalid codepoint");
    return msValueNil();
  }
  char buf[4];
  int32_t n = msUtf8Encode((ms_u32)cp, buf);  // 共享编码器（str_methods 同源；码点已前置校验）
  MsObjString* s = msObjStringNew(vm, buf, n);
  if (s == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// pow(e)：e int 或可容 bigint；负指数浮点幂（02 §3），否则 msBigPow（RANGE → OverflowError）
static MsValue numPow(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: pow expects 1 argument");
    return msValueNil();
  }
  const MsValue* b = &args[0];
  const MsValue* e = &args[1];
  ms_i64 ev = 0;
  if (e->kind == MS_VAL_I64) {
    ev = e->as.i;
  } else if (msIsBigint(e)) {
    if (!msObjBigintFitsI64((const MsObjBigInt*)e->as.obj, &ev)) {
      methodError(vm, "ValueError: pow exponent must be a non-negative int");
      return msValueNil();
    }
  } else {
    methodError(vm, "TypeError: pow exponent must be int or bigint");
    return msValueNil();
  }
  if (ev < 0) {  // 负指数浮点幂（与运算符一致，02 §3）
    double base = b->kind == MS_VAL_I64 ? (double)b->as.i : msObjBigintToF64((const MsObjBigInt*)b->as.obj);
    return msValueFloat(pow(base, (double)ev));
  }
  MsBigView v;
  uint32_t buf[2];
  if (b->kind == MS_VAL_I64) {
    msBigViewI64(&v, buf, b->as.i);
  } else {
    msBigViewOf(&v, (const MsObjBigInt*)b->as.obj);
  }
  MsObjBigInt* r = NULL;
  MsBigErr err = msBigPow(vm, &v, ev, &r);
  if (err == MS_BIG_RANGE) {
    methodError(vm, "OverflowError: bigint result too large");
    return msValueNil();
  }
  if (err != MS_BIG_OK) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

// ---- 方法表（10 §4） ----

static const MsMethodRef kNumMethods[] = {
    {numToString, "toString", 8},
    {numChar, "char", 4},
    {numPow, "pow", 3},
};

bool msNumMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kNumMethods) / sizeof(kNumMethods[0]); i++) {
    if (kNumMethods[i].nameLen == len && memcmp(name, kNumMethods[i].name, (size_t)len) == 0) {
      *out = kNumMethods[i];
      return true;
    }
  }
  return false;
}
