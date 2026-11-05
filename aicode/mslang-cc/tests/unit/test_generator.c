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
