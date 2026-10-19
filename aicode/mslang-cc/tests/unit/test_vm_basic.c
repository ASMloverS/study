// T011：VM 值/执行单测（docs/tasks/T011 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_harness.h"
#include "vm/vm.h"

// ---------- 值层（Task 1） ----------

TEST(valueConstructors) {
  MsValue n = msValueNil();
  EXPECT_EQ(ctx, MS_VAL_NIL, n.kind);
  MsValue b = msValueBool(true);
  EXPECT_EQ(ctx, MS_VAL_BOOL, b.kind);
  EXPECT_EQ(ctx, 1, b.as.b);
  MsValue i = msValueInt(42);
  EXPECT_EQ(ctx, MS_VAL_INT, i.kind);
  EXPECT_EQ(ctx, 42, i.as.i);
}

TEST(valueTruthyRules) {  // 02 §2：仅 false/nil 为假
  MsValue f = msValueBool(false);
  MsValue n = msValueNil();
  MsValue z = msValueInt(0);
  MsValue one = msValueInt(1);
  EXPECT(ctx, !msValueIsTruthy(&f));
  EXPECT(ctx, !msValueIsTruthy(&n));
  EXPECT(ctx, msValueIsTruthy(&z));
  EXPECT(ctx, msValueIsTruthy(&one));
}

TEST(valueEquality) {  // 1 == true 为 false；1 == 1.0 为 true（02 §2 数值塔）
  MsValue one = msValueInt(1);
  MsValue t = msValueBool(true);
  MsValue oneF = msValueFloat(1.0);
  MsValue two = msValueInt(2);
  EXPECT(ctx, !msValueEqual(&one, &t));
  EXPECT(ctx, msValueEqual(&one, &oneF));
  EXPECT(ctx, !msValueEqual(&one, &two));
}

TEST(valueOrder) {
  MsValue a = msValueInt(1);
  MsValue b = msValueInt(3);
  MsValue bf = msValueFloat(3.0);
  MsValue s = msValueBool(true);
  int32_t cmp = 0;
  EXPECT(ctx, msValueOrder(&a, &b, &cmp) && cmp < 0);
  EXPECT(ctx, msValueOrder(&b, &a, &cmp) && cmp > 0);
  EXPECT(ctx, msValueOrder(&a, &bf, &cmp) && cmp < 0);
  EXPECT(ctx, !msValueOrder(&a, &s, &cmp));  // 不可比较 → 调用方报 TypeError
}

TEST(valueTypeNames) {
  MsValue v = msValueNil();
  EXPECT_STREQ(ctx, "nil", msValueTypeName(&v));
  v = msValueInt(1);
  EXPECT_STREQ(ctx, "int", msValueTypeName(&v));
  v = msValueFloat(0.5);
  EXPECT_STREQ(ctx, "float", msValueTypeName(&v));
}

TEST(valueToStrForms) {
  char buf[64];
  MsValue v = msValueNil();
  msValueToStr(&v, buf, sizeof(buf));
  EXPECT_STREQ(ctx, "nil", buf);
  v = msValueBool(false);
  msValueToStr(&v, buf, sizeof(buf));
  EXPECT_STREQ(ctx, "false", buf);
  v = msValueInt(-12);
  msValueToStr(&v, buf, sizeof(buf));
  EXPECT_STREQ(ctx, "-12", buf);
}

TEST(floatFormatShortestRoundTrip) {
  char buf[64];
  msFormatFloat(buf, sizeof(buf), 0.5);
  EXPECT_STREQ(ctx, "0.5", buf);
  msFormatFloat(buf, sizeof(buf), 2.0);
  EXPECT_STREQ(ctx, "2.0", buf);
  msFormatFloat(buf, sizeof(buf), 0.1);
  EXPECT_STREQ(ctx, "0.1", buf);
  msFormatFloat(buf, sizeof(buf), -0.25);
  EXPECT_STREQ(ctx, "-0.25", buf);
  msFormatFloat(buf, sizeof(buf), 1000.0);
  EXPECT_STREQ(ctx, "1000.0", buf);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      valueConstructors,
      valueTruthyRules,
      valueEquality,
      valueOrder,
      valueTypeNames,
      valueToStrForms,
      floatFormatShortestRoundTrip,
  };
  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  for (int i = 0; i < total; i++) {
    if (only < 0 || only == i) {
      tests[i](&ctx);
    }
  }
  if (ctx.failures == 0) {
    printf("all tests passed\n");
  }
  return ctx.failures == 0 ? 0 : 1;
}
