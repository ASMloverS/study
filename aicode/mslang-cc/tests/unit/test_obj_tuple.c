// T015：tuple 数据面 + 相等/repr 单测（docs/tasks/T015 §5.1）
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
#include "obj/obj_tuple.h"
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

TEST(tupleAllocItemsNil) {  // 创建即全 nil（分配点 GC 安全态）；负索引/越界读取
  MsVM vm;
  msVmInit(&vm);
  MsObjTuple* t = msObjTupleNew(&vm, 3);
  EXPECT(ctx, t != NULL);
  EXPECT_EQ(ctx, 3, t->count);
  EXPECT(ctx, !t->hashValid);  // 惰性 hash 缓存初始无效（Task 2 msValueHash 写入）
  for (int32_t i = 0; i < t->count; i++) {
    EXPECT(ctx, t->items[i].kind == MS_VAL_NIL);
  }
  msVmPush(&vm, msValueObj(&t->head));  // root：覆写前落栈（分配窗口 GC 纪律）
  t->items[0] = msValueInt(1);
  const MsValue* last = msObjTupleGet(t, -1);
  EXPECT(ctx, last != NULL && last->kind == MS_VAL_NIL);  // -1 → items[count-1]
  const MsValue* first = msObjTupleGet(t, 0);
  EXPECT(ctx, first != NULL && first->kind == MS_VAL_I64 && first->as.i == 1);
  EXPECT(ctx, msObjTupleGet(t, 3) == NULL);
  EXPECT(ctx, msObjTupleGet(t, -4) == NULL);
  EXPECT(ctx, msObjTupleNew(&vm, -1) == NULL);  // 负计数拒绝
  msVmFree(&vm);
}

TEST(tupleEqualRepr) {  // 深相等（不同对象逐元素值等价）；repr 形态 (1, "a")
  MsVM vm;
  msVmInit(&vm);
  MsObjTuple* a = msObjTupleNew(&vm, 2);
  EXPECT(ctx, a != NULL);
  msVmPush(&vm, msValueObj(&a->head));  // root：后续 string/tuple 分配窗口 GC 纪律
  MsObjTuple* b = msObjTupleNew(&vm, 2);
  EXPECT(ctx, b != NULL);
  msVmPush(&vm, msValueObj(&b->head));
  MsObjString* s = msObjStringNew(&vm, "a", 1);
  EXPECT(ctx, s != NULL);
  msVmPush(&vm, msValueObj(&s->head));
  a->items[0] = msValueInt(1);
  a->items[1] = msValueObj(&s->head);
  b->items[0] = msValueInt(1);
  b->items[1] = msValueObj(&s->head);
  MsValue va = msValueObj(&a->head);
  MsValue vb = msValueObj(&b->head);
  EXPECT(ctx, msValueEqual(&va, &vb));  // 非同对象、逐元素深等价
  MsValue one = msValueInt(1);
  EXPECT(ctx, !msValueEqual(&va, &one));  // tuple vs int
  MsObjString* r = msValueReprForm(&vm, &va);
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 8, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "(1, \"a\")", 8) == 0);
  msVmFree(&vm);
}

TEST(tupleReprEdgeForms) {  // 空 () / 单元素尾随逗号 (1,) / 嵌套；str 与 repr 同构
  MsVM vm;
  msVmInit(&vm);
  MsObjTuple* empty = msObjTupleNew(&vm, 0);
  EXPECT(ctx, empty != NULL);
  msVmPush(&vm, msValueObj(&empty->head));
  MsValue ve = msValueObj(&empty->head);
  MsObjString* r = msValueReprForm(&vm, &ve);
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 2, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "()", 2) == 0);
  MsObjTuple* single = msObjTupleNew(&vm, 1);
  EXPECT(ctx, single != NULL);
  msVmPush(&vm, msValueObj(&single->head));
  single->items[0] = msValueInt(1);
  MsValue vs = msValueObj(&single->head);
  r = msValueReprForm(&vm, &vs);
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 4, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "(1,)", 4) == 0);
  MsObjTuple* inner = msObjTupleNew(&vm, 2);
  EXPECT(ctx, inner != NULL);
  msVmPush(&vm, msValueObj(&inner->head));  // root：outer 分配窗口 GC 纪律
  inner->items[0] = msValueInt(2);
  inner->items[1] = msValueInt(3);
  MsObjTuple* outer = msObjTupleNew(&vm, 2);
  EXPECT(ctx, outer != NULL);
  msVmPush(&vm, msValueObj(&outer->head));
  outer->items[0] = msValueInt(1);
  outer->items[1] = msValueObj(&inner->head);
  MsValue vo = msValueObj(&outer->head);
  r = msValueReprForm(&vm, &vo);
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 11, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "(1, (2, 3))", 11) == 0);
  r = msValueStrForm(&vm, &vo);  // tuple str/repr 同构（元素一律 repr，同 list）
  EXPECT(ctx, r != NULL);
  EXPECT_EQ(ctx, 11, r->byteLen);
  EXPECT(ctx, memcmp(r->bytes, "(1, (2, 3))", 11) == 0);
  msVmFree(&vm);
}

TEST(tupleGcKeepalive) {  // markChildren 递归链：list 仅经 tuple items 存活
  MsVM vm;
  msVmInit(&vm);
  MsObjList* list = msObjListNew(&vm);
  EXPECT(ctx, list != NULL);
  msVmPush(&vm, msValueObj(&list->head));  // root：tuple 分配窗口 GC 纪律
  EXPECT(ctx, msObjListPush(&vm, list, msValueInt(42)));
  MsObjTuple* t = msObjTupleNew(&vm, 2);
  EXPECT(ctx, t != NULL);
  msVmPush(&vm, msValueObj(&t->head));
  t->items[0] = msValueObj(&list->head);
  vm.stack[0] = vm.stack[1];  // 撤 list 槽：tuple 成唯一根（list 只能经 markChildren 存活）
  vm.stackTop--;
  gcCollectAll(&vm);
  EXPECT_EQ(ctx, 2, gcHeapObjCount(&vm));  // tuple + list
  EXPECT_EQ(ctx, 2, t->count);
  EXPECT(ctx, ((MsObjList*)t->items[0].as.obj) == list);  // 幸存金丝雀（误收则 UAF）
  EXPECT_EQ(ctx, 1, ((MsObjList*)t->items[0].as.obj)->count);
  msVmFree(&vm);
}

// ---------- 执行面（Task 4：字面量 + 解构 for-in） ----------

TEST(execBuildTupleUnpack) {  // 字面量构造 + 元组目标解构赋值（04 §2：无星号目标为赋值，须已声明绑定）
  RunResult r = runSrc(
      "var t = (1, \"a\")\n"
      "print(t)\n"
      "var x = 0\n"
      "var y = 0\n"
      "x, y = t\n"
      "print(x)\n"
      "print(y)\n"
      "print((1,))\n"
      "print(())\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "(1, \"a\")\n1\na\n(1,)\n()\n") == 0);  // print 走 str 形态：裸 string 无引号
  free(r.out);
}

TEST(execForInMultiVar) {  // 多变量 for-in：逐项 UNPACK_SEQ 展开（04 §4 解构迭代）
  RunResult r = runSrc("for k, v in [[1, 2], [3, 4]] {\n  print(k + v)\n}\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "3\n7\n") == 0);
  free(r.out);
}

TEST(execForInNestedTupleTarget) {  // 嵌套元组目标：a, (b, c) 解构（逆序消费嵌套在先）
  RunResult r = runSrc("for a, (b, c) in [(1, (2, 3))] {\n  print(a + b + c)\n}\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "6\n") == 0);
  free(r.out);
}

TEST(execTupleLiteralErrors) {  // list 元素入 tuple 字面量本身合法（hash 门控 Task 5/6 触达）：仅验证无编译错
  RunResult r = runSrc("var t = ([1], 2)\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  free(r.out);
}

TEST(execTupleEqualityExec) {  // 身份短路/空元组相等/计数不等（Task 1 遗留，quality review 补课）
  RunResult r = runSrc("print((1, \"a\") == (1, \"a\"))\nprint(() == ())\nprint((1,) == (1, 2))\nprint([(2, 3)])\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "true\ntrue\nfalse\n[(2, 3)]\n") == 0);
  free(r.out);
}

TEST(execForInMultiVarBreakCont) {  // 多变量 for-in 内 break/continue 控制流（Task 4 review 补课）
  RunResult r = runSrc(
      "for k, v in [[1, 2], [3, 4]] { if k == 3 { break } print(k + v) }\n"
      "for k, v in [[1, 2], [3, 4]] { if k == 1 { continue } print(k + v) }\n");
  EXPECT(ctx, r.compileOk && !r.hasError);
  EXPECT(ctx, strcmp(r.out, "3\n7\n") == 0);
  free(r.out);
}

int main(void) {
  TestCtx ctx = {0};
  tupleAllocItemsNil(&ctx);
  tupleEqualRepr(&ctx);
  tupleReprEdgeForms(&ctx);
  tupleGcKeepalive(&ctx);
  execBuildTupleUnpack(&ctx);
  execForInMultiVar(&ctx);
  execForInNestedTupleTarget(&ctx);
  execTupleLiteralErrors(&ctx);
  execTupleEqualityExec(&ctx);
  execForInMultiVarBreakCont(&ctx);
  if (ctx.failures > 0) {
    fprintf(stderr, "%d test(s) failed\n", ctx.failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
