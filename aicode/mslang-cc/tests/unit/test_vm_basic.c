// T011：VM 值/执行单测（docs/tasks/T011 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "obj/obj_string.h"
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
  EXPECT_EQ(ctx, MS_VAL_I64, i.kind);
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

TEST(valuePredicateFamily) {  // T012 五 tag 判型族 + T013 OBJ 分派（typeName deref obj->type）
  MsValue n = msValueNil();
  EXPECT(ctx, msIsNil(&n) && !msIsBool(&n) && !msIsI64(&n) && !msIsF64(&n) && !msIsObj(&n));
  MsValue b = msValueBool(false);
  EXPECT(ctx, msIsBool(&b) && !msIsNil(&b) && !msIsI64(&b));
  MsValue i = msValueInt(7);
  EXPECT(ctx, msIsI64(&i) && !msIsF64(&i) && !msIsBool(&i));
  MsValue f = msValueFloat(0.5);
  EXPECT(ctx, msIsF64(&f) && !msIsI64(&f));
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "s", 1);
  MsValue sv = msValueObj(&s->head);
  EXPECT(ctx, msIsObj(&sv) && !msIsI64(&sv) && !msIsNil(&sv));
  EXPECT_STREQ(ctx, "string", msValueTypeName(&sv));
  EXPECT(ctx, msValueIsTruthy(&sv));
  MsObjString* s2 = msObjStringNew(&vm, "t", 1);
  MsValue sv2 = msValueObj(&s2->head);
  EXPECT(ctx, !msValueEqual(&sv, &sv2));  // 不同内容不同指针 → 不等
  EXPECT(ctx, msValueEqual(&sv, &sv));    // 同指针
  MsValue iv = msValueInt(3);
  EXPECT(ctx, !msValueEqual(&sv, &iv));  // string vs int
  msVmFree(&vm);
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

TEST(stringValueMigration) {  // T013：string 即 MS_VAL_OBJ；值相等（intern 合一）+ 字典序
  RunResult r = runSrc("var a = \"abc\"\nvar b = \"\\x61\\x62c\"\nprint(a == b)\nprint(a < b)\nprint(\"b\" < \"c\")");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true\nfalse\ntrue\n", r.out);
  free(r.out);
}

TEST(stringConstLoadUnderGcPressure) {  // 回归：低阈值下字面量经 LOAD_CONST 不被误收（C1 消费式 intern）
  // runSrc 每 VM 重建：窗口前累计分配仅一个临时串（~90B），阈值须 ≤ 该值才在 intern 拷贝分配点武装 guard；
  // 取 1 = 任意平台确定复现（reviewer 探针同值）。gcCollectAll 不使分配失败，无 OOM 环风险
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=1");
#else
  setenv("MS_GC_HEAP_MAX", "1", 1);
#endif
  RunResult r = runSrc("var s = \"abcdefghijklmnopqrstuvwxyz0123456789\"\nprint(s)\nprint(s)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "abcdefghijklmnopqrstuvwxyz0123456789\nabcdefghijklmnopqrstuvwxyz0123456789\n", r.out);
  free(r.out);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
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

// ---------- 控制流（Task 4） ----------

TEST(runTruthyZeroJumps) {  // §5.1 真值：0 为真
  RunResult r = runSrc("if 0 { print(\"t\") }");
  EXPECT_STREQ(ctx, "t\n", r.out);
  free(r.out);
}

TEST(runIfElseChain) {  // 04 §3
  RunResult r = runSrc("var x = 0\nif x > 0 {\n  print(\"pos\")\n} else if x == 0 {\n  print(\"zero\")\n"
                       "} else {\n  print(\"neg\")\n}");
  EXPECT_STREQ(ctx, "zero\n", r.out);
  free(r.out);
}

TEST(runForTriSum) {  // §5.1 for 三段
  RunResult r = runSrc("var s = 0\nfor i := 0; i < 5; i++ { s += i }\nprint(s)");
  EXPECT_STREQ(ctx, "10\n", r.out);
  free(r.out);
}

TEST(runForRangeSum) {  // §5.1 迭代 range
  RunResult r = runSrc("var s = 0\nfor x in range(5) { s += x }\nprint(s)");
  EXPECT_STREQ(ctx, "10\n", r.out);
  free(r.out);
}

TEST(runRangeForms) {  // 1/2/3 参与负步进
  RunResult r = runSrc("var a = 0\nfor x in range(3) { a += x }\n"
                       "var b = 0\nfor x in range(2, 5) { b += x }\n"
                       "var c = 0\nfor x in range(8, 0, -3) { c += x }\n"
                       "print(a, b, c)");
  EXPECT_STREQ(ctx, "3 9 15\n", r.out);  // 0+1+2；2+3+4；8+5+2
  free(r.out);
}

TEST(runRangeEmpty) {
  RunResult r = runSrc("var n = 0\nfor x in range(0) { n += 1 }\nprint(n)");
  EXPECT_STREQ(ctx, "0\n", r.out);
  free(r.out);
}

TEST(runNestedBreak) {  // §5.1 嵌套 break：内层断、外层完整
  RunResult r = runSrc("var total = 0\nfor i := 0; i < 3; i++ {\n"
                       "  for j := 0; j < 5; j++ {\n"
                       "    if j == 2 { break }\n"
                       "    total += 1\n"
                       "  }\n"
                       "}\nprint(total)");
  EXPECT_STREQ(ctx, "6\n", r.out);
  free(r.out);
}

TEST(runContinueSkips) {
  RunResult r = runSrc("var s = 0\nfor i := 0; i < 6; i++ {\n"
                       "  if i % 2 == 0 { continue }\n"
                       "  s += i\n"
                       "}\nprint(s)");
  EXPECT_STREQ(ctx, "9\n", r.out);
  free(r.out);
}

TEST(runContinueInForIn) {  // for-in continue 回 FOR_ITER
  RunResult r = runSrc("var s = 0\nfor x in range(5) {\n"
                       "  if x == 3 { continue }\n"
                       "  s += x\n"
                       "}\nprint(s)");
  EXPECT_STREQ(ctx, "7\n", r.out);  // 0+1+2+4
  free(r.out);
}

TEST(runBreakInForIn) {  // for-in break 弹迭代器后干净退出
  RunResult r = runSrc("var s = 0\nfor x in range(100) {\n"
                       "  if x == 3 { break }\n"
                       "  s += x\n"
                       "}\nprint(s)");
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

TEST(runWhileLoop) {
  RunResult r = runSrc("var n = 1\nwhile n < 8 { n = n * 2 }\nprint(n)");
  EXPECT_STREQ(ctx, "8\n", r.out);
  free(r.out);
}

TEST(runForCondForm) {  // for cond ≡ while
  RunResult r = runSrc("var n = 3\nfor n > 0 { n -= 1 }\nprint(n)");
  EXPECT_STREQ(ctx, "0\n", r.out);
  free(r.out);
}

TEST(runInfiniteForBreak) {  // i++ 仅 for 头合法（01 §3），体内用复合赋值
  RunResult r = runSrc("var i = 0\nfor {\n  i += 1\n  if i >= 3 { break }\n}\nprint(i)");
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

TEST(runSwitchValueMulti) {  // §5.1 switch 值式
  RunResult r = runSrc("switch 2 {\ncase 1, 2:\n  print(\"a\")\ndefault:\n  print(\"b\")\n}");
  EXPECT_STREQ(ctx, "a\n", r.out);
  free(r.out);
}

TEST(runSwitchValueDefault) {
  RunResult r = runSrc("switch 9 {\ncase 1, 2:\n  print(\"a\")\ndefault:\n  print(\"b\")\n}");
  EXPECT_STREQ(ctx, "b\n", r.out);
  free(r.out);
}

TEST(runSwitchNoFallthrough) {  // 04 §5.1 隐式 break
  RunResult r = runSrc("switch 1 {\ncase 1:\n  print(\"one\")\ncase 2:\n  print(\"two\")\n}");
  EXPECT_STREQ(ctx, "one\n", r.out);
  free(r.out);
}

TEST(runSwitchExprSubject) {  // 表达式判别式
  RunResult r = runSrc("switch 1 + 1 {\ncase 2:\n  print(\"two\")\ndefault:\n  print(\"?\")\n}");
  EXPECT_STREQ(ctx, "two\n", r.out);
  free(r.out);
}

TEST(runSwitchTypeInt) {  // §5.1 switch 类型式
  RunResult r = runSrc("switch type(1) {\ncase int:\n  print(\"i\")\ndefault:\n  print(\"?\")\n}");
  EXPECT_STREQ(ctx, "i\n", r.out);
  free(r.out);
}

TEST(runSwitchTypeAsString) {  // as 绑定（模块层 → 全局）
  RunResult r = runSrc("switch type(\"hi\") {\ncase int:\n  print(\"i\")\ncase string as s:\n  print(s)\n"
                       "default:\n  print(\"?\")\n}");
  EXPECT_STREQ(ctx, "hi\n", r.out);
  free(r.out);
}

TEST(runSwitchTypeMulti) {  // case int, bigint: 多类型名
  RunResult r = runSrc("switch type(2.5) {\ncase int, bigint:\n  print(\"int-ish\")\ncase float:\n  print(\"float\")\n"
                       "default:\n  print(\"?\")\n}");
  EXPECT_STREQ(ctx, "float\n", r.out);
  free(r.out);
}

// ---------- --trace 与 dump 一致性（DoD：指令序列与 ms dump 一致） ----------

static int32_t decodeOne(const Chunk* c, int32_t off, ms_u8* opOut) {
  int32_t p = off + 1;
  while (c->code[p - 1] == OP_EXTENDED_ARG) {
    p += 2;
  }
  *opOut = c->code[p - 1];
  int32_t n = msOpLayouts[*opOut] == OPL_NONE || msOpLayouts[*opOut] == OPL_SPECIAL ? 0 : 1;
  return (p - off) + n;  // 本用例仅单操作数指令即够（偏移 < 256 无 EXT 链）
}

TEST(traceMatchesInstructionStream) {
  const char* src = "var a = 1\nvar b = a + 2\nprint(b)\n";
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  Binder b;
  binderInit(&b, &arena, "t.ms", &diags);
  binderBind(&b, mod);
  binderDestroy(&b);
  Chunk c;
  EXPECT(ctx, compilerCompile(mod, &c, "t.ms", &diags));
  MsVM vm;
  msVmInit(&vm);
  FILE* f = tmpfile();
  vm.trace = f;
  msVmRun(&vm, &c);
  char* trace = slurpTmp(f);
  EXPECT(ctx, !vm.hasError);
  // 逐行 "offset name" 必须与 chunk 对应偏移解码一致（顺序 = 执行序；手工解析避开 CRT 安全族）
  int32_t pos = 0;
  int32_t lines = 0;
  while (trace[pos] != '\0') {
    int off = 0;
    while (trace[pos] >= '0' && trace[pos] <= '9') {
      off = off * 10 + (trace[pos] - '0');
      pos++;
    }
    EXPECT(ctx, trace[pos] == ' ');
    pos++;
    char name[32] = {0};
    int n = 0;
    while (trace[pos] != '\n' && trace[pos] != '\0' && n < 31) {
      name[n++] = trace[pos++];
    }
    ms_u8 op = OP_NOP;
    decodeOne(&c, off, &op);
    EXPECT_STREQ(ctx, msOpNames[op], name);
    lines++;
    if (trace[pos] == '\n') {
      pos++;
    }
  }
  EXPECT(ctx, lines > 5);  // 至少覆盖装载/运算/print/RETURN
  free(trace);
  msVmFree(&vm);
  chunkFree(&c);
  astArenaDestroy(&arena);
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
      valuePredicateFamily,
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
      stringValueMigration,
      stringConstLoadUnderGcPressure,
      runUnaryOps,
      runShiftOps,
      runTernaryExpr,
      runPrintForms,
      runFloatReprForms,
      runCallUserFuncPlaceholder,
      runRangeArgErrors,
      runTruthyZeroJumps,
      runIfElseChain,
      runForTriSum,
      runForRangeSum,
      runRangeForms,
      runRangeEmpty,
      runNestedBreak,
      runContinueSkips,
      runContinueInForIn,
      runBreakInForIn,
      runWhileLoop,
      runForCondForm,
      runInfiniteForBreak,
      runSwitchValueMulti,
      runSwitchValueDefault,
      runSwitchNoFallthrough,
      runSwitchExprSubject,
      runSwitchTypeInt,
      runSwitchTypeAsString,
      runSwitchTypeMulti,
      traceMatchesInstructionStream,
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
