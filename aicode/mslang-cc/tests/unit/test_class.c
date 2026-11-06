// T020：类对象层单测（Task 1）——MsObjClass/MsObjInstance 构造 + 字段表 + typeName + GC 标记
// 纯 C-API 用例（执行驱动随 Task 3 首个脚本级用例一并引入）
#if !defined(_MSC_VER)
#define _POSIX_C_SOURCE 200809L  // setenv/unsetenv（MS_GC_HEAP_MAX 注入；严格 c11 下需显式暴露）
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_class.h"
#include "obj/obj_string.h"
#include "obj/shape.h"
#include "test_harness.h"
#include "vm/vm.h"

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
  // 极低堆压（阈值 1B：每次分配前整堆回收）——构造期子表分配即触发 GC：
  // D6 临时 root（构造器内 msVmPush）+ markFn 容忍 NULL children（载荷先置 NULL）
  // 堆存量钉 9：串 1 + 类 1 + 子表 5 + 实例 1 + 字段表 1（构造期回收后仍全活 → root 纪律回归闸）
  MsVM vm;
#if defined(_MSC_VER)
  _putenv("MS_GC_HEAP_MAX=1");
#else
  setenv("MS_GC_HEAP_MAX", "1", 1);
#endif
  msVmInit(&vm);  // gcHeapInit 在此读取 MS_GC_HEAP_MAX（逐 VM 生效）
#if defined(_MSC_VER)
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
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
  gcCollectAll(&vm);
  EXPECT(ctx, !vm.hasError);
  EXPECT_EQ(ctx, 9, gcHeapObjCount(&vm));
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
