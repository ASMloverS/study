// T017 Task 1：泛化迭代器对象与 GET_ITER/FOR_ITER 协议单测（22 §4 / 05 §4）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "obj/obj_iter.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"
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

// ---------- 迭代协议（Task 1） ----------

TEST(iterSixSources) {  // 05 §4 六源：list/string/map/set/tuple/range
  RunResult r = runSrc("var s = 0\nfor x in [1, 2] { s += x }\nprint(s)\n"
                       "var cs = 0\nfor c in \"ab\" { cs += c }\nprint(cs)\n"
                       "for k in {\"a\": 1} { print(k) }\n"
                       "var t = 0\nfor v in {1, 2} { t += v }\nprint(t)\n"
                       "var u = 0\nfor w in (7, 8) { u += w }\nprint(u)\n"
                       "var q = 0\nfor n in range(3) { q += n }\nprint(q)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n195\na\n3\n15\n3\n", r.out);  // 'a'+'b'=195；set 无序取插入序 1+2
  free(r.out);
}

TEST(rangeNegStep) {  // 负步进饱和推进（原 FOR_ITER inline 语义等价迁移）
  RunResult r = runSrc("for x in range(10, 0, -2) { print(x) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "10\n8\n6\n4\n2\n", r.out);
  free(r.out);
}

TEST(rangeBoxedIdentity) {  // range 装箱为 MsObjIter：type 名与迭代器直通
  RunResult r = runSrc("print(type(range(3)))\nvar s = 0\nfor x in range(3) { s += x }\nprint(s)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "iterator\n3\n", r.out);
  free(r.out);
}

TEST(unpackString) {  // string 解构按 rune 码点（T017；04 §2 解构为赋值形态）
  RunResult r = runSrc("var a = 0\nvar b = 0\na, b = \"hi\"\nprint(a, b)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "104 105\n", r.out);  // 'h'=104 'i'=105
  free(r.out);
}

TEST(forSumAccum) {  // list 迭代经统一入口（版本号语义保持）
  RunResult r = runSrc("var done = 0\nfor x in [1, 2] { done += x }\nprint(done)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

// ---------- advZip C 层（zip 内建随 Task 2 落地，先经 API 直测） ----------

TEST(zipStrictCLevel) {  // strict：等长同尽干净收尾；仅单侧产出（hasA != hasB）才 ValueError
  MsVM vm;
  msVmInit(&vm);
  // 等长 strict：产出 2 对后同尽，不得报错（回归：曾对同尽误报 ValueError）
  MsObjIter* a1 = msObjIterRangeNew(&vm, 0, 2, 1);
  EXPECT(ctx, a1 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&a1->head)));  // 源落栈 root 跨后续分配
  MsObjIter* b1 = msObjIterRangeNew(&vm, 0, 2, 1);
  EXPECT(ctx, b1 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&b1->head)));
  MsObjIter* z1 = msObjIterZipNew(&vm, vm.stack[vm.stackTop - 2], vm.stack[vm.stackTop - 1], true);
  EXPECT(ctx, z1 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&z1->head)));  // zip 落栈 root（推进内 tuple 分配窗口）
  int32_t got = 0;
  MsValue item;
  while (msIterNext(&vm, vm.stack[vm.stackTop - 1], &item)) {
    EXPECT(ctx, msIsTuple(&item));
    got++;
  }
  EXPECT(ctx, !vm.hasError);
  EXPECT_EQ(ctx, 2, got);
  vm.stackTop -= 3;  // 弹 zip 与两源（源由 zip 持有，栈槽可弃）
  vm.hasError = false;  // 独立化下半场：上半场失败时错误槽不外溢
  vm.errMsg[0] = '\0';
  // 失配 strict：短侧先尽 → ValueError（hasA false / hasB true 路径）
  MsObjIter* a2 = msObjIterRangeNew(&vm, 0, 2, 1);
  EXPECT(ctx, a2 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&a2->head)));
  MsObjIter* b2 = msObjIterRangeNew(&vm, 0, 3, 1);
  EXPECT(ctx, b2 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&b2->head)));
  MsObjIter* z2 = msObjIterZipNew(&vm, vm.stack[vm.stackTop - 2], vm.stack[vm.stackTop - 1], true);
  EXPECT(ctx, z2 != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&z2->head)));
  got = 0;
  while (msIterNext(&vm, vm.stack[vm.stackTop - 1], &item)) {
    got++;
  }
  EXPECT_EQ(ctx, 2, got);
  EXPECT(ctx, vm.hasError);
  EXPECT(ctx, strstr(vm.errMsg, "ValueError: zip() strict: iterables have unequal lengths") != NULL);
  msVmFree(&vm);
}

// TODO(T017 Task2): iterNextExhaustNil（iter/next 内置随 Task 2 落地）

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      iterSixSources,
      rangeNegStep,
      rangeBoxedIdentity,
      unpackString,
      forSumAccum,
      zipStrictCLevel,
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
