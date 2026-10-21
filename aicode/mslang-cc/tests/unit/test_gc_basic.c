// T012：堆/GC 单测（docs/tasks/T012 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj.h"
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

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      allocAndStats,
      heapFreeReleasesAll,
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
