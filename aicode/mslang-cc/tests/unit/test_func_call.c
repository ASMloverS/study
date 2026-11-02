// T018：函数对象值层单测（Task 1）——对象迁移回归钉：type/print/相等语义迁移前后不变
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

// ---------- 值层（Task 1） ----------

TEST(funcValueTypeAndPrint) {  // typeName "function" + print "<func 名>"（对象迁移回归钉）
  RunResult r = runSrc("func add(a, b) { return a + b }\nprint(type(add))\nprint(add)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "function\n<func add>\n", r.out);
  free(r.out);
}

TEST(funcIdentityEquality) {  // 同名值身份相等；与 native（print）不等（迁移回归钉）
  RunResult r = runSrc("func f() {}\nprint(f == f, f == print)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true false\n", r.out);
  free(r.out);
}

// ---------- 调用层（Task 2） ----------

TEST(callBasicAdd) {  // 两参调用 + 返回值（00-overview §3）
  RunResult r = runSrc("func add(a, b) { return a + b }\nprint(add(1, 2))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

TEST(callRecursiveFib) {  // 递归经全局名回查（帧切换深度路径）
  RunResult r =
      runSrc("func fib(n) {\n    if n < 2 { return n }\n    return fib(n - 1) + fib(n - 2)\n}\nprint(fib(10))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "55\n", r.out);
  free(r.out);
}

TEST(callArityTooFew) {  // 实参不足 → ArgumentError
  RunResult r = runSrc("func f(a) { return a }\nf()");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(callArityTooMany) {  // 实参过多（无 rest）→ ArgumentError
  RunResult r = runSrc("func f(a) { return a }\nf(1, 2)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(callFrameLimit) {  // 无穷递归 → 帧深上限 StackOverflowError
  RunResult r = runSrc("func f() { return f() }\nf()");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "StackOverflowError") != NULL);
  free(r.out);
}

TEST(anonFuncExpr) {  // 匿名函数表达式（05 §1 FUNC_EXPR）
  RunResult r = runSrc("var double = func(x) { return x * 2 }\nprint(double(21))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "42\n", r.out);
  free(r.out);
}

TEST(arrowExprBody) {  // -> 表达式体（05 §1）：隐式 return
  RunResult r = runSrc("func double(x) -> x * 2\nprint(double(4))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "8\n", r.out);
  free(r.out);
}

TEST(multiReturnAndDestructure) {  // return a, b 元组 + UNPACK_SEQ/UNPACK_STAR 消费（05 §5 赋值式解构接收）
  RunResult r = runSrc("func swap(a, b) { return b, a }\nvar x = 0\nvar y = 0\nx, y = swap(1, 2)\nprint(x, y)\n"
                       "func trio() { return 1, 2, 3 }\na, *mid, z = trio()\nprint(a, z, mid)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "2 1\n1 3 [2]\n", r.out);
  free(r.out);
}

TEST(localFuncShadowAndRedeclare) {  // 同名重声明覆盖（declareSoft）+ 局部函数遮蔽外层局部
  RunResult r = runSrc("func f() { return 1 }\nfunc f() { return 2 }\nprint(f())\n"
                       "func outer() {\n    var v = 1\n    func inner() { var v = 2; return v }\n"
                       "    return inner() + v\n}\nprint(outer())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "2\n3\n", r.out);
  free(r.out);
}

TEST(implicitReturnFallthrough) {  // 条件 return 无 else：false 路径隐式 nil 返回（不得越界执行）
  RunResult r = runSrc(
      "func earlyIf(n) { if n > 0 { return n } }\n"
      "print(earlyIf(-5))\n"
      "print(earlyIf(5))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "nil\n5\n", r.out);
  free(r.out);
}

TEST(noReturnBodyNil) {  // 无 return 体隐式 nil 返回
  RunResult r = runSrc("func f(a) { var t = a * 2 }\nprint(f(1))");
  EXPECT_STREQ(ctx, "nil\n", r.out);
  free(r.out);
}

TEST(callArgEvalOrderPinned) {  // 左到右实参求值序跨调用边界
  RunResult r = runSrc(
      "func tag(x) { print(x); return x }\n"
      "print(tag(1) + tag(2) * 10)");
  EXPECT_STREQ(ctx, "1\n2\n21\n", r.out);
  free(r.out);
}

TEST(arityErrorInsideExpression) {  // 调用者表达式临时值存活期的参数错误（unwind 不破坏栈平衡）
  RunResult r = runSrc("func f(a) { return a }\nprint(1 + f())");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

// ---------- 默认参/rest/展开（Task 3） ----------

TEST(defaultParams) {  // 默认参缺省/覆盖（05 §1）
  RunResult r = runSrc("func greet(name, prefix = \"Mr.\") { return \"${prefix} ${name}\" }\n"
                       "print(greet(\"x\"))\nprint(greet(\"x\", \"Dr.\"))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "Mr. x\nDr. x\n", r.out);
  free(r.out);
}

TEST(defaultEvaluatedAtDefSite) {  // 默认参定义处求值（Python 语义）：后续改 k 不影响已绑定值
  RunResult r = runSrc("var k = 10\nfunc f(a, b = k) { return a + b }\nk = 100\nprint(f(1))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "11\n", r.out);
  free(r.out);
}

TEST(restParam) {  // rest 参收集余量为 list
  RunResult r = runSrc("func varargs(fmt, *args) { return [fmt, args] }\nprint(varargs(1, 2, 3))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[1, [2, 3]]\n", r.out);
  free(r.out);
}

TEST(restParamEmpty) {  // rest 无余量 → 空 list
  RunResult r = runSrc("func f(a, *r) { return r }\nprint(f(1))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[]\n", r.out);
  free(r.out);
}

TEST(spreadCall) {  // 末位 * 展开实参（parser 限定 spread 恒末位）
  RunResult r = runSrc("func f(a, b, c) { return a + b + c }\nvar xs = [2, 3]\nprint(f(1, *xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "6\n", r.out);
  free(r.out);
}

TEST(spreadWithDefaults) {  // 展开与默认参组合
  RunResult r =
      runSrc("func f(a, b = 5, c = 6) { return [a, b, c] }\nprint(f(*[1]))\nprint(f(1, *[9]))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[1, 5, 6]\n[1, 9, 6]\n", r.out);
  free(r.out);
}

TEST(spreadNonList) {  // 展开目标非 list → TypeError
  RunResult r = runSrc("func f(a) { return a }\nprint(f(*5))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(nativeSpread) {  // native 调用展开：print(*[1, 2, 3])
  RunResult r = runSrc("print(*[1, 2, 3])");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1 2 3\n", r.out);
  free(r.out);
}

TEST(spreadEmptyListDefaultsFill) {
  RunResult r = runSrc("func f(a = 1, b = 2) { return [a, b] }\nprint(f(*[]))");
  EXPECT_STREQ(ctx, "[1, 2]\n", r.out);
  free(r.out);
}
TEST(spreadEmptyListTooFew) {
  RunResult r = runSrc("func g(a) { return a }\ng(*[])");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}
TEST(spreadIntoRestParam) {
  RunResult r = runSrc(
      "func varargs(fmt, *args) { return [fmt, args] }\n"
      "print(varargs(1, *[9, 9, 9]))\n"
      "print(varargs(*[1]))");
  EXPECT_STREQ(ctx, "[1, [9, 9, 9]]\n[1, []]\n", r.out);
  free(r.out);
}
TEST(spreadOnMethodCall) {  // CALL_METHOD 展开路径：接收者槽位不受影响
  RunResult r = runSrc("var l = []\nl.append(*[[1, 2]])\nprint(l)");
  EXPECT_STREQ(ctx, "[[1, 2]]\n", r.out);
  free(r.out);
}
TEST(spreadOverflowArgc) {  // u8 argc 边界：>255 展开实参走 ArgumentError 而非截断
  RunResult r = runSrc(
      "func add3(a, b, c) { return a + b + c }\n"
      "var xs = []\n"
      "for i in range(300) { xs.append(i) }\n"
      "add3(*xs)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}
TEST(defaultCapturesEnclosingLocal) {  // 默认参定义处求值捕获外层局部（含工厂形态）
  // 注：mk()(1) 调用表达式作 callee 尚未支持，经变量接回后再调用（语义不变）
  RunResult r = runSrc(
      "func mk() {\n"
      "    var x = 7\n"
      "    func f(a, b = x) { return a + b }\n"
      "    return f\n"
      "}\n"
      "var g = mk()\n"
      "print(g(1))");
  EXPECT_STREQ(ctx, "8\n", r.out);
  free(r.out);
}
TEST(defaultReferencingParamUndefined) {  // 默认参在定义作用域求值：引用参数名 → 定义期未定义名错误
  RunResult r = runSrc("func f(a, b = a) { return b }\nf(1)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "undefined name") != NULL);
  free(r.out);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      funcValueTypeAndPrint,
      funcIdentityEquality,
      callBasicAdd,
      callRecursiveFib,
      callArityTooFew,
      callArityTooMany,
      callFrameLimit,
      anonFuncExpr,
      arrowExprBody,
      multiReturnAndDestructure,
      localFuncShadowAndRedeclare,
      implicitReturnFallthrough,
      noReturnBodyNil,
      callArgEvalOrderPinned,
      arityErrorInsideExpression,
      defaultParams,
      defaultEvaluatedAtDefSite,
      restParam,
      restParamEmpty,
      spreadCall,
      spreadWithDefaults,
      spreadNonList,
      nativeSpread,
      spreadEmptyListDefaultsFill,
      spreadEmptyListTooFew,
      spreadIntoRestParam,
      spreadOnMethodCall,
      spreadOverflowArgc,
      defaultCapturesEnclosingLocal,
      defaultReferencingParamUndefined,
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
