// T016：bigint 数据面 + 数值塔单测（docs/tasks/T016 §5.1；VM 级用例随 Task 3+ 追加）
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_bigint.h"
#include "obj/obj_string.h"
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
  int sign = (int)(rngNext() % 3) - 1;  // -1/0/+1（0 时 New 规范化为 0）
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

int main(void) {
  TestCtx ctx = {0};
  bigintParsePrintRoundtrip(&ctx);
  bigintToStringBase(&ctx);
  bigintHashNumeric(&ctx);
  bigintCompare(&ctx);
  bigintCmpF64Tiny(&ctx);
  bigintRandomRoundtrip(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
