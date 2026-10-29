// T016：bigint 数据面 + 数值塔单测（docs/tasks/T016 §5.1；VM 级用例随 Task 3+ 追加）
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "gc/gc.h"
#include "obj/obj_bigint.h"
#include "obj/obj_string.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

// ---------- 数据面（Task 1） ----------

// 确定性 LCG（跨平台一致；不依赖 rand()）
static ms_u32 rngState = 0x12345678u;

static ms_u32 rngNext(void) {
  rngState = rngState * 1664525u + 1013904223u;
  return rngState >> 8;
}

static MsObjBigInt* randomBig(MsVM* vm, int32_t limbCount) {  // 顶层 limb 非零
  uint32_t* buf = malloc((size_t)limbCount * sizeof(uint32_t));
  for (int32_t i = 0; i < limbCount; i++) {
    buf[i] = (rngNext() << 24) ^ rngNext();
  }
  buf[limbCount - 1] |= 0x80000000u;  // 保证位长
  int sign = (int)(rngNext() % 3) - 1;  // -1/0/+1 三选一；0 重映射 +1（直传 0 会被 New 规范化成零值，顶层 limb 白设）
  MsObjBigInt* b = msObjBigintNew(vm, buf, limbCount, sign == 0 ? 1 : sign);
  free(buf);
  return b;
}

// C 字符串快照（toString(10)；调用方 free；失败 NULL）——vm 供末端 msObjStringNew 分配
static char* bigDec(MsVM* vm, const MsObjBigInt* b) {
  MsObjString* s = msObjBigintToString(vm, b, 10);
  if (s == NULL) {
    return NULL;
  }
  char* r = malloc(s->byteLen + 1);
  memcpy(r, s->bytes, s->byteLen);
  r[s->byteLen] = '\0';
  return r;
}

// root 辅助：bigint 经 VM 栈 root 后再继续（防后续分配窗口回收）
static void rootBig(MsVM* vm, const MsObjBigInt* b) {
  msVmPush(vm, msValueObj((MsObj*)&b->head));
}

TEST(bigintParsePrintRoundtrip) {  // §5.1 解析/打印：往返一致
  MsVM vm;
  msVmInit(&vm);
  const char* cases[] = {"0", "1", "-1", "123456789012345678901234567890",
                         "-987654321098765432109876543210", "18446744073709551616"};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    MsObjBigInt* b = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, cases[i], (int32_t)strlen(cases[i]), 10, &b) == MS_BIG_OK);
    rootBig(&vm, b);
    char* s = bigDec(&vm, b);
    EXPECT_STREQ(ctx, cases[i], s);
    free(s);
  }
  // 前缀/下划线/显式 base 前缀
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "0xff", 4, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  char* s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "255", s);
  free(s);
  EXPECT(ctx, msObjBigintParse(&vm, "0b1010", 6, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "10", s);
  free(s);
  EXPECT(ctx, msObjBigintParse(&vm, "1_000_000", 9, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "1000000", s);
  free(s);
  // 非法：空串 / 坏字符 / 空前缀 / base 域外（M-4）
  EXPECT(ctx, msObjBigintParse(&vm, "", 0, 10, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "1e3", 3, 10, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "0x", 2, 0, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "1", 1, 37, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "0", 1, 1, &b) == MS_BIG_PARSE);
  msVmFree(&vm);
}

TEST(bigintToStringBase) {  // §5.1：255 → 2/8/16 + 显式 base 解析回读；非法 base
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "255", 3, 10, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  const int bases[] = {2, 8, 16};
  const char* want[] = {"11111111", "377", "ff"};
  for (int i = 0; i < 3; i++) {
    MsObjString* s = msObjBigintToString(&vm, b, bases[i]);
    EXPECT(ctx, s != NULL);
    EXPECT(ctx, s->byteLen == (int32_t)strlen(want[i]) && memcmp(s->bytes, want[i], s->byteLen) == 0);
    msVmPush(&vm, msValueObj(&s->head));  // root：回读解析的分配窗口
    MsObjBigInt* back = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, s->bytes, (int32_t)s->byteLen, bases[i], &back) == MS_BIG_OK);
    rootBig(&vm, back);
    EXPECT_EQ(ctx, 0, msObjBigintCompare(b, back));
  }
  EXPECT(ctx, msObjBigintToString(&vm, b, 3) == NULL);  // 非法 base
  msVmFree(&vm);
}

TEST(bigintHashNumeric) {  // §5.1：hash(1) == hash(1n) == hash(1.0)；跨塔一致性
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* one = msObjBigintFromI64(&vm, 1);
  rootBig(&vm, one);
  MsObjBigInt* big = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "9223372036854775808", 19, 10, &big) == MS_BIG_OK);  // 2^63
  rootBig(&vm, big);
  EXPECT(ctx, msHashNumericI64(1) == msHashNumericBigint(one));
  EXPECT(ctx, msHashNumericI64(1) == msHashNumericF64(1.0));
  EXPECT(ctx, msHashNumericBigint(big) == msHashNumericF64(9223372036854775808.0));
  EXPECT(ctx, msHashNumericI64(9007199254740993LL) != msHashNumericF64(9007199254740992.0));  // ≥2^53 精确化（值不等 → hash 不等）
  EXPECT(ctx, msHashNumericI64(9007199254740992LL) == msHashNumericF64(9007199254740992.0));  // 值相等 → hash 相等
  EXPECT(ctx, msHashNumericI64(-1) != msHashNumericI64(1));
  EXPECT(ctx, msHashNumericF64(-0.0) == msHashNumericI64(0));
  msVmFree(&vm);
}

TEST(bigintCompare) {  // 符号域 + i64/f64 精确比较
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* a = NULL;
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "9223372036854775808", 19, 10, &a) == MS_BIG_OK);  // 2^63
  rootBig(&vm, a);
  EXPECT(ctx, msObjBigintParse(&vm, "-9223372036854775808", 20, 10, &b) == MS_BIG_OK);  // -2^63
  rootBig(&vm, b);
  EXPECT_EQ(ctx, 1, msObjBigintCompare(a, b));
  EXPECT_EQ(ctx, 0, msObjBigintCmpF64(a, 9223372036854775808.0));   // 2^63 float 精确相等
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(a, 9223372036854777856.0));  // 2^63 的下一个 double（2^63+2048）
  EXPECT_EQ(ctx, 2, msObjBigintCmpF64(a, NAN));                     // NaN 无序
  EXPECT_EQ(ctx, 1, msObjBigintCmpI64(a, INT64_MAX));   // 2^63 > INT64_MAX（i64 上界外）
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(b, INT64_MIN));   // -2^63 == INT64_MIN（精确下界）
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(b, -1.5));  // -2^63 < -1.5
  // 非整邻域：bigint 5 vs 5.5 / 5.0
  MsObjBigInt* five = msObjBigintFromI64(&vm, 5);
  rootBig(&vm, five);
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(five, 5.5));
  EXPECT_EQ(ctx, 0, msObjBigintCmpF64(five, 5.0));
  // FitsI64 边界
  ms_i64 iv = 0;
  EXPECT(ctx, !msObjBigintFitsI64(a, &iv));
  EXPECT(ctx, msObjBigintFitsI64(b, &iv));
  EXPECT_EQ(ctx, INT64_MIN, iv);
  msVmFree(&vm);
}

TEST(bigintCmpF64Tiny) {  // review I-1：极小 double（-k ≥ 64，含亚正规）floor 移位钳制回归
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* one = msObjBigintFromI64(&vm, 1);
  rootBig(&vm, one);
  MsObjBigInt* negOne = msObjBigintFromI64(&vm, -1);
  rootBig(&vm, negOne);
  EXPECT_EQ(ctx, 1, msObjBigintCmpF64(one, 1e-30));
  EXPECT_EQ(ctx, 1, msObjBigintCmpF64(one, ldexp(3.0, -65)));  // -k 恰 65：x86 掩码移位曾误得 1
  EXPECT_EQ(ctx, 1, msObjBigintCmpF64(one, DBL_TRUE_MIN));
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(negOne, -1e-30));  // 负镜像
  msVmFree(&vm);
}

TEST(bigintRandomRoundtrip) {  // review I-2：随机 parse/print 往返 + ToF64/FitsI64 边界四态
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 50; iter++) {
    int32_t limbs = 2 + (int32_t)(rngNext() % 79);  // 2..80 limb
    MsObjBigInt* b = randomBig(&vm, limbs);
    EXPECT(ctx, b != NULL);
    rootBig(&vm, b);
    char* s = bigDec(&vm, b);
    EXPECT(ctx, s != NULL);
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, s, (int32_t)strlen(s), 10, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCompare(b, r));
    char* s2 = bigDec(&vm, r);
    EXPECT(ctx, s2 != NULL && strcmp(s, s2) == 0);
    free(s);
    free(s2);
  }
  // ToF64 精确域：2^63 恰为可精确表示的幂二进制
  MsObjBigInt* big = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "9223372036854775808", 19, 10, &big) == MS_BIG_OK);
  rootBig(&vm, big);
  EXPECT(ctx, msObjBigintToF64(big) == 9223372036854775808.0);
  // FitsI64 边界：上界内/上界外/下界外/恰 INT64_MIN
  MsObjBigInt* imax = msObjBigintFromI64(&vm, INT64_MAX);
  rootBig(&vm, imax);
  ms_i64 iv = 0;
  EXPECT(ctx, msObjBigintFitsI64(imax, &iv) && iv == INT64_MAX);
  EXPECT(ctx, !msObjBigintFitsI64(big, &iv));
  MsObjBigInt* below = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "-9223372036854775809", 20, 10, &below) == MS_BIG_OK);
  rootBig(&vm, below);
  EXPECT(ctx, !msObjBigintFitsI64(below, &iv));
  MsObjBigInt* imin = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "-9223372036854775808", 20, 10, &imin) == MS_BIG_OK);
  rootBig(&vm, imin);
  EXPECT(ctx, msObjBigintFitsI64(imin, &iv));
  EXPECT_EQ(ctx, INT64_MIN, iv);
  msVmFree(&vm);
}

// ---------- 十进制软参考（T016 §6：独立朴素实现防同源错误；仅 +、-、×、比较） ----------
// 大端十进制数字数组（d[0] 最高位）；sign ∈ {-1,0,1}；容量 8200 覆盖 300 limb（9600 bit ≈ 2891 位）
// 乘积上限（≤ 5782 位）+ 进位裕量（计划勘误：原 4600 不足，统一提到 8200）

typedef struct DecNum {
  int sign;
  int len;       // 有效位数（0 ⇔ 值 0）
  char d[8200];
} DecNum;

static void decNorm(DecNum* n) {
  int32_t i = 0;
  while (i < n->len && n->d[i] == 0) {
    i++;
  }
  memmove(n->d, n->d + i, (size_t)(n->len - i));
  n->len -= i;
  if (n->len == 0) {
    n->sign = 0;
  }
}

static void decFromString(DecNum* out, const char* s) {
  out->sign = s[0] == '-' ? -1 : 1;
  const char* p = s[0] == '-' || s[0] == '+' ? s + 1 : s;
  out->len = (int32_t)strlen(p);
  memcpy(out->d, p, (size_t)out->len);
  for (int32_t i = 0; i < out->len; i++) {
    out->d[i] = (char)(out->d[i] - '0');
  }
  decNorm(out);
}

static void decToChars(const DecNum* n, char* buf, size_t cap) {
  if (n->len == 0) {
    snprintf(buf, cap, "0");
    return;
  }
  int32_t o = 0;
  if (n->sign < 0) {
    buf[o++] = '-';
  }
  for (int32_t i = 0; i < n->len; i++) {
    buf[o + i] = (char)('0' + n->d[i]);
  }
  buf[o + n->len] = '\0';
}

// |a| + |b|（大端）
static void decMagAdd(DecNum* a, const DecNum* b) {
  char r[8200];
  int32_t al = a->len, bl = b->len;
  int32_t rl = al > bl ? al : bl;
  int carry = 0;
  for (int32_t i = 0; i < rl; i++) {
    int da = al - 1 - i >= 0 ? a->d[al - 1 - i] : 0;
    int db = bl - 1 - i >= 0 ? b->d[bl - 1 - i] : 0;
    int t = da + db + carry;
    r[rl - 1 - i] = (char)(t % 10);
    carry = t / 10;
  }
  if (carry != 0) {
    memmove(r + 1, r, (size_t)rl);
    r[0] = (char)carry;
    rl++;
  }
  memcpy(a->d, r, (size_t)rl);
  a->len = rl;
}

// |a| - |b|（|a| ≥ |b|）
static void decMagSub(DecNum* a, const DecNum* b) {
  int32_t al = a->len, bl = b->len;
  int borrow = 0;
  for (int32_t i = 0; i < al; i++) {
    int da = a->d[al - 1 - i];
    int db = bl - 1 - i >= 0 ? b->d[bl - 1 - i] : 0;
    int t = da - db - borrow;
    if (t < 0) {
      t += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    a->d[al - 1 - i] = (char)t;
  }
  decNorm(a);
}

static int decCmpAbs(const DecNum* a, const DecNum* b) {
  if (a->len != b->len) {
    return a->len < b->len ? -1 : 1;
  }
  for (int32_t i = 0; i < a->len; i++) {
    if (a->d[i] != b->d[i]) {
      return a->d[i] < b->d[i] ? -1 : 1;
    }
  }
  return 0;
}

// 有符号 a ± b
static void decAdd(DecNum* a, const DecNum* b, bool sub) {
  int bs = sub ? -b->sign : b->sign;
  if (a->sign == 0) {
    *a = *b;
    a->sign = bs;
    return;
  }
  if (bs == 0) {
    return;
  }
  if (a->sign == bs) {
    decMagAdd(a, b);
  } else {
    int c = decCmpAbs(a, b);
    if (c == 0) {
      a->len = 0;
      a->sign = 0;
    } else if (c > 0) {
      decMagSub(a, b);
    } else {
      DecNum t = *b;
      decMagSub(&t, a);
      *a = t;
      a->sign = bs;
    }
  }
}

// |a|·|b|（大端卷积）
static void decMul(DecNum* out, const DecNum* a, const DecNum* b) {
  char r[8200] = {0};
  int32_t rl = a->len + b->len;
  for (int32_t i = 0; i < a->len; i++) {
    int carry = 0;
    for (int32_t j = 0; j < b->len; j++) {
      int t = r[rl - 1 - (i + j)] + a->d[a->len - 1 - i] * b->d[b->len - 1 - j] + carry;
      r[rl - 1 - (i + j)] = (char)(t % 10);
      carry = t / 10;
    }
    int32_t k = i + b->len;
    while (carry != 0) {
      int t = r[rl - 1 - k] + carry;
      r[rl - 1 - k] = (char)(t % 10);
      carry = t / 10;
      k++;
    }
  }
  memcpy(out->d, r, (size_t)rl);
  out->len = rl;
  out->sign = a->sign * b->sign;
  decNorm(out);
}

// 运算包装：两 bigint 运算并以十进制比对（oracle 由 dec* 提供）
static void checkBinOp(TestCtx* ctx, MsVM* vm, const char* op, const MsObjBigInt* a, const MsObjBigInt* b,
                       const DecNum* want) {
  rootBig(vm, a);
  rootBig(vm, b);
  MsBigView va, vb;
  msBigViewOf(&va, a);
  msBigViewOf(&vb, b);
  MsObjBigInt* r = NULL;
  MsBigErr e = MS_BIG_OK;
  if (strcmp(op, "+") == 0) {
    e = msBigAdd(vm, &va, &vb, &r);
  } else if (strcmp(op, "-") == 0) {
    e = msBigSub(vm, &va, &vb, &r);
  } else if (strcmp(op, "*") == 0) {
    e = msBigMul(vm, &va, &vb, &r);
  } else {
    e = msBigDiv(vm, &va, &vb, &r);
  }
  EXPECT(ctx, e == MS_BIG_OK);
  if (e != MS_BIG_OK) {
    return;
  }
  rootBig(vm, r);
  char* got = bigDec(vm, r);
  char wantChars[8200];
  decToChars(want, wantChars, sizeof(wantChars));
  EXPECT_STREQ(ctx, wantChars, got);
  free(got);
}

TEST(bigintAddSubOracle) {  // §5.1：随机 2048 位 ± 与十进制参考一致
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 100; iter++) {
    int32_t la = 1 + (int32_t)(rngNext() % 64);  // ≤ 2048 bit
    int32_t lb = 1 + (int32_t)(rngNext() % 64);
    MsObjBigInt* a = randomBig(&vm, la);
    MsObjBigInt* b = randomBig(&vm, lb);
    EXPECT(ctx, a != NULL && b != NULL);
    char* as = bigDec(&vm, a);
    char* bs = bigDec(&vm, b);
    DecNum da, db, want;
    decFromString(&da, as);
    decFromString(&db, bs);
    want = da;
    decAdd(&want, &db, false);
    checkBinOp(ctx, &vm, "+", a, b, &want);
    want = da;
    decAdd(&want, &db, true);
    checkBinOp(ctx, &vm, "-", a, b, &want);
    free(as);
    free(bs);
  }
  msVmFree(&vm);
}

TEST(bigintMulOracle) {  // §5.1/DoD：阈值两侧 200 组随机（含 Karatsuba 路径）
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 200; iter++) {
    // 100..300 limb（3200..9600 bit）跨 4096 bit 阈值；DecNum 容量按 300 limb 乘积上限
    int32_t la = 100 + (int32_t)(rngNext() % 201);
    int32_t lb = 100 + (int32_t)(rngNext() % 201);
    MsObjBigInt* a = randomBig(&vm, la);
    MsObjBigInt* b = randomBig(&vm, lb);
    EXPECT(ctx, a != NULL && b != NULL);
    char* as = bigDec(&vm, a);
    char* bs = bigDec(&vm, b);
    DecNum da, db, want;
    decFromString(&da, as);
    decFromString(&db, bs);
    decMul(&want, &da, &db);
    checkBinOp(ctx, &vm, "*", a, b, &want);
    free(as);
    free(bs);
  }
  msVmFree(&vm);
}

TEST(bigintMulKaratsubaZeroLow) {  // 回归：低半窗全零（a0len+b0len < zLen）时 z0 槽尾未清零
  MsVM vm;
  msVmInit(&vm);
  // a = 2^8192（257 limb，低 128 limb 全零，alen 奇 → a1len = m+1 兼查 sa 槽位溢出路径）；
  // b = 3·2^5000（157 limb，低 128 limb 全零）→ m = 128，Karatsuba 分支，z0 = 0·0 = 0
  MsObjBigInt* one = msObjBigintFromI64(&vm, 1);
  rootBig(&vm, one);
  MsObjBigInt* three = msObjBigintFromI64(&vm, 3);
  rootBig(&vm, three);
  MsBigView v1, v3;
  msBigViewOf(&v1, one);
  msBigViewOf(&v3, three);
  MsObjBigInt* a = NULL;
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msBigShl(&vm, &v1, 8192, &a) == MS_BIG_OK);
  rootBig(&vm, a);
  EXPECT(ctx, msBigShl(&vm, &v3, 5000, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  char* as = bigDec(&vm, a);
  char* bs = bigDec(&vm, b);
  DecNum da, db, want;
  decFromString(&da, as);
  decFromString(&db, bs);
  decMul(&want, &da, &db);
  checkBinOp(ctx, &vm, "*", a, b, &want);  // want = 3·2^13192
  free(as);
  free(bs);
  msVmFree(&vm);
}

TEST(bigintDivModSemantics) {  // §5.1：除/模符号 + 恒等式性质
  MsVM vm;
  msVmInit(&vm);
  // 固定符号用例（02 §3.5 Go 语义）；末两组为单 limb 除数（Knuth D n==1 内核，含 rhat==0 最坏型）。
  // 注意：计划原稿 4 组表配 i<8 越界——此处按实际行数 6 遍历（计划勘误）
  const char* triples[] = {
      "-7", "2", "-3", "-1", "7", "-2", "-3", "1", "5", "2", "2", "1", "0", "5", "0", "0",
      "6", "2", "3", "0", "1000000000000000000000007", "3", "333333333333333333333335", "2",
  };
  for (int i = 0; i < 6; i++) {
    MsObjBigInt* a = NULL;
    MsObjBigInt* b = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, triples[4 * i], (int32_t)strlen(triples[4 * i]), 10, &a) == MS_BIG_OK);
    rootBig(&vm, a);
    EXPECT(ctx, msObjBigintParse(&vm, triples[4 * i + 1], (int32_t)strlen(triples[4 * i + 1]), 10, &b) == MS_BIG_OK);
    rootBig(&vm, b);
    MsBigView va, vb;
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    MsObjBigInt* q = NULL;
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigDiv(&vm, &va, &vb, &q) == MS_BIG_OK);
    rootBig(&vm, q);
    EXPECT(ctx, msBigMod(&vm, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    char* qs = bigDec(&vm, q);
    char* rs = bigDec(&vm, r);
    EXPECT_STREQ(ctx, triples[4 * i + 2], qs);
    EXPECT_STREQ(ctx, triples[4 * i + 3], rs);
    free(qs);
    free(rs);
  }
  // 除零
  MsObjBigInt* zero = msObjBigintFromI64(&vm, 0);
  rootBig(&vm, zero);
  MsBigView va2;
  msBigViewOf(&va2, zero);
  MsObjBigInt* r2 = NULL;
  EXPECT(ctx, msBigMod(&vm, &va2, &va2, &r2) == MS_BIG_DIVZERO);
  // 随机大数恒等式：a == q·b + r ∧ |r| < |b| ∧ (r == 0 ∨ sign(r) == sign(a))
  for (int iter = 0; iter < 50; iter++) {
    MsObjBigInt* a = randomBig(&vm, 2 + (int32_t)(rngNext() % 80));
    MsObjBigInt* b = randomBig(&vm, 1 + (int32_t)(rngNext() % 40));
    EXPECT(ctx, a != NULL && b != NULL);
    rootBig(&vm, a);
    rootBig(&vm, b);
    MsBigView vwa, vwb;
    msBigViewOf(&vwa, a);
    msBigViewOf(&vwb, b);
    MsObjBigInt* q = NULL;
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigDiv(&vm, &vwa, &vwb, &q) == MS_BIG_OK);
    rootBig(&vm, q);
    EXPECT(ctx, msBigMod(&vm, &vwa, &vwb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    // q·b + r == a
    MsBigView vq, vr;
    msBigViewOf(&vq, q);
    msBigViewOf(&vwb, b);
    msBigViewOf(&vr, r);
    MsObjBigInt* qb = NULL;
    MsObjBigInt* sum = NULL;
    EXPECT(ctx, msBigMul(&vm, &vq, &vwb, &qb) == MS_BIG_OK);
    rootBig(&vm, qb);
    msBigViewOf(&vq, qb);
    EXPECT(ctx, msBigAdd(&vm, &vq, &vr, &sum) == MS_BIG_OK);
    rootBig(&vm, sum);
    EXPECT(ctx, msObjBigintCompare(sum, a) == 0);
    EXPECT(ctx, r->len == 0 || r->sign == a->sign);  // 余符号随被除数
  }
  msVmFree(&vm);
}

TEST(bigintBitwiseVsI64) {  // §5.1：小数值域以 i64 位运算为模型（含负数补码语义）
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 200; iter++) {
    ms_i64 x = (ms_i64)((rngNext() % 2001) - 1000);
    ms_i64 y = (ms_i64)((rngNext() % 2001) - 1000);
    MsObjBigInt* a = msObjBigintFromI64(&vm, x);
    MsObjBigInt* b = msObjBigintFromI64(&vm, y);
    rootBig(&vm, a);
    rootBig(&vm, b);
    MsBigView va, vb;
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_AND, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x & y));
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_OR, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x | y));
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_XOR, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x ^ y));
  }
  // 移位/幂/~ 语义锚点
  MsObjBigInt* m3 = msObjBigintFromI64(&vm, -3);
  rootBig(&vm, m3);
  MsBigView v3;
  msBigViewOf(&v3, m3);
  MsObjBigInt* r = NULL;
  EXPECT(ctx, msBigShr(&vm, &v3, 1, &r) == MS_BIG_OK);  // floor(-3/2) = -2
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, -2));
  msBigViewOf(&v3, m3);
  EXPECT(ctx, msBigNot(&vm, &v3, &r) == MS_BIG_OK);  // ~(-3) = 2
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, 2));
  MsObjBigInt* m1 = msObjBigintFromI64(&vm, -1);
  rootBig(&vm, m1);
  MsBigView v1;
  msBigViewOf(&v1, m1);
  EXPECT(ctx, msBigShr(&vm, &v1, 1000, &r) == MS_BIG_OK);  // -1 >> 大数 = -1
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, -1));
  MsObjBigInt* two = msObjBigintFromI64(&vm, 2);
  rootBig(&vm, two);
  MsBigView v2;
  msBigViewOf(&v2, two);
  EXPECT(ctx, msBigPow(&vm, &v2, 100, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  char* s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "1267650600228229401496703205376", s);  // 2^100
  free(s);
  msVmFree(&vm);
}

TEST(bigintMiscAnchors) {  // review M-1：Neg 符号翻转 + 零快速路径（加/乘/自减）
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* neg = NULL;
  MsObjBigInt* pos = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "-123", 4, 10, &neg) == MS_BIG_OK);
  rootBig(&vm, neg);
  EXPECT(ctx, msObjBigintParse(&vm, "123", 3, 10, &pos) == MS_BIG_OK);
  rootBig(&vm, pos);
  MsBigView vn, vp;
  msBigViewOf(&vn, neg);
  MsObjBigInt* r = NULL;
  EXPECT(ctx, msBigNeg(&vm, &vn, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  char* s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "123", s);  // -(-123)
  free(s);
  msBigViewOf(&vp, pos);
  EXPECT(ctx, msBigNeg(&vm, &vp, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "-123", s);  // -(123)
  free(s);
  // 零快速路径：x + 0 == x；0·y == 0；x - x == 0
  MsObjBigInt* zero = msObjBigintFromI64(&vm, 0);
  rootBig(&vm, zero);
  MsObjBigInt* x = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "123456789012345678901234567890", 30, 10, &x) == MS_BIG_OK);
  rootBig(&vm, x);
  MsObjBigInt* y = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "999999999999999999999999999999", 30, 10, &y) == MS_BIG_OK);
  rootBig(&vm, y);
  MsBigView vz, vx, vy;
  msBigViewOf(&vz, zero);
  msBigViewOf(&vx, x);
  msBigViewOf(&vy, y);
  EXPECT(ctx, msBigAdd(&vm, &vx, &vz, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCompare(r, x));  // x + 0 == x（值同含符号）
  msBigViewOf(&vz, zero);
  msBigViewOf(&vy, y);
  EXPECT(ctx, msBigMul(&vm, &vz, &vy, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "0", s);  // 0 · y
  free(s);
  msBigViewOf(&vx, x);
  EXPECT(ctx, msBigSub(&vm, &vx, &vx, &r) == MS_BIG_OK);  // 别名同对象：magCmp 相等 → 0
  rootBig(&vm, r);
  s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "0", s);  // x - x
  free(s);
  msVmFree(&vm);
}

// ---------- VM 级（Task 3）：混算表 / TypeError / 提升 / 词法衔接 ----------

// 执行面桥（test_obj_map.c 同型）：全管线跑 .ms 源码

typedef struct RunResult {
  char* out;
  bool compileOk;
  bool hasError;
  char err[240];
  int32_t errLine;
} RunResult;

static char* slurpTmp(FILE* f) {
  fflush(f);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc((size_t)len + 1);
  size_t got = fread(buf, 1, (size_t)len, f);
  buf[got] = '\0';
  fclose(f);
  return buf;
}

static RunResult runSrc(const char* src) {
  RunResult r = {NULL, false, false, "", -1};
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  if (mod != NULL) {
    Binder b;
    binderInit(&b, &arena, "t.ms", &diags);
    binderBind(&b, mod);
    binderDestroy(&b);
    Chunk c;
    if (compilerCompile(mod, &c, "t.ms", &diags)) {
      r.compileOk = true;
      MsVM vm;
      msVmInit(&vm);
      FILE* f = tmpfile();
      vm.out = f;
      msVmRun(&vm, &c);
      r.hasError = vm.hasError;
      snprintf(r.err, sizeof(r.err), "%s", vm.errMsg);
      r.errLine = vm.errLine;
      r.out = slurpTmp(f);
      msVmFree(&vm);
      chunkFree(&c);
    }
  }
  astArenaDestroy(&arena);
  if (r.out == NULL) {
    r.out = calloc(1, 1);
  }
  return r;
}

static void freeRunResult(RunResult* r) { free(r->out); }

// 整值复合字面量取址便捷构造（块内生存期覆盖调用表达式；test_obj_map 同型）
#define MV_INT(n) (&(MsValue){.kind = MS_VAL_I64, .as = {.i = (n)}})

TEST(vmOverflowPromote) {  // DoD：max + 1 → bigint；字面量静默升格
  RunResult r = runSrc("var a = 9223372036854775807\nvar b = 1\nprint(a + b)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "9223372036854775808\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(9223372036854775808)");  // 无 n 后缀：静默升 bigint（不再 MSE_INT_OVERFLOW）
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "9223372036854775808\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(type(9223372036854775807 * 2))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "bigint\n", r.out);
  freeRunResult(&r);
}

TEST(vmMixedArithTable) {  // 02 §3.4 混算表（含 a 侧 int 零快速路径）
  RunResult r = runSrc(
      "print(1 + 1n)\nprint(type(1 + 1n))\nprint(1n + 0.5)\nprint(1 == 1n)\nprint(1n == 1.0)\nprint(0 + "
      "5n)\nprint(0 - 5n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2\nbigint\n1.5\ntrue\ntrue\n5\n-5\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(2 ** 64 == 1n << 64)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\n", r.out);
  freeRunResult(&r);
}

TEST(vmDivModGoSemantics) {  // 02 §3.5
  RunResult r =
      runSrc("print(-7n / 2n)\nprint(-7n % 2n)\nprint(7n / -2n)\nprint(7n % -2n)\nprint(5n / 2n)\nprint(5n % 2n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-3\n-1\n-3\n1\n2\n1\n", r.out);
  freeRunResult(&r);
}

TEST(vmBitopDomain) {  // 02 §3.1：float 拒绝；bigint 域与超宽左移
  RunResult r = runSrc("print(1.5 & 1)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(1 << 100)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1267650600228229401496703205376\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(-3n >> 1)\nprint(~5n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-2\n-6\n", r.out);
  freeRunResult(&r);
}

TEST(vmCmpI64F64Exact) {  // review C-1：int vs float 精确比较（变量路径，防 binder 常量折叠）
  RunResult r = runSrc("var i = 5\nvar f = 5.5\nprint(i < f)\nprint(f < i)\nprint(i < 6)\nprint(6 < f)\nprint(f < 6)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\nfalse\ntrue\nfalse\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("var n = -5\nvar g = -5.5\nprint(n > g)\nprint(g > n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\nfalse\n", r.out);
  freeRunResult(&r);
}

TEST(vmBigintCmpInf) {  // review C-2：bigint vs ±inf 不挂起（frexp(inf) UB 短路）；有限 bigint 恒小/大于 inf
  RunResult r = runSrc(
      "var big = 4n\nvar inf = 1e308 * 10.0\nprint(big < inf)\nprint(big > inf)\nprint(-big < inf)\nprint(big < "
      "-inf)\nprint(-big > -inf)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\nfalse\ntrue\nfalse\ntrue\n", r.out);
  freeRunResult(&r);
}

TEST(vmFoldCmpRuntimeConsistency) {  // review M-1：混合 int/float 比较折叠停用；字面量/变量两路一致（精确语义）
  RunResult r = runSrc("print(9007199254740993 == 9007199254740992.0)\nprint(9007199254740993 != 9007199254740992.0)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "false\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("var i = 9007199254740993\nvar f = 9007199254740992.0\nprint(i == f)\nprint(i > f)\nprint(i < f)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "false\ntrue\nfalse\n", r.out);
  freeRunResult(&r);
}

TEST(vmShlOverflowPromotes) {  // review M-2：左移结果溢出精确升 bigint；折叠/运行时两路一致
  RunResult r = runSrc("var a = 1\nvar b = 63\nprint(a << b)\nprint(type(a << b))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "9223372036854775808\nbigint\n", r.out);
  freeRunResult(&r);
  r = runSrc("var a = 1\nvar b = 63\nprint((1 << 63) == (a << b))\nprint((1 << 62) == (a << 62))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("var x = -3\nvar y = 1\nprint(x << y)\nprint((x << y) == (-3 << 1))\nprint(type(x << y))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-6\ntrue\nint\n", r.out);
  freeRunResult(&r);
  r = runSrc("var x = -4611686018427387904\nprint(x << 1)\nprint((x << 1) == (-4611686018427387904 << 1))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-9223372036854775808\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("var x = 4\nprint(x << 62)\nprint((x << 62) == (4 << 62))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "18446744073709551616\ntrue\n", r.out);
  freeRunResult(&r);
}

// ---------- 内置与方法（Task 4，10 §1/§4） ----------

TEST(vmConversions) {
  RunResult r = runSrc("print(int(\"ff\", 16))\nprint(int(1.9))\nprint(int(-1.9))\nprint(int(2n))\nprint(float(\"1.5\"))\nprint(bigint(\"123\") * 2)\nprint(bigint(1e3) + 1n)\nprint(abs(-2n))\nprint(abs(-5))\nprint(int(1e19))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "255\n1\n-1\n2\n1.5\n246\n1001\n2\n5\n10000000000000000000\n", r.out);
  freeRunResult(&r);
  // 策略锚定：float(bigint) 精度损失静默、含溢出 ±inf（02 §3.3；msFormatFloat 保留 .0 形态）
  r = runSrc("print(float(3n))\nprint(float(10n ** 400) > 1e308)\nprint(float(-(10n ** 400)) < -1e308)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3.0\ntrue\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(int(\"1e3\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(bigint(\"1e3\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(bigint(1.5))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(float(\"x\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
}

TEST(vmNumericMethods) {  // 10 §4：toString([base]) / char / pow / hash
  RunResult r = runSrc("print(255n.toString(16))\nprint(255.toString(16))\nprint(255n.toString(2))\nprint(int(255n.toString(8), 8))\nprint(2n.pow(10))\nprint(2.pow(10))\nprint(65.char())");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "ff\nff\n11111111\n255\n1024\n1024\nA\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(hash(1) == hash(1n))\nprint(hash(1n) == hash(1.0))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(255n.toString(3))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
}

int main(void) {
  TestCtx ctx = {0};
  bigintParsePrintRoundtrip(&ctx);
  bigintToStringBase(&ctx);
  bigintHashNumeric(&ctx);
  bigintCompare(&ctx);
  bigintCmpF64Tiny(&ctx);
  bigintRandomRoundtrip(&ctx);
  bigintAddSubOracle(&ctx);
  bigintMulOracle(&ctx);
  bigintMulKaratsubaZeroLow(&ctx);
  bigintDivModSemantics(&ctx);
  bigintBitwiseVsI64(&ctx);
  bigintMiscAnchors(&ctx);
  vmOverflowPromote(&ctx);
  vmMixedArithTable(&ctx);
  vmDivModGoSemantics(&ctx);
  vmBitopDomain(&ctx);
  vmCmpI64F64Exact(&ctx);
  vmBigintCmpInf(&ctx);
  vmFoldCmpRuntimeConsistency(&ctx);
  vmShlOverflowPromotes(&ctx);
  vmConversions(&ctx);
  vmNumericMethods(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
