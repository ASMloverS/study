// T019：生成器单测——编译层标志、协程对象、执行语义、genexp 惰性、GC 压力
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
#include "obj/obj_coroutine.h"
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
      msVmInit(&vm);
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

// ---------- 编译层（Task 1） ----------

TEST(genStarFuncCompiles) {  // func* 声明：AST_YIELD 可编译（今日 default 分支报"不支持"→ compileOk false）
  RunResult r = runSrc("func* g() {\n    yield 1\n}");
  EXPECT(ctx, r.compileOk);
  free(r.out);
}

TEST(implicitYieldFuncCompiles) {  // 体内直层 yield 隐式判定为生成器（05 §4 二选一）
  RunResult r = runSrc("func f() {\n    yield 1\n}");
  EXPECT(ctx, r.compileOk);
  free(r.out);
}

// ---------- 协程对象（Task 2） ----------

TEST(coroObjectBasics) {  // 分配 + 状态初值 + typeName/ToStr + 不可达回收不崩溃
  MsVM vm;
  msVmInit(&vm);
  MsObjCoroutine* co = msObjCoroutineNew(&vm, NULL, NULL);
  EXPECT(ctx, co != NULL);
  EXPECT(ctx, co->state == MS_CORO_START);
  MsValue v = msValueObj(&co->head);
  EXPECT_STREQ(ctx, "generator", msValueTypeName(&v));
  char buf[64];
  msValueToStr(&v, buf, sizeof(buf));
  EXPECT_STREQ(ctx, "<generator>", buf);
  gcCollectAll(&vm);  // 协程不可达 → 连同 stack/links 数组一并清扫（freeData 路径）
  msVmFree(&vm);
}

// ---------- 执行语义（Task 3） ----------

TEST(genCallIsLazy) {  // 调用不执行体（副作用计数 0；DoD）
  RunResult r = runSrc("func* g() {\n    print(\"ran\")\n}\nvar it = g()\nprint(\"created\")");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "created\n", r.out);
  free(r.out);
}

TEST(genFibSixNexts) {  // 05 §4 fib 生成器：next ×6 → 0 1 1 2 3 5
  RunResult r = runSrc(
      "func* fib() {\n"
      "    var a = 0\n"
      "    var b = 1\n"
      "    for {\n"
      "        yield a\n"
      "        var t = a + b\n"
      "        a = b\n"
      "        b = t\n"
      "    }\n"
      "}\n"
      "var g = fib()\n"
      "print(next(g), next(g), next(g), next(g), next(g), next(g))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0 1 1 2 3 5\n", r.out);
  free(r.out);
}

TEST(genExhaustedNilForever) {  // 有限生成器：先值后 nil nil（DoD）
  RunResult r = runSrc(
      "func* two() {\n    yield 1\n    yield 2\n}\nvar g = two()\nprint(next(g), next(g), next(g), next(g))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1 2 nil nil\n", r.out);
  free(r.out);
}

TEST(genForInDirect) {  // for v in gen() 直迭代（GET_ITER 返回自身 + FOR_ITER 推进）
  RunResult r = runSrc(
      "func* three() {\n    yield 10\n    yield 20\n    yield 30\n}\n"
      "var total = 0\nfor v in three() {\n    total = total + v\n}\nprint(total)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "60\n", r.out);
  free(r.out);
}

TEST(genCallbackPathLazy) {  // C 回调路径（map）传入生成器：入口即挂起（体不执行），结果为协程
  RunResult r = runSrc(
      "func* g(x) {\n    print(\"body\")\n    yield x\n}\n"
      "var r = [1, 2].map(g)\n"
      "print(\"mapped\")\n"
      "print(next(r[0]), next(r[1]))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "mapped\nbody\nbody\n1 2\n", r.out);
  free(r.out);
}

// ---------- 行为锁定（Task 4） ----------

TEST(genClosureAliasingAcrossYield) {  // 别名语义：挂起期间闭包写 closed → 恢复回拷帧槽（relink）
  RunResult r = runSrc(
      "func* g() {\n"
      "    var i = 0\n"
      "    var set = func(v) { i = v }\n"
      "    yield set\n"
      "    yield i\n"
      "}\n"
      "var it = g()\n"
      "var setter = next(it)\n"
      "setter(42)\n"
      "print(next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "42\n", r.out);
  free(r.out);
}

TEST(genTwoInstancesIndependent) {  // 双生成器交错 next：各自序列（快照互不干扰）
  RunResult r = runSrc(
      "func* seq(start) {\n    yield start\n    yield start + 1\n}\n"
      "var a = seq(1)\nvar b = seq(100)\n"
      "print(next(a), next(b), next(a), next(b), next(a), next(b))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1 100 2 101 nil nil\n", r.out);
  free(r.out);
}

TEST(genYieldInNestedFuncBelongsToNested) {  // 闭包内 yield 属闭包自身判定：外层非生成器、内层是
  RunResult r = runSrc(
      "func outer() {\n    var inner = func*() {\n        yield 7\n    }\n    return inner()\n}\n"
      "var it = outer()\nprint(next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "7\n", r.out);
  free(r.out);
}

TEST(genImplicitYieldRuntime) {  // 无 func* 的隐式生成器运行时等价（05 §4 二选一）
  RunResult r = runSrc("func f() {\n    yield 5\n}\nvar it = f()\nprint(next(it), next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "5 nil\n", r.out);
  free(r.out);
}

TEST(genStatePreservedAcrossResume) {  // 状态跨恢复保持（yield 表达式 + 多次恢复）
  RunResult r = runSrc(
      "func* counter() {\n"
      "    var n = 0\n"
      "    for i in range(3) {\n"
      "        n = n + i\n"
      "        yield n\n"
      "    }\n"
      "}\n"
      "var it = counter()\nprint(next(it), next(it), next(it), next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0 1 3 nil\n", r.out);
  free(r.out);
}

TEST(genForInWithBreak) {  // for-in + break：55 截断（05 §4 样例形态）
  RunResult r = runSrc(
      "func* fib() {\n"
      "    var a = 0\n"
      "    var b = 1\n"
      "    for {\n"
      "        yield a\n"
      "        var t = a + b\n"
      "        a = b\n"
      "        b = t\n"
      "    }\n"
      "}\n"
      "var last = 0\nfor v in fib() {\n    if v > 50 {\n        break\n    }\n    last = v\n}\nprint(last)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "34\n", r.out);
  free(r.out);
}

TEST(genDefaultAndRestParams) {  // 默认参/rest 生成器：setupCall 装配路径入快照（spec §4 边界）
  RunResult r = runSrc(
      "func* g(a, b = 10, *rest) {\n    yield a + b\n    yield rest\n}\n"
      "var it = g(1)\n"
      "var jt = g(1, 2, 3, 4)\n"
      "print(it.next(), jt.next())\n"
      "print(it.next(), jt.next())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "11 3\n[] [3, 4]\n", r.out);
  free(r.out);
}

TEST(genMethodNextForm) {  // g.next() 方法形态（iter 方法表经 msIsIterator 泛化到生成器）
  RunResult r = runSrc(
      "func* two() {\n    yield 1\n    yield 2\n}\n"
      "var g = two()\nprint(g.next(), g.next(), g.next())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1 2 nil\n", r.out);
  free(r.out);
}

TEST(genReentrantResumeRejected) {  // next 自身重入 → 精确 RuntimeError（CPython "already executing" 对齐）
  RunResult r = runSrc(
      "var it = nil\n"
      "func* g() {\n    yield 1\n    yield next(it)\n}\n"
      "it = g()\n"
      "print(next(it))\n"
      "print(next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "generator already executing") != NULL);
  EXPECT_EQ(ctx, -1, r.errLine);
  EXPECT_STREQ(ctx, "1\n", r.out);
  free(r.out);
}

// ---------- genexp 惰性（Task 5） ----------

TEST(genexpLazyTakeOnlyThree) {  // DoD：大源 take(3) 仅计算 3 次（计数器断言）
  RunResult r = runSrc(
      "var n = 0\n"
      "func bump(x) {\n    n = n + 1\n    return x\n}\n"
      "var it = (bump(x) for x in range(1000000))\n"
      "print(n)\n"
      "var picked = it.take(3)\n"
      "print(picked.toList())\n"
      "print(n)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "0\n[0, 1, 2]\n3\n", r.out);
  free(r.out);
}

TEST(genexpFirstIterableEager) {  // 首子句 iterable 创建时急求值（05 §4 脱糖形参语义）
  RunResult r = runSrc(
      "func mk() {\n    print(\"mk\")\n    return [1, 2, 3]\n}\n"
      "var it = (x for x in mk())\n"
      "print(\"created\")\n"
      "print(it.toList())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "mk\ncreated\n[1, 2, 3]\n", r.out);
  free(r.out);
}

TEST(genexpEquivalenceWithComprehension) {  // DoD：genexp 与推导式同输出对照（生成器单用，取两实例排干）
  RunResult r = runSrc(
      "var xs = range(10).toList()\n"
      "var a = [x * x for x in xs if x % 2 == 1]\n"
      "var b = (x * x for x in xs if x % 2 == 1)\n"
      "var c = (x * x for x in xs if x % 2 == 1)\n"
      "print(a == b.toList(), c.toList())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true [1, 9, 25, 49, 81]\n", r.out);
  free(r.out);
}

TEST(genexpSoleCallArgStillWorks) {  // 唯一调用实参形态：sum 排干（既有 genexp_pre 语义）
  RunResult r = runSrc("print(sum(x * x for x in range(5)))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "30\n", r.out);
  free(r.out);
}

TEST(genexpMultiFor) {  // 多 for 子句 genexp：嵌套 for 体块包裹（bindBlock 不变量）+ 推导式对照
  RunResult r = runSrc(
      "var xs = [1, 2]\nvar ys = [10, 20]\n"
      "var a = (x + y for x in xs for y in ys)\n"
      "var b = [x + y for x in xs for y in ys]\n"
      "var got = a.toList()\n"
      "print(got == b, got)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true [11, 21, 12, 22]\n", r.out);
  free(r.out);
}

TEST(genexpCapturesOuterLocal) {  // genexp 于函数体内捕获外层局部（upvalue 回填路径）
  RunResult r = runSrc(
      "func make() {\n    var base = 10\n    var g = (x + base for x in [1, 2, 3])\n    return g\n}\n"
      "var g = make()\nprint(g.toList())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[11, 12, 13]\n", r.out);
  free(r.out);
}

TEST(genexpMultiForDependent) {  // 依赖型第二 iterable：fors[i≥1] 逐轮求值锁定
  RunResult r = runSrc(
      "var xss = [[1, 2], [3]]\n"
      "var g = (y for x in xss for y in x)\n"
      "var got = g.toList()\n"
      "print(got)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[1, 2, 3]\n", r.out);
  free(r.out);
}

// ---------- GC 压力（Task 7） ----------

TEST(gcSuspendedReachableSurvives) {  // DoD：挂起生成器可达 → 快照/闭包跨多轮回收存活
  RunResult r = runSrcHeapCap(
      "func* nums() {\n    yield 1\n    yield 2\n    yield 3\n}\n"
      "var g = nums()\nprint(next(g))\n"
      "for i in range(100) {\n    var junk = range(50).toList()\n}\n"
      "print(next(g), next(g), next(g))",
      "1024");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1\n2 3 nil\n", r.out);
  free(r.out);
}

TEST(gcUnreachableCoroutinesSwept) {  // DoD：不可达挂起协程连同快照清扫（freeData 路径）
  RunResult r = runSrcHeapCap(
      "func* nums() {\n    var big = range(20).toList()\n    yield len(big)\n    yield 99\n}\n"
      "var total = 0\nfor i in range(200) {\n    var junk = nums()\n    next(junk)\n    total = total + 1\n}\n"
      "print(total)",
      "1024");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "200\n", r.out);
  free(r.out);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      genStarFuncCompiles,
      implicitYieldFuncCompiles,
      coroObjectBasics,
      genCallIsLazy,
      genFibSixNexts,
      genExhaustedNilForever,
      genForInDirect,
      genCallbackPathLazy,
      genClosureAliasingAcrossYield,
      genTwoInstancesIndependent,
      genYieldInNestedFuncBelongsToNested,
      genImplicitYieldRuntime,
      genStatePreservedAcrossResume,
      genForInWithBreak,
      genDefaultAndRestParams,
      genMethodNextForm,
      genReentrantResumeRejected,
      genexpLazyTakeOnlyThree,
      genexpFirstIterableEager,
      genexpEquivalenceWithComprehension,
      genexpSoleCallArgStillWorks,
      genexpMultiFor,
      genexpCapturesOuterLocal,
      genexpMultiForDependent,
      gcSuspendedReachableSurvives,
      gcUnreachableCoroutinesSwept,
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
