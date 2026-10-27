// MsObjBigInt（T016，22 §5）：小端 uint32 limb + 符号；FAM 单块分配（同 tuple 模式，无子分配）
#ifndef INCLUDE_MS_OBJ_OBJ_BIGINT_H_
#define INCLUDE_MS_OBJ_OBJ_BIGINT_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjBigInt {
  MsObj head;
  int32_t len;       // limb 数（规范化：高位零 limb 已剥离；0 ⇔ 值为 0）
  int8_t sign;       // -1 / 0 / +1；len == 0 时必为 0
  uint32_t limbs[];  // 小端序（limbs[0] 最低位）
} MsObjBigInt;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 运算错误码（调用方映射错误类：OOM→RuntimeError、PARSE→ValueError、DIVZERO→ArithmeticError、
// RANGE→OverflowError；docs/tasks/T016 §2）
typedef enum MsBigErr {
  MS_BIG_OK = 0,
  MS_BIG_OOM,
  MS_BIG_PARSE,
  MS_BIG_DIVZERO,
  MS_BIG_RANGE,
} MsBigErr;

#define MS_BIGINT_MAX_BITS (1LL << 24)  // 结果位长护栏（16M bit ≈ 8MB limbs；基准调优归 T044）

// 位运算种类（msBigBitOp）
typedef enum MsBigBitOp {
  MS_BIG_BIT_AND,
  MS_BIG_BIT_OR,
  MS_BIG_BIT_XOR,
} MsBigBitOp;

// 借用幅值视图：limbs 指向对象内部或调用方栈缓冲（i64 经 msBigViewI64 就地构造）
typedef struct MsBigView {
  const uint32_t* limbs;
  int32_t len;
  int8_t sign;
} MsBigView;

static inline void msBigViewI64(MsBigView* v, uint32_t buf[2], ms_i64 x) {
  ms_u64 m = x < 0 ? (ms_u64)(-(x + 1)) + 1 : (ms_u64)x;
  buf[0] = (uint32_t)m;
  buf[1] = (uint32_t)(m >> 32);
  v->limbs = buf;
  v->len = buf[1] != 0 ? 2 : (buf[0] != 0 ? 1 : 0);
  v->sign = x < 0 ? (int8_t)-1 : (x > 0 ? (int8_t)1 : (int8_t)0);
}

static inline void msBigViewOf(MsBigView* v, const MsObjBigInt* b) {
  v->limbs = b->limbs;
  v->len = b->len;
  v->sign = b->sign;
}

// —— 构造 ——
// limbs 须存活至分配完成（栈缓冲/malloc/常量文本安全；未 root 的 GC 对象内部存储须先经
// malloc 暂存——本文件运算函数均遵守，外部直接调用者注意）
MsObjBigInt* msObjBigintNew(MsVM* vm, const uint32_t* limbs, int32_t len, int sign);
MsObjBigInt* msObjBigintFromI64(MsVM* vm, ms_i64 v);
// 文本解析：base 0 = 0x/0o/0b 前缀自动识别（默认十进制）；显式 base 时匹配前缀亦跳过；
// 允许前导 +/- 与 '_' 分组；空数字/非法字符 → MS_BIG_PARSE
MsBigErr msObjBigintParse(MsVM* vm, const char* text, int32_t len, int base, MsObjBigInt** out);
// 字符串化（base 2/8/10/16；负数带 '-'；非法 base/OOM → NULL）
MsObjString* msObjBigintToString(MsVM* vm, const MsObjBigInt* b, int base);

// —— 只读比较 / 转换（无分配） ——
int msObjBigintCompare(const MsObjBigInt* a, const MsObjBigInt* b);  // 全序 -1/0/1
int msObjBigintCmpI64(const MsObjBigInt* b, ms_i64 v);
int msObjBigintCmpF64(const MsObjBigInt* b, double v);  // NaN → 2（无序哨兵）
bool msObjBigintFitsI64(const MsObjBigInt* b, ms_i64* out);
double msObjBigintToF64(const MsObjBigInt* b);  // 允许精度损失（02 §3.3）
int64_t msObjBigintBitLen(const MsObjBigInt* b);  // 值幅位长（0 → 0）

// —— 运算（输入为借用视图；结果单块分配。GC 纪律：运算先落 malloc 临时区、末端单次分配，
//     分配窗口后不再读输入视图 limbs（vm.c BINARY_* 弹栈契约，21 §3.2） ——
MsBigErr msBigAdd(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigSub(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigMul(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // Karatsuba > 4096 bit
MsBigErr msBigDiv(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // 截断除法（02 §3.5）
MsBigErr msBigMod(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // 余符号随被除数
MsBigErr msBigPow(MsVM* vm, const MsBigView* a, ms_i64 exp, MsObjBigInt** out);          // exp ≥ 0
MsBigErr msBigNeg(MsVM* vm, const MsBigView* a, MsObjBigInt** out);
MsBigErr msBigNot(MsVM* vm, const MsBigView* a, MsObjBigInt** out);  // ~a = -(a+1)
MsBigErr msBigBitOp(MsVM* vm, MsBigBitOp op, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigShl(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out);  // 符号保持
MsBigErr msBigShr(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out);  // 负数向下取整

// —— 数值塔 hash 归一（02 §7：hash(1) == hash(1n) == hash(1.0)；obj_map.c 回填） ——
ms_u64 msHashNumericI64(ms_i64 v);
ms_u64 msHashNumericF64(double f);
ms_u64 msHashNumericBigint(const MsObjBigInt* b);

static inline bool msIsBigint(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_BIGINT;
}

#endif  // INCLUDE_MS_OBJ_OBJ_BIGINT_H_
