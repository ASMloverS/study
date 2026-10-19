// T011：VM 值/执行单测（docs/tasks/T011 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
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

// ---------- 执行驱动：scan → parse → bind → compile → run（stdout 捕获 tmpfile） ----------

typedef struct RunResult {
  char* out;  // stdout 捕获（malloc）
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

TEST(runArithPrecedence) {  // §5.1 算术
  RunResult r = runSrc("print(1 + 2 * 3)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "7\n", r.out);
  free(r.out);
}

TEST(runFloatDivision) {  // §5.1 浮点
  RunResult r = runSrc("print(1 / 2.0)");
  EXPECT_STREQ(ctx, "0.5\n", r.out);
  free(r.out);
}

TEST(runDivisionByZero) {  // §5.1 除零 → 错误（退出码 1 由 fixture 验）
  RunResult r = runSrc("print(1 / 0)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArithmeticError") != NULL);
  EXPECT(ctx, strstr(r.err, "division by zero") != NULL);
  free(r.out);
}

TEST(runModuloByZero) {
  RunResult r = runSrc("print(5 % 0)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArithmeticError") != NULL);
  free(r.out);
}

TEST(runIntOverflowPlaceholder) {  // 已知临时语义（T016 前）；字面量折叠走 bigint，变量路径才达运行时
  RunResult r = runSrc("var a = 9223372036854775807\nvar b = 1\nprint(a + b)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "OverflowError") != NULL);
  free(r.out);
}

TEST(runShortCircuitValues) {  // §5.1 短路值 + &&/|| 返回操作数；0 为真（02 §2）
  RunResult r = runSrc("print(nil || \"d\")\nprint(false && 1)\nprint(0 && 1)");
  EXPECT_STREQ(ctx, "d\nfalse\n1\n", r.out);
  free(r.out);
}

TEST(runGlobalsReadWrite) {
  RunResult r = runSrc("var x = 1\nx = x + 1\nprint(x)");
  EXPECT_STREQ(ctx, "2\n", r.out);
  free(r.out);
}

TEST(runBoolIntNotConvertible) {
  RunResult r = runSrc("print(1 == true)\nprint(1 == 1.0)\nprint(nil == nil)");
  EXPECT_STREQ(ctx, "false\ntrue\ntrue\n", r.out);
  free(r.out);
}

TEST(runUnaryOps) {
  RunResult r = runSrc("print(-5)\nprint(!true)\nprint(!nil)\nprint(!0)\nprint(~5)");
  EXPECT_STREQ(ctx, "-5\nfalse\ntrue\nfalse\n-6\n", r.out);
  free(r.out);
}

TEST(runShiftOps) {
  RunResult r = runSrc("print(1 << 10)\nprint(-8 >> 1)");
  EXPECT_STREQ(ctx, "1024\n-4\n", r.out);
  free(r.out);
}

TEST(runTernaryExpr) {
  RunResult r = runSrc("print(true ? 1 : 2)");
  EXPECT_STREQ(ctx, "1\n", r.out);
  free(r.out);
}

TEST(runPrintForms) {  // 空格连接 + 换行；空参仅换行
  RunResult r = runSrc("print(1, 2.5, true, nil)\nprint()");
  EXPECT_STREQ(ctx, "1 2.5 true nil\n\n", r.out);
  free(r.out);
}

TEST(runFloatReprForms) {
  RunResult r = runSrc("print(2.0)\nprint(0.1)\nprint(1.0e3)");
  EXPECT_STREQ(ctx, "2.0\n0.1\n1000.0\n", r.out);
  free(r.out);
}

TEST(runCallUserFuncPlaceholder) {  // 函数声明可执行，调用为 T018 占位错误
  RunResult r = runSrc("func f() { return 1 }\nprint(f())");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "T018") != NULL);
  free(r.out);
}

TEST(runRangeArgErrors) {
  RunResult r = runSrc("var r = range(1, 2, 0)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "range step") != NULL);
  free(r.out);
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
      runArithPrecedence,
      runFloatDivision,
      runDivisionByZero,
      runModuloByZero,
      runIntOverflowPlaceholder,
      runShortCircuitValues,
      runGlobalsReadWrite,
      runBoolIntNotConvertible,
      runUnaryOps,
      runShiftOps,
      runTernaryExpr,
      runPrintForms,
      runFloatReprForms,
      runCallUserFuncPlaceholder,
      runRangeArgErrors,
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
