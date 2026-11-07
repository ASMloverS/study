// T020：类对象层单测（Task 1）——MsObjClass/MsObjInstance 构造 + 字段表 + typeName + GC 标记
// Task 3 起附执行驱动与编译层用例（类声明编译/this 规则；VM 执行断言随 Task 4）
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

// ---------- 编译层（Task 3）：类声明编译 + this 规则（VM 执行断言随 Task 4） ----------

TEST(classDeclCompiles) {  // 类声明编译通过：无诊断，产出 NEW_CLASS/DEFINE_METHOD/INHERIT_INIT
  RunResult r = runSrc("class Point {\n    var x, y\n    func __init__(a, b) {\n        this.x = a\n"
                       "        this.y = b\n    }\n    func len() -> this.x + this.y\n}\n");
  EXPECT(ctx, r.compileOk);  // NEW_CLASS 等指令 VM 处理器随 Task 4：此处仅验证编译无诊断
  free(r.out);
}

TEST(thisOutsideMethodRejected) {  // 模块层 this → binder 诊断（编译失败）
  RunResult r = runSrc("print(this)");
  EXPECT(ctx, !r.compileOk);
  free(r.out);
}

TEST(thisCapturedInClosureCompiles) {  // 方法内嵌套闭包引用 this（捕获路径编译通过）
  RunResult r = runSrc(
      "class A {\n    var v\n    func mk() -> func() { return this.v }\n}\n");
  EXPECT(ctx, r.compileOk);
  free(r.out);
}

TEST(thisInPlainFunctionRejected) {  // 普通函数内 this → 诊断
  RunResult r = runSrc("func f() { return this }\n");
  EXPECT(ctx, !r.compileOk);
  free(r.out);
}

// ---------- VM 类创建（Task 4）----------

TEST(classCreationRuns) {  // 类建立：type(A) == "class"（未继承路径，方法已登记）
  RunResult r = runSrc("class A {\n    func f() -> 1\n}\nprint(type(A))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "class\n", r.out);
  free(r.out);
}

TEST(traitCreationRuns) {  // trait 声明同路径
  RunResult r = runSrc("trait T {\n    func m() -> 2\n}\nprint(type(T))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "class\n", r.out);
  free(r.out);
}

TEST(fieldDefaultEvaluatedOnce) {  // 类字段默认：类创建期求值一次（var x = mk() 只算一次）
  RunResult r = runSrc(
      "var n = 0\nfunc mk() { n += 1; return n }\nclass C {\n    var x = mk()\n}\nprint(C, n)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "<class C> 1\n", r.out);
  free(r.out);
}

TEST(baseNotClassTypeError) {  // 基类非类值 → TypeError（运行时，模块体执行期）
  RunResult r = runSrc("var notClass = 5\nclass C : notClass {\n}");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(mroBuildSurvivesGcStress) {  // 极低堆压（阈值 1B：每次分配前整堆回收）类创建 + MRO 线性化
  RunResult r = runSrcHeapCap(
      "class A {\n    func f() -> 1\n}\nclass C : A {\n    func g() -> 2\n}\nprint(type(C))", "1");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "class\n", r.out);
  free(r.out);
}

TEST(setAttrStaticsWriteAndRead) {  // 类属性写 + 读回（SET_ATTR/GET_ATTR CLASS 路径）
  RunResult r = runSrc("class C {\n}\nC.x = 1\nprint(C.x)\nC.x += 2\nprint(C.x)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1\n3\n", r.out);
  free(r.out);
}

TEST(setAttrNonObjectTypeError) {  // 值类型属性赋值 → TypeError
  RunResult r = runSrc("var s = \"a\"\ns.x = 1");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(duplicateBaseSyntaxError) {  // bases 清单内部重复 → SyntaxError 等价
  RunResult r = runSrc("class A {\n}\nclass C : A, A {\n}");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "SyntaxError") != NULL);
  free(r.out);
}

TEST(mroConflictSyntaxError) {  // C3 冲突：X/Y 顺序矛盾 → SyntaxError
  RunResult r = runSrc(
      "class X {\n}\nclass Y {\n}\nclass A : X, Y {\n}\nclass B : Y, X {\n}\nclass C : A, B {\n}");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "SyntaxError") != NULL);
  free(r.out);
}

TEST(longClassNameTruncatedSafe) {  // 超长类名形态截断（C1 回归钉）：无崩溃、前缀完整、A 串无垃圾字节
  char name[161];  // 160×'A' + NUL（> 87：必触发 buf[96] 截断路径）
  memset(name, 'A', 160);
  name[160] = '\0';
  char src[512];
  snprintf(src, sizeof(src), "class %s {\n}\nprint(%s)", name, name);
  RunResult r = runSrc(src);
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT(ctx, strncmp(r.out, "<class AAAA", 11) == 0);  // n=11：恰比较 11 字符前缀（12 会比对字面 NUL）
  EXPECT(ctx, strspn(r.out + 7, "A") >= 87);  // 截断形态 = "<class " + 纯 A 串（95 字符上限，无 '>' 尾）
  EXPECT_EQ(ctx, 96, (int)strlen(r.out));  // "<class " 7 + 88×'A' + '\n' = 96：精确钉无尾随垃圾
  free(r.out);
}

TEST(staticFuncParamsNoThisShift) {  // static 带参方法无 this 槽移位（compileOk 钉；槽布局另由金集覆盖）
  RunResult r = runSrc("class S {\n    static func add(a, b) -> a + b\n}\n");
  EXPECT(ctx, r.compileOk);
  free(r.out);
}

TEST(methodChunkContract) {  // 方法 chunk 元数据契约：METHOD flag + paramCount 含 this + params[0]
  const char* src = "class M {\n    func m(a) { return a }\n}";
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, &diags);
  AstNode* mod = parserParseModule(&p);
  EXPECT(ctx, mod != NULL);
  if (mod == NULL) {
    astArenaDestroy(&arena);
    return;
  }
  Binder b;
  binderInit(&b, &arena, "t.ms", &diags);
  binderBind(&b, mod);
  binderDestroy(&b);
  Chunk c;
  bool ok = compilerCompile(mod, &c, "t.ms", &diags);
  EXPECT(ctx, ok);
  bool found = false;
  if (ok) {  // 失败路径 chunk 亦已 chunkInit：下方 chunkFree 安全
    for (int32_t i = 0; i < c.constsLen && !found; i++) {
      if (c.consts[i].kind != CONST_PROTO) {
        continue;
      }
      const Chunk* proto = c.consts[i].as.proto;
      if (proto->nameLen == 1 && proto->name[0] == 'm') {
        found = true;
        EXPECT(ctx, (proto->flags & MS_CHUNK_METHOD) != 0);
        EXPECT_EQ(ctx, 2, proto->paramCount);  // this + a
        EXPECT_EQ(ctx, 4, proto->params[0].len);
        EXPECT(ctx, memcmp(proto->params[0].name, "this", 4) == 0);
      }
    }
    EXPECT(ctx, found);
  }
  chunkFree(&c);
  astArenaDestroy(&arena);
}

// ---------- 实例化与属性（Task 5，06 §1 §4）----------

TEST(animalInstantiate) {  // 06 §1 原例：__init__/this 写字段/describe 插值
  RunResult r = runSrc(
      "class Animal {\n"
      "    var name, age\n"
      "    func __init__(name, age = 1) {\n"
      "        this.name = name\n"
      "        this.age = age\n"
      "    }\n"
      "    func speak() -> \"...\"\n"
      "    func describe() -> \"${this.name} (${this.age})\"\n"
      "}\n"
      "var a = Animal(\"Cat\", 3)\n"
      "print(a.describe())\n"
      "print(a.speak())");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "Cat (3)\n...\n", r.out);
  free(r.out);
}

TEST(defaultCtorParam) {  // 默认参构造：Animal("Dog") → age=1
  RunResult r = runSrc(
      "class Animal {\n"
      "    var name, age\n"
      "    func __init__(name, age = 1) {\n"
      "        this.name = name\n"
      "        this.age = age\n"
      "    }\n"
      "}\n"
      "var d = Animal(\"Dog\")\n"
      "print(d.age)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1\n", r.out);
  free(r.out);
}

TEST(fieldDefaultsCopied) {  // 声明字段默认：无 init 置 nil、有 init 拷贝；__init__ 覆盖
  RunResult r = runSrc(
      "class C {\n"
      "    var a\n"
      "    var b = 5\n"
      "}\n"
      "var c = C()\n"
      "print(c.a, c.b)\n"
      "c.b = 6\n"
      "print(c.b)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "nil 5\n6\n", r.out);
  free(r.out);
}

TEST(noInitCtorArity) {  // 无 __init__ 传参 → ArgumentError
  RunResult r = runSrc("class C {\n}\nC(1)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(initArityError) {  // __init__ 存在时实参不足 → ArgumentError（经方法帧装配）
  RunResult r = runSrc("class C {\n    func __init__(a, b) { }\n}\nC(1)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ArgumentError") != NULL);
  free(r.out);
}

TEST(initReturnValueIgnored) {  // __init__ 返回值忽略：实例为调用结果
  RunResult r = runSrc(
      "class C {\n    var hit\n    func __init__() { this.hit = 1; return 99 }\n}\n"
      "var c = C()\nprint(c.hit, type(c))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1 instance\n", r.out);
  free(r.out);
}

TEST(dynamicField) {  // 未声明字段首赋创建可读（06 §1）
  RunResult r = runSrc("class C {\n}\nvar c = C()\nc.tag = \"x\"\nprint(c.tag)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "x\n", r.out);
  free(r.out);
}

TEST(fieldShadowsMethod) {  // 实例字段遮蔽方法（06 §4 查找序）
  RunResult r = runSrc(
      "class C {\n"
      "    func v() -> \"method\"\n"
      "}\n"
      "var c = C()\n"
      "c.v = \"field\"\n"
      "print(c.v)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "field\n", r.out);
  free(r.out);
}

TEST(attrMissingAttributeError) {  // 实例未命中 AttributeError
  RunResult r = runSrc("class C {\n}\nvar c = C()\nprint(c.nope)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "AttributeError") != NULL);
  free(r.out);
}

TEST(classAttrMissingAttributeError) {  // 类未命中 AttributeError（措辞更新回归钉）
  RunResult r = runSrc("class C {\n}\nprint(C.nope)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "AttributeError") != NULL);
  free(r.out);
}

TEST(thisCapturedInClosure) {  // 方法内闭包捕获 this（upvalue 路径端到端）
  RunResult r = runSrc(
      "class A {\n"
      "    var v\n"
      "    func __init__() { this.v = 10 }\n"
      "    func mk() -> func() { return this.v }\n"
      "}\n"
      "var a = A()\n"
      "var f = a.mk()\n"
      "print(f())");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "10\n", r.out);
  free(r.out);
}

TEST(methodWithDefaultsAndRest) {  // 方法默认参 + rest（方法帧装配完整路径）
  RunResult r = runSrc(
      "class C {\n"
      "    func m(a, b = 2, *rest) -> a + b + len(rest)\n"
      "}\n"
      "var c = C()\n"
      "print(c.m(1))\n"
      "print(c.m(1, 5))\n"
      "print(c.m(1, 5, 7, 9))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3\n6\n8\n", r.out);  // m(1,5,7,9)：a+b+len(rest)=1+5+2
  free(r.out);
}

TEST(generatorMethodEntrySuspends) {  // func* 方法：调用返回生成器（入口挂起，体不执行）
  RunResult r = runSrc(
      "class G {\n"
      "    func* nums() {\n"
      "        yield 1\n"
      "        yield 2\n"
      "    }\n"
      "}\n"
      "var g = G()\n"
      "var it = g.nums()\n"
      "print(type(it))\n"
      "for v in it { print(v) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "generator\n1\n2\n", r.out);
  free(r.out);
}

TEST(boundGeneratorMethod) {  // var f = g.nums 后 f()：bound 物化 + OP_CALL bound 路径入口挂起
  RunResult r = runSrc(
      "class G {\n"
      "    func* nums() {\n"
      "        yield 5\n"
      "    }\n"
      "}\n"
      "var g = G()\n"
      "var f = g.nums\n"
      "var it = f()\n"
      "print(type(it))\n"
      "for v in it { print(v) }");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "generator\n5\n", r.out);
  free(r.out);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      classObjectBasics,
      shapeFieldTable,
      classGcMarkChildren,
      classDeclCompiles,
      thisOutsideMethodRejected,
      thisCapturedInClosureCompiles,
      thisInPlainFunctionRejected,
      classCreationRuns,
      traitCreationRuns,
      fieldDefaultEvaluatedOnce,
      baseNotClassTypeError,
      mroBuildSurvivesGcStress,
      setAttrStaticsWriteAndRead,
      setAttrNonObjectTypeError,
      duplicateBaseSyntaxError,
      mroConflictSyntaxError,
      longClassNameTruncatedSafe,
      staticFuncParamsNoThisShift,
      methodChunkContract,
      animalInstantiate,
      defaultCtorParam,
      fieldDefaultsCopied,
      noInitCtorArity,
      initArityError,
      initReturnValueIgnored,
      dynamicField,
      fieldShadowsMethod,
      attrMissingAttributeError,
      classAttrMissingAttributeError,
      thisCapturedInClosure,
      methodWithDefaultsAndRest,
      generatorMethodEntrySuspends,
      boundGeneratorMethod,
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
