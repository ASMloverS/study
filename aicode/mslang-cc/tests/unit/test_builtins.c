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
#include "obj/obj_map.h"
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

TEST(zipNonStrictRemainderCLevel) {  // 非 strict：短侧尽即止，长侧余量保留可续推（CPython 语义）
  MsVM vm;
  msVmInit(&vm);
  MsObjIter* a = msObjIterRangeNew(&vm, 0, 2, 1);
  EXPECT(ctx, a != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&a->head)));  // 源落栈 root 跨后续分配
  MsObjIter* b = msObjIterRangeNew(&vm, 0, 3, 1);
  EXPECT(ctx, b != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&b->head)));
  MsValue av = vm.stack[vm.stackTop - 2];
  MsValue bv = vm.stack[vm.stackTop - 1];  // zip 尽后续推长侧用（值拷贝，对象不移动）
  MsObjIter* z = msObjIterZipNew(&vm, av, bv, false);
  EXPECT(ctx, z != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&z->head)));  // zip 落栈 root（推进内 tuple 分配窗口）
  int32_t got = 0;
  MsValue item;
  while (msIterNext(&vm, vm.stack[vm.stackTop - 1], &item)) {
    got++;
  }
  EXPECT(ctx, !vm.hasError);
  EXPECT_EQ(ctx, 2, got);
  EXPECT(ctx, msIterNext(&vm, bv, &item));  // 长侧第 3 元素未被 zip 消费
  EXPECT_EQ(ctx, 2, item.as.i);
  EXPECT(ctx, !msIterNext(&vm, bv, &item));  // 此后才尽
  msVmFree(&vm);
}

TEST(mapIterModifiedCLevel) {  // MAP 游标硬化：迭代中结构性修改（删除转墓碑）→ IteratorError
  MsVM vm;
  msVmInit(&vm);
  MsObjMap* m = msObjMapNew(&vm);
  EXPECT(ctx, m != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&m->head)));  // map 落栈 root（迭代期间 put/remove 触达）
  for (int32_t i = 0; i < 4; i++) {
    MsValue k = msValueInt(i);
    EXPECT(ctx, msObjMapPut(&vm, m, &k, &k) == MS_MAP_OK);
  }
  MsObjIter* it = msObjIterMapNew(&vm, m);
  EXPECT(ctx, it != NULL);
  EXPECT(ctx, msVmPush(&vm, msValueObj(&it->head)));  // 迭代器落栈 root
  MsValue item;
  EXPECT(ctx, msIterNext(&vm, vm.stack[vm.stackTop - 1], &item));  // 首键 0（插入序）
  EXPECT_EQ(ctx, 0, item.as.i);
  EXPECT(ctx, !vm.hasError);
  MsValue k0 = msValueInt(0);
  EXPECT(ctx, msObjMapRemove(m, &k0, NULL) == MS_MAP_OK);  // 游标槽转墓碑（seq 链同时摘除）
  EXPECT(ctx, !msIterNext(&vm, vm.stack[vm.stackTop - 1], &item));
  EXPECT(ctx, vm.hasError);
  EXPECT(ctx, strstr(vm.errMsg, "IteratorError: map modified during iteration") != NULL);
  msVmFree(&vm);
}

// ---------- Tier0 内置函数全集（Task 2） ----------

TEST(iterNextExhaustNil) {  // iter/next：耗尽返回 nil（05 §4）
  RunResult r = runSrc("var it = iter([1, 2])\nprint(next(it))\nprint(next(it))\nprint(next(it))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1\n2\nnil\n", r.out);
  free(r.out);
}

TEST(lenAllTypes) {  // len：string 字节数 + list/map/set/tuple 元素数（10 §1）
  RunResult r = runSrc("print(len(\"中a\"), len([1, 2]), len({\"a\": 1}), len({1, 2}), len((1, 2, 3)))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "4 2 1 2 3\n", r.out);  // "中" 3 UTF-8 字节 + 'a'
  free(r.out);
}

TEST(typeAllTypes) {  // type：全类别类型名（10 §1；range 装箱为 iterator）
  RunResult r = runSrc("print(type(nil), type(true), type(1), type(1.5), type(\"\"), type([]), type(()), "
                       "type({}), type({1}), type(range(1)))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "nil bool int float string list tuple map set iterator\n", r.out);
  free(r.out);
}

TEST(rangeNegStepSum) {  // sum 消费负步进 range：10+8+6+4+2
  RunResult r = runSrc("print(sum(range(10, 0, -2)))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "30\n", r.out);
  free(r.out);
}

TEST(enumerateStart) {  // enumerate 起始值 + tuple 目标解构（04 §4）
  RunResult r = runSrc("for i, v in enumerate([\"a\", \"b\"], 1) { print(i, v) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1 a\n2 b\n", r.out);
  free(r.out);
}

TEST(zipStrictError) {  // zip strict 长度失配 → ValueError（错误槽文案）
  RunResult r = runSrc("for p in zip([1], [1, 2], true) { print(p) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError: zip() strict") != NULL);
  free(r.out);
}

TEST(zipSecondNotIterable) {  // zip 第二参不可迭代：TypeError 必须真抛错（FromValue errMsg 翻译）
  RunResult r = runSrc("print(zip([], 2))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "'int' is not iterable") != NULL);
  EXPECT_STREQ(ctx, "", r.out);
  free(r.out);
}

TEST(zipShortest) {  // zip 非 strict 短板对齐：单对后短侧尽即止（元素 repr 带 quote）
  RunResult r = runSrc("for p in zip([1, 2, 3], [\"a\"]) { print(p) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "(1, \"a\")\n", r.out);
  free(r.out);
}

TEST(sortedKeyStable) {  // sorted + len 键：按键值升序（1 < 2 = 2）
  RunResult r = runSrc("print(sorted([\"bb\", \"a\", \"cc\"], len))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "[\"a\", \"bb\", \"cc\"]\n", r.out);
  free(r.out);
}

TEST(sortedReverse) {  // sorted reverse=true：降序
  RunResult r = runSrc("print(sorted([3, 1, 2], nil, true))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "[3, 2, 1]\n", r.out);
  free(r.out);
}

TEST(sortedStableTies) {  // 稳定性：键相等保持原序（a/c 与 bb/dd 两组平键）
  RunResult r = runSrc("print(sorted([\"bb\", \"a\", \"dd\", \"c\"], len))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "[\"a\", \"c\", \"bb\", \"dd\"]\n", r.out);
  free(r.out);
}

TEST(minMaxContainer) {  // min/max 单容器参数：容器内最值
  RunResult r = runSrc("print(min([3, 1, 2]), max([3, 1, 2]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1 3\n", r.out);
  free(r.out);
}

TEST(minMaxVariadic) {  // min/max 多参数形态
  RunResult r = runSrc("print(min(4, 2, 9), max(4, 2, 9))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2 9\n", r.out);
  free(r.out);
}

TEST(sumForms) {  // sum 数值 init / 字符串拼接 init / 裸 range（init 起算：10+1+2=13）
  RunResult r = runSrc("print(sum([1, 2], 10))\nprint(sum([\"a\", \"b\"], \"\"))\nprint(sum(range(5)))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "13\nab\n10\n", r.out);  // 计划书 golden "12" 为笔误（10+3=13）
  free(r.out);
}

TEST(sumTypeMismatch) {  // sum 类型失配必须真抛错（arithBinary 只写 errMsg，内置须补置 hasError）
  RunResult r = runSrc("print(sum([1], \"x\"))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "unsupported operand types for '+'") != NULL);
  EXPECT_STREQ(ctx, "", r.out);  // 错误后不得继续执行（栈已弃）
  free(r.out);
}

TEST(sumListInit) {  // sum 泛化 '+' 的 list 拼接 init（msValueAdd list+list 分支）
  RunResult r = runSrc("print(sum([[1], [2]], []))\nprint(sum([], []))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "[1, 2]\n[]\n", r.out);
  free(r.out);
}

TEST(sumListNoInitTypeError) {  // 缺省 init 为 int 0：0+[1] 不可加 → TypeError（语义钉住）
  RunResult r = runSrc("print(sum([[1], [2]]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "unsupported operand types for '+'") != NULL);
  free(r.out);
}

TEST(gcPressureBuiltins) {  // 小堆重复回收窗口下聚合族正确性（env 同款见 test_obj_map.c）
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=4096");
#else
  setenv("MS_GC_HEAP_MAX", "4096", 1);
#endif
  RunResult r = runSrc("print(sum(range(200)))\nprint(len(set(range(50))))\n"
                       "var xs = [9, 1, 8, 2, 7, 3, 6, 4, 5, 0]\n"
                       "print(sorted(xs, nil, false))\nprint(sorted(xs, nil, true))\n"
                       "print(sum([[1], [2], [3]], []))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "19900\n50\n[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]\n[9, 8, 7, 6, 5, 4, 3, 2, 1, 0]\n"
                    "[1, 2, 3]\n",
               r.out);
  free(r.out);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}

TEST(sortedTypeError) {  // sorted 混型比较中止
  RunResult r = runSrc("print(sorted([1, \"a\"]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "'<' not supported") != NULL);
  free(r.out);
}

TEST(minMixedTypeError) {  // min 容器形混型比较中止
  RunResult r = runSrc("print(min([1, \"a\"]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "'<' not supported") != NULL);
  free(r.out);
}

TEST(minStringCodepoint) {  // string 迭代产 rune 码点 int（05 §4）：min("bca") = 97（'a'）
  RunResult r = runSrc("print(min(\"bca\"))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "97\n", r.out);
  free(r.out);
}

TEST(minEmptyError) {  // 空容器最值 → ValueError
  RunResult r = runSrc("print(min([]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "empty sequence") != NULL);
  free(r.out);
}

TEST(hashNumericTower) {  // hash 数值塔归一：hash(1) == hash(1.0)（02 §7）
  RunResult r = runSrc("print(hash(1) == hash(1.0))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\n", r.out);
  free(r.out);
}

TEST(assertFail) {  // assert 假条件 + 字符串消息 → AssertionError
  RunResult r = runSrc("assert(false, \"m\")");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "AssertionError: m") != NULL);
  free(r.out);
}

TEST(assertPass) {  // assert 真条件：无输出无错误
  RunResult r = runSrc("assert(1 == 1)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "", r.out);
  free(r.out);
}

TEST(isinstanceForms) {  // isinstance 字符串与 list 集合两种形态；非类型实参报 TypeError
  RunResult r = runSrc("print(isinstance([], \"list\"), isinstance(1, [\"string\", \"int\"]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true true\n", r.out);
  free(r.out);
  r = runSrc("isinstance(1, 2)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "expects a type name") != NULL);
  free(r.out);
}

TEST(callableKinds) {  // callable：原生函数真 / int 假 / bound 方法真
  RunResult r = runSrc("print(callable(print), callable(1), callable(\"x\".toUpper))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true false true\n", r.out);
  free(r.out);
}

TEST(builtinVars) {  // __name__/__version__ 内置变量（10 §5）
  RunResult r = runSrc("print(__name__ == \"__main__\", __version__ != \"\")");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true true\n", r.out);
  free(r.out);
}

TEST(placeholders) {  // 占位内置：各自报 RuntimeError 并指明目标任务
  const char* srcs[] = {"make(nil)", "cap(nil)", "race(nil)", "WeakRef(nil)", "input()"};
  const char* lands[] = {"T030", "T030", "T031", "T027", "T035"};
  for (int i = 0; i < 5; i++) {
    RunResult r = runSrc(srcs[i]);
    EXPECT(ctx, r.compileOk);
    EXPECT(ctx, r.hasError);
    EXPECT(ctx, strstr(r.err, "RuntimeError") != NULL);
    EXPECT(ctx, strstr(r.err, "lands in T0") != NULL);
    EXPECT(ctx, strstr(r.err, lands[i]) != NULL);
    free(r.out);
  }
}

TEST(idStable) {  // id：同一对象稳定相等，不同对象相异（活跃期唯一）
  RunResult r = runSrc("var a = []\nvar b = []\nprint(id(a) == id(a), id(a) == id(b))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true false\n", r.out);
  free(r.out);
}

TEST(allSeq) {  // all：全真 / 含假 / 空集（真值规则 02 §2：仅 nil/false 为假）
  RunResult r = runSrc("print(all([1, true, \"x\"]), all([1, false]), all([]))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true false true\n", r.out);
  free(r.out);
}

TEST(setFromIterable) {  // set(iterable)：去重构造
  RunResult r = runSrc("print(len(set([1, 1, 2])))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2\n", r.out);
  free(r.out);
}

TEST(reversedIter) {  // reversed(list)：反向迭代
  RunResult r = runSrc("for x in reversed([1, 2, 3]) { print(x) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n2\n1\n", r.out);
  free(r.out);
}

TEST(reversedString) {  // reversed(string)：反向 rune 码点（'b'=98 'a'=97）
  RunResult r = runSrc("for c in reversed(\"ab\") { print(c) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "98\n97\n", r.out);
  free(r.out);
}

TEST(enumerateTupleForm) {  // enumerate 产出 (i, v) 二元组（起始值生效；元素 repr 带 quote）
  RunResult r = runSrc("var it = enumerate([\"a\"], 5)\nvar p = next(it)\nprint(p)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "(5, \"a\")\n", r.out);
  free(r.out);
}

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
      zipNonStrictRemainderCLevel,
      mapIterModifiedCLevel,
      iterNextExhaustNil,
      lenAllTypes,
      typeAllTypes,
      rangeNegStepSum,
      enumerateStart,
      zipStrictError,
      zipSecondNotIterable,
      zipShortest,
      sortedKeyStable,
      sortedReverse,
      sortedStableTies,
      minMaxContainer,
      minMaxVariadic,
      sumForms,
      sumTypeMismatch,
      sumListInit,
      sumListNoInitTypeError,
      gcPressureBuiltins,
      sortedTypeError,
      minMixedTypeError,
      minStringCodepoint,
      minEmptyError,
      hashNumericTower,
      assertFail,
      assertPass,
      isinstanceForms,
      callableKinds,
      builtinVars,
      placeholders,
      idStable,
      allSeq,
      setFromIterable,
      reversedIter,
      reversedString,
      enumerateTupleForm,
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
