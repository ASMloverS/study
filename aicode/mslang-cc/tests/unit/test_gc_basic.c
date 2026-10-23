// T012：堆/GC 单测（docs/tasks/T012 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj.h"
#include "obj/obj_list.h"
#include "test_harness.h"
#include "vm/vm.h"

// 测试负载：NULL 行为类型 + magic 载荷（链式引用用 child；MS_OBJ_ITERATOR 行为归 T017）
typedef struct TestNode {
  MsObj head;
  MsValue child;
  ms_u64 magic;
} TestNode;

static TestNode* allocNode(MsVM* vm, ms_u64 magic) {
  TestNode* n = (TestNode*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(TestNode));
  n->child = msValueNil();
  n->magic = magic;
  return n;
}

static void testMarkNode(MsVM* vm, MsObj* obj) {
  // 标记 TestNode.child（容器任务 markFn 同构）
  // 注册纪律：g_markFns 为进程级表，msVmInit 已注册生产 string 迭代器 mark（MS_OBJ_ITERATOR）——
  // 分配该类型的用例须在 msVmInit 之后自行覆盖注册（TestNode 布局与 MsObjStrIter 不同，误用生产 mark 即类型混淆）；
  // 负载分配时必须初始化全部 MsValue 字段（allocNode 置 nil）
  gcMarkValue(vm, ((TestNode*)obj)->child);
}

TEST(allocAndStats) {
  MsVM vm;
  msVmInit(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, gcHeapBytes(&vm));
  TestNode* a = allocNode(&vm, 1);
  TestNode* b = allocNode(&vm, 2);
  EXPECT(ctx, a != NULL && b != NULL);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 2 * (long long)sizeof(TestNode), (long long)gcHeapBytes(&vm));
  EXPECT_EQ(ctx, MS_OBJ_ITERATOR, (MsObjType)a->head.type);
  EXPECT_EQ(ctx, 0, a->head.flags);
  EXPECT(ctx, vm.heap->objects == &b->head && b->head.next == &a->head);  // 头插
  msVmFree(&vm);
}

TEST(heapFreeReleasesAll) {  // vmFree 全释放（泄漏由 ASan 门禁验证）
  MsVM vm;
  msVmInit(&vm);
  for (int i = 0; i < 100; i++) {
    EXPECT(ctx, allocNode(&vm, (ms_u64)i) != NULL);
  }
  EXPECT_EQ(ctx, 100, gcHeapObjCount(&vm));
  msVmFree(&vm);
}

TEST(vmFreeZeroesState) {  // msVmFree 后零态（不暗置新堆）；重新 init 可复用（泄漏由 ASan 门禁验证）
  MsVM vm;
  msVmInit(&vm);
  (void)allocNode(&vm, 1);
  msVmFree(&vm);
  EXPECT(ctx, vm.heap == NULL && vm.stack == NULL && vm.frames == NULL && vm.globals == NULL);
  msVmInit(&vm);
  EXPECT(ctx, allocNode(&vm, 2) != NULL);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  msVmFree(&vm);
}

TEST(valueDirectStorageNoHeap) {  // 值直存：nil/bool/i64/f64 无堆分配（DoD）
  MsVM vm;
  msVmInit(&vm);
  (void)msValueNil();
  (void)msValueBool(true);
  (void)msValueInt(9);
  (void)msValueFloat(1.5);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, gcHeapBytes(&vm));
  msVmFree(&vm);
}

TEST(collectDeadObjects) {  // 分配回收：死对象全收、字节回落（DoD）
  MsVM vm;
  msVmInit(&vm);
  for (int i = 0; i < 50; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 无任何引用 → 全死
  }
  EXPECT_EQ(ctx, 50, gcHeapObjCount(&vm));
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0, (long long)gcHeapBytes(&vm));
  msVmFree(&vm);
}

TEST(stackKeepalive) {  // 栈保活：模块帧值栈即整个栈（23-gc §1 过渡）
  MsVM vm;
  msVmInit(&vm);
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);  // 覆盖生产 mark（注册纪律见 testMarkNode）
  TestNode* live = allocNode(&vm, 0xC0FFEE);
  for (int i = 0; i < 10; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 噪声死对象
  }
  vm.stack = malloc(sizeof(MsValue));  // 直接构造值栈槽
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&live->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0xC0FFEE, (long long)live->magic);  // 误收则 ASan UAF
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  msVmFree(&vm);
}

TEST(globalKeepalive) {  // 全局表保活
  MsVM vm;
  msVmInit(&vm);
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);  // 覆盖生产 mark（注册纪律见 testMarkNode）
  TestNode* live = allocNode(&vm, 0xABCD);
  vm.globals = malloc(sizeof(MsGlobalVar));
  vm.globalCount = vm.globalCap = 1;
  vm.globals[0].name = "keep";
  vm.globals[0].len = 4;
  vm.globals[0].value = msValueObj(&live->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0xABCD, (long long)live->magic);
  msVmFree(&vm);  // globals 由 msVmFree 释放
}

TEST(chainReference) {  // A→B→C：A 可达全活；不可达则同收
  MsVM vm;
  msVmInit(&vm);
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);  // 覆盖生产 mark（注册纪律见 testMarkNode）
  TestNode* a = allocNode(&vm, 1);
  TestNode* b = allocNode(&vm, 2);
  TestNode* c = allocNode(&vm, 3);
  a->child = msValueObj(&b->head);
  b->child = msValueObj(&c->head);
  vm.stack = malloc(sizeof(MsValue));
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&a->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 3, gcHeapObjCount(&vm));  // A 活则 B C 活
  EXPECT_EQ(ctx, 2, (long long)b->magic);  // 幸存者金丝雀（误收则 ASan UAF）
  EXPECT_EQ(ctx, 3, (long long)c->magic);
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));  // A 失根后 B C 同收
  msVmFree(&vm);
}

TEST(selfReferenceCycle) {  // 自引用：可达环存活且标记终止；失根后追踪式天然回收
  MsVM vm;
  msVmInit(&vm);
  gcSetMarkFn(MS_OBJ_ITERATOR, testMarkNode);  // 覆盖生产 mark（注册纪律见 testMarkNode）
  TestNode* a = allocNode(&vm, 7);
  a->child = msValueObj(&a->head);
  vm.stack = malloc(sizeof(MsValue));
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&a->head);
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));  // 可达环：已标记早退保证标记终止不死循环
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));  // 失根环：正确回收（追踪式 vs 引用计数式分水岭）
  EXPECT_EQ(ctx, 0, (long long)gcHeapBytes(&vm));
  msVmFree(&vm);
}

TEST(freeFnReleasesListData) {  // T013：sweep/heapFree 经 freeFn 释放 list items（泄漏由 ASan 门禁验证）
  MsVM vm;
  msVmInit(&vm);
  MsObjList* xs = msObjListNew(&vm);
  EXPECT(ctx, xs != NULL);
  EXPECT(ctx, msObjListPush(&vm, xs, msValueInt(1)));
  EXPECT(ctx, msObjListPush(&vm, xs, msValueInt(2)));
  gcCollectAll(&vm);  // 无 root：list 连同 items 经 freeFn 释放（误路径崩溃/UAF 即失败）
  EXPECT_EQ(ctx, 0, gcHeapObjCount(&vm));
  xs = msObjListNew(&vm);  // heapFree 路径同样覆盖
  (void)msObjListPush(&vm, xs, msValueInt(3));
  msVmFree(&vm);
}

TEST(permanentSurvives) {  // permanent：无 root 引用也不回收；heapFree 释放
  MsVM vm;
  msVmInit(&vm);
  MsObj* perm = &allocNode(&vm, 0x1234)->head;
  perm->flags |= MS_OBJ_PERMANENT;  // intern/常量池雏形（intern 表归 T013）
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 1, gcHeapObjCount(&vm));
  EXPECT_EQ(ctx, 0x1234, (long long)((TestNode*)perm)->magic);  // 金丝雀：统计与释放失同步时兜底（误收则 ASan UAF）
  msVmFree(&vm);  // 含 permanent 全释放（ASan 无泄漏）
}

TEST(allocGuardAutoTrigger) {  // MS_GC_HEAP_MAX 小阈值：循环分配堆稳定（DoD）
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=4096");
#else
  setenv("MS_GC_HEAP_MAX", "4096", 1);
#endif
  MsVM vm;
  msVmInit(&vm);  // gcHeapInit 读 env
  size_t maxSeen = 0;
  for (int i = 0; i < 10000; i++) {
    (void)allocNode(&vm, (ms_u64)i);  // 死对象：guard 触发全量回收
    size_t now = gcHeapBytes(&vm);
    if (now > maxSeen) {
      maxSeen = now;
    }
  }
  EXPECT(ctx, maxSeen < 4096 + 2 * sizeof(TestNode));  // 阈值 + 单次分配余量
  EXPECT(ctx, gcHeapObjCount(&vm) < 200);
  msVmFree(&vm);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      allocAndStats,
      heapFreeReleasesAll,
      vmFreeZeroesState,
      valueDirectStorageNoHeap,
      collectDeadObjects,
      stackKeepalive,
      globalKeepalive,
      chainReference,
      selfReferenceCycle,
      freeFnReleasesListData,
      permanentSurvives,
      allocGuardAutoTrigger,
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
