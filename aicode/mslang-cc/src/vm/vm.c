// VM 核心（T011，21 §6 / 20 §7）：dispatch 双路径 + 基础指令执行
// dispatch 采用 computed goto（GCC/Clang）/ switch（MSVC）双展开、单一指令处理体；
// 运行时错误置错误槽后经 dispatchExit 统一 unwind（28 §6 goto 例外：主循环 unwind）
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_bigint.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_set.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"
#include "plat/plat_math.h"

#define MS_STACK_MAX (64 * 1024)  // 值栈条目上限（溢出报错；栈深真实限制 T018 评估）
#define MS_SAFEPOINT_BUDGET 1000  // CHECK_SAFEPOINT 回边预算（NOP 递减；抢占 T029）

// bound 方法 children 标记（T013，D5）：receiver + method（native 对象）
static void boundMark(MsVM* vm, MsObj* obj) {
  MsObjBoundMethod* bm = (MsObjBoundMethod*)obj;
  gcMarkValue(vm, bm->receiver);
  gcMarkValue(vm, msValueObj(&bm->method->head));
}

// 迭代器 children 标记（T013 D8 / T014）：str/list 共享 MS_OBJ_ITERATOR tag，按首载荷 kind 分派
static void iterMark(MsVM* vm, MsObj* obj) {
  if (((MsObjIterShell*)obj)->kind == MS_ITER_LIST) {
    gcMarkValue(vm, msValueObj(&((MsObjListIter*)obj)->list->head));
    return;
  }
  gcMarkValue(vm, msValueObj(&((MsObjStrIter*)obj)->str->head));
}

// 方法查找公共面（T013 string / T014 list / T015 map,set,tuple）：命中 0；未命中 -1
static int methodRefLookup(const MsValue* recv, const char* name, int32_t len, MsMethodRef* out) {
  if (msIsString(recv)) {
    return msStringMethodLookup(name, len, out) ? 0 : -1;
  }
  if (recv->kind == MS_VAL_OBJ && recv->as.obj->type == MS_OBJ_LIST) {
    return msListMethodLookup(name, len, out) ? 0 : -1;
  }
  if (msIsMap(recv)) {
    return msMapMethodLookup(name, len, out) ? 0 : -1;
  }
  if (msIsSet(recv)) {
    return msSetMethodLookup(name, len, out) ? 0 : -1;
  }
  if (msIsTuple(recv)) {
    return msTupleMethodLookup(name, len, out) ? 0 : -1;
  }
  if (recv->kind == MS_VAL_I64 || msIsBigint(recv)) {
    return msNumMethodLookup(name, len, out) ? 0 : -1;
  }
  return -1;
}

void msVmInit(MsVM* vm) {
  vm->stack = NULL;
  vm->stackTop = vm->stackCap = 0;
  vm->frames = NULL;
  vm->frameCount = vm->frameCap = 0;
  vm->globals = NULL;  // 全局表 msVmRun 首入分配（T015 map 承载；保持 init 后零堆基线）
  vm->heap = malloc(sizeof(GcHeap));
  MS_ASSERT(vm->heap != NULL);
  gcHeapInit(vm->heap);
  gcSetMarkFn(MS_OBJ_LIST, msObjListMarkChildren);  // D6：children 标记统一注册（进程级表，幂等）
  gcSetFreeFn(MS_OBJ_LIST, msObjListFreeData);  // items 数组随对象释放（sweep/heapFree 前回调）
  gcSetMarkFn(MS_OBJ_TUPLE, msObjTupleMarkChildren);  // T015：FAM 元素（单块分配无 freeFn）
  gcSetMarkFn(MS_OBJ_MAP, msObjMapMarkChildren);  // T015：entry 键值（seq 链遍历）
  gcSetFreeFn(MS_OBJ_MAP, msObjMapFreeData);  // slots 数组随对象释放
  gcSetMarkFn(MS_OBJ_SET, msObjSetMarkChildren);  // T015：内层 map（无独立数据，freeFn 不需）
  gcSetMarkFn(MS_OBJ_BOUND_METHOD, boundMark);  // D5：bound 持 receiver + native 方法对象
  gcSetMarkFn(MS_OBJ_ITERATOR, iterMark);  // D8/T014：迭代器持 str/list（kind 分派，T017 泛化）
  vm->intern = malloc(sizeof(MsInternTable));
  MS_ASSERT(vm->intern != NULL);
  msInternInit(vm->intern);
  vm->out = stdout;
  vm->trace = NULL;
  vm->hasError = false;
  vm->errMsg[0] = '\0';
  vm->errLine = -1;
  vm->safepointBudget = MS_SAFEPOINT_BUDGET;
}

void msVmFree(MsVM* vm) {
  if (vm->heap != NULL) {
    gcHeapFree(vm->heap);
    free(vm->heap);
    vm->heap = NULL;
  }
  if (vm->intern != NULL) {  // 槽位数组释放；intern 对象已随上句 gcHeapFree 归堆释放
    msInternFree(vm->intern);
    free(vm->intern);
    vm->intern = NULL;
  }
  free(vm->stack);
  free(vm->frames);
  // globals map 归堆所有：随上方 gcHeapFree 释放（T015）
  memset(vm, 0, sizeof(*vm));  // 零态收尾：不重建堆（避免每周期泄漏一个 GcHeap）；复用需再 msVmInit
}

bool msVmPush(MsVM* vm, MsValue v) {
  if (vm->stackTop >= MS_STACK_MAX) {
    vm->hasError = true;
    snprintf(vm->errMsg, sizeof(vm->errMsg), "StackOverflowError: value stack exceeded %d entries",
             MS_STACK_MAX);
    vm->errLine = -1;
    return false;
  }
  MS_ARRAY_PUSH(vm->stack, vm->stackTop, vm->stackCap, v);
  return true;
}

static MsValue pop(MsVM* vm) {
  MS_ASSERT(vm->stackTop > 0);  // 编译器生成的代码维持栈平衡
  return vm->stack[--vm->stackTop];
}

// 全局名 → intern 键（T015）：miss 建 PERMANENT 串——跨 GC 存活（未定义名探测的无害残留），
// 临时串入表前无分配窗口（intern.c 消费式契约）；NULL = OOM，调用方翻译 RuntimeError
static MsObjString* globalKey(MsVM* vm, const char* name, int32_t len) {
  return msInternBytes(vm, name, len);
}

// ---- 数值塔（T016，02 §3）：int/int 快路径（plat_math 溢出三件套）+ bigint 域 + float 域 ----

static bool intPow(ms_i64 base, ms_i64 e, ms_i64* r) {
  if (base >= -1 && base <= 1) {  // |base| ≤ 1：任意非负 e 结果在 {-1,0,1}
    *r = base == -1 ? (e % 2 == 0 ? 1 : -1) : base;
    return true;
  }
  if (e >= 64) {  // |base| ≥ 2 且 e ≥ 64 必溢出
    return false;
  }
  ms_i64 acc = 1;
  for (ms_i64 i = 0; i < e; i++) {
    if (msMulOverflowI64(acc, base, &acc)) {
      return false;
    }
  }
  *r = acc;
  return true;
}

typedef enum BinArith {
  BA_ADD,
  BA_SUB,
  BA_MUL,
  BA_DIV,
  BA_MOD,
  BA_POW,
  BA_BIT_AND,
  BA_BIT_OR,
  BA_BIT_XOR,
  BA_SHL,
  BA_SHR,
} BinArith;

static const char* kBinArithNames[] = {"+", "-", "*", "/", "%", "**", "&", "|", "^", "<<", ">>"};

static bool isNumVal(const MsValue* v) {  // 数值域（02 §3.4）：int/float/bigint（bool 排除）
  if (v->kind == MS_VAL_I64 || v->kind == MS_VAL_F64) {
    return true;
  }
  return msIsBigint(v);
}

static double numToF64(const MsValue* v) {
  return v->kind == MS_VAL_I64 ? (double)v->as.i
                               : (v->kind == MS_VAL_F64 ? v->as.f
                                                        : msObjBigintToF64((const MsObjBigInt*)v->as.obj));
}

// bigErr → 错误槽（已置槽错误如 StackOverflow 不覆盖）
static bool bigErrSet(MsVM* vm, MsBigErr e) {
  if (vm->hasError) {
    return false;
  }
  switch (e) {
    case MS_BIG_OOM:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
      break;
    case MS_BIG_PARSE:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid bigint literal");
      break;
    case MS_BIG_DIVZERO:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "ArithmeticError: division by zero");
      break;
    case MS_BIG_RANGE:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: bigint result too large");
      break;
    case MS_BIG_OK:
      break;
  }
  return false;
}

// int⊗bigint / bigint⊗{int,bigint} 全域（02 §3.4 表）。操作数为 C 局部（弹栈）：
// obj_bigint 层约定分配窗口后不再读输入 limbs → 安全（21 §3.2，同 string concat 契约）
static bool bigArith(MsVM* vm, BinArith k, MsValue a, MsValue b, MsValue* out) {
  MsBigView va, vb;
  uint32_t ba[2], bb[2];
  if (a.kind == MS_VAL_I64) {
    msBigViewI64(&va, ba, a.as.i);
  } else {
    msBigViewOf(&va, (const MsObjBigInt*)a.as.obj);
  }
  if (b.kind == MS_VAL_I64) {
    msBigViewI64(&vb, bb, b.as.i);
  } else {
    msBigViewOf(&vb, (const MsObjBigInt*)b.as.obj);
  }
  MsObjBigInt* r = NULL;
  MsBigErr e = MS_BIG_OK;
  if ((k == BA_DIV || k == BA_MOD) && vb.len == 0) {
    snprintf(vm->errMsg, sizeof(vm->errMsg),
             k == BA_DIV ? "ArithmeticError: division by zero" : "ArithmeticError: modulo by zero");
    return false;
  }
  switch (k) {
    case BA_ADD:
      e = msBigAdd(vm, &va, &vb, &r);
      break;
    case BA_SUB:
      e = msBigSub(vm, &va, &vb, &r);
      break;
    case BA_MUL:
      e = msBigMul(vm, &va, &vb, &r);
      break;
    case BA_DIV:
      e = msBigDiv(vm, &va, &vb, &r);
      break;
    case BA_MOD:
      e = msBigMod(vm, &va, &vb, &r);
      break;
    case BA_POW: {
      ms_i64 ev = 0;
      if (b.kind != MS_VAL_I64) {  // bigint 指数：可容 i64 且非负才可用
        if (!msObjBigintFitsI64((const MsObjBigInt*)b.as.obj, &ev) || ev < 0) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: pow exponent must be a non-negative int");
          return false;
        }
        e = msBigPow(vm, &va, ev, &r);
        break;
      }
      if (b.as.i < 0) {  // 负指数浮点幂（与 int 路径一致，02 §3）
        *out = msValueFloat(pow(numToF64(&a), (double)b.as.i));
        return true;
      }
      e = msBigPow(vm, &va, b.as.i, &r);
      break;
    }
    case BA_BIT_AND:
      e = msBigBitOp(vm, MS_BIG_BIT_AND, &va, &vb, &r);
      break;
    case BA_BIT_OR:
      e = msBigBitOp(vm, MS_BIG_BIT_OR, &va, &vb, &r);
      break;
    case BA_BIT_XOR:
      e = msBigBitOp(vm, MS_BIG_BIT_XOR, &va, &vb, &r);
      break;
    case BA_SHL:
    case BA_SHR: {
      if (b.kind != MS_VAL_I64) {
        snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: shift count must be int");
        return false;
      }
      if (b.as.i < 0) {
        snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: negative shift count");
        return false;
      }
      e = k == BA_SHL ? msBigShl(vm, &va, b.as.i, &r) : msBigShr(vm, &va, b.as.i, &r);
      break;
    }
  }
  if (e != MS_BIG_OK) {
    return bigErrSet(vm, e);
  }
  *out = msValueObj(&r->head);
  return true;
}

// 二元算术（T016 数值塔，02 §3.4）：int/int 快路径 + bigint 域 + float 域；失败 → false + errMsg
static bool arithBinary(MsVM* vm, BinArith k, MsValue a, MsValue b, MsValue* out) {
  bool aNum = isNumVal(&a);
  bool bNum = isNumVal(&b);
  if (aNum && bNum && a.kind == MS_VAL_I64 && b.kind == MS_VAL_I64) {  // int/int 快路径
    ms_i64 r = 0;
    switch (k) {
      case BA_ADD:
        if (msAddOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);  // 溢出升 bigint（02 §3.1）
        }
        break;
      case BA_SUB:
        if (msSubOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);
        }
        break;
      case BA_MUL:
        if (msMulOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);
        }
        break;
      case BA_DIV:
        if (b.as.i == 0) goto divZero;
        if (a.as.i == INT64_MIN && b.as.i == -1) {
          return bigArith(vm, k, a, b, out);  // 2^63 超 int64 → bigint
        }
        r = a.as.i / b.as.i;  // C 截断除法 == Go 语义（-7/2 == -3）
        break;
      case BA_MOD:
        if (b.as.i == 0) goto modZero;
        r = b.as.i == -1 ? 0 : a.as.i % b.as.i;  // INT64_MIN % -1 == 0（避免 UB）
        break;
      case BA_POW:
        if (b.as.i < 0) {  // 负指数转浮点幂（Python 语义）
          *out = msValueFloat(pow((double)a.as.i, (double)b.as.i));
          return true;
        }
        if (!intPow(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);  // 溢出升 bigint
        }
        break;
      case BA_SHL:
        if (b.as.i < 0) goto negShift;
        if (b.as.i >= 64) {
          return bigArith(vm, k, a, b, out);  // 超宽移位精确升 bigint（T016，替代 Go-0）
        }
        r = (ms_i64)((ms_u64)a.as.i << b.as.i);  // 无符号左移良定义；溢出检测用回移比对
        if ((r >> b.as.i) != a.as.i) {
          return bigArith(vm, k, a, b, out);  // 结果超 i64 → 精确升 bigint（与 binder 折叠/数值塔一致）
        }
        break;
      case BA_SHR:
        if (b.as.i < 0) goto negShift;
        r = b.as.i >= 64 ? (a.as.i < 0 ? -1 : 0) : a.as.i >> b.as.i;  // 算术右移 = floor
        break;
      case BA_BIT_AND:
        r = a.as.i & b.as.i;
        break;
      case BA_BIT_OR:
        r = a.as.i | b.as.i;
        break;
      case BA_BIT_XOR:
        r = a.as.i ^ b.as.i;
        break;
    }
    *out = msValueInt(r);
    return true;
  }
  if (aNum && bNum) {
    if (a.kind != MS_VAL_F64 && b.kind != MS_VAL_F64) {
      return bigArith(vm, k, a, b, out);  // bigint 域（02 §3.4 表）
    }
    if (k >= BA_BIT_AND) {  // 位运算拒绝 float（02 §3.1）
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: bit operation '%s' requires int or bigint operands",
               kBinArithNames[k]);
      return false;
    }
    double da = numToF64(&a);  // 含 float：升 double（bigint 转 double 允许精度损失，02 §3.3）
    double db = numToF64(&b);
    switch (k) {
      case BA_ADD:
        *out = msValueFloat(da + db);
        return true;
      case BA_SUB:
        *out = msValueFloat(da - db);
        return true;
      case BA_MUL:
        *out = msValueFloat(da * db);
        return true;
      case BA_DIV:
        if (db == 0.0) goto divZero;
        *out = msValueFloat(da / db);  // float 真除
        return true;
      case BA_MOD:
        if (db == 0.0) goto modZero;
        *out = msValueFloat(fmod(da, db));  // 符号随被除数 == C fmod
        return true;
      case BA_POW:
        *out = msValueFloat(pow(da, db));
        return true;
      default:
        break;  // 位运算已在上方拒绝（防御）
    }
    snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: unreachable float arith path");
    return false;
  }
  if (k == BA_ADD && a.kind == MS_VAL_OBJ && b.kind == MS_VAL_OBJ &&
      a.as.obj->type == MS_OBJ_STRING && b.as.obj->type == MS_OBJ_STRING) {
    const MsObjString* sa = (const MsObjString*)a.as.obj;
    const MsObjString* sb = (const MsObjString*)b.as.obj;
    MsObjString* r = msObjStringConcat(vm, sa, sb->bytes, (int32_t)sb->byteLen);
    if (r == NULL) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: string concatenation result too large");
      return false;
    }
    *out = msValueObj((MsObj*)r);
    return true;
  }
  if (k == BA_MUL && a.kind == MS_VAL_OBJ && a.as.obj->type == MS_OBJ_STRING &&
      b.kind == MS_VAL_I64) {
    MsObjString* s = msObjStringRepeat(vm, (const MsObjString*)a.as.obj, b.as.i);
    if (s != NULL) {
      *out = msValueObj((MsObj*)s);
      return true;
    }
    snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: string repetition result too large");
    return false;
  }
  snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: unsupported operand types for '%s': '%s' and '%s'",
           kBinArithNames[k], msValueTypeName(&a), msValueTypeName(&b));
  return false;
divZero:
  snprintf(vm->errMsg, sizeof(vm->errMsg), "ArithmeticError: division by zero");
  return false;
modZero:
  snprintf(vm->errMsg, sizeof(vm->errMsg), "ArithmeticError: modulo by zero");
  return false;
negShift:
  snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: negative shift count");
  return false;
}

// ---- dispatch 双路径框架（20 §7：GCC/Clang computed goto；MSVC switch） ----
// 单一指令处理体：OPCODE(op) 起标签/case，NEXT() 取下一条；PRE_DISPATCH 含错误检查与 trace

#define GLUE(a, b) a##b
#define LBL(op) GLUE(L_, op)

void msVmRun(MsVM* vm, const Chunk* chunk) {
  if (vm->hasError) {
    return;
  }
  if (vm->globals == NULL) {  // 全局表首入落堆（T015）：值栈此时为空，分配窗口无未根对象
    vm->globals = msObjMapNew(vm);
    if (vm->globals == NULL) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
      vm->errLine = -1;
      vm->hasError = true;
      return;
    }
  }
  MsFrame initFrame = {chunk, 0, 0};  // 本任务单模块帧（T018 起函数帧切换）
  MS_ARRAY_PUSH(vm->frames, vm->frameCount, vm->frameCap, initFrame);
  const ms_u8* code = chunk->code;
  const ConstEntry* consts = chunk->consts;
  int32_t ip = 0;
  int32_t extAcc = 0;    // EXTENDED_ARG 累积（21 §1）
  int32_t instrOff = 0;  // 当前指令起始偏移（trace / 行号）
  ms_u8 op = OP_NOP;

#define RUNTIME_ERROR(...)                                 \
  do {                                                     \
    snprintf(vm->errMsg, sizeof(vm->errMsg), __VA_ARGS__); \
    vm->errLine = chunkLineAt(chunk, instrOff);            \
    vm->hasError = true;                                   \
    goto dispatchExit;                                     \
  } while (0)

// READ_OPERAND(k)：EXT 链拼接操作数（const/jump），读后清零累积器
#define READ_OPERAND(k)                                                    \
  int32_t k = (int32_t)(((ms_u32)extAcc << 8) | (ms_u32)code[ip++]); \
  extAcc = 0

#define PRE_DISPATCH()                                          \
  do {                                                          \
    if (vm->hasError) {                                         \
      goto dispatchExit;                                        \
    }                                                           \
    instrOff = ip;                                              \
    op = code[ip++];                                            \
    if (vm->trace != NULL) {                                    \
      fprintf(vm->trace, "%04d %s\n", instrOff, msOpNames[op]); \
    }                                                           \
  } while (0)

#if MS_USE_COMPUTED_GOTO
  static void* dispatchTable[OP__COUNT];
  for (int i = 0; i < OP__COUNT; i++) {
    dispatchTable[i] = &&L_TRAP;  // 未启用指令统一陷阱（T010 §6 分组启用约定）
  }
#define SET_HANDLER(opname) dispatchTable[(opname)] = &&LBL(opname)
  SET_HANDLER(OP_NOP);
  SET_HANDLER(OP_EXTENDED_ARG);
  SET_HANDLER(OP_POP);
  SET_HANDLER(OP_DUP);
  SET_HANDLER(OP_DUP2);
  SET_HANDLER(OP_ROT);
  SET_HANDLER(OP_SWAP);
  SET_HANDLER(OP_LOAD_NIL);
  SET_HANDLER(OP_LOAD_TRUE);
  SET_HANDLER(OP_LOAD_FALSE);
  SET_HANDLER(OP_LOAD_CONST);
  SET_HANDLER(OP_LOAD_LOCAL);
  SET_HANDLER(OP_STORE_LOCAL);
  SET_HANDLER(OP_LOAD_GLOBAL);
  SET_HANDLER(OP_STORE_GLOBAL);
  SET_HANDLER(OP_DEF_GLOBAL);
  SET_HANDLER(OP_BINARY_ADD);
  SET_HANDLER(OP_BINARY_SUB);
  SET_HANDLER(OP_BINARY_MUL);
  SET_HANDLER(OP_BINARY_DIV);
  SET_HANDLER(OP_BINARY_MOD);
  SET_HANDLER(OP_BINARY_POW);
  SET_HANDLER(OP_BINARY_AND);
  SET_HANDLER(OP_BINARY_OR);
  SET_HANDLER(OP_BINARY_XOR);
  SET_HANDLER(OP_BINARY_SHL);
  SET_HANDLER(OP_BINARY_SHR);
  SET_HANDLER(OP_UNARY_NEG);
  SET_HANDLER(OP_UNARY_NOT);
  SET_HANDLER(OP_UNARY_INVERT);
  SET_HANDLER(OP_COMPARE_EQ);
  SET_HANDLER(OP_COMPARE_NE);
  SET_HANDLER(OP_COMPARE_LT);
  SET_HANDLER(OP_COMPARE_LE);
  SET_HANDLER(OP_COMPARE_GT);
  SET_HANDLER(OP_COMPARE_GE);
  SET_HANDLER(OP_IS);
  SET_HANDLER(OP_IN);
  SET_HANDLER(OP_GET_INDEX);
  SET_HANDLER(OP_SET_INDEX);
  SET_HANDLER(OP_BUILD_SLICE);
  SET_HANDLER(OP_MAKE_FUNCTION);
  SET_HANDLER(OP_CALL);
  SET_HANDLER(OP_CALL_METHOD);
  SET_HANDLER(OP_RETURN);
  SET_HANDLER(OP_GET_ATTR);
  SET_HANDLER(OP_JMP);
  SET_HANDLER(OP_JMP_IF_FALSE);
  SET_HANDLER(OP_JMP_IF_TRUE);
  SET_HANDLER(OP_JMP_IF_FALSE_OR_POP);
  SET_HANDLER(OP_JMP_IF_TRUE_OR_POP);
  SET_HANDLER(OP_POP_JUMP_IF_NIL);
  SET_HANDLER(OP_CHECK_TYPE);
  SET_HANDLER(OP_GET_ITER);
  SET_HANDLER(OP_FOR_ITER);
  SET_HANDLER(OP_BUILD_LIST);
  SET_HANDLER(OP_BUILD_MAP);
  SET_HANDLER(OP_BUILD_TUPLE);
  SET_HANDLER(OP_BUILD_SET);
  SET_HANDLER(OP_UNPACK_SEQ);
  SET_HANDLER(OP_UNPACK_STAR);
  SET_HANDLER(OP_FORMAT_STRING);
  SET_HANDLER(OP_CHECK_SAFEPOINT);
#define OPCODE(opname) LBL(opname):
#define NEXT()               \
  do {                       \
    PRE_DISPATCH();          \
    goto *dispatchTable[op]; \
  } while (0)
#define TRAP_DEFAULT() L_TRAP:
  PRE_DISPATCH();
  goto *dispatchTable[op];
#else
#define OPCODE(opname) case opname:
#define NEXT() break
#define TRAP_DEFAULT() default:
  for (;;) {
    PRE_DISPATCH();
    switch (op) {
#endif

      OPCODE(OP_NOP)
      NEXT();

      OPCODE(OP_EXTENDED_ARG)
      extAcc = (int32_t)(((ms_u32)extAcc << 8) | (ms_u32)code[ip++]);
      NEXT();

      OPCODE(OP_POP)
      (void)pop(vm);
      NEXT();

      OPCODE(OP_DUP) {
        MsValue v = vm->stack[vm->stackTop - 1];
        msVmPush(vm, v);
        NEXT();
      }

      OPCODE(OP_DUP2) {  // a b → a b a b
        MsValue a = vm->stack[vm->stackTop - 2];
        msVmPush(vm, a);
        MsValue b = vm->stack[vm->stackTop - 2];
        msVmPush(vm, b);
        NEXT();
      }

      OPCODE(OP_ROT) {  // a b c → b c a
        MsValue a = vm->stack[vm->stackTop - 3];
        vm->stack[vm->stackTop - 3] = vm->stack[vm->stackTop - 2];
        vm->stack[vm->stackTop - 2] = vm->stack[vm->stackTop - 1];
        vm->stack[vm->stackTop - 1] = a;
        NEXT();
      }

      OPCODE(OP_SWAP) {
        MsValue t = vm->stack[vm->stackTop - 1];
        vm->stack[vm->stackTop - 1] = vm->stack[vm->stackTop - 2];
        vm->stack[vm->stackTop - 2] = t;
        NEXT();
      }

      OPCODE(OP_LOAD_NIL)
      msVmPush(vm, msValueNil());
      NEXT();

      OPCODE(OP_LOAD_TRUE)
      msVmPush(vm, msValueBool(true));
      NEXT();

      OPCODE(OP_LOAD_FALSE)
      msVmPush(vm, msValueBool(false));
      NEXT();

      OPCODE(OP_LOAD_CONST) {
        READ_OPERAND(k);
        bool ok = false;
        MsValue v = msValueFromConst(vm, &consts[k], &ok);
        if (!ok) {
          RUNTIME_ERROR("RuntimeError: invalid constant (interp strings land in Task 11)");
        }
        msVmPush(vm, v);
        NEXT();
      }

      OPCODE(OP_LOAD_LOCAL) {
        int32_t slot = code[ip++];
        msVmPush(vm, vm->stack[vm->frames[vm->frameCount - 1].base + slot]);
        NEXT();
      }

      OPCODE(OP_STORE_LOCAL) {
        int32_t slot = code[ip++];
        vm->stack[vm->frames[vm->frameCount - 1].base + slot] = pop(vm);
        NEXT();
      }

      OPCODE(OP_LOAD_GLOBAL) {
        READ_OPERAND(k);
        const ConstEntry* name = &consts[k];
        MsObjString* key = globalKey(vm, name->as.text.start, name->as.text.len);
        if (key == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsValue kv = msValueObj(&key->head);
        MsValue v;
        if (msObjMapGet(vm->globals, &kv, &v) == MS_MAP_OK) {
          msVmPush(vm, v);
          NEXT();
        }
        MsValue builtin = msValueNil();
        if (msBuiltinLookup(name->as.text.start, name->as.text.len, &builtin)) {
          msVmPush(vm, builtin);
          NEXT();
        }
        RUNTIME_ERROR("RuntimeError: undefined name '%.*s'", (int)name->as.text.len, name->as.text.start);
      }

      OPCODE(OP_STORE_GLOBAL) {
        READ_OPERAND(k);
        const ConstEntry* name = &consts[k];
        // GC 纪律（T015 关键序）：先建键、值留栈——intern miss 分配窗口内若值已弹入 C 局部即失根，
        // GC 触发则被收后再写入即悬垂；故 pop 推迟到 put 成功之后
        MsObjString* key = globalKey(vm, name->as.text.start, name->as.text.len);
        if (key == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsValue kv = msValueObj(&key->head);
        MsValue cur;
        if (msObjMapGet(vm->globals, &kv, &cur) != MS_MAP_OK) {  // 键在而值 nil 亦 OK（按槽态非值判存）
          RUNTIME_ERROR("RuntimeError: assignment to undefined name '%.*s'", (int)name->as.text.len,
                        name->as.text.start);
        }
        if (msObjMapPut(vm, vm->globals, &kv, &vm->stack[vm->stackTop - 1]) == MS_MAP_OOM) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        vm->stackTop--;  // put 成功后才弹：put 再哈希分配窗口内值仍在栈 root
        NEXT();
      }

      // peek TOS 定义/覆盖（STORE_GLOBAL 随后弹出落盘；
      // 21 §4 声明形态；put 同键原位换值 = 后定义覆盖）
      OPCODE(OP_DEF_GLOBAL) {
        READ_OPERAND(k);
        const ConstEntry* name = &consts[k];
        // GC 纪律：peek 不弹——intern miss 与 put 再哈希的分配窗口内值均在栈 root
        MsObjString* key = globalKey(vm, name->as.text.start, name->as.text.len);
        if (key == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsValue kv = msValueObj(&key->head);
        if (msObjMapPut(vm, vm->globals, &kv, &vm->stack[vm->stackTop - 1]) == MS_MAP_OOM) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        NEXT();
      }

// 分配纪律：两操作数先弹入 C 局部再进 arithBinary——string concat/repeat 的分配窗口内操作数
// 不在栈 root。安全依赖 msObjStringConcat/msObjStringRepeat 的次序契约：源字节先 memcpy 进
// malloc 缓冲、之后才经 newString 触发 GC（见 obj_string.c），此序不可倒。
#define BINARY_ARITH_CASE(opname, kind)       \
  OPCODE(opname) {                            \
    MsValue b = pop(vm);                      \
    MsValue a = pop(vm);                      \
    MsValue r;                                \
    if (!arithBinary(vm, (kind), a, b, &r)) { \
      RUNTIME_ERROR("%s", vm->errMsg);        \
    }                                         \
    msVmPush(vm, r);                              \
    NEXT();                                   \
  }

      OPCODE(OP_BINARY_ADD) {  // list+list 拼接 peek（分配窗口 operands 栈 root）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_LIST &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_LIST) {
          MsObjList* r = msObjListConcat(vm, (MsObjList*)vm->stack[vm->stackTop - 2].as.obj,
                                         (MsObjList*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_ADD, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      OPCODE(OP_BINARY_MUL) {  // list*int 重复 peek（02 §5.1 单向，同 string*int）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_LIST &&
            vm->stack[vm->stackTop - 1].kind == MS_VAL_I64) {
          MsObjList* r = msObjListRepeat(vm, (MsObjList*)vm->stack[vm->stackTop - 2].as.obj,
                                         vm->stack[vm->stackTop - 1].as.i);
          if (r == NULL) {
            RUNTIME_ERROR("OverflowError: list repetition result too large");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_MUL, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      OPCODE(OP_BINARY_SUB) {  // set-set 差集 peek（02 §5.4）：操作数留栈 root 过分配窗口；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_SET &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_SET) {
          MsObjSet* r = msObjSetDiff(vm, (MsObjSet*)vm->stack[vm->stackTop - 2].as.obj,
                                     (MsObjSet*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_SUB, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      BINARY_ARITH_CASE(OP_BINARY_DIV, BA_DIV)
      BINARY_ARITH_CASE(OP_BINARY_MOD, BA_MOD)
      BINARY_ARITH_CASE(OP_BINARY_POW, BA_POW)

      OPCODE(OP_BINARY_AND) {  // set-set 交集 peek（02 §5.4）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_SET &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_SET) {
          MsObjSet* r = msObjSetIntersect(vm, (MsObjSet*)vm->stack[vm->stackTop - 2].as.obj,
                                          (MsObjSet*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_BIT_AND, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      OPCODE(OP_BINARY_OR) {  // set-set 并集 peek（02 §5.4）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_SET &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_SET) {
          MsObjSet* r = msObjSetUnion(vm, (MsObjSet*)vm->stack[vm->stackTop - 2].as.obj,
                                      (MsObjSet*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_BIT_OR, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      OPCODE(OP_BINARY_XOR) {  // set-set 对称差 peek（02 §5.4）；其余走 arithBinary
        if (vm->stack[vm->stackTop - 1].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 1].as.obj->type == MS_OBJ_SET &&
            vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_SET) {
          MsObjSet* r = msObjSetSymDiff(vm, (MsObjSet*)vm->stack[vm->stackTop - 2].as.obj,
                                        (MsObjSet*)vm->stack[vm->stackTop - 1].as.obj);
          if (r == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stackTop -= 2;
          msVmPush(vm, msValueObj(&r->head));
          NEXT();
        }
        MsValue b = pop(vm);
        MsValue a = pop(vm);
        MsValue r;
        if (!arithBinary(vm, BA_BIT_XOR, a, b, &r)) {
          RUNTIME_ERROR("%s", vm->errMsg);
        }
        msVmPush(vm, r);
        NEXT();
      }

      BINARY_ARITH_CASE(OP_BINARY_SHL, BA_SHL)
      BINARY_ARITH_CASE(OP_BINARY_SHR, BA_SHR)

      OPCODE(OP_UNARY_NEG) {
        MsValue a = pop(vm);
        if (a.kind == MS_VAL_I64) {
          if (a.as.i == INT64_MIN) {  // 溢出升 bigint：-INT64_MIN = 2^63（02 §3.1）
            uint32_t limbs[2] = {0, 0x80000000u};
            MsObjBigInt* r = msObjBigintNew(vm, limbs, 2, 1);
            if (r == NULL) {
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            msVmPush(vm, msValueObj(&r->head));
            NEXT();
          }
          msVmPush(vm, msValueInt(-a.as.i));
        } else if (a.kind == MS_VAL_F64) {
          msVmPush(vm, msValueFloat(-a.as.f));
        } else if (msIsBigint(&a)) {  // T016 bigint 取负
          MsBigView v;
          msBigViewOf(&v, (const MsObjBigInt*)a.as.obj);
          MsObjBigInt* r = NULL;
          if (msBigNeg(vm, &v, &r) != MS_BIG_OK) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          msVmPush(vm, msValueObj(&r->head));
        } else {
          RUNTIME_ERROR("TypeError: unary '-' requires a numeric operand, got '%s'", msValueTypeName(&a));
        }
        NEXT();
      }

      OPCODE(OP_UNARY_NOT) {  // ! 返回 bool（03 §2）：真值取反
        MsValue a = pop(vm);
        msVmPush(vm, msValueBool(!msValueIsTruthy(&a)));
        NEXT();
      }

      OPCODE(OP_UNARY_INVERT) {
        MsValue a = pop(vm);
        if (a.kind == MS_VAL_I64) {
          msVmPush(vm, msValueInt(~a.as.i));
        } else if (msIsBigint(&a)) {  // ~a = -(a+1)
          MsBigView v;
          msBigViewOf(&v, (const MsObjBigInt*)a.as.obj);
          MsObjBigInt* r = NULL;
          if (msBigNot(vm, &v, &r) != MS_BIG_OK) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          msVmPush(vm, msValueObj(&r->head));
        } else {
          RUNTIME_ERROR("TypeError: '~' requires int or bigint operand, got '%s'", msValueTypeName(&a));
        }
        NEXT();
      }

#define COMPARE_CASE(opname, expr) \
  OPCODE(opname) {                 \
    MsValue b = pop(vm);           \
    MsValue a = pop(vm);           \
    msVmPush(vm, msValueBool(expr));   \
    NEXT();                        \
  }

      COMPARE_CASE(OP_COMPARE_EQ, msValueEqual(&a, &b))
      COMPARE_CASE(OP_COMPARE_NE, !msValueEqual(&a, &b))
      COMPARE_CASE(OP_IS, msValueEqual(&a, &b))  // T011 即值同一；堆对象身份 T012 修正

#define ORDERED_COMPARE_CASE(opname, symbol, okExpr)                     \
  OPCODE(opname) {                                                       \
    MsValue b = pop(vm);                                                 \
    MsValue a = pop(vm);                                                 \
    int32_t cmp = 0;                                                     \
    if (!msValueOrder(&a, &b, &cmp)) {                                   \
      RUNTIME_ERROR("TypeError: '%s' not supported between '%s' and '%s'", symbol, \
                    msValueTypeName(&a), msValueTypeName(&b));           \
    }                                                                    \
    msVmPush(vm, msValueBool(okExpr));                                       \
    NEXT();                                                              \
  }

      ORDERED_COMPARE_CASE(OP_COMPARE_LT, "<", cmp < 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_LE, "<=", cmp <= 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_GT, ">", cmp > 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_GE, ">=", cmp >= 0)

      OPCODE(OP_IN) {  // 03 §5：probe in container（__contains__ 协议 T021；此处内置容器路径）
        MsValue b = pop(vm);  // 编译序 lhs=probe → rhs=container（21 §3）：先弹容器
        MsValue a = pop(vm);
        bool hit = false;
        if (msIsString(&b)) {  // string 语义为子串包含；probe 须 string（03 §5）
          if (!msIsString(&a)) {
            RUNTIME_ERROR("TypeError: 'in' on string expects a string operand");
          }
          const MsObjString* h = (const MsObjString*)b.as.obj;
          const MsObjString* n = (const MsObjString*)a.as.obj;
          for (int32_t i = 0; !hit && (ms_i64)i + n->byteLen <= (ms_i64)h->byteLen; i++) {
            // 字节子串扫描（Windows 无 memmem；空 probe 恒命中）
            hit = n->byteLen == 0 ||
                  (h->bytes[i] == n->bytes[0] && memcmp(h->bytes + i, n->bytes, n->byteLen) == 0);
          }
        } else if (b.kind == MS_VAL_OBJ && b.as.obj->type == MS_OBJ_LIST) {
          hit = msObjListIndexOf((const MsObjList*)b.as.obj, &a) >= 0;
        } else if (b.kind == MS_VAL_OBJ && b.as.obj->type == MS_OBJ_TUPLE) {  // 定长线性深探测
          const MsObjTuple* t = (const MsObjTuple*)b.as.obj;
          for (int32_t i = 0; !hit && i < t->count; i++) {
            hit = msValueEqual(&t->items[i], &a);
          }
        } else if (b.kind == MS_VAL_OBJ &&
                   (b.as.obj->type == MS_OBJ_MAP || b.as.obj->type == MS_OBJ_SET)) {
          // map 键 / set 元素：set 直探内层 map——msObjSetHas 对不可 hash 恒 false，无法区分 TypeError
          const MsObjMap* m = b.as.obj->type == MS_OBJ_MAP ? (const MsObjMap*)b.as.obj
                                                           : ((const MsObjSet*)b.as.obj)->map;
          MsMapOp mop = msObjMapGet(m, &a, NULL);  // 无分配路径：弹栈后局部引用安全
          if (mop == MS_MAP_UNHASHABLE) {
            RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&a));
          }
          hit = mop == MS_MAP_OK;
        } else {
          RUNTIME_ERROR("TypeError: 'in' not supported for '%s'", msValueTypeName(&b));
        }
        msVmPush(vm, msValueBool(hit));
        NEXT();
      }

      OPCODE(OP_GET_INDEX) {  // peek 求值完成后再弹栈（分配窗口内保 root；slice 分支同守 peek 纪律）
        MsValue idx = vm->stack[vm->stackTop - 1];
        MsValue obj = vm->stack[vm->stackTop - 2];
        if (msIsString(&obj)) {
          MsObjString* s = (MsObjString*)obj.as.obj;
          if (idx.kind == MS_VAL_I64) {
            ms_u32 cp = 0;
            if (!msObjStringRuneAt(vm, s, idx.as.i, &cp)) {
              RUNTIME_ERROR("IndexError: string index out of range");
            }
            vm->stackTop -= 2;
            msVmPush(vm, msValueInt((ms_i64)cp));
            NEXT();
          }
          if (idx.kind == MS_VAL_SLICE) {
            bool zero = false;
            MsObjString* r = msObjStringSlice(vm, s, (idx.as.slice.mask & 1) != 0, idx.as.slice.start,
                                              (idx.as.slice.mask & 2) != 0, idx.as.slice.end,
                                              (idx.as.slice.mask & 4) != 0, idx.as.slice.step, &zero);
            if (r == NULL) {  // s 在栈上保 root：msObjStringSlice 分配期间不被回收
              if (zero) {
                RUNTIME_ERROR("ValueError: slice step must not be zero");
              }
              RUNTIME_ERROR("OverflowError: string slice result too large");
            }
            vm->stackTop -= 2;
            msVmPush(vm, msValueObj(&r->head));
            NEXT();
          }
          RUNTIME_ERROR("TypeError: string indices must be int or slice");
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {
          if (idx.kind == MS_VAL_SLICE) {  // T014：切片返回新 list（源 peek 保 root）
            bool zero = false;
            MsObjList* r = msObjListSlice(vm, (MsObjList*)obj.as.obj, (idx.as.slice.mask & 1) != 0,
                                          idx.as.slice.start, (idx.as.slice.mask & 2) != 0, idx.as.slice.end,
                                          (idx.as.slice.mask & 4) != 0, idx.as.slice.step, &zero);
            if (r == NULL) {
              if (zero) {
                RUNTIME_ERROR("ValueError: slice step must not be zero");
              }
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            vm->stackTop -= 2;
            msVmPush(vm, msValueObj(&r->head));
            NEXT();
          }
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: list indices must be int");
          }
          MsValue item;
          if (!msObjListGet((MsObjList*)obj.as.obj, idx.as.i, &item)) {
            RUNTIME_ERROR("IndexError: list index out of range");
          }
          vm->stackTop -= 2;
          msVmPush(vm, item);
          NEXT();
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_MAP) {  // T015：任意 hashable 键（02 §5.2）
          MsValue out = msValueNil();
          MsMapOp mop = msObjMapGet((const MsObjMap*)obj.as.obj, &idx, &out);  // 无分配：peek 期无 GC 点
          if (mop == MS_MAP_OK) {
            vm->stackTop -= 2;
            msVmPush(vm, out);
            NEXT();
          }
          if (mop == MS_MAP_NOENT) {
            RUNTIME_ERROR("KeyError: key not found");
          }
          RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&idx));
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_TUPLE) {  // T015：负索引换算；slice 暂不涉
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: tuple indices must be int");
          }
          const MsValue* item = msObjTupleGet((const MsObjTuple*)obj.as.obj, idx.as.i);
          if (item == NULL) {
            RUNTIME_ERROR("IndexError: tuple index out of range");
          }
          vm->stackTop -= 2;
          msVmPush(vm, *item);
          NEXT();
        }
        // set 不支持索引：落入下方 not indexable 兜底（02 §5.4）
        RUNTIME_ERROR("TypeError: '%s' is not indexable", msValueTypeName(&obj));
      }

      OPCODE(OP_SET_INDEX) {  // 弹序 val/idx/obj（03 §4 求值序 obj→idx→v 已在编译侧定型）
        MsValue val = pop(vm);
        MsValue idx = pop(vm);
        MsValue obj = pop(vm);
        // T015：put 再哈希为 malloc 路径（无 GC 点）——弹栈后局部引用安全（02 §5.2）
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_MAP) {
          MsMapOp mop = msObjMapPut(vm, (MsObjMap*)obj.as.obj, &idx, &val);
          if (mop == MS_MAP_UNHASHABLE) {
            RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&idx));
          }
          if (mop == MS_MAP_OOM) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          NEXT();
        }
        if (msIsString(&obj)) {
          RUNTIME_ERROR("TypeError: 'string' does not support item assignment");
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {  // T014：负索引/越界
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: list indices must be int");
          }
          if (!msObjListSet((MsObjList*)obj.as.obj, idx.as.i, val)) {
            RUNTIME_ERROR("IndexError: list index out of range");
          }
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' does not support item assignment", msValueTypeName(&obj));
      }

      OPCODE(OP_BUILD_SLICE) {  // <u8 argc=3>：start/end/step（nil = 省略）→ MS_VAL_SLICE（无分配）
        (void)code[ip++];
        MsValue step = vm->stack[vm->stackTop - 1];
        MsValue end = vm->stack[vm->stackTop - 2];
        MsValue start = vm->stack[vm->stackTop - 3];
        if ((start.kind != MS_VAL_NIL && start.kind != MS_VAL_I64) ||
            (end.kind != MS_VAL_NIL && end.kind != MS_VAL_I64) ||
            (step.kind != MS_VAL_NIL && step.kind != MS_VAL_I64)) {
          RUNTIME_ERROR("TypeError: slice indices must be int");
        }
        vm->stackTop -= 3;
        MsValue v = {MS_VAL_SLICE, {0}};
        v.as.slice.start = start.kind == MS_VAL_I64 ? start.as.i : 0;
        v.as.slice.end = end.kind == MS_VAL_I64 ? end.as.i : 0;
        v.as.slice.step = step.kind == MS_VAL_I64 ? step.as.i : 0;
        v.as.slice.mask = (ms_u8)((start.kind == MS_VAL_I64 ? 1 : 0) | (end.kind == MS_VAL_I64 ? 2 : 0) |
                                  (step.kind == MS_VAL_I64 ? 4 : 0));
        msVmPush(vm, v);
        NEXT();
      }

      OPCODE(OP_MAKE_FUNCTION) {  // <u8 flags>（默认参/rest 形态 T018）
        (void)code[ip++];
        MsValue proto = pop(vm);
        msVmPush(vm, proto);  // T011：函数值即 chunk 引用，闭包捕获 T018
        NEXT();
      }

      OPCODE(OP_CALL) {
        int32_t argc = code[ip++];
        MsValue* args = &vm->stack[vm->stackTop - argc];
        MsValue callee = vm->stack[vm->stackTop - argc - 1];
        if (callee.kind == MS_VAL_OBJ && callee.as.obj->type == MS_OBJ_BOUND_METHOD) {
          // bound 分派在弹栈前：接收者+实参原位留栈（调用期间为 GC root）
          MsObjBoundMethod* bm = (MsObjBoundMethod*)callee.as.obj;
          MsValue recv = bm->receiver;
          MsNativeFn fn = bm->method->fn;  // 先读出：调用期间 bm/native 可能被回收（fn/recv 已在寄存器）
          vm->stack[vm->stackTop - argc - 1] = recv;  // callee 槽改写为接收者，args 原位
          MsValue r = fn(vm, argc + 1, &vm->stack[vm->stackTop - argc - 1]);
          if (vm->hasError) {
            goto dispatchExit;
          }
          vm->stackTop -= argc + 1;
          msVmPush(vm, r);
          NEXT();
        }
        if (callee.kind == MS_VAL_NATIVE) {
          // native 分派在弹栈前：callee+实参原位留栈（调用期间为 GC root，与 bound 路径同纪律）；
          // print/str/repr 内部分配触发回收，实参若先弹即成白对象（Task 11 D10）
          MsValue r = callee.as.native(vm, argc, args);
          if (vm->hasError) {
            goto dispatchExit;
          }
          vm->stackTop -= argc + 1;
          msVmPush(vm, r);
          NEXT();
        }
        if (callee.kind == MS_VAL_FUNCTION) {
          RUNTIME_ERROR("RuntimeError: script function calls execute in T018");
        }
        RUNTIME_ERROR("TypeError: '%s' is not callable", msValueTypeName(&callee));
      }

      OPCODE(OP_CALL_METHOD) {  // <const name> <u8 ic> <u8 argc>：零分配直调（args[0]=接收者）
        READ_OPERAND(k);
        (void)code[ip++];  // icSlot（T022）
        int32_t argc = code[ip++];
        MsValue* args = &vm->stack[vm->stackTop - argc - 1];  // peek：接收者+实参均在栈
        MsMethodRef ref;
        if (methodRefLookup(&args[0], consts[k].as.text.start, consts[k].as.text.len, &ref) == 0) {
          MsValue r = ref.fn(vm, argc + 1, args);
          vm->stackTop -= argc + 1;
          if (vm->hasError) {  // 方法内置槽：统一 unwind
            goto dispatchExit;
          }
          msVmPush(vm, r);
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' has no method '%.*s'", msValueTypeName(&args[0]),
                      (int)consts[k].as.text.len, consts[k].as.text.start);
      }

      OPCODE(OP_RETURN)
      (void)pop(vm);  // 模块帧返回值丢弃；帧切换 T018
      vm->frameCount--;
      goto dispatchExit;

      OPCODE(OP_GET_ATTR) {  // <const name> <u8 ic>：string 方法物化 bound（类属性 T020）
        READ_OPERAND(k);
        (void)code[ip++];  // icSlot（T022）
        MsValue recv = vm->stack[vm->stackTop - 1];  // peek：物化分配期间接收者在栈
        MsMethodRef ref;
        if (methodRefLookup(&recv, consts[k].as.text.start, consts[k].as.text.len, &ref) == 0) {
          MsObjNative* native = msObjNativeNew(vm, ref.fn, ref.name, ref.nameLen);
          if (native == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          msVmPush(vm, msValueObj(&native->head));  // native 落栈保活（接收者槽未动：两次分配均有 Root）
          if (vm->hasError) {  // push 溢出：后续裸栈操作（-2 槽）会越界，须立即 unwind（Task 8 review）
            goto dispatchExit;
          }
          MsObjBoundMethod* bound = msObjBoundNew(vm, recv, native);
          if (bound == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 2] = msValueObj(&bound->head);  // bound 覆盖接收者槽
          vm->stackTop--;  // 弹 native
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' has no attribute '%.*s'", msValueTypeName(&recv),
                      (int)consts[k].as.text.len, consts[k].as.text.start);
      }

      OPCODE(OP_JMP) {
        READ_OPERAND(t);
        ip = t;
        NEXT();
      }

      OPCODE(OP_JMP_IF_FALSE) {
        READ_OPERAND(t);
        MsValue v = pop(vm);
        if (!msValueIsTruthy(&v)) {
          ip = t;
        }
        NEXT();
      }

      OPCODE(OP_JMP_IF_TRUE) {
        READ_OPERAND(t);
        MsValue v = pop(vm);
        if (msValueIsTruthy(&v)) {
          ip = t;
        }
        NEXT();
      }

      OPCODE(OP_JMP_IF_FALSE_OR_POP) {  // &&：假跳保留值 / 真弹出
        READ_OPERAND(t);
        if (!msValueIsTruthy(&vm->stack[vm->stackTop - 1])) {
          ip = t;
        } else {
          (void)pop(vm);
        }
        NEXT();
      }

      OPCODE(OP_JMP_IF_TRUE_OR_POP) {  // ||：真跳保留值 / 假弹出
        READ_OPERAND(t);
        if (msValueIsTruthy(&vm->stack[vm->stackTop - 1])) {
          ip = t;
        } else {
          (void)pop(vm);
        }
        NEXT();
      }

      OPCODE(OP_POP_JUMP_IF_NIL) {
        READ_OPERAND(t);
        MsValue v = pop(vm);
        if (v.kind == MS_VAL_NIL) {
          ip = t;
        }
        NEXT();
      }

      OPCODE(OP_CHECK_TYPE) {  // 类型 switch 比较链（class 归 T020）：弹值压 bool
        READ_OPERAND(k);
        MsValue v = pop(vm);
        const ConstEntry* name = &consts[k];
        const char* tn = msValueTypeName(&v);
        msVmPush(vm, msValueBool((int32_t)strlen(tn) == name->as.text.len &&
                             memcmp(tn, name->as.text.start, (size_t)name->as.text.len) == 0));
        NEXT();
      }

      OPCODE(OP_GET_ITER) {  // 迭代器直通（range/对象）；string → 新 rune 迭代器（D8；协议 T017 泛化）
        MsValue v = vm->stack[vm->stackTop - 1];  // peek：迭代器分配期间接收者在栈保 root
        if (v.kind == MS_VAL_RANGE_ITER ||
            (v.kind == MS_VAL_OBJ && v.as.obj->type == MS_OBJ_ITERATOR)) {
          NEXT();
        }
        if (msIsString(&v)) {
          MsObjStrIter* it = msObjStrIterNew(vm, (MsObjString*)v.as.obj, false);
          if (it == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 1] = msValueObj(&it->head);  // 覆盖接收者槽：迭代器持 str
          NEXT();
        }
        if (v.kind == MS_VAL_OBJ && v.as.obj->type == MS_OBJ_LIST) {
          MsObjListIter* it = msObjListIterNew(vm, (MsObjList*)v.as.obj);  // peek：分配期间源在栈
          if (it == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 1] = msValueObj(&it->head);  // 覆盖接收者槽：迭代器持 list
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' is not iterable", msValueTypeName(&v));
      }

      OPCODE(OP_FOR_ITER) {  // <t exit>：有值压值；耗尽弹迭代器并跳出
        READ_OPERAND(t);
        MsValue* it = &vm->stack[vm->stackTop - 1];
        if (it->kind == MS_VAL_OBJ && it->as.obj->type == MS_OBJ_ITERATOR) {
          ms_u8 ik = ((const MsObjIterShell*)it->as.obj)->kind;  // str/list 首载荷判别（T014）
          MsValue item;
          bool modified = false;
          bool done;
          if (ik == MS_ITER_LIST) {  // 22 §4：版本号防并发修改
            done = !msListIterNext((MsObjListIter*)it->as.obj, &item, &modified);
          } else {
            done = !msStrIterNext(vm, (MsObjStrIter*)it->as.obj, &item);  // D8
          }
          if (done) {
            if (modified) {
              RUNTIME_ERROR("IteratorError: list modified during iteration");
            }
            (void)pop(vm);  // 耗尽弹迭代器（再迭代须重新 GET_ITER）
            ip = t;
            NEXT();
          }
          msVmPush(vm, item);  // 两迭代器 next 均无分配：it 槽位此后不再经缓存指针访问
          NEXT();
        }
        if (it->kind != MS_VAL_RANGE_ITER) {
          RUNTIME_ERROR("TypeError: FOR_ITER over non-iterator '%s'", msValueTypeName(it));
        }
        ms_i64 cur = it->as.range.cur;
        ms_i64 stop = it->as.range.stop;
        ms_i64 step = it->as.range.step;
        bool has = step > 0 ? cur < stop : cur > stop;
        if (!has) {
          (void)pop(vm);
          ip = t;
          NEXT();
        }
        msVmPush(vm, msValueInt(cur));
        it = &vm->stack[vm->stackTop - 2];  // push 可能扩容：重取迭代器槽位（-1 为新压入值）
        // 推进（饱和到 stop 防 INT64 边界回绕）
        if (step > 0 ? cur > INT64_MAX - step : cur < INT64_MIN - step) {
          it->as.range.cur = stop;
        } else {
          it->as.range.cur = cur + step;
        }
        NEXT();
      }

      OPCODE(OP_BUILD_LIST) {  // <u8 n>：peek 直填（循环零分配）后统一弹栈（D6；GC 纪律）
        int32_t n = code[ip++];
        MsObjList* list = msObjListNew(vm);
        if (list == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&list->head));  // list 落栈保活
        if (vm->hasError) {  // push 溢出：后续槽位腾挪（-1-n 索引）会越界，须立即 unwind（同 OP_GET_ATTR）
          goto dispatchExit;
        }
        if (!msObjListReserve(vm, list, n)) {  // 分配点：n 个源值仍在栈上安全
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        const MsValue* src = &vm->stack[vm->stackTop - 1 - n];  // 首元素（list 之下）
        for (int32_t i = 0; i < n; i++) {
          list->items[i] = src[i];
        }
        list->count = n;
        vm->stack[vm->stackTop - n - 1] = vm->stack[vm->stackTop - 1];  // list 下移覆盖首源槽
        vm->stackTop -= n;  // 弹出 n 个源值，list 留栈顶（n=0 时为自赋值无害）
        NEXT();
      }

      OPCODE(OP_BUILD_MAP) {  // <u8 2n>：k1 v1 … kn vn → map（重键后者胜，序保持首插位；21 §3.9）
        int32_t n = code[ip++] / 2;
        MsObjMap* map = msObjMapNew(vm);
        if (map == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&map->head));  // map 落栈保活（源值在其下，均 root）
        if (vm->hasError) {  // push 溢出：后续槽位腾挪（-1-2n 索引）会越界，须立即 unwind（同 OP_BUILD_LIST）
          goto dispatchExit;
        }
        const MsValue* src = &vm->stack[vm->stackTop - 1 - 2 * n];  // 首键（map 之下）
        for (int32_t i = 0; i < n; i++) {  // put 再哈希为 malloc 路径，无 GC 窗口
          MsMapOp mop = msObjMapPut(vm, map, &src[2 * i], &src[2 * i + 1]);
          if (mop == MS_MAP_UNHASHABLE) {
            RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&src[2 * i]));
          }
          if (mop == MS_MAP_OOM) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
        }
        vm->stack[vm->stackTop - 2 * n - 1] = vm->stack[vm->stackTop - 1];  // map 下移覆盖首源槽
        vm->stackTop -= 2 * n;  // 弹出 2n 个源值，map 留栈顶（n=0 时为自赋值无害）
        NEXT();
      }

      OPCODE(OP_BUILD_TUPLE) {  // <u8 n>：peek 直填（单块 FAM 分配，items 预 nil）后统一弹栈（D6）
        int32_t n = code[ip++];
        MsObjTuple* t = msObjTupleNew(vm, n);
        if (t == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&t->head));  // tuple 落栈保活
        if (vm->hasError) {
          goto dispatchExit;  // push 溢出：裸槽位运算前必须 unwind（同 OP_BUILD_LIST）
        }
        const MsValue* src = &vm->stack[vm->stackTop - 1 - n];  // 首元素（tuple 之下）
        memcpy(t->items, src, sizeof(MsValue) * (size_t)n);  // 纯值拷贝无分配窗口
        vm->stack[vm->stackTop - n - 1] = vm->stack[vm->stackTop - 1];  // tuple 下移覆盖首源槽
        vm->stackTop -= n;  // 弹出 n 个源值，tuple 留栈顶（n=0 时为自赋值无害）
        NEXT();
      }

      OPCODE(OP_BUILD_SET) {  // <u8 n>：e1 … en → set（重复元素去重，插入序保持首入位）
        int32_t n = code[ip++];
        MsObjSet* set = msObjSetNew(vm);  // 构造器内部借栈自 root 双分配（D6）；返回至压栈间无分配点
        if (set == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&set->head));  // set 落栈保活（源值在其下）
        if (vm->hasError) {
          goto dispatchExit;
        }
        const MsValue* src = &vm->stack[vm->stackTop - 1 - n];  // 首元素（set 之下）
        for (int32_t i = 0; i < n; i++) {  // add 内层 put 再哈希为 malloc 路径，无 GC 窗口
          MsMapOp mop = msObjSetAdd(vm, set, &src[i]);
          if (mop == MS_MAP_UNHASHABLE) {
            RUNTIME_ERROR("TypeError: unhashable type: '%s'", msValueTypeName(&src[i]));
          }
          if (mop == MS_MAP_OOM) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
        }
        vm->stack[vm->stackTop - n - 1] = vm->stack[vm->stackTop - 1];  // set 下移覆盖首源槽
        vm->stackTop -= n;  // 弹出 n 个源值，set 留栈顶（n=0 时为自赋值无害）
        NEXT();
      }

      OPCODE(OP_UNPACK_SEQ) {  // <u8 n>：TOS list/tuple → 弹源；不足 n 报 ValueError；按序压 items[0..n)（多余丢弃）
        int32_t n = code[ip++];
        MsValue v = vm->stack[vm->stackTop - 1];
        if (v.kind == MS_VAL_OBJ && v.as.obj->type == MS_OBJ_TUPLE) {  // tuple 同 list 语义（22 §4）
          MsObjTuple* t = (MsObjTuple*)v.as.obj;
          if (t->count < n) {
            RUNTIME_ERROR("ValueError: not enough values to unpack (expected %d, got %d)", n, t->count);
          }
          vm->stackTop--;  // 弹源：后续纯值拷贝无分配窗口
          for (int32_t i = 0; i < n; i++) {
            msVmPush(vm, t->items[i]);
          }
          NEXT();
        }
        if (v.kind != MS_VAL_OBJ || v.as.obj->type != MS_OBJ_LIST) {  // string 解构归 T017
          RUNTIME_ERROR("TypeError: cannot unpack '%s' value", msValueTypeName(&v));
        }
        MsObjList* list = (MsObjList*)v.as.obj;
        if (list->count < n) {
          RUNTIME_ERROR("ValueError: not enough values to unpack (expected %d, got %d)", n, list->count);
        }
        vm->stackTop--;  // 弹源：后续纯值拷贝无分配窗口
        for (int32_t i = 0; i < n; i++) {
          msVmPush(vm, list->items[i]);
        }
        NEXT();
      }

      OPCODE(OP_UNPACK_STAR) {  // <u8 before> <u8 after>：前段逐值 + 中段新 list + 后段逐值
        int32_t before = code[ip++];
        int32_t after = code[ip++];
        MsValue v = vm->stack[vm->stackTop - 1];
        if (v.kind != MS_VAL_OBJ || v.as.obj->type != MS_OBJ_LIST) {
          RUNTIME_ERROR("TypeError: cannot unpack '%s' value", msValueTypeName(&v));
        }
        MsObjList* list = (MsObjList*)v.as.obj;
        if (list->count < before + after) {
          RUNTIME_ERROR("ValueError: not enough values to unpack (expected at least %d, got %d)",
                        before + after, list->count);
        }
        // 源保留在栈直至 rest 物化（msObjListNew 为 GC 触发点：源/已压前段均 root）
        int32_t base = vm->stackTop - 1;  // 源槽
        for (int32_t i = 0; i < before; i++) {
          msVmPush(vm, list->items[i]);
          if (vm->hasError) {  // push 溢出：裸槽位运算前必须 unwind
            goto dispatchExit;
          }
        }
        MsObjList* rest = msObjListNew(vm);
        if (rest == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        msVmPush(vm, msValueObj(&rest->head));
        if (vm->hasError) {
          goto dispatchExit;
        }
        int32_t mid = list->count - before - after;
        if (mid > 0 && !msObjListReserve(vm, rest, mid)) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        for (int32_t k = 0; k < mid; k++) {
          rest->items[k] = list->items[before + k];
        }
        rest->count = mid;
        for (int32_t j = 0; j < after; j++) {
          msVmPush(vm, list->items[before + mid + j]);
          if (vm->hasError) {
            goto dispatchExit;
          }
        }
        // 移除源槽：[src, p…, rest, s…] → [p…, rest, s…]
        memmove(&vm->stack[base], &vm->stack[base + 1],
                sizeof(MsValue) * (size_t)(before + 1 + after));
        vm->stackTop--;
        NEXT();
      }

      OPCODE(OP_CHECK_SAFEPOINT)  // 预算递减 NOP（抢占/GC 握手 T029/T026）
      if (--vm->safepointBudget <= 0) {
        vm->safepointBudget = MS_SAFEPOINT_BUDGET;
      }
      NEXT();

      OPCODE(OP_FORMAT_STRING) {  // <const k-interp>：表达式值已在栈顶区；逐段 str 形态拼接（D10）
        // GC 纪律：两阶段全程栈 root——表达式值原位；各 form 逐个 push（list 递归内亦 push），
        // 结果一次性分配后统一弹出（分配窗口内已产出 form 无一失根）
        READ_OPERAND(k);
        const InterpSeg* segs = consts[k].as.interp.segs;
        int32_t count = consts[k].as.interp.count;
        int32_t nExpr = 0;
        for (int32_t i = 0; i < count; i++) {
          nExpr += segs[i].isExpr ? 1 : 0;
        }
        int32_t base = vm->stackTop - nExpr;  // 首表达式值槽（下标语义：push 扩容搬移后仍有效）
        int32_t j = 0;
        for (int32_t i = 0; i < count; i++) {  // 阶段一：逐表达式物化 form 落栈 root
          if (!segs[i].isExpr) {
            continue;
          }
          MsObjString* form = msValueStrForm(vm, &vm->stack[base + j]);
          j++;
          if (form == NULL) {
            if (!vm->hasError) {
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            goto dispatchExit;  // StrForm 内 push 溢出：槽已置，栈由 dispatchExit 废弃
          }
          if (!msVmPush(vm, msValueObj(&form->head))) {
            goto dispatchExit;  // StackOverflow 槽已置
          }
        }
        ms_i64 total = 0;  // 阶段二：总长（字面段 + 栈上 form 段）
        for (int32_t i = 0, f = 0; i < count; i++) {
          if (segs[i].isExpr) {
            total += (int32_t)((const MsObjString*)vm->stack[base + nExpr + f++].as.obj)->byteLen;
          } else {
            total += segs[i].len;
          }
        }
        if (total > INT32_MAX) {
          RUNTIME_ERROR("OverflowError: format string result too large");
        }
        char* buf = malloc((size_t)total + 1);  // +1：零长结果时避开 malloc(0) 语义
        MS_ASSERT(buf != NULL);
        int32_t len = 0;
        for (int32_t i = 0, f = 0; i < count; i++) {
          const char* p;
          int32_t n;
          if (segs[i].isExpr) {
            const MsObjString* form = (const MsObjString*)vm->stack[base + nExpr + f++].as.obj;
            p = form->bytes;
            n = (int32_t)form->byteLen;
          } else {
            p = segs[i].start;
            n = segs[i].len;
          }
          memcpy(buf + len, p, (size_t)n);
          len += n;
        }
        MsObjString* result = msObjStringNew(vm, buf, len);  // 分配点：表达式值与 form 均在栈 root
        free(buf);
        if (result == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        vm->stackTop = base;  // 弹表达式值与全部 form，压结果
        msVmPush(vm, msValueObj(&result->head));
        NEXT();
      }

      TRAP_DEFAULT()
      RUNTIME_ERROR("RuntimeError: unimplemented instruction: %s", msOpNames[op]);

#if !MS_USE_COMPUTED_GOTO
    }  // switch
  }    // for(;;)
#endif

dispatchExit:
  return;
}
