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

// ---------- 执行面桥（Task 4+：全管线跑 .ms 源码） ----------

typedef struct RunResult {
  char* out;
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

// ---------- 数据面（Task 2） ----------

static MsObjList* listFromInts(MsVM* vm, const ms_i64* xs, int32_t n) {
  MsObjList* list = msObjListNew(vm);
  for (int32_t i = 0; i < n; i++) {
    if (!msObjListPush(vm, list, msValueInt(xs[i]))) {
      return NULL;
    }
  }
  msVmPush(vm, msValueObj(&list->head));  // root：slice/concat 内分配点 GC 不误收
  return list;
}

TEST(listSliceRanges) {  // §5.1 切片：[1,2,3,4][1::2] → [2,4]；[::-1] → [4,3,2,1]
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3, 4};
  MsObjList* list = listFromInts(&vm, xs, 4);
  bool zero = true;
  MsObjList* r = msObjListSlice(&vm, list, true, 1, false, 0, true, 2, &zero);
  EXPECT(ctx, r != NULL && !zero && r->count == 2);
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 2);
  EXPECT(ctx, msObjListGet(r, 1, &v) && v.as.i == 4);
  r = msObjListSlice(&vm, list, false, 0, false, 0, true, -1, &zero);  // [::-1]
  EXPECT(ctx, r != NULL && r->count == 4);
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 4);
  EXPECT(ctx, msObjListGet(r, 3, &v) && v.as.i == 1);
  r = msObjListSlice(&vm, list, true, 0, true, 0, true, 0, &zero);  // step 显式 0
  EXPECT(ctx, r == NULL && zero);
  msVmFree(&vm);
}

TEST(listConcatRepeat) {  // §5.1 拼接/重复
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 a[] = {1};
  const ms_i64 b[] = {2};
  MsObjList* la = listFromInts(&vm, a, 1);
  MsObjList* lb = listFromInts(&vm, b, 1);
  MsObjList* r = msObjListConcat(&vm, la, lb);
  EXPECT(ctx, r != NULL && r->count == 2);
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(r, 0, &v) && v.as.i == 1);
  EXPECT(ctx, msObjListGet(r, 1, &v) && v.as.i == 2);
  const ms_i64 z[] = {0};
  MsObjList* lz = listFromInts(&vm, z, 1);
  r = msObjListRepeat(&vm, lz, 3);  // [0]*3
  EXPECT(ctx, r != NULL && r->count == 3);
  EXPECT(ctx, msObjListGet(r, 2, &v) && v.as.i == 0);
  r = msObjListRepeat(&vm, lz, -2);  // 负数 → 空表
  EXPECT(ctx, r != NULL && r->count == 0);
  msVmFree(&vm);
}

TEST(listSortStableAndTyped) {  // 缺省 < 序；不可比较失败原序保持
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {3, 1, 2};
  MsObjList* list = listFromInts(&vm, xs, 3);
  EXPECT_EQ(ctx, MS_LIST_SORT_OK, msObjListSort(list));
  MsValue v = msValueNil();
  EXPECT(ctx, msObjListGet(list, 0, &v) && v.as.i == 1);
  EXPECT(ctx, msObjListGet(list, 2, &v) && v.as.i == 3);
  MsObjString* s = msObjStringNew(&vm, "b", 1);
  msVmPush(&vm, msValueObj(&s->head));
  EXPECT(ctx, msObjListPush(&vm, list, msValueObj(&s->head)));  // [1,2,3,"b"]
  EXPECT_EQ(ctx, MS_LIST_SORT_TYPE, msObjListSort(list));  // int vs string 不可比较
  EXPECT(ctx, msObjListGet(list, 0, &v) && v.as.i == 1);   // 原序保持
  msVmFree(&vm);
}

// ---------- 迭代器（Task 3） ----------

TEST(listIterWalkAndExhaust) {
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3};
  MsObjList* list = listFromInts(&vm, xs, 3);
  MsObjListIter* it = msObjListIterNew(&vm, list);
  msVmPush(&vm, msValueObj(&it->head));  // root
  MsValue v = msValueNil();
  bool modified = true;
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 1 && !modified);
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 2);
  EXPECT(ctx, msListIterNext(it, &v, &modified) && v.as.i == 3);
  EXPECT(ctx, !msListIterNext(it, &v, &modified) && !modified);  // 耗尽
  msVmFree(&vm);
}

TEST(listIterVersionInvalidation) {  // §5.1 版本号：迭代中 append 后 next → errModified
  MsVM vm;
  msVmInit(&vm);
  const ms_i64 xs[] = {1, 2, 3};
  MsObjList* list = listFromInts(&vm, xs, 3);
  MsObjListIter* it = msObjListIterNew(&vm, list);
  msVmPush(&vm, msValueObj(&it->head));
  MsValue v = msValueNil();
  bool modified = false;
  EXPECT(ctx, msListIterNext(it, &v, &modified) && !modified);
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(4)));  // 迭代中修改
  EXPECT(ctx, !msListIterNext(it, &v, &modified) && modified);
  msVmFree(&vm);
}

// ---------- 执行面（Task 4） ----------

TEST(runListIndexWriteAndSlice) {  // §5.1：xs[0]=v、xs[a:b:c]、xs[-1]
  RunResult r = runSrc("var xs = [1, 2, 3, 4]\nxs[0] = 9\nprint(xs)\nprint(xs[1::2])\n"
                       "print(xs[::-1])\nprint(xs[1:3])\nprint(xs[-1])\nprint(len(xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[9, 2, 3, 4]\n[2, 4]\n[4, 3, 2, 9]\n[2, 3]\n4\n4\n", r.out);
  free(r.out);
}

TEST(runListIndexWriteErrors) {
  RunResult r = runSrc("var xs = [1]\nxs[5] = 0");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nxs[\"k\"] = 0");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs[::0])");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
}

TEST(runListConcatRepeatOps) {  // §5.1：[1]+[2] / [0]*3 / 3*[0] 不支持
  RunResult r = runSrc("print([1] + [2])\nprint([0] * 3)\nprint([1, 2] * 0)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[1, 2]\n[0, 0, 0]\n[]\n", r.out);
  free(r.out);
  r = runSrc("print([1] + 2)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("print(3 * [0])");  // int * list 不支持（同 string * int 单向）
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(runListDeepEquality) {  // 02 §6：list 逐元素；指针短路
  RunResult r = runSrc("var a = [1, [2, \"x\"]]\nvar b = [1, [2, \"x\"]]\n"
                       "print(a == b)\nprint(a == a)\nprint(a == [1])\nprint(a != b)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true\ntrue\nfalse\nfalse\n", r.out);
  free(r.out);
}

// ---------- 方法族（Task 5） ----------

TEST(runListMethodFamily) {  // 10 §3 方法族组合
  RunResult r = runSrc("var xs = [3, 1, 2]\nxs.insert(1, 9)\nprint(xs)\nxs.remove(9)\nprint(xs)\n"
                       "print(xs.indexOf(2))\nprint(xs.contains(5))\nxs.sort()\nprint(xs)\n"
                       "xs.reverse()\nprint(xs)\nprint(xs.concat([7, 8]))\nprint(xs.repeat(2))\n"
                       "print(xs.first())\nprint(xs.last())\nvar ys = xs.copy()\nys.append(99)\n"
                       "print(xs)\nprint(ys)\nprint(xs.pop())\nprint(xs.pop(0))\nprint(xs)\n"
                       "xs.clear()\nprint(xs)\nprint(len(xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[3, 9, 1, 2]\n[3, 1, 2]\n2\nfalse\n[1, 2, 3]\n[3, 2, 1]\n"
                   "[3, 2, 1, 7, 8]\n[3, 2, 1, 3, 2, 1]\n3\n1\n[3, 2, 1]\n[3, 2, 1, 99]\n"
                   "1\n3\n[2]\n[]\n0\n", r.out);
  free(r.out);
}

TEST(runListMethodErrors) {  // remove 缺值 ValueError；pop 越界 IndexError；元数错
  RunResult r = runSrc("var xs = [1]\nxs.remove(5)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs.pop(5))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs.first())\nxs.clear()\nprint(xs.first())");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\nxs.append()");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(runListSortTypeError) {  // 混型排序 TypeError
  RunResult r = runSrc("var xs = [1, \"a\"]\nxs.sort()\nprint(xs)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(runListHigherOrderNatives) {  // map/filter/each/reduce：本任务仅原生函数（print/str 桩）
  RunResult r = runSrc("var xs = [1, 2]\nvar ys = xs.map(str)\nprint(ys)\n"
                       "print(xs.filter(str))\nxs.each(print)\nprint(xs.reduce(print, 0))\n"
                       "print(xs)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[\"1\", \"2\"]\n[1, 2]\n1 0\n2 1\n0 1\nnil 2\nnil\n[1, 2]\n", r.out);
  free(r.out);
}

// ---------- 迭代接线（Task 6） ----------

TEST(runListForIteration) {  // list 迭代 + for 解构铺垫（单目标）
  RunResult r = runSrc("var s = 0\nfor v in [1, 2, 3] { s += v }\nprint(s)\n"
                       "var t = \"\"\nfor c in [\"a\", \"b\"] { t = t + c }\nprint(t)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "6\nab\n", r.out);
  free(r.out);
}

TEST(runListIterConcurrentModify) {  // §5.1 迭代中修改 → IteratorError
  RunResult r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.append(v)\n}\nprint(\"unreachable\")");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.pop()\n}");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nfor v in xs {\n  xs.sort()\n}");  // sort 亦失效（任务书 §2）
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IteratorError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nvar n = 0\nfor v in xs {\n  xs[0] = 9\n  n += 1\n}\nprint(n)");  // SET_INDEX 不失效
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n", r.out);
  free(r.out);
}

TEST(runListIterExhaustNoError) {  // 耗尽后重新迭代新迭代器
  RunResult r = runSrc("var xs = [1]\nfor v in xs { }\nxs.append(2)\nvar n = 0\n"
                       "for v in xs { n += 1 }\nprint(n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2\n", r.out);
  free(r.out);
}

// ---------- 解构（Task 7） ----------

TEST(runUnpackSwapAndFewer) {  // §5.1 交换 / 3 目标收 5 元素多余丢弃
  RunResult r = runSrc("var a = 1\nvar b = 2\na, b = b, a\nprint(a)\nprint(b)\n"
                       "var xs = [1, 2, 3, 4, 5]\nvar p = 0\nvar q = 0\nvar w = 0\n"
                       "p, q, w = xs\nprint(p)\nprint(q)\nprint(w)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "2\n1\n1\n2\n3\n", r.out);
  free(r.out);
}

TEST(runUnpackStarPositions) {  // §5.1 星号任意位：a, *m, z = [1,2,3,4]
  RunResult r = runSrc("var xs = [1, 2, 3, 4]\nx1, *mid, z9 = xs\nprint(x1)\nprint(mid)\nprint(z9)\n"
                       "*head2, tail = xs\nprint(head2)\nprint(tail)\n"
                       "a0, *rest0 = xs\nprint(rest0)\n*all = xs\nprint(all)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1\n[2, 3]\n4\n[1, 2, 3]\n4\n[2, 3, 4]\n[1, 2, 3, 4]\n", r.out);
  free(r.out);
}

TEST(runUnpackStarTupleRhs) {  // 右值多表达式：BUILD_LIST 物化后统一 UNPACK_STAR
  RunResult r = runSrc("x1, *mid, z9 = 1, 2, 3, 4\nprint(x1)\nprint(mid)\nprint(z9)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "1\n[2, 3]\n4\n", r.out);
  free(r.out);
}

TEST(runUnpackErrors) {  // 不可解构 TypeError；不足 ValueError；未声明目标 RuntimeError
  RunResult r = runSrc("a, b = 5");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\na, b = xs");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("a, *m, z = 5");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1]\na, *m, z = xs");  // before+after=2 > 1
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("var xs = [1, 2, 3]\nvar a = 0\nvar b = 0\na, b, c = xs");  // c 未声明：同单目标赋值语义
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "undefined name") != NULL);
  free(r.out);
}

TEST(runUnpackIndexTargets) {  // 索引目标（04 §2）：元组与星号形态
  RunResult r = runSrc("var xs = [0, 0]\nvar v = 0\nxs[0], v = [7, 8]\nprint(xs)\nprint(v)\n"
                       "xs[1], *tail = [9, 1, 2]\nprint(xs[1])\nprint(tail)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "[7, 0]\n8\n9\n[1, 2]\n", r.out);
  free(r.out);
}

int main(void) {
  TestCtx ctx = {0};
  listGrowthDoubling(&ctx);
  listNegativeIndexAndBounds(&ctx);
  listInsertRemoveClearReverse(&ctx);
  listVersionBumpRules(&ctx);
  listIndexOf(&ctx);
  listSliceRanges(&ctx);
  listConcatRepeat(&ctx);
  listSortStableAndTyped(&ctx);
  listIterWalkAndExhaust(&ctx);
  listIterVersionInvalidation(&ctx);
  runListIndexWriteAndSlice(&ctx);
  runListIndexWriteErrors(&ctx);
  runListConcatRepeatOps(&ctx);
  runListDeepEquality(&ctx);
  runListMethodFamily(&ctx);
  runListMethodErrors(&ctx);
  runListSortTypeError(&ctx);
  runListHigherOrderNatives(&ctx);
  runListForIteration(&ctx);
  runListIterConcurrentModify(&ctx);
  runListIterExhaustNoError(&ctx);
  runUnpackSwapAndFewer(&ctx);
  runUnpackStarPositions(&ctx);
  runUnpackStarTupleRhs(&ctx);
  runUnpackErrors(&ctx);
  runUnpackIndexTargets(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
