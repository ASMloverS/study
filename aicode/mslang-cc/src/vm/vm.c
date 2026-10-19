// VM 核心（T011，21 §6 / 20 §7）：dispatch 双路径 + 基础指令执行
// dispatch 采用 computed goto（GCC/Clang）/ switch（MSVC）双展开、单一指令处理体；
// 运行时错误置错误槽后经 dispatchExit 统一 unwind（28 §6 goto 例外：主循环 unwind）
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MS_STACK_MAX (64 * 1024)  // 值栈条目上限（溢出报错；栈深真实限制 T018 评估）
#define MS_SAFEPOINT_BUDGET 1000  // CHECK_SAFEPOINT 回边预算（NOP 递减；抢占 T029）

void msVmInit(MsVM* vm) {
  vm->stack = NULL;
  vm->stackTop = vm->stackCap = 0;
  vm->frames = NULL;
  vm->frameCount = vm->frameCap = 0;
  vm->globals = NULL;
  vm->globalCount = vm->globalCap = 0;
  vm->heap = NULL;
  vm->out = stdout;
  vm->trace = NULL;
  vm->hasError = false;
  vm->errMsg[0] = '\0';
  vm->errLine = -1;
  vm->safepointBudget = MS_SAFEPOINT_BUDGET;
}

void msVmFree(MsVM* vm) {
  free(vm->stack);
  free(vm->frames);
  free(vm->globals);
  msVmInit(vm);
}

static void push(MsVM* vm, MsValue v) {
  if (vm->stackTop >= MS_STACK_MAX) {
    vm->hasError = true;
    snprintf(vm->errMsg, sizeof(vm->errMsg), "StackOverflowError: value stack exceeded %d entries",
             MS_STACK_MAX);
    vm->errLine = -1;
    return;
  }
  MS_ARRAY_PUSH(vm->stack, vm->stackTop, vm->stackCap, v);
}

static MsValue pop(MsVM* vm) {
  MS_ASSERT(vm->stackTop > 0);  // 编译器生成的代码维持栈平衡
  return vm->stack[--vm->stackTop];
}

static MsGlobalVar* findGlobal(MsVM* vm, const char* name, int32_t len) {
  for (int32_t i = vm->globalCount - 1; i >= 0; i--) {  // 后定义覆盖（soft 函数名 05 §1）
    if (vm->globals[i].len == len && memcmp(vm->globals[i].name, name, (size_t)len) == 0) {
      return &vm->globals[i];
    }
  }
  return NULL;
}

static void defGlobal(MsVM* vm, const char* name, int32_t len, MsValue v) {
  MsGlobalVar* hit = findGlobal(vm, name, len);
  if (hit != NULL) {  // 运行时重定义仅 as 绑定兜底等边角；同名重复声明由 binder 拦截
    hit->value = v;
    return;
  }
  MsGlobalVar g = {name, len, v};
  MS_ARRAY_PUSH(vm->globals, vm->globalCount, vm->globalCap, g);
}

// ---- 溢出检查助手（int/int 快路径；溢出为占位错误，bigint 升级归 T016） ----

static bool addOv(ms_i64 a, ms_i64 b, ms_i64* r) {
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
    return false;
  }
  *r = a + b;
  return true;
}

static bool subOv(ms_i64 a, ms_i64 b, ms_i64* r) {
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
    return false;
  }
  *r = a - b;
  return true;
}

static ms_u64 absI64(ms_i64 v) { return v < 0 ? (ms_u64)(-(v + 1)) + 1 : (ms_u64)v; }

static bool mulOv(ms_i64 a, ms_i64 b, ms_i64* r) {
  if (a == 0 || b == 0) {
    *r = 0;
    return true;
  }
  ms_u64 ua = absI64(a);
  ms_u64 ub = absI64(b);
  if (ua > (ms_u64)INT64_MAX / ub) {
    return false;
  }
  ms_u64 um = ua * ub;  // |积| ≤ INT64_MAX，正负号均可表示
  *r = (a < 0) != (b < 0) ? -(ms_i64)um : (ms_i64)um;
  return true;
}

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
    if (!mulOv(acc, base, &acc)) {
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

// 二元算术（21 §3.2 快路径）：int/int 与 float/float；混算升 float；失败 → false + errMsg
static bool arithBinary(MsVM* vm, BinArith k, MsValue a, MsValue b, MsValue* out) {
  bool aNum = a.kind == MS_VAL_INT || a.kind == MS_VAL_FLOAT;
  bool bNum = b.kind == MS_VAL_INT || b.kind == MS_VAL_FLOAT;
  if (aNum && bNum && a.kind == MS_VAL_INT && b.kind == MS_VAL_INT) {
    ms_i64 r = 0;
    switch (k) {
      case BA_ADD:
        if (!addOv(a.as.i, b.as.i, &r)) goto overflow;
        break;
      case BA_SUB:
        if (!subOv(a.as.i, b.as.i, &r)) goto overflow;
        break;
      case BA_MUL:
        if (!mulOv(a.as.i, b.as.i, &r)) goto overflow;
        break;
      case BA_DIV:
        if (b.as.i == 0) goto divZero;
        if (a.as.i == INT64_MIN && b.as.i == -1) goto overflow;
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
        if (!intPow(a.as.i, b.as.i, &r)) goto overflow;
        break;
      case BA_SHL:
        if (b.as.i < 0) goto negShift;
        r = b.as.i >= 64 ? 0 : a.as.i << b.as.i;  // Go 语义：超宽移位为 0
        break;
      case BA_SHR:
        if (b.as.i < 0) goto negShift;
        r = b.as.i >= 64 ? (a.as.i < 0 ? -1 : 0) : a.as.i >> b.as.i;  // 算术右移补符号
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
  if (aNum && bNum) {  // 含 float：升 double（02 §3.4 混算表）
    double da = a.kind == MS_VAL_INT ? (double)a.as.i : a.as.f;
    double db = b.kind == MS_VAL_INT ? (double)b.as.i : b.as.f;
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
      default:  // 位运算拒绝 float（02 §3.1）
        snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: bit operation '%s' requires int operands",
                 kBinArithNames[k]);
        return false;
    }
  }
  if (k == BA_ADD && a.kind == MS_VAL_STRING && b.kind == MS_VAL_STRING) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: string concatenation executes in T013");
    return false;  // 堆 string 归 T012/T013
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
overflow:
  snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: int overflow (bigint promotion lands in T016)");
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
  SET_HANDLER(OP_MAKE_FUNCTION);
  SET_HANDLER(OP_CALL);
  SET_HANDLER(OP_RETURN);
  SET_HANDLER(OP_JMP);
  SET_HANDLER(OP_JMP_IF_FALSE);
  SET_HANDLER(OP_JMP_IF_TRUE);
  SET_HANDLER(OP_JMP_IF_FALSE_OR_POP);
  SET_HANDLER(OP_JMP_IF_TRUE_OR_POP);
  SET_HANDLER(OP_POP_JUMP_IF_NIL);
  SET_HANDLER(OP_CHECK_TYPE);
  SET_HANDLER(OP_GET_ITER);
  SET_HANDLER(OP_FOR_ITER);
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
        push(vm, v);
        NEXT();
      }

      OPCODE(OP_DUP2) {  // a b → a b a b
        MsValue a = vm->stack[vm->stackTop - 2];
        push(vm, a);
        MsValue b = vm->stack[vm->stackTop - 2];
        push(vm, b);
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
      push(vm, msValueNil());
      NEXT();

      OPCODE(OP_LOAD_TRUE)
      push(vm, msValueBool(true));
      NEXT();

      OPCODE(OP_LOAD_FALSE)
      push(vm, msValueBool(false));
      NEXT();

      OPCODE(OP_LOAD_CONST) {
        READ_OPERAND(k);
        bool ok = false;
        MsValue v = msValueFromConst(&consts[k], &ok);
        if (!ok) {
          RUNTIME_ERROR("RuntimeError: bigint executes in T016 (placeholder)");
        }
        push(vm, v);
        NEXT();
      }

      OPCODE(OP_LOAD_LOCAL) {
        int32_t slot = code[ip++];
        push(vm, vm->stack[vm->frames[vm->frameCount - 1].base + slot]);
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
        MsGlobalVar* g = findGlobal(vm, name->as.text.start, name->as.text.len);
        if (g != NULL) {
          push(vm, g->value);
          NEXT();
        }
        MsValue builtin = msValueNil();
        if (msBuiltinLookup(name->as.text.start, name->as.text.len, &builtin)) {
          push(vm, builtin);
          NEXT();
        }
        RUNTIME_ERROR("RuntimeError: undefined name '%.*s'", (int)name->as.text.len, name->as.text.start);
      }

      OPCODE(OP_STORE_GLOBAL) {
        READ_OPERAND(k);
        MsValue v = pop(vm);
        const ConstEntry* name = &consts[k];
        MsGlobalVar* g = findGlobal(vm, name->as.text.start, name->as.text.len);
        if (g == NULL) {
          RUNTIME_ERROR("RuntimeError: assignment to undefined name '%.*s'", (int)name->as.text.len,
                        name->as.text.start);
        }
        g->value = v;
        NEXT();
      }

      OPCODE(OP_DEF_GLOBAL) {  // peek TOS 定义（STORE_GLOBAL 随后弹出落盘；21 §4 声明形态）
        READ_OPERAND(k);
        const ConstEntry* name = &consts[k];
        defGlobal(vm, name->as.text.start, name->as.text.len, vm->stack[vm->stackTop - 1]);
        NEXT();
      }

#define BINARY_ARITH_CASE(opname, kind)       \
  OPCODE(opname) {                            \
    MsValue b = pop(vm);                      \
    MsValue a = pop(vm);                      \
    MsValue r;                                \
    if (!arithBinary(vm, (kind), a, b, &r)) { \
      RUNTIME_ERROR("%s", vm->errMsg);        \
    }                                         \
    push(vm, r);                              \
    NEXT();                                   \
  }

      BINARY_ARITH_CASE(OP_BINARY_ADD, BA_ADD)
      BINARY_ARITH_CASE(OP_BINARY_SUB, BA_SUB)
      BINARY_ARITH_CASE(OP_BINARY_MUL, BA_MUL)
      BINARY_ARITH_CASE(OP_BINARY_DIV, BA_DIV)
      BINARY_ARITH_CASE(OP_BINARY_MOD, BA_MOD)
      BINARY_ARITH_CASE(OP_BINARY_POW, BA_POW)
      BINARY_ARITH_CASE(OP_BINARY_AND, BA_BIT_AND)
      BINARY_ARITH_CASE(OP_BINARY_OR, BA_BIT_OR)
      BINARY_ARITH_CASE(OP_BINARY_XOR, BA_BIT_XOR)
      BINARY_ARITH_CASE(OP_BINARY_SHL, BA_SHL)
      BINARY_ARITH_CASE(OP_BINARY_SHR, BA_SHR)

      OPCODE(OP_UNARY_NEG) {
        MsValue a = pop(vm);
        if (a.kind == MS_VAL_INT) {
          if (a.as.i == INT64_MIN) {
            RUNTIME_ERROR("OverflowError: int overflow (bigint promotion lands in T016)");
          }
          push(vm, msValueInt(-a.as.i));
        } else if (a.kind == MS_VAL_FLOAT) {
          push(vm, msValueFloat(-a.as.f));
        } else {
          RUNTIME_ERROR("TypeError: unary '-' requires a numeric operand, got '%s'", msValueTypeName(&a));
        }
        NEXT();
      }

      OPCODE(OP_UNARY_NOT) {  // ! 返回 bool（03 §2）：真值取反
        MsValue a = pop(vm);
        push(vm, msValueBool(!msValueIsTruthy(&a)));
        NEXT();
      }

      OPCODE(OP_UNARY_INVERT) {
        MsValue a = pop(vm);
        if (a.kind != MS_VAL_INT) {
          RUNTIME_ERROR("TypeError: '~' requires int operand, got '%s'", msValueTypeName(&a));
        }
        push(vm, msValueInt(~a.as.i));
        NEXT();
      }

#define COMPARE_CASE(opname, expr) \
  OPCODE(opname) {                 \
    MsValue b = pop(vm);           \
    MsValue a = pop(vm);           \
    push(vm, msValueBool(expr));   \
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
    push(vm, msValueBool(okExpr));                                       \
    NEXT();                                                              \
  }

      ORDERED_COMPARE_CASE(OP_COMPARE_LT, "<", cmp < 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_LE, "<=", cmp <= 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_GT, ">", cmp > 0)
      ORDERED_COMPARE_CASE(OP_COMPARE_GE, ">=", cmp >= 0)

      OPCODE(OP_IN)
      RUNTIME_ERROR("RuntimeError: 'in' executes in T017");

      OPCODE(OP_MAKE_FUNCTION) {  // <u8 flags>（默认参/rest 形态 T018）
        (void)code[ip++];
        MsValue proto = pop(vm);
        push(vm, proto);  // T011：函数值即 chunk 引用，闭包捕获 T018
        NEXT();
      }

      OPCODE(OP_CALL) {
        int32_t argc = code[ip++];
        MsValue* args = &vm->stack[vm->stackTop - argc];
        MsValue callee = vm->stack[vm->stackTop - argc - 1];
        vm->stackTop -= argc + 1;
        if (callee.kind == MS_VAL_NATIVE) {
          MsValue r = callee.as.native(vm, argc, args);
          push(vm, r);
          NEXT();
        }
        if (callee.kind == MS_VAL_FUNCTION) {
          RUNTIME_ERROR("RuntimeError: script function calls execute in T018");
        }
        RUNTIME_ERROR("TypeError: '%s' is not callable", msValueTypeName(&callee));
      }

      OPCODE(OP_RETURN)
      (void)pop(vm);  // 模块帧返回值丢弃；帧切换 T018
      vm->frameCount--;
      goto dispatchExit;

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
        push(vm, msValueBool((int32_t)strlen(tn) == name->as.text.len &&
                             memcmp(tn, name->as.text.start, (size_t)name->as.text.len) == 0));
        NEXT();
      }

      OPCODE(OP_GET_ITER) {  // T011 仅 range 迭代器直通（协议 T017）
        MsValue v = pop(vm);
        if (v.kind != MS_VAL_RANGE_ITER) {
          RUNTIME_ERROR("TypeError: '%s' is not iterable (T011 supports range only)", msValueTypeName(&v));
        }
        push(vm, v);
        NEXT();
      }

      OPCODE(OP_FOR_ITER) {  // <t exit>：有值压值；耗尽弹迭代器并跳出
        READ_OPERAND(t);
        MsValue* it = &vm->stack[vm->stackTop - 1];
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
        push(vm, msValueInt(cur));
        it = &vm->stack[vm->stackTop - 1];  // push 可能扩容：重取迭代器槽位
        // 推进（饱和到 stop 防 INT64 边界回绕）
        if (step > 0 ? cur > INT64_MAX - step : cur < INT64_MIN - step) {
          it->as.range.cur = stop;
        } else {
          it->as.range.cur = cur + step;
        }
        NEXT();
      }

      OPCODE(OP_CHECK_SAFEPOINT)  // 预算递减 NOP（抢占/GC 握手 T029/T026）
      if (--vm->safepointBudget <= 0) {
        vm->safepointBudget = MS_SAFEPOINT_BUDGET;
      }
      NEXT();

      TRAP_DEFAULT()
      RUNTIME_ERROR("RuntimeError: unimplemented instruction: %s", msOpNames[op]);

#if !MS_USE_COMPUTED_GOTO
    }  // switch
  }    // for(;;)
#endif

dispatchExit:
  return;
}
