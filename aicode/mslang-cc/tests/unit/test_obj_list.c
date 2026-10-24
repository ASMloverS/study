// T014：list 数据面 + 执行面单测（docs/tasks/T014 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "gc/gc.h"
#include "obj/obj_list.h"
#include "obj/obj_string.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

// ---------- 数据面（Task 1） ----------

TEST(listGrowthDoubling) {  // §5.1 倍增：append 1000，cap 序列 8→16→…、均摊 O(1)
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  int32_t lastCap = 0;
  int32_t growthSteps = 0;
  for (int i = 0; i < 1000; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i)));
    if (list->cap != lastCap) {
      growthSteps++;
      if (lastCap == 0) {
        EXPECT_EQ(ctx, 8, list->cap);  // 初始 8（22 §2 表）
      } else {
        EXPECT_EQ(ctx, lastCap * 2, list->cap);  // 倍增
      }
      lastCap = list->cap;
    }
  }
  EXPECT_EQ(ctx, 1000, list->count);
  EXPECT(ctx, growthSteps <= 8);  // 8→16→…→1024 共 8 步（均摊 O(1)）
  msVmFree(&vm);
}

TEST(listNegativeIndexAndBounds) {  // §5.1 负索引/越界
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  for (int i = 0; i < 3; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i + 1)));
  }
  MsValue out = msValueNil();
  EXPECT(ctx, msObjListGet(list, -1, &out) && out.as.i == 3);  // xs[-1] → 末元素
  EXPECT(ctx, !msObjListGet(list, 10, &out));                  // 越界 false
  EXPECT(ctx, msObjListSet(list, -1, msValueInt(9)));
  EXPECT(ctx, msObjListGet(list, 2, &out) && out.as.i == 9);
  EXPECT(ctx, !msObjListSet(list, 10, msValueInt(0)));  // 越界写 false
  msVmFree(&vm);
}

TEST(listInsertRemoveClearReverse) {
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  for (int i = 0; i < 3; i++) {
    EXPECT(ctx, msObjListPush(&vm, list, msValueInt(i + 1)));  // [1,2,3]
  }
  ms_u32 v0 = list->version;
  EXPECT(ctx, msObjListInsert(list, 1, msValueInt(9)));  // [1,9,2,3]
  EXPECT_EQ(ctx, 4, list->count);
  MsValue mid = msValueNil();
  EXPECT(ctx, msObjListGet(list, 1, &mid) && mid.as.i == 9);
  EXPECT(ctx, list->version != v0);  // 结构修改 → version 递增
  EXPECT(ctx, msObjListInsert(list, -99, msValueInt(0)));  // 负索引钳位 0 → [0,1,9,2,3]
  EXPECT(ctx, msObjListGet(list, 0, &mid) && mid.as.i == 0);
  MsValue out = msValueNil();
  EXPECT(ctx, msObjListRemoveAt(list, -1, &out) && out.as.i == 3);  // [0,1,9,2]
  EXPECT_EQ(ctx, 4, list->count);
  EXPECT(ctx, !msObjListRemoveAt(list, 5, &out));
  msObjListReverse(list);  // [2,9,1,0]
  EXPECT(ctx, msObjListGet(list, 0, &out) && out.as.i == 2);
  EXPECT(ctx, msObjListGet(list, 3, &out) && out.as.i == 0);
  msObjListClear(list);
  EXPECT_EQ(ctx, 0, list->count);
  msVmFree(&vm);
}

TEST(listVersionBumpRules) {  // 22 §4：结构修改失效；SET_INDEX 非结构不失效
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(1)));
  ms_u32 v = list->version;
  EXPECT(ctx, msObjListSet(list, 0, msValueInt(2)));
  EXPECT_EQ(ctx, v, list->version);  // 非结构性
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(3)));
  EXPECT(ctx, list->version != v);
  v = list->version;
  msObjListClear(list);
  EXPECT_EQ(ctx, 0, list->count);
  EXPECT(ctx, list->version != v);
  msVmFree(&vm);
}

TEST(listIndexOf) {
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(5)));
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(6)));
  MsValue needle = msValueInt(6);
  EXPECT_EQ(ctx, 1, msObjListIndexOf(list, &needle));
  needle = msValueInt(9);
  EXPECT_EQ(ctx, -1, msObjListIndexOf(list, &needle));
  msVmFree(&vm);
}

int main(void) {
  TestCtx ctx = {0};
  listGrowthDoubling(&ctx);
  listNegativeIndexAndBounds(&ctx);
  listInsertRemoveClearReverse(&ctx);
  listVersionBumpRules(&ctx);
  listIndexOf(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
