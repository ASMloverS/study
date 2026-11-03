// Tier0 内置全集（T017，10 §1/§5）：函数注册表 + __name__/__version__ 内置变量
// 自 builtins_bridge.c（T011 桥）原样迁入并扩展；错误约定：置 vm->hasError + errMsg 后返回 nil
// GC 纪律：实参恒由 OP_CALL peek 保栈 root；内置自产的迭代器/累积值压栈 root 后才进分配窗口
// （nativeSet/nativeSum/nativeSorted 注释逐点说明）；错误路径不恢复 stackTop（整场 run 经 dispatchExit 废弃）
#include "vm/vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_bigint.h"
#include "obj/obj_func.h"
#include "obj/obj_iter.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_set.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"
#include "vm/call.h"

static void bErr(MsVM* vm, const char* fmt, ...) {  // 同 list_methods.c methodError 形态
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

static bool needInt(MsVM* vm, const MsValue* v, ms_i64* out) {
  if (v->kind != MS_VAL_I64) {
    bErr(vm, "TypeError: range arguments must be int");
    return false;
  }
  *out = v->as.i;
  return true;
}

// msIterFromValue 失败统一翻译（obj_iter.c 错误契约）：不可迭代 TypeError 仅写 errMsg——
// 调用方置 hasError；构造 OOM 已置 hasError 直接止步
static MsValue iterArgErr(MsVM* vm) {
  if (!vm->hasError) {
    vm->hasError = true;
  }
  return msValueNil();
}

// 比较失败统一翻译（02 §6）
static void typeOrderErr(MsVM* vm, const MsValue* a, const MsValue* b) {
  bErr(vm, "TypeError: '<' not supported between '%s' and '%s'", msValueTypeName(a), msValueTypeName(b));
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
        bErr(vm, "RuntimeError: out of memory");
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
    bErr(vm, "ArgumentError: len expects 1 argument");
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
  bErr(vm, "TypeError: len expects a string, list, tuple, map or set");
  return msValueNil();
}

// set() / set(iterable)：空集构造（{} 为空 map，02 §5.4）或可迭代去重构造（10 §1）
static MsValue nativeSet(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc > 1) {
    bErr(vm, "ArgumentError: set expects 0-1 arguments");
    return msValueNil();
  }
  if (argc == 0) {
    MsObjSet* st = msObjSetNew(vm);  // 构造器自 root（D6）
    if (st == NULL) {
      bErr(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&st->head);
  }
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  if (!msVmPush(vm, it)) {
    return msValueNil();  // StackOverflow 槽已置
  }
  MsObjSet* st = msObjSetNew(vm);  // 构造器自 root；返回至下述 push 间无分配点
  if (st == NULL) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  if (!msVmPush(vm, msValueObj(&st->head))) {
    return msValueNil();  // set 落栈 root：迭代中分配窗口（string runeIndex/适配器 tuple）存活
  }
  for (;;) {
    MsValue item;
    if (!msIterNext(vm, vm->stack[vm->stackTop - 2], &item)) {
      if (vm->hasError) {
        return msValueNil();  // 错误：stackTop 不恢复
      }
      break;
    }
    if (!msVmPush(vm, item)) {
      return msValueNil();
    }
    MsMapOp op = msObjSetAdd(vm, (MsObjSet*)vm->stack[vm->stackTop - 2].as.obj, &vm->stack[vm->stackTop - 1]);
    if (op == MS_MAP_UNHASHABLE) {
      bErr(vm, "TypeError: unhashable type: '%s'", msValueTypeName(&vm->stack[vm->stackTop - 1]));
      return msValueNil();
    }
    vm->stackTop--;  // 弹 item（add 内层 put 为 malloc 路径，无 GC 窗口）
    if (op == MS_MAP_OOM) {
      bErr(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
  }
  MsValue r = vm->stack[vm->stackTop - 1];
  vm->stackTop -= 2;  // 弹 set 与迭代器（干净路径）
  return r;
}

// type(x)：类型名字符串（10 §1）
static MsValue nativeType(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: type expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];  // 值拷贝：msObjStringNew 分配窗口实参在 OP_CALL 栈 root，指针不跨调用复用
  const char* n = msValueTypeName(&a);
  MsObjString* s = msObjStringNew(vm, n, (int32_t)strlen(n));
  if (s == NULL) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// str(x)：str 形态转换（02 §8 显式转换；__str__ 归 T021）
static MsValue nativeStr(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: str expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];  // 值拷贝：StrForm 内部 push 可能搬移栈，args 指针不跨分配复用
  MsObjString* s = msValueStrForm(vm, &a);
  if (s == NULL) {
    if (!vm->hasError) {
      bErr(vm, "RuntimeError: out of memory");
    }
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// repr(x)：repr 形态转换（string 带引号转义；__repr__ 归 T021）
static MsValue nativeRepr(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: repr expects 1 argument");
    return msValueNil();
  }
  MsValue a = args[0];
  MsObjString* s = msValueReprForm(vm, &a);
  if (s == NULL) {
    if (!vm->hasError) {
      bErr(vm, "RuntimeError: out of memory");
    }
    return msValueNil();
  }
  return msValueObj(&s->head);
}

// range([start,] stop[, step])：惰性 int 迭代器（装箱 MsObjIter；实参在 OP_CALL 栈 root）
static MsValue nativeRange(MsVM* vm, int32_t argc, const MsValue* args) {
  ms_i64 start = 0;
  ms_i64 stop = 0;
  ms_i64 step = 1;
  if (argc < 1 || argc > 3) {
    bErr(vm, "ArgumentError: range expects 1-3 arguments");
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
    bErr(vm, "ValueError: range step must not be zero");
    return msValueNil();
  }
  MsObjIter* it = msObjIterRangeNew(vm, start, stop, step);
  if (it == NULL) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

// —— 数值转换族（T016，10 §1）：int/float/bigint/bool/hash/abs ——

// float 精确分解 mant·2^k 升 bigint（nativeInt/nativeBigint 共用）：frexp 取 53 位尾数，
// 压缩移位量（mant·2^k 不变量：右移须 k++）；输入须有限且已截断/验整——k < 0 不可达防御；
// 失败仅 OOM 可达（结果 ≤ ~1024 bit，远低护栏），调用方统一报错
static MsObjBigInt* bigFromF64(MsVM* vm, double t) {
  int e2 = 0;
  double frac = frexp(fabs(t), &e2);
  ms_i64 mant = (ms_i64)(frac * 9007199254740992.0);
  int64_t k = (int64_t)e2 - 53;
  while (k > 0 && (mant & 1) == 0) {
    mant >>= 1;
    k++;
  }
  MsBigView v;
  uint32_t buf[2];
  msBigViewI64(&v, buf, t < 0 ? -mant : mant);
  MsObjBigInt* b = NULL;
  if (k < 0 || msBigShl(vm, &v, k, &b) != MS_BIG_OK) {
    return NULL;
  }
  return b;
}

// int()：float 截断 / string 按 base（失败 ValueError）/ bigint 可容则降、不容保持 bigint
// （int 域闭合于 i64，超界升格与字面量同策略，02 §3.1）
static MsValue nativeInt(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bErr(vm, "ArgumentError: int expects 1-2 arguments");
    return msValueNil();
  }
  if (argc == 2 && args[1].kind != MS_VAL_I64) {
    bErr(vm, "TypeError: int base must be int");
    return msValueNil();
  }
  ms_i64 base = argc == 2 ? args[1].as.i : 10;
  if (base != 0 && (base < 2 || base > 36)) {
    bErr(vm, "ValueError: int base must be 0 or in 2-36");
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
        bErr(vm, "ValueError: cannot convert float NaN to integer");
        return msValueNil();
      }
      if (isinf(f)) {
        bErr(vm, "OverflowError: cannot convert float infinity to integer");
        return msValueNil();
      }
      double t = trunc(f);
      if (t >= -9223372036854775808.0 && t < 9223372036854775808.0) {
        return msValueInt((ms_i64)t);
      }
      MsObjBigInt* b = bigFromF64(vm, t);  // 超界：精确分解升 bigint（共用助手）
      if (b == NULL) {
        bErr(vm, "RuntimeError: out of memory");
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
          bErr(vm, "ValueError: invalid literal for int with base %lld: '%.*s'", (long long)base,
               (int)s->byteLen, s->bytes);
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bErr(vm, "RuntimeError: out of memory");
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
  bErr(vm, "TypeError: int() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeFloat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: float expects 1 argument");
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
          bErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        memcpy(tmp, s->bytes, s->byteLen);
        tmp[s->byteLen] = '\0';
        char* end = NULL;
        double d = strtod(tmp, &end);
        bool ok = end != tmp && *end == '\0';
        free(tmp);
        if (!ok) {
          bErr(vm, "ValueError: invalid literal for float: '%.*s'", (int)s->byteLen, s->bytes);
          return msValueNil();
        }
        return msValueFloat(d);
      }
      break;
    default:
      break;
  }
  bErr(vm, "TypeError: float() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeBigint(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: bigint expects 1 argument");
    return msValueNil();
  }
  switch (args[0].kind) {
    case MS_VAL_I64: {
      MsObjBigInt* b = msObjBigintFromI64(vm, args[0].as.i);
      if (b == NULL) {
        bErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_F64: {
      double f = args[0].as.f;
      if (isnan(f) || isinf(f) || f != floor(f)) {  // 须整（10 §1）
        char buf[64];
        msValueToStr(&args[0], buf, sizeof(buf));
        bErr(vm, "ValueError: cannot convert float %s to bigint", buf);
        return msValueNil();
      }
      if (f >= -9223372036854775808.0 && f < 9223372036854775808.0) {
        MsObjBigInt* b = msObjBigintFromI64(vm, (ms_i64)f);
        if (b == NULL) {
          bErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        return msValueObj(&b->head);
      }
      MsObjBigInt* b = bigFromF64(vm, f);  // 大值精确分解（共用助手；f 已验整）
      if (b == NULL) {
        bErr(vm, "RuntimeError: out of memory");
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
          bErr(vm, "ValueError: invalid literal for bigint: '%.*s'", (int)s->byteLen, s->bytes);
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        return msValueObj(&b->head);
      }
      break;
    default:
      break;
  }
  bErr(vm, "TypeError: bigint() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeBool(MsVM* vm, int32_t argc, const MsValue* args) {  // 真值规则（02 §2）
  if (argc != 1) {
    bErr(vm, "ArgumentError: bool expects 1 argument");
    return msValueNil();
  }
  return msValueBool(msValueIsTruthy(&args[0]));
}

static MsValue nativeHash(MsVM* vm, int32_t argc, const MsValue* args) {  // 数值塔归一（02 §7）
  if (argc != 1) {
    bErr(vm, "ArgumentError: hash expects 1 argument");
    return msValueNil();
  }
  ms_u64 h = 0;
  if (msValueHash(&args[0], &h) != MS_HASH_OK) {
    bErr(vm, "TypeError: unhashable type: '%s'", msValueTypeName(&args[0]));
    return msValueNil();
  }
  return msValueInt((ms_i64)h);
}

static MsValue nativeAbs(MsVM* vm, int32_t argc, const MsValue* args) {  // 保类型（10 §1）
  if (argc != 1) {
    bErr(vm, "ArgumentError: abs expects 1 argument");
    return msValueNil();
  }
  const MsValue* x = &args[0];
  if (x->kind == MS_VAL_I64) {
    if (x->as.i == INT64_MIN) {  // |INT64_MIN| = 2^63 升 bigint
      uint32_t limbs[2] = {0, 0x80000000u};
      MsObjBigInt* b = msObjBigintNew(vm, limbs, 2, 1);
      if (b == NULL) {
        bErr(vm, "RuntimeError: out of memory");
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
      bErr(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&r->head);
  }
  bErr(vm, "TypeError: abs() expects an int, bigint or float");
  return msValueNil();
}

// —— 聚合/迭代族（T017，10 §1） ——

// min/max 共用实现（10 §1）：单可迭代参数取容器内最值；多参数取变参最值
// 容器形：迭代器与当前最值均栈 root（迭代推进的分配窗口存活）；变参形：实参在 OP_CALL 栈
// root，msValueOrder 纯比较无分配窗口（C 局部安全）
static MsValue minMaxImpl(MsVM* vm, int32_t argc, const MsValue* args, bool isMax) {
  const char* nm = isMax ? "max" : "min";
  if (argc == 0) {
    bErr(vm, "ArgumentError: %s expects at least 1 argument", nm);
    return msValueNil();
  }
  if (argc == 1) {
    MsValue it;
    if (!msIterFromValue(vm, args[0], &it)) {
      return iterArgErr(vm);
    }
    int32_t base = vm->stackTop;  // it 落位槽（push 扩容搬移后按下标重取；best 恒在 base+1）
    if (!msVmPush(vm, it)) {
      return msValueNil();
    }
    bool hasBest = false;
    for (;;) {
      MsValue item;
      if (!msIterNext(vm, vm->stack[base], &item)) {
        if (vm->hasError) {
          return msValueNil();
        }
        break;
      }
      if (!msVmPush(vm, item)) {
        return msValueNil();
      }
      if (hasBest) {  // 栈顶 item 挑战 best（base+1）：cmp<0 换守（min）/ cmp>0 换守（max）
        int32_t cmp = 0;
        if (!msValueOrder(&vm->stack[vm->stackTop - 1], &vm->stack[base + 1], &cmp)) {
          typeOrderErr(vm, &vm->stack[vm->stackTop - 1], &vm->stack[base + 1]);
          return msValueNil();
        }
        if (isMax ? cmp > 0 : cmp < 0) {
          vm->stack[base + 1] = vm->stack[vm->stackTop - 1];
        }
        vm->stackTop--;  // 弹挑战者（best 槽已更新或保持）
      } else {
        hasBest = true;  // 首元素留栈为 best（base+1）
      }
    }
    if (!hasBest) {
      vm->stackTop--;
      bErr(vm, "ValueError: %s() arg is an empty sequence", nm);
      return msValueNil();
    }
    MsValue best = vm->stack[base + 1];
    vm->stackTop = base;  // 弹 best 与迭代器（干净路径）
    return best;
  }
  MsValue best = args[0];
  for (int32_t i = 1; i < argc; i++) {
    int32_t cmp = 0;
    if (!msValueOrder(&args[i], &best, &cmp)) {
      typeOrderErr(vm, &args[i], &best);
      return msValueNil();
    }
    if (isMax ? cmp > 0 : cmp < 0) {
      best = args[i];
    }
  }
  return best;
}

static MsValue nativeMin(MsVM* vm, int32_t argc, const MsValue* args) {
  return minMaxImpl(vm, argc, args, false);
}

static MsValue nativeMax(MsVM* vm, int32_t argc, const MsValue* args) {
  return minMaxImpl(vm, argc, args, true);
}

// sum(xs[, init])：泛化 '+' 累加（数值塔 + string/list 拼接；init 改类型如 sum(xs, "")）
// GC 纪律：迭代器与 acc 均栈 root——迭代推进（适配器 tuple/runeIndex）分配窗口内 acc 在 C 局部
// 即失根；msValueAdd 操作数按栈槽指针传入，满足 arithBinary 操作数契约（concat 先拷源字节后触发
// GC，见 vm.c BINARY_ARITH_CASE 注释），结果寄存器值即时回写 acc 槽
static MsValue nativeSum(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bErr(vm, "ArgumentError: sum expects 1-2 arguments");
    return msValueNil();
  }
  MsValue init = argc == 2 ? args[1] : msValueInt(0);
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  int32_t base = vm->stackTop;  // it 落位槽（push 扩容搬移后按下标重取）
  if (!msVmPush(vm, it) || !msVmPush(vm, init)) {
    return msValueNil();  // StackOverflow 槽已置
  }
  for (;;) {
    MsValue item;
    if (!msIterNext(vm, vm->stack[base], &item)) {
      if (vm->hasError) {
        return msValueNil();
      }
      break;
    }
    if (!msVmPush(vm, item)) {
      return msValueNil();
    }
    MsValue r;
    if (!msValueAdd(vm, &vm->stack[base + 1], &vm->stack[vm->stackTop - 1], &r)) {
      vm->hasError = true;  // arithBinary 仅写 errMsg 不置 hasError（OP_BINARY_* 包装层置位）——此处补置
      return msValueNil();
    }
    vm->stack[base + 1] = r;
    vm->stackTop--;  // 弹 item
  }
  MsValue r = vm->stack[base + 1];
  vm->stackTop = base;  // 弹 acc 与迭代器（干净路径）
  return r;
}

// 索引稳定归并排序（sorted 专设）：keys 视图上比较 idx 序；reverse 取反（平键保持原序，Python 语义）
typedef struct SortCtx {
  MsVM* vm;
  const MsValue* keys;
  bool reverse;
  bool failed;  // 不可比较 → 错误槽已置，逐层中止
} SortCtx;

static bool sortKeyCmp(SortCtx* c, int32_t a, int32_t b, int32_t* out) {
  int32_t raw = 0;
  if (!msValueOrder(&c->keys[a], &c->keys[b], &raw)) {
    bErr(c->vm, "TypeError: '<' not supported between '%s' and '%s'", msValueTypeName(&c->keys[a]),
         msValueTypeName(&c->keys[b]));
    c->failed = true;
    return false;
  }
  *out = c->reverse ? -raw : raw;
  return true;
}

// [lo, hi) 归并：tmp 暂存后拷回；cmp<=0 取左（平键左先 = 稳定）
static void sortRec(SortCtx* c, int32_t* idx, int32_t* tmp, int32_t lo, int32_t hi) {
  if (hi - lo < 2) {
    return;
  }
  int32_t mid = lo + (hi - lo) / 2;
  sortRec(c, idx, tmp, lo, mid);
  sortRec(c, idx, tmp, mid, hi);
  if (c->failed) {
    return;
  }
  int32_t i = lo;
  int32_t j = mid;
  int32_t k = lo;
  while (i < mid && j < hi) {
    int32_t cmp = 0;
    if (!sortKeyCmp(c, idx[i], idx[j], &cmp)) {
      return;
    }
    tmp[k++] = cmp <= 0 ? idx[i++] : idx[j++];
  }
  while (i < mid) {
    tmp[k++] = idx[i++];
  }
  while (j < hi) {
    tmp[k++] = idx[j++];
  }
  memcpy(idx + lo, tmp + lo, sizeof(int32_t) * (size_t)(hi - lo));
}

// sorted 键物化（专设）：L（栈 base 槽 root）逐元素调 key 压栈 → CollectStackToList → keys list 压栈；
// 返回 false 时错误槽已置（key 调用/栈溢出/OOM），错误路径 stackTop 不恢复
static bool sortedCollectKeys(MsVM* vm, const MsValue* keyFn, int32_t base, int32_t n) {
  MsObjList* L = (MsObjList*)vm->stack[base].as.obj;
  for (int32_t i = 0; i < n; i++) {
    MsValue k;
    if (!msVmCallValue(vm, keyFn, &L->items[i], 1, &k)) {  // 实参首压即 root（call.c 公共面）
      if (!vm->hasError) {
        bErr(vm, "RuntimeError: out of memory");
      }
      return false;
    }
    if (!msVmPush(vm, k)) {
      return false;  // StackOverflow 槽已置
    }
  }
  MsValue kv = msVmCollectStackToList(vm, n);  // 栈顶 n 个键 → keys list
  if (vm->hasError) {
    return false;
  }
  return msVmPush(vm, kv);
}

// sorted(xs[, key[, reverse]])：新排序 list（10 §1； ratified 位置参形态——key 缺省 nil、reverse 缺省 false）
// 两趟物化：先排干源收集 items list L（栈 root），再对 L 逐元素调 key 收集 keys list K（栈 root）；
// 之后索引排序（比较纯函数无 GC 窗口）→ 按 idx 序回填新 list（items/keys/result 均栈 root）
static MsValue nativeSorted(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 3) {
    bErr(vm, "ArgumentError: sorted expects 1-3 arguments");
    return msValueNil();
  }
  bool reverse = false;
  if (argc >= 3) {
    if (args[2].kind != MS_VAL_BOOL) {
      bErr(vm, "TypeError: sorted reverse must be bool");
      return msValueNil();
    }
    reverse = args[2].as.b;
  }
  MsValue keyFn = msValueNil();
  if (argc >= 2) {
    if (args[1].kind != MS_VAL_NIL && args[1].kind != MS_VAL_NATIVE && !msIsCallableObj(&args[1])) {
      bErr(vm, "TypeError: sorted key must be a function");
      return msValueNil();
    }
    keyFn = args[1];
  }
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  if (!msVmPush(vm, it)) {
    return msValueNil();
  }
  MsValue items;
  if (!msIterCollectList(vm, vm->stack[vm->stackTop - 1], &items)) {
    return msValueNil();  // 错误槽已置
  }
  vm->stackTop--;  // 弹 it（items 寄存器值：至下述 push 间无分配窗口）
  int32_t base = vm->stackTop;  // items list 落位槽（此后所有 push 按下标重取）
  if (!msVmPush(vm, items)) {
    return msValueNil();
  }
  MsObjList* L = (MsObjList*)vm->stack[base].as.obj;
  int32_t n = L->count;
  const MsValue* keys = L->items;  // key 缺省：元素自身即键
  if (keyFn.kind != MS_VAL_NIL && n > 0) {
    if (!sortedCollectKeys(vm, &keyFn, base, n)) {
      return msValueNil();
    }
    keys = ((const MsObjList*)vm->stack[vm->stackTop - 1].as.obj)->items;
  }
  int32_t* idx = NULL;
  int32_t* tmp = NULL;
  if (n > 1) {
    idx = (int32_t*)malloc(sizeof(int32_t) * (size_t)n);
    tmp = (int32_t*)malloc(sizeof(int32_t) * (size_t)n);
    MS_ASSERT(idx != NULL && tmp != NULL);  // Release 下 OOM 致命（common.h 约定）
    for (int32_t i = 0; i < n; i++) {
      idx[i] = i;
    }
    SortCtx sc = {vm, keys, reverse, false};
    sortRec(&sc, idx, tmp, 0, n);
    if (sc.failed) {
      free(idx);
      free(tmp);
      return msValueNil();  // TypeError 槽已置
    }
  }
  MsObjList* r = msObjListNew(vm);  // L/keys/result 均栈 root（分配窗口存活）
  if (r == NULL) {
    free(idx);
    free(tmp);
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  if (!msVmPush(vm, msValueObj(&r->head))) {
    free(idx);
    free(tmp);
    return msValueNil();
  }
  bool ok = true;
  for (int32_t i = 0; ok && i < n; i++) {  // Push 为 malloc 路径（无 GC 窗口）
    ok = msObjListPush(vm, r, idx != NULL ? L->items[idx[i]] : L->items[i]);
  }
  free(idx);
  free(tmp);
  if (!ok) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  MsValue rv = vm->stack[vm->stackTop - 1];
  vm->stackTop = base;  // 弹 result/keys/items（干净路径）
  return rv;
}

// reversed(xs)：list/string/tuple 反向迭代器（10 §1）
static MsValue nativeReversed(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: reversed expects 1 argument");
    return msValueNil();
  }
  // args[0] 已在 OP_CALL 栈 root：构造器仅分配迭代器对象自身（src 值拷贝入 iter）
  MsObjIter* it = msObjIterReverseNew(vm, args[0]);
  if (it == NULL) {
    bool seqOk = args[0].kind == MS_VAL_OBJ && (args[0].as.obj->type == MS_OBJ_LIST ||
                                                args[0].as.obj->type == MS_OBJ_STRING ||
                                                args[0].as.obj->type == MS_OBJ_TUPLE);
    bErr(vm, seqOk ? "RuntimeError: out of memory" : "TypeError: reversed() expects a list, string or tuple");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

// enumerate(xs[, start])：(i, v) 适配器（10 §1）
static MsValue nativeEnumerate(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bErr(vm, "ArgumentError: enumerate expects 1-2 arguments");
    return msValueNil();
  }
  ms_i64 start = 0;
  if (argc == 2) {
    if (args[1].kind != MS_VAL_I64) {
      bErr(vm, "TypeError: enumerate start must be int");
      return msValueNil();
    }
    start = args[1].as.i;
  }
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  if (!msVmPush(vm, it)) {
    return msValueNil();
  }
  MsObjIter* w = msObjIterEnumerateNew(vm, vm->stack[vm->stackTop - 1], start);  // 分配窗口：源迭代器栈 root
  vm->stackTop--;
  if (w == NULL) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&w->head);
}

// zip(xs, ys[, strict])：短板对齐配对适配器（10 §1；strict 长度失配 ValueError 由推进侧报）
static MsValue nativeZip(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 2 || argc > 3) {
    bErr(vm, "ArgumentError: zip expects 2-3 arguments");
    return msValueNil();
  }
  bool strict = false;
  if (argc == 3) {
    if (args[2].kind != MS_VAL_BOOL) {
      bErr(vm, "TypeError: zip strict must be bool");
      return msValueNil();
    }
    strict = args[2].as.b;
  }
  MsValue a;
  if (!msIterFromValue(vm, args[0], &a)) {
    return iterArgErr(vm);
  }
  if (!msVmPush(vm, a)) {
    return msValueNil();
  }
  MsValue b;
  if (!msIterFromValue(vm, args[1], &b)) {
    return iterArgErr(vm);  // TypeError 补置 hasError；a 留栈由 unwind 废弃（不恢复 stackTop）
  }
  if (!msVmPush(vm, b)) {
    return msValueNil();
  }
  MsObjIter* z = msObjIterZipNew(vm, vm->stack[vm->stackTop - 2], vm->stack[vm->stackTop - 1], strict);
  vm->stackTop -= 2;  // 弹两源迭代器（zip 值拷贝持有，返回寄存器值至 OP_CALL 回压间无分配）
  if (z == NULL) {
    bErr(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&z->head);
}

// iter(x)：值 → 迭代器（迭代器直通；05 §4）
static MsValue nativeIter(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: iter expects 1 argument");
    return msValueNil();
  }
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  return it;
}

// next(it)：推进；耗尽返回 nil（05 §4）；args[0] 已在 OP_CALL 栈 root
static MsValue nativeNext(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: next expects 1 argument");
    return msValueNil();
  }
  if (!msIsIterator(&args[0])) {
    bErr(vm, "TypeError: next() expects an iterator");
    return msValueNil();
  }
  MsValue out;
  if (msIterNext(vm, args[0], &out)) {
    return out;
  }
  return msValueNil();  // 耗尽 → nil；错误路径槽已置
}

// 类型名匹配（isinstance 专设；class/MRO 归 T020）
static bool typeNameMatch(const MsValue* x, const MsValue* t) {
  const MsObjString* s = (const MsObjString*)t->as.obj;
  const char* n = msValueTypeName(x);
  return (ms_i64)s->byteLen == (ms_i64)strlen(n) && memcmp(s->bytes, n, s->byteLen) == 0;
}

// isinstance(x, T)：T 为类型名字符串或 list/tuple 类型名集合（10 §1）
static MsValue nativeIsinstance(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    bErr(vm, "ArgumentError: isinstance expects 2 arguments");
    return msValueNil();
  }
  const MsValue* t = &args[1];
  if (msIsString(t)) {
    return msValueBool(typeNameMatch(&args[0], t));
  }
  const MsValue* items = NULL;
  int32_t count = 0;
  if (t->kind == MS_VAL_OBJ && t->as.obj->type == MS_OBJ_LIST) {
    items = ((const MsObjList*)t->as.obj)->items;
    count = ((const MsObjList*)t->as.obj)->count;
  } else if (msIsTuple(t)) {
    items = ((const MsObjTuple*)t->as.obj)->items;
    count = ((const MsObjTuple*)t->as.obj)->count;
  } else {
    bErr(vm, "TypeError: isinstance() expects a type name or a list/tuple of type names");
    return msValueNil();
  }
  for (int32_t i = 0; i < count; i++) {  // 纯比较无分配窗口
    if (msIsString(&items[i]) && typeNameMatch(&args[0], &items[i])) {
      return msValueBool(true);
    }
  }
  return msValueBool(false);
}

// callable(x)：原生/脚本函数值或 bound 方法对象（10 §1；__call__ 实例归 T021）
static MsValue nativeCallable(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: callable expects 1 argument");
    return msValueNil();
  }
  const MsValue* v = &args[0];
  return msValueBool(v->kind == MS_VAL_NATIVE || v->kind == MS_VAL_FUNCTION || msIsCallableObj(v) ||
                    (v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_BOUND_METHOD));
}

// id(x)：堆对象取指针标识（活跃期唯一，10 §1）；立即值取 kind 高位 ^ 载荷位
static MsValue nativeId(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: id expects 1 argument");
    return msValueNil();
  }
  const MsValue* v = &args[0];
  if (v->kind == MS_VAL_OBJ) {
    return msValueInt((ms_i64)(intptr_t)v->as.obj);
  }
  ms_u64 raw = 0;
  if (v->kind == MS_VAL_BOOL) {
    raw = v->as.b ? 1 : 0;
  } else if (v->kind == MS_VAL_I64) {
    raw = (ms_u64)v->as.i;
  } else if (v->kind == MS_VAL_F64) {
    memcpy(&raw, &v->as.f, sizeof(raw));
  }
  // 注意：kind 高位 ^ 载荷位仅为立即值的尽力区分——i64/f64 载荷自身可占高位，跨 kind 理论可碰撞
  // （如 id(1) == id(某位型恰差 1<<56 的 float)）；唯一性仅对堆对象保证（10 §1 活跃期唯一）
  return msValueInt(((ms_i64)v->kind << 56) ^ (ms_i64)raw);
}

// assert(cond[, msg])：假则抛 AssertionError（10 §1；Error 子类归 T020）
static MsValue nativeAssert(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bErr(vm, "ArgumentError: assert expects 1-2 arguments");
    return msValueNil();
  }
  if (msValueIsTruthy(&args[0])) {
    return msValueNil();
  }
  if (argc == 2) {
    if (msIsString(&args[1])) {  // string 直取字节（args 在 OP_CALL 栈 root）
      const MsObjString* s = (const MsObjString*)args[1].as.obj;
      snprintf(vm->errMsg, sizeof(vm->errMsg), "AssertionError: %.*s", (int)s->byteLen, s->bytes);
      vm->hasError = true;
      return msValueNil();
    }
    MsObjString* form = msValueStrForm(vm, &args[1]);  // 非 string 走 str 形态（args root）
    if (form == NULL) {
      if (!vm->hasError) {
        bErr(vm, "RuntimeError: out of memory");
      }
      return msValueNil();
    }
    snprintf(vm->errMsg, sizeof(vm->errMsg), "AssertionError: %.*s", (int)form->byteLen, form->bytes);
    vm->hasError = true;
    return msValueNil();
  }
  bErr(vm, "AssertionError: assertion failed");
  return msValueNil();
}

// all(xs)：迭代器全真（10 §1；futures 并发重载归 08/T031）；空集真、含假早退
static MsValue nativeAll(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bErr(vm, "ArgumentError: all expects 1 argument");
    return msValueNil();
  }
  MsValue it;
  if (!msIterFromValue(vm, args[0], &it)) {
    return iterArgErr(vm);
  }
  if (!msVmPush(vm, it)) {
    return msValueNil();
  }
  for (;;) {
    MsValue item;
    if (!msIterNext(vm, vm->stack[vm->stackTop - 1], &item)) {
      if (vm->hasError) {
        return msValueNil();
      }
      break;
    }
    if (!msValueIsTruthy(&item)) {
      vm->stackTop--;  // 早退干净路径弹迭代器
      return msValueBool(false);
    }
  }
  vm->stackTop--;
  return msValueBool(true);
}

// —— 占位内置（各自指明目标任务；实现随对应任务落地） ——

static MsValue nativeCap(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)argc;
  (void)args;
  bErr(vm, "RuntimeError: cap() lands in T030 (channels)");
  return msValueNil();
}

static MsValue nativeMake(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)argc;
  (void)args;
  bErr(vm, "RuntimeError: make() lands in T030 (channels)");
  return msValueNil();
}

static MsValue nativeInput(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)argc;
  (void)args;
  bErr(vm, "RuntimeError: input() lands in T035 (io stdlib)");
  return msValueNil();
}

static MsValue nativeRace(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)argc;
  (void)args;
  bErr(vm, "RuntimeError: race() lands in T031 (futures)");
  return msValueNil();
}

static MsValue nativeWeakref(MsVM* vm, int32_t argc, const MsValue* args) {
  (void)argc;
  (void)args;
  bErr(vm, "RuntimeError: WeakRef lands in T027 (gc weakrefs)");
  return msValueNil();
}

// —— 注册表（10 §1 表序） ——

typedef struct BuiltinEntry {
  const char* name;
  MsNativeFn fn;
} BuiltinEntry;

static const BuiltinEntry kBuiltins[] = {
    {"print", nativePrint},     {"len", nativeLen},           {"cap", nativeCap},
    {"range", nativeRange},     {"type", nativeType},         {"repr", nativeRepr},
    {"str", nativeStr},         {"int", nativeInt},           {"bigint", nativeBigint},
    {"float", nativeFloat},     {"bool", nativeBool},         {"hash", nativeHash},
    {"abs", nativeAbs},         {"min", nativeMin},           {"max", nativeMax},
    {"sum", nativeSum},         {"sorted", nativeSorted},     {"reversed", nativeReversed},
    {"enumerate", nativeEnumerate}, {"zip", nativeZip},       {"iter", nativeIter},
    {"next", nativeNext},       {"isinstance", nativeIsinstance}, {"callable", nativeCallable},
    {"make", nativeMake},       {"id", nativeId},             {"input", nativeInput},
    {"assert", nativeAssert},   {"all", nativeAll},           {"race", nativeRace},
    {"WeakRef", nativeWeakref}, {"set", nativeSet},
};

bool msBuiltinLookup(MsVM* vm, const char* name, int32_t len, MsValue* out) {
  // 内置变量（10 §5）：intern 产物属永久代——重复查找零分配，NULL（OOM）按未命中 false 交调用方报错
  if (len == 8 && memcmp(name, "__name__", 8) == 0) {
    MsObjString* s = msInternBytes(vm, "__main__", 8);
    if (s == NULL) {
      return false;
    }
    *out = msValueObj(&s->head);
    return true;
  }
  if (len == 11 && memcmp(name, "__version__", 11) == 0) {
    MsObjString* s = msInternBytes(vm, MS_VERSION, (int32_t)strlen(MS_VERSION));
    if (s == NULL) {
      return false;
    }
    *out = msValueObj(&s->head);
    return true;
  }
  for (size_t i = 0; i < sizeof(kBuiltins) / sizeof(kBuiltins[0]); i++) {  // 函数表命中零分配
    const char* n = kBuiltins[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      out->kind = MS_VAL_NATIVE;
      out->as.native = kBuiltins[i].fn;
      return true;
    }
  }
  return false;
}
