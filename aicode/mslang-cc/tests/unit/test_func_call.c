// T018：函数对象值层单测（Task 1/4）——对象迁移回归钉 + 闭包/upvalue 链
#if !defined(_MSC_VER)
#define _POSIX_C_SOURCE 200809L  // setenv/unsetenv（MS_GC_HEAP_MAX 注入；严格 c11 下需显式暴露）
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "gc/gc.h"
#include "obj/obj_func.h"
#include "obj/obj_map.h"
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

static RunResult runSrcCommon(const char* src, const char* heapCap) {
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
      if (heapCap != NULL) {
        char env[64];
        snprintf(env, sizeof(env), "MS_GC_HEAP_MAX=%s", heapCap);
#if defined(_MSC_VER)
        _putenv(env);
#else
        setenv("MS_GC_HEAP_MAX", heapCap, 1);
#endif
      }
      MsVM vm;
      msVmInit(&vm);  // gcHeapInit 在此读取 MS_GC_HEAP_MAX（逐 VM 生效）
      if (heapCap != NULL) {
#if defined(_MSC_VER)
        _putenv("MS_GC_HEAP_MAX=");
#else
        unsetenv("MS_GC_HEAP_MAX");
#endif
      }
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

static RunResult runSrc(const char* src) { return runSrcCommon(src, NULL); }

// 分配压力触发多轮 GC：MS_GC_HEAP_MAX 于 msVmInit 前置入环境，init 后即还原（不泄入后续用例）
static RunResult runSrcHeapCap(const char* src, const char* cap) { return runSrcCommon(src, cap); }

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

// ---------- 闭包与每轮新绑定（Task 4） ----------

TEST(closureCounter) {  // 独立计数器：开放/闭合 upvalue 读写共享（05 §2）
  RunResult r = runSrc(
      "func counter() {\n    var n = 0\n    return func() { n += 1; return n }\n}\n"
      "var c1 = counter()\nvar c2 = counter()\nprint(c1(), c1(), c2(), c1())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1 2 1 3\n", r.out);
  free(r.out);
}
TEST(closureThroughTwoLevels) {  // 跨两层透传链：b 闭合后 c 经 b 的闭包取 a 帧 v
  RunResult r = runSrc(
      "func a() {\n    var v = 7\n    func b() {\n        func c() { return v }\n        return c\n    }\n"
      "    return b\n}\nvar bb = a()\nvar cc = bb()\nprint(cc())");
  EXPECT_STREQ(ctx, "7\n", r.out);
  free(r.out);
}
TEST(loopFreshForIn) {  // 模块层 for-in 目标每轮新绑定（04 §4 Go 1.22）
  RunResult r = runSrc(
      "var fns = []\nfor i in range(3) { fns.append(func() { return i }) }\n"
      "print(fns[0](), fns[1](), fns[2]())");
  EXPECT_STREQ(ctx, "0 1 2\n", r.out);
  free(r.out);
}
TEST(loopFreshForTri) {  // tri 形态：隐藏 carry 槽 + 每轮新鲜槽（04 §4）
  RunResult r = runSrc(
      "var fns = []\nfor j := 0; j < 3; j++ { fns.append(func() { return j }) }\n"
      "print(fns[0](), fns[1](), fns[2]())");
  EXPECT_STREQ(ctx, "0 1 2\n", r.out);
  free(r.out);
}
TEST(loopFreshMutationVisibleInIteration) {  // 同轮迭代内开放可写（f() 写穿入快照）；下轮闭合定格体终值
  // 注：期望为 Go 1.22 语义（04 §4）：闭包快照 = 该轮迭代体终值（f() 已写 k+10），
  // print 再各 +10 → 20 21 22（共享单槽会得 22 32 42；值拷贝会得 10 11 12）
  RunResult r = runSrc(
      "var fns = []\n"
      "for k in range(3) { var f = func() { k += 10; return k }; fns.append(f); f() }\n"
      "print(fns[0](), fns[1](), fns[2]())");
  EXPECT_STREQ(ctx, "20 21 22\n", r.out);
  free(r.out);
}
TEST(closureKeepsOuterLocalAcrossFrames) {  // 外层帧返回后闭包仍持有外层局部
  RunResult r = runSrc(
      "func mk() {\n    var msg = \"alive\"\n    return func() { return msg }\n}\n"
      "var f = mk()\nprint(f())");
  EXPECT_STREQ(ctx, "alive\n", r.out);
  free(r.out);
}
TEST(closureGcLivenessUnderPressure) {
  // 分配压力触发多轮 GC（MS_GC_HEAP_MAX 由 runSrcHeapCap 在 msVmInit 前置；gcHeapInit 逐 VM 读取）
  RunResult r = runSrcHeapCap(
      "func mk() {\n    var keep = [42]\n    return func() { return keep[0] }\n}\n"
      "var c = mk()\n"
      "var junk = []\n"
      "for i in range(2000) { junk = [i, i, i, i] }\n"
      "print(c())",
      "262144");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "42\n", r.out);
  free(r.out);
}
TEST(upvalueRebaseOnStackGrowth) {  // 开放 upvalue 指入值栈：扩容搬移后须重定基（msVmPush T018）
  // 深实参列表在闭包捕获之后压栈，越过栈容量触发 realloc 搬移；下轮闭合仍须读到正确槽值
  RunResult r = runSrc(
      "var fns = []\n"
      "func g(f, a, b, c, d, e, h, i, j, k, l) { return f }\n"
      "for i in range(2) { fns.append(g(func() { return i }, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)) }\n"
      "print(fns[0](), fns[1]())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0 1\n", r.out);
  free(r.out);
}
TEST(closureInComprehension) {  // 推导式 fresh 目标捕获：伪帧槽重映射 + 每轮闭合（04 §4）
  RunResult r = runSrc(
      "func mk(xs) { return [func() { return x } for x in xs] }\n"
      "var fs = mk([10, 20])\n"
      "print(fs[0](), fs[1]())\n"
      "var gs = [func() { return y } for y in range(3)]\n"
      "print(gs[0](), gs[1](), gs[2]())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "10 20\n0 1 2\n", r.out);
  free(r.out);
}
TEST(closureSurvivesExplicitCollect) {  // 手动管线：闭包入全局 → 两轮整堆回收 → 全局仍取到闭包对象
  const char* src = "func mk() { var keep = [7] ; return func() { return keep[0] } }\nvar c = mk()";
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  EXPECT(ctx, mod != NULL);
  Binder b;
  binderInit(&b, &arena, "t.ms", &diags);
  binderBind(&b, mod);
  binderDestroy(&b);
  Chunk c;
  EXPECT(ctx, compilerCompile(mod, &c, "t.ms", &diags));
  MsVM vm;
  msVmInit(&vm);
  FILE* f = tmpfile();
  vm.out = f;
  msVmRun(&vm, &c);
  EXPECT(ctx, !vm.hasError);
  gcCollectAll(&vm);
  gcCollectAll(&vm);
  MsObjString* key = msInternBytes(&vm, "c", 1);
  EXPECT(ctx, key != NULL);
  MsValue v;
  MsValue kv = msValueObj(&key->head);
  EXPECT(ctx, msObjMapGet(vm.globals, &kv, &v) == MS_MAP_OK);
  EXPECT(ctx, msIsCallableObj(&v));  // 行为调用归 Task 5 msVmCallValue
  fclose(f);
  chunkFree(&c);
  msVmFree(&vm);
  astArenaDestroy(&arena);
}

TEST(nestedCapturedTriLoops) {  // 嵌套捕获型 tri 循环：alias 栈正确分层（曾死循环/错捕获）
  RunResult r = runSrc(
      "var fns = []\n"
      "for i := 0; i < 2; i++ {\n"
      "    for j := 0; j < 2; j++ {\n"
      "        fns.append(func() { return i * 10 + j })\n"
      "    }\n"
      "}\n"
      "print(fns[0](), fns[1](), fns[2](), fns[3]())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0 1 10 11\n", r.out);
  free(r.out);
}
TEST(capturedTriLoopWithBreak) {  // break 前已闭合的快照不受影响
  RunResult r = runSrc(
      "var fns = []\n"
      "for i := 0; i < 5; i++ {\n"
      "    if i == 2 { break }\n"
      "    fns.append(func() { return i })\n"
      "}\n"
      "print(fns[0](), fns[1]())");
  EXPECT_STREQ(ctx, "0 1\n", r.out);
  free(r.out);
}
TEST(capturedRestParam) {  // rest 参数捕获为 upvalue
  RunResult r = runSrc(
      "func mk(*xs) { return func() { return xs } }\n"
      "var f = mk(1, 2)\nprint(f())");
  EXPECT_STREQ(ctx, "[1, 2]\n", r.out);
  free(r.out);
}
TEST(upvalueWriteThroughThreeLevels) {  // 三级链中间层写入
  RunResult r = runSrc(
      "func a() {\n"
      "    var v = 1\n"
      "    func b() { return func() { v += 10; return v } }\n"
      "    return b\n"
      "}\n"
      "var bb = a()\nvar c1 = bb()\nvar c2 = bb()\n"
      "print(c1(), c1(), c2())");
  EXPECT_STREQ(ctx, "11 21 31\n", r.out);
  free(r.out);
}

// ---------- 高阶内置回调 + 内省（Task 5） ----------

TEST(introspectionNameParams) {
  RunResult r = runSrc(
      "func greet(name, prefix = \"Mr.\", *rest) {}\n"
      "print(greet.__name__)\n"
      "print(greet.__params__)");  // 05 §6：含默认参名、rest 名不含 *
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "greet\n[\"name\", \"prefix\", \"rest\"]\n", r.out);
  free(r.out);
}
TEST(listMapWithClosure) {
  RunResult r = runSrc(
      "var xs = [1, 2, 3]\n"
      "var doubled = xs.map(func(x) { return x * 2 })\n"
      "print(doubled)\n"
      "print(xs.filter(func(x) { return x % 2 == 1 }))\n"
      "print(xs.reduce(func(a, b) { return a + b }))\n"
      "var total = 0\n"
      "xs.each(func(v, i) { total += v })\n"  // list.each 契约 f(v, i)（10 §3）
      "print(total)");
  EXPECT_STREQ(ctx, "[2, 4, 6]\n[1, 3]\n6\n6\n", r.out);
  free(r.out);
}
TEST(sortedWithScriptKey) {
  RunResult r = runSrc("print(sorted([3, 1, 2], func(x) { return -x }))");
  EXPECT_STREQ(ctx, "[3, 2, 1]\n", r.out);
  free(r.out);
}
TEST(iterLazyMapFilterScript) {
  RunResult r = runSrc(
      "var it = range(5).map(func(x) { return x * x }).filter(func(x) { return x % 2 == 0 })\n"
      "print(it.toList())");
  EXPECT_STREQ(ctx, "[0, 4, 16]\n", r.out);
  free(r.out);
}
TEST(closureCapturedByMap) {
  RunResult r = runSrc(
      "func make() {\n    var base = 100\n    return [1, 2].map(func(x) { return base + x })\n}\n"
      "print(make())");
  EXPECT_STREQ(ctx, "[101, 102]\n", r.out);
  free(r.out);
}
TEST(eachScriptFnWritingCapturedVar) {  // 高阶回调内写捕获变量（嵌套 runFrames 与 upvalue 共存）
  // 注：块内语句不以 [ 起头（解析器将前句尾 ] 续读为索引链，既有行为），list 先入变量
  RunResult r = runSrc(
      "func run() {\n"
      "    var acc = []\n"
      "    var xs = [1, 2, 3]\n"
      "    xs.each(func(v, i) { acc.append(v * v) })\n"
      "    return acc\n"
      "}\n"
      "print(run())");
  EXPECT_STREQ(ctx, "[1, 4, 9]\n", r.out);
  free(r.out);
}
TEST(mapCallbackDeepRecursionFramesGrowth) {  // 回调内深递归扩 frames 数组：外层 dispatch 帧缓存须重取
  RunResult r = runSrc(
      "func rec(n) { if n <= 0 { return 0 } return rec(n - 1) + 1 }\n"
      "func tail(a) { return a }\n"
      "var xs = [1, 2, 3]\n"
      "var r = xs.map(func(x) { return rec(20) })\n"  // 嵌套帧深 20+ → frames realloc 于嵌套执行中
      "print(tail(r))");  // 后续脚本调用写 frame->ip：缓存失效应已重取
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "[20, 20, 20]\n", r.out);
  free(r.out);
}
TEST(nativeBoundaryNestingCapped) {  // map 回调递归：C 层重入上限触发运行时错误而非进程栈溢出
  RunResult r = runSrc(
      "func rec(n) {\n"
      "    if n <= 0 { return 0 }\n"
      "    var xs = [n]\n"
      "    return xs.map(func(x) { return rec(n - 1) })[0]\n"
      "}\n"
      "print(rec(400))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "StackOverflowError") != NULL);
  free(r.out);
}
TEST(moderateNestingStillWorks) {  // 合理嵌套深度不受影响
  RunResult r = runSrc(
      "func rec(n) {\n"
      "    if n <= 0 { return 0 }\n"
      "    var xs = [n]\n"
      "    return xs.map(func(x) { return rec(n - 1) })[0]\n"
      "}\n"
      "print(rec(20))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0\n", r.out);
  free(r.out);
}
TEST(scriptCallbackErrorPropagates) {  // 回调内运行时错误经嵌套 unwind 冒泡（评审 Minor #2 pin）
  RunResult r = runSrc(
      "var xs = [1]\n"
      "xs.map(func(x) { return x.nope })\n"
      "print(\"unreachable\")");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.out, "unreachable") == NULL);
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
      closureCounter,
      closureThroughTwoLevels,
      loopFreshForIn,
      loopFreshForTri,
      loopFreshMutationVisibleInIteration,
      closureKeepsOuterLocalAcrossFrames,
      closureGcLivenessUnderPressure,
      upvalueRebaseOnStackGrowth,
      closureInComprehension,
      closureSurvivesExplicitCollect,
      nestedCapturedTriLoops,
      capturedTriLoopWithBreak,
      capturedRestParam,
      upvalueWriteThroughThreeLevels,
      introspectionNameParams,
      listMapWithClosure,
      sortedWithScriptKey,
      iterLazyMapFilterScript,
      closureCapturedByMap,
      eachScriptFnWritingCapturedVar,
      mapCallbackDeepRecursionFramesGrowth,
      nativeBoundaryNestingCapped,
      moderateNestingStillWorks,
      scriptCallbackErrorPropagates,
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
