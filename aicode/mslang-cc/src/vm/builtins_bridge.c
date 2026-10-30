// 内置桥（T011）：print / range / len / str / repr 最小实现（10-builtins §1；完整 Tier0 归 T017）
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_bigint.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_set.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"

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

// len(x)：string 字节数 / 容器元素数（10 §1：list/tuple/map/set）
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
  if (msIsTuple(&args[0])) {
    return msValueInt((ms_i64)((const MsObjTuple*)args[0].as.obj)->count);
  }
  if (msIsMap(&args[0])) {
    return msValueInt((ms_i64)((const MsObjMap*)args[0].as.obj)->liveCount);
  }
  if (msIsSet(&args[0])) {
    return msValueInt((ms_i64)msObjSetCount((const MsObjSet*)args[0].as.obj));
  }
  bridgeErr(vm, "TypeError: len expects a string, list, tuple, map or set");
  return msValueNil();
}

// set()：空集构造（{} 为空 map，02 §5.4；可迭代实参形态归 T017）
static MsValue nativeSet(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)args;
  if (argc != 0) {
    bridgeErr(vm, "ArgumentError: set expects 0 arguments (iterable forms land in T017)");
    return msValueNil();
  }
  MsObjSet* st = msObjSetNew(vm);  // 构造器自 root（D6）
  if (st == NULL) {
    bridgeErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&st->head);
}

// type(x)：类型名字符串（10 §1）
static MsValue nativeType(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: type expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];  // 值拷贝：msObjStringNew 分配窗口实参在 OP_CALL 栈 root，指针不跨调用复用
  const char* n = msValueTypeName(&a);
  MsObjString* s = msObjStringNew(vm, n, (int32_t)strlen(n));
  if (s == NULL) {
    bridgeErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&s->head);
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

// —— 数值转换族（T016，10 §1）：int/float/bigint/bool/hash/abs ——
// int()：float 截断 / string 按 base（失败 ValueError）/ bigint 可容则降、不容保持 bigint
// （int 域闭合于 i64，超界升格与字面量同策略，02 §3.1）
static MsValue nativeInt(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bridgeErr(vm, "ArgumentError: int expects 1-2 arguments");
    return msValueNil();
  }
  if (argc == 2 && args[1].kind != MS_VAL_I64) {
    bridgeErr(vm, "TypeError: int base must be int");
    return msValueNil();
  }
  ms_i64 base = argc == 2 ? args[1].as.i : 10;
  if (base != 0 && (base < 2 || base > 36)) {
    bridgeErr(vm, "ValueError: int base must be 0 or in 2-36");
    return msValueNil();
  }
  const MsValue* x = &args[0];
  switch (x->kind) {
    case MS_VAL_BOOL:
      return msValueInt(x->as.b ? 1 : 0);
    case MS_VAL_I64:
      return *x;
    case MS_VAL_F64: {
      double f = x->as.f;
      if (isnan(f)) {
        bridgeErr(vm, "ValueError: cannot convert float NaN to integer");
        return msValueNil();
      }
      if (isinf(f)) {
        bridgeErr(vm, "OverflowError: cannot convert float infinity to integer");
        return msValueNil();
      }
      double t = trunc(f);
      if (t >= -9223372036854775808.0 && t < 9223372036854775808.0) {
        return msValueInt((ms_i64)t);
      }
      int e2 = 0;  // 超界：frexp 精确分解 mant·2^k 升 bigint
      double frac = frexp(fabs(t), &e2);
      ms_i64 mant = (ms_i64)(frac * 9007199254740992.0);
      int64_t k = (int64_t)e2 - 53;
      while (k > 0 && (mant & 1) == 0) {  // 压缩移位量（mant·2^k 不变量：右移须 k++）
        mant >>= 1;
        k++;
      }
      MsBigView v;
      uint32_t buf[2];
      msBigViewI64(&v, buf, t < 0 ? -mant : mant);
      MsObjBigInt* b = NULL;
      if (k < 0 || msBigShl(vm, &v, k, &b) != MS_BIG_OK) {  // t 已截断必整：k < 0 不可达防御；shl 失败仅 OOM 可达（结果 ≤ ~1024 bit，远低护栏）
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_OBJ:
      if (x->as.obj->type == MS_OBJ_BIGINT) {
        ms_i64 iv = 0;
        if (msObjBigintFitsI64((const MsObjBigInt*)x->as.obj, &iv)) {
          return msValueInt(iv);
        }
        return *x;  // 超界保持 bigint（升格不回降）
      }
      if (x->as.obj->type == MS_OBJ_STRING) {
        const MsObjString* s = (const MsObjString*)x->as.obj;
        MsObjBigInt* b = NULL;
        MsBigErr e = msObjBigintParse(vm, s->bytes, (int32_t)s->byteLen, (int)base, &b);
        if (e == MS_BIG_PARSE) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for int with base %lld: '%.*s'",
                   (long long)base, (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        ms_i64 iv = 0;
        if (msObjBigintFitsI64(b, &iv)) {
          return msValueInt(iv);
        }
        return msValueObj(&b->head);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: int() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeFloat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: float expects 1 argument");
    return msValueNil();
  }
  switch (args[0].kind) {
    case MS_VAL_BOOL:
      return msValueFloat(args[0].as.b ? 1.0 : 0.0);
    case MS_VAL_I64:
      return msValueFloat((double)args[0].as.i);
    case MS_VAL_F64:
      return args[0];
    case MS_VAL_OBJ:
      if (args[0].as.obj->type == MS_OBJ_BIGINT) {  // 精度损失不报错、含溢出 ±inf（02 §3.3）
        return msValueFloat(msObjBigintToF64((const MsObjBigInt*)args[0].as.obj));
      }
      if (args[0].as.obj->type == MS_OBJ_STRING) {  // strtod 严格全消费
        const MsObjString* s = (const MsObjString*)args[0].as.obj;
        char* tmp = (char*)malloc((size_t)s->byteLen + 1);
        if (tmp == NULL) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        memcpy(tmp, s->bytes, s->byteLen);
        tmp[s->byteLen] = '\0';
        char* end = NULL;
        double d = strtod(tmp, &end);
        bool ok = end != tmp && *end == '\0';
        free(tmp);
        if (!ok) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for float: '%.*s'",
                   (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        return msValueFloat(d);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: float() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeBigint(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: bigint expects 1 argument");
    return msValueNil();
  }
  switch (args[0].kind) {
    case MS_VAL_I64: {
      MsObjBigInt* b = msObjBigintFromI64(vm, args[0].as.i);
      if (b == NULL) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_F64: {
      double f = args[0].as.f;
      if (isnan(f) || isinf(f) || f != floor(f)) {  // 须整（10 §1）
        char buf[64];
        msValueToStr(&args[0], buf, sizeof(buf));
        snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: cannot convert float %s to bigint", buf);
        vm->hasError = true;
        return msValueNil();
      }
      if (f >= -9223372036854775808.0 && f < 9223372036854775808.0) {
        MsObjBigInt* b = msObjBigintFromI64(vm, (ms_i64)f);
        if (b == NULL) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        return msValueObj(&b->head);
      }
      int e2 = 0;  // 大值精确分解（同 nativeInt）
      double frac = frexp(fabs(f), &e2);
      ms_i64 mant = (ms_i64)(frac * 9007199254740992.0);
      int64_t k = (int64_t)e2 - 53;
      while (k > 0 && (mant & 1) == 0) {  // 压缩移位量（mant·2^k 不变量：右移须 k++）
        mant >>= 1;
        k++;
      }
      MsBigView v;
      uint32_t buf[2];
      msBigViewI64(&v, buf, f < 0 ? -mant : mant);
      MsObjBigInt* b = NULL;
      if (k < 0 || msBigShl(vm, &v, k, &b) != MS_BIG_OK) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_OBJ:
      if (args[0].as.obj->type == MS_OBJ_BIGINT) {
        return args[0];
      }
      if (args[0].as.obj->type == MS_OBJ_STRING) {  // 严格十进制（不识别前缀，同 Python int(s)）
        const MsObjString* s = (const MsObjString*)args[0].as.obj;
        MsObjBigInt* b = NULL;
        MsBigErr e = msObjBigintParse(vm, s->bytes, (int32_t)s->byteLen, 10, &b);
        if (e == MS_BIG_PARSE) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for bigint: '%.*s'",
                   (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        return msValueObj(&b->head);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: bigint() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeBool(MsVM* vm, int32_t argc, const MsValue* args) {  // 真值规则（02 §2）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: bool expects 1 argument");
    return msValueNil();
  }
  return msValueBool(msValueIsTruthy(&args[0]));
}

static MsValue nativeHash(MsVM* vm, int32_t argc, const MsValue* args) {  // 数值塔归一（02 §7）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: hash expects 1 argument");
    return msValueNil();
  }
  ms_u64 h = 0;
  if (msValueHash(&args[0], &h) != MS_HASH_OK) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: unhashable type: '%s'", msValueTypeName(&args[0]));
    vm->hasError = true;
    return msValueNil();
  }
  return msValueInt((ms_i64)h);
}

static MsValue nativeAbs(MsVM* vm, int32_t argc, const MsValue* args) {  // 保类型（10 §1）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: abs expects 1 argument");
    return msValueNil();
  }
  const MsValue* x = &args[0];
  if (x->kind == MS_VAL_I64) {
    if (x->as.i == INT64_MIN) {  // |INT64_MIN| = 2^63 升 bigint
      uint32_t limbs[2] = {0, 0x80000000u};
      MsObjBigInt* b = msObjBigintNew(vm, limbs, 2, 1);
      if (b == NULL) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    return msValueInt(x->as.i < 0 ? -x->as.i : x->as.i);
  }
  if (x->kind == MS_VAL_F64) {
    return msValueFloat(fabs(x->as.f));
  }
  if (msIsBigint(x)) {  // 实参栈 root：New 跨分配安全
    const MsObjBigInt* b = (const MsObjBigInt*)x->as.obj;
    MsObjBigInt* r = msObjBigintNew(vm, b->limbs, b->len, 1);
    if (r == NULL) {
      bridgeErr(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&r->head);
  }
  bridgeErr(vm, "TypeError: abs() expects an int, bigint or float");
  return msValueNil();
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
  if (len == 3 && memcmp(name, "set", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeSet;
    return true;
  }
  if (len == 4 && memcmp(name, "type", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeType;
    return true;
  }
  if (len == 3 && memcmp(name, "int", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeInt;
    return true;
  }
  if (len == 5 && memcmp(name, "float", 5) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeFloat;
    return true;
  }
  if (len == 6 && memcmp(name, "bigint", 6) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeBigint;
    return true;
  }
  if (len == 4 && memcmp(name, "bool", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeBool;
    return true;
  }
  if (len == 4 && memcmp(name, "hash", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeHash;
    return true;
  }
  if (len == 3 && memcmp(name, "abs", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeAbs;
    return true;
  }
  return false;
}
