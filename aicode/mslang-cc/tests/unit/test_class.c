// T020：类对象层单测（Task 1）——MsObjClass/MsObjInstance 构造 + 字段表 + typeName + GC 标记
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
#include "obj/obj_class.h"
#include "obj/obj_func.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_string.h"
#include "obj/shape.h"
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

// ---------- 对象层（Task 1）：构造/字段表/typeName/GC ----------

TEST(classObjectBasics) {  // 构造 + 名字 + mro 空/单元素 + typeName "class"/"instance"
  MsVM vm;
  msVmInit(&vm);
  MsObjString* name = msInternBytes(&vm, "Animal", 6);
  EXPECT(ctx, name != NULL);
  MsObjClass* klass = msObjClassNew(&vm, name);
  EXPECT(ctx, klass != NULL);
  EXPECT(ctx, klass->bases != NULL && klass->bases->count == 0);
  EXPECT(ctx, klass->methods != NULL && klass->statics != NULL);
  EXPECT(ctx, klass->fieldNames != NULL && klass->fieldDefaults != NULL);
  MsValue kv = msValueObj(&klass->head);
  EXPECT_STREQ(ctx, "class", msValueTypeName(&kv));

  MsObjInstance* inst = msObjInstanceNew(&vm, klass);
  EXPECT(ctx, inst != NULL && inst->klass == klass);
  MsValue iv = msValueObj(&inst->head);
  EXPECT_STREQ(ctx, "instance", msValueTypeName(&iv));

  msVmFree(&vm);
}

TEST(shapeFieldTable) {  // 字段表 API：置/取/改/未命中
  MsVM vm;
  msVmInit(&vm);
  MsObjInstance* inst = msObjInstanceNew(&vm, NULL);
  EXPECT(ctx, inst != NULL);
  MsObjString* k1 = msInternBytes(&vm, "name", 4);
  MsValue v1 = msValueInt(7);
  EXPECT(ctx, msShapeFieldSet(&vm, inst, k1, &v1));
  MsValue got;
  EXPECT(ctx, msShapeFieldGet(inst, k1, &got));
  EXPECT_EQ(ctx, 7, got.as.i);
  MsValue v2 = msValueInt(9);
  EXPECT(ctx, msShapeFieldSet(&vm, inst, k1, &v2));  // 覆盖
  EXPECT(ctx, msShapeFieldGet(inst, k1, &got));
  EXPECT_EQ(ctx, 9, got.as.i);
  MsObjString* k2 = msInternBytes(&vm, "missing", 7);
  EXPECT(ctx, !msShapeFieldGet(inst, k2, &got));
  msVmFree(&vm);
}

TEST(classGcMarkChildren) {  // 注册 mark 后整堆回收不崩（子表全可达标记）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* name = msInternBytes(&vm, "C", 1);
  MsObjClass* klass = msObjClassNew(&vm, name);
  EXPECT(ctx, klass != NULL);
  if (!msVmPush(&vm, msValueObj(&klass->head))) {  // root
    EXPECT(ctx, false);
  }
  MsObjInstance* inst = msObjInstanceNew(&vm, klass);
  EXPECT(ctx, inst != NULL);
  if (!msVmPush(&vm, msValueObj(&inst->head))) {
    EXPECT(ctx, false);
  }
  gcCollectAll(&vm);
  EXPECT(ctx, !vm.hasError);
  msVmFree(&vm);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      classObjectBasics,
      shapeFieldTable,
      classGcMarkChildren,
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
