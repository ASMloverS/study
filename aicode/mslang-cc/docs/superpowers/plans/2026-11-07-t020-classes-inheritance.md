# T020 class 与继承 mixin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 ms 语言的 class/单继承/mixin（MRO）、this、绑定方法、super、static 类属性与 instanceof，使 `docs/language/06-classes.md` §1-§4 用例全部通过。

**Architecture:** 三层推进——对象层（MsObjClass/MsObjInstance/字段表 + bound 泛化）、编译层（NEW_CLASS/DEFINE_METHOD/INHERIT_INIT 编码 + this 占 slot 0 + super 编码）、VM 层（类创建/实例化/属性查找/MRO 线性化）。spec 见 `docs/superpowers/specs/2026-11-07-t020-classes-design.md`。

**Tech Stack:** C11（MSVC + GCC/Clang）、CMake、自研 test_harness（TEST/EXPECT 宏）、ctest。

**关键机制约定（全计划适用）：**

- 方法 chunk：`paramCount` 含 this（this=slot 0，参数 slot 1..n）；`chunk->flags |= MS_CHUNK_METHOD`。
- 方法函数物化恒走 `OP_CLOSURE`（即使 0 捕获）——帧 `closure` 恒非 NULL，GET_SUPER 经 `closure->fn->owner` 取定义类。
- DEFINE_METHOD flags：`MS_DEFM_STATIC=1`（值入 statics：static var/const、类体 const、static func）、`MS_DEFM_DUNDER=2`（标记，T021）、`MS_DEFM_FIELD=4`（登记 fieldNames）、`MS_DEFM_FIELD_DEF=8`（值入 fieldDefaults）。
- NEW_CLASS 编码 `<nameIdx> <u8 baseCount>`，bases 值依序压栈；无 bases 时 mro=[self]。
- 错误前缀纪律：MRO 冲突 `SyntaxError: ...`（ms.c 路由退出码 3）；属性 `AttributeError`；类型 `TypeError`。
- 构建命令（worktree 根 = 仓库根）：`cmake --build build --config Debug`；测试 `ctest --test-dir build -C Debug -R <name> --output-on-failure`。

---

### Task 1: 对象层——MsObjClass/MsObjInstance/字段表 + GC + typeName

**Files:**
- Create: `src/obj/shape.h`, `src/obj/shape.c`
- Create: `src/obj/obj_class.h`, `src/obj/obj_class.c`
- Modify: `src/vm/value.c:296-318`（typeName 加 class/instance）
- Modify: `CMakeLists.txt`（ms_obj 加源文件）
- Modify: `src/vm/vm.c`（msVmInit 注册 markFn）
- Test: `tests/unit/test_class.c`（本任务起建，后续任务追加用例）+ `tests/CMakeLists.txt`

- [ ] **Step 1: 写失败测试（C 层构造 class/instance + 字段表读写 + typeName）**

创建 `tests/unit/test_class.c`（头部执行驱动与 test_func_call.c 相同，本任务先只写驱动 + 值层用例）：

```c
// T020：class/继承/mixin——对象层（Task 1）起逐步累积；执行驱动同 test_func_call.c
#if !defined(_MSC_VER)
#define _POSIX_C_SOURCE 200809L
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
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_string.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_harness.h"
#include "vm/vm.h"

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
    if (compiler_compile(mod, &c, "t.ms", &diags)) {
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
      msVmInit(&vm);
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
```

`tests/CMakeLists.txt` 末尾追加（generator 行之后）：

```cmake
ms_add_test(class ms_obj ms_gc ms_vm ms_compiler ms_binder)  # T020 类与继承
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build --config Debug --target test_class 2>&1 | Select-Object -Last 5`
Expected: 编译失败——`obj/obj_class.h`、`obj/shape.h` 不存在。

- [ ] **Step 3: 实现 shape 与 obj_class**

创建 `src/obj/shape.h`：

```c
// 实例字段表（T020，22 §6 基础版）：intern 名 → 值直查；
// T022 升级为 Shape 转移链 + 内联槽 + 溢出数组（本文件 API 不变）
#ifndef INCLUDE_MS_OBJ_SHAPE_H_
#define INCLUDE_MS_OBJ_SHAPE_H_

#include <stdbool.h>

#include "obj/obj.h"

struct MsObjInstance;  // obj_class.h（按指针避免头环）

// 字段读取：命中写 *out 返回 true；未命中 false（无错误场景）
bool msShapeFieldGet(const struct MsObjInstance* inst, const MsObjString* name, MsValue* out);
// 字段写入（首赋创建）；OOM 返回 false（调用方翻译错误槽）
bool msShapeFieldSet(struct MsVM* vm, struct MsObjInstance* inst, const MsObjString* name, const MsValue* v);

#endif  // INCLUDE_MS_OBJ_SHAPE_H_
```

创建 `src/obj/shape.c`：

```c
// 字段表基础版实现（T020）：单层 MsObjMap 直查
#include "obj/shape.h"

#include "obj/obj_class.h"
#include "obj/obj_map.h"

bool msShapeFieldGet(const MsObjInstance* inst, const MsObjString* name, MsValue* out) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapGet(inst->fields, &key, out) == MS_MAP_OK;
}

bool msShapeFieldSet(MsVM* vm, MsObjInstance* inst, const MsObjString* name, const MsValue* v) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapPut(vm, inst->fields, &key, v) == MS_MAP_OK;
}
```

创建 `src/obj/obj_class.h`：

```c
// 类对象族（T020，22 §2/06）：CLASS（方法表/MRO/字段默认）与 INSTANCE（字段表）
#ifndef INCLUDE_MS_OBJ_OBJ_CLASS_H_
#define INCLUDE_MS_OBJ_OBJ_CLASS_H_

#include <stdbool.h>

#include "obj/obj.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"

typedef struct MsObjClass {
  MsObj head;
  MsObjString* name;           // intern 类名
  MsObjList*   bases;          // 声明清单（首基类 + mixins；空 = 无基类）
  MsObjList*   mro;            // 线性化结果（自身在首位；INHERIT_INIT 前为 NULL）
  MsObjMap*    methods;        // intern 名 → 函数值（普通方法）
  MsObjMap*    statics;        // intern 名 → 类属性（static var/const 值、static func 函数值）
  MsObjList*   fieldNames;     // 声明字段名清单（intern 串）
  MsObjMap*    fieldDefaults;  // 声明字段名 → 默认值（var x = e 类创建时求值）
} MsObjClass;

typedef struct MsObjInstance {
  MsObj head;
  MsObjClass* klass;
  MsObjMap*   fields;  // 字段表（Shape 基础版；shape.h API）
} MsObjInstance;

// 创建空类（bases/methods/statics/fieldNames/fieldDefaults 均空表；mro = NULL）；失败 NULL
MsObjClass* msObjClassNew(MsVM* vm, MsObjString* name);
// 创建实例（klass 可 NULL——C 测试直构场景；fields 空表）；失败 NULL
MsObjInstance* msObjInstanceNew(MsVM* vm, MsObjClass* klass);

// children 标记（gcSetMarkFn 注册转发）
void msObjClassMarkChildren(MsVM* vm, MsObj* obj);
void msObjInstanceMarkChildren(MsVM* vm, MsObj* obj);

// MRO 中类成员判定（GET_ATTR/instanceof 共用）：x 的 mro 含 target
bool msClassMroContains(const MsObjClass* x, const MsObjClass* target);

static inline bool msIsClass(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_CLASS;
}
static inline bool msIsInstance(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_INSTANCE;
}

#endif  // INCLUDE_MS_OBJ_OBJ_CLASS_H_
```

创建 `src/obj/obj_class.c`：

```c
// 类对象族实现（T020）：构造 + GC 标记 + MRO 包含判定
#include "obj/obj_class.h"

#include "obj/shape.h"

MsObjClass* msObjClassNew(MsVM* vm, MsObjString* name) {
  MsObjClass* c = (MsObjClass*)msObjAlloc(vm, MS_OBJ_CLASS, sizeof(MsObjClass));
  if (c == NULL) {
    return NULL;
  }
  c->name = name;
  c->mro = NULL;
  c->bases = msObjListNew(vm);         // 分配期 name 无堆依赖（intern 永久）
  c->methods = msObjMapNew(vm);
  c->statics = msObjMapNew(vm);
  c->fieldNames = msObjListNew(vm);
  c->fieldDefaults = msObjMapNew(vm);
  if (c->bases == NULL || c->methods == NULL || c->statics == NULL || c->fieldNames == NULL ||
      c->fieldDefaults == NULL) {
    return NULL;  // 失败子对象由 GC 回收（c 已挂堆链）
  }
  return c;
}

MsObjInstance* msObjInstanceNew(MsVM* vm, MsObjClass* klass) {
  MsObjInstance* i = (MsObjInstance*)msObjAlloc(vm, MS_OBJ_INSTANCE, sizeof(MsObjInstance));
  if (i == NULL) {
    return NULL;
  }
  i->klass = klass;
  i->fields = msObjMapNew(vm);  // klass 无堆依赖；fields 失败由 GC 回收
  if (i->fields == NULL) {
    return NULL;
  }
  return i;
}

void msObjClassMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjClass* c = (MsObjClass*)obj;
  gcMarkValue(vm, msValueObj((MsObj*)c->name));
  gcMarkValue(vm, msValueObj(&c->bases->head));
  if (c->mro != NULL) {
    gcMarkValue(vm, msValueObj(&c->mro->head));
  }
  gcMarkValue(vm, msValueObj(&c->methods->head));
  gcMarkValue(vm, msValueObj(&c->statics->head));
  gcMarkValue(vm, msValueObj(&c->fieldNames->head));
  gcMarkValue(vm, msValueObj(&c->fieldDefaults->head));
}

void msObjInstanceMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjInstance* i = (MsObjInstance*)obj;
  if (i->klass != NULL) {
    gcMarkValue(vm, msValueObj(&i->klass->head));
  }
  gcMarkValue(vm, msValueObj(&i->fields->head));
}

bool msClassMroContains(const MsObjClass* x, const MsObjClass* target) {
  if (x->mro == NULL) {
    return x == target;
  }
  for (int32_t i = 0; i < x->mro->count; i++) {
    if (x->mro->items[i].as.obj == (const MsObj*)target) {
      return true;
    }
  }
  return false;
}
```

`src/vm/value.c` typeName 的 `MS_VAL_OBJ` switch 中 `MS_OBJ_COROUTINE` 分支后加：

```c
        case MS_OBJ_CLASS:  // T020
          return "class";
        case MS_OBJ_INSTANCE:
          return "instance";
```

`src/vm/vm.c` msVmInit 中现有 `gcSetMarkFn(MS_OBJ_BOUND_METHOD, boundMark);` 行后加（需 `#include "obj/obj_class.h"` 与 `#include "obj/shape.h"` 按现有 include 区放置）：

```c
  gcSetMarkFn(MS_OBJ_CLASS, msObjClassMarkChildren);
  gcSetMarkFn(MS_OBJ_INSTANCE, msObjInstanceMarkChildren);
```

`CMakeLists.txt`（obj_coroutine.c 行后）：

```cmake
# T020：类对象族 + 实例字段表（Shape 基础版）
target_sources(ms_obj PRIVATE src/obj/obj_class.c src/obj/shape.c)
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug --target test_class 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure`
Expected: `100% tests passed, 0 tests failed`（3 用例过）。

再跑全量：`ctest --test-dir build -C Debug`
Expected: 156/156（155 基线 + class）。

- [ ] **Step 5: Commit**

```bash
git add src/obj/obj_class.h src/obj/obj_class.c src/obj/shape.h src/obj/shape.c src/vm/value.c src/vm/vm.c CMakeLists.txt tests/unit/test_class.c tests/CMakeLists.txt
git commit -m "✨ feat(obj): add MsObjClass/MsObjInstance with field table and GC marking (T020)"
```

---

### Task 2: bound 泛化 + fn->owner（重构，现有测试守护）

**Files:**
- Modify: `src/obj/obj.h:55-63`（MsObjBoundMethod.method → MsValue）
- Modify: `src/obj/obj.c:36-45`（msObjBoundNew 签名）
- Modify: `src/obj/obj_func.h:15-20` + `src/obj/obj_func.c`（MsObjFunction 加 owner）
- Modify: `src/vm/vm.c`（boundMark + OP_CALL bound 分派 + GET_ATTR native 物化点）
- Test: 现有 155+ 全量绿（无新用例；泛化是行为保持重构）

- [ ] **Step 1: 改 MsObjBoundMethod 与构造函数**

`src/obj/obj.h`：

```c
// 绑定方法（T013 D5 / T020 泛化）：接收者 + 方法值（native 或函数/闭包）；
// OP_CALL bound 分派按 method 值类型选择 native 直调 / 脚本函数帧切换
typedef struct MsObjBoundMethod {
  MsObj head;
  MsValue receiver;
  MsValue method;  // MS_VAL_NATIVE 或 FUNCTION/CLOSURE 对象值
} MsObjBoundMethod;

MsObjNative* msObjNativeNew(MsVM* vm, MsNativeFn fn, const char* name, int32_t nameLen);
MsObjBoundMethod* msObjBoundNew(MsVM* vm, MsValue receiver, MsValue method);
```

`src/obj/obj.c`：

```c
MsObjBoundMethod* msObjBoundNew(MsVM* vm, MsValue receiver, MsValue method) {
  MsObjBoundMethod* b = (MsObjBoundMethod*)msObjAlloc(vm, MS_OBJ_BOUND_METHOD, sizeof(MsObjBoundMethod));
  if (b == NULL) {
    return NULL;
  }
  b->receiver = msValueNil();  // MsValue 字段先置 nil（markFn 纪律）
  b->method = msValueNil();
  b->receiver = receiver;
  b->method = method;
  return b;
}
```

- [ ] **Step 2: MsObjFunction 加 owner**

`src/obj/obj_func.h` MsObjFunction：

```c
typedef struct MsObjFunction {
  MsObj head;
  const Chunk* chunk;  // 原型（参数元数据/upvalue 描述符在 chunk）
  int32_t defaultCount;
  struct MsObjClass* owner;  // T020：定义类（普通方法非 NULL；super 经帧闭包回溯）
  MsValue defaults[];        // FAM：默认参值（定义处求值拷贝）
} MsObjFunction;
```

`src/obj/obj_func.c` 的 msObjFunctionNew 中 defaults 拷贝前加 `fn->owner = NULL;`（与其它字段初始化同区）。

- [ ] **Step 3: vm.c 适配（boundMark / GET_ATTR 物化 / OP_CALL bound 分派）**

vm.c boundMark（91 行注册的函数）改为：

```c
static void boundMark(MsVM* vm, MsObj* obj) {
  MsObjBoundMethod* b = (MsObjBoundMethod*)obj;
  gcMarkValue(vm, b->receiver);
  gcMarkValue(vm, b->method);  // T020：方法值（native 对象或函数/闭包）
}
```

OP_GET_ATTR native 物化段（原 `msObjBoundNew(vm, recv, native)`）改为（native 压栈 root 逻辑不变）：

```c
          MsObjBoundMethod* bound = msObjBoundNew(vm, recv, msValueObj(&native->head));
```

OP_CALL 的 `MS_OBJ_BOUND_METHOD` 分派改为（native 路径不变 + 脚本函数路径）：

```c
        if (callee.kind == MS_VAL_OBJ && callee.as.obj->type == MS_OBJ_BOUND_METHOD) {
          // bound 分派在弹栈前：接收者+实参原位留栈（调用期间为 GC root）
          MsObjBoundMethod* bm = (MsObjBoundMethod*)callee.as.obj;
          MsValue recv = bm->receiver;  // 先读出：调用期间 bm 可能被回收
          if (bm->method.kind == MS_VAL_NATIVE) {
            MsNativeFn fn = bm->method.as.native;
            vm->stack[vm->stackTop - argc - 1] = recv;  // callee 槽改写为接收者，args 原位
            MsValue r = fn(vm, argc + 1, &vm->stack[vm->stackTop - argc - 1]);
            if (vm->hasError) {
              goto dispatchExit;
            }
            vm->stackTop -= argc + 1;
            msVmPush(vm, r);
            frame = &vm->frames[vm->frameCount - 1];
            NEXT();
          }
          if (bm->method.kind == MS_VAL_OBJ &&
              (bm->method.as.obj->type == MS_OBJ_FUNCTION || bm->method.as.obj->type == MS_OBJ_CLOSURE)) {
            // T020：脚本方法 bound——栈形 [callee, a1..an] → [fn, recv, a1..an]（recv 作帧 slot 0）
            if (!msVmPush(vm, msValueNil())) {  // 上移占位（扩容安全）
              goto dispatchExit;
            }
            MsValue* slot0 = &vm->stack[vm->stackTop - argc - 2];
            memmove(slot0 + 1, slot0, sizeof(MsValue) * (size_t)(argc + 1));
            *slot0 = bm->method;
            slot0[1] = recv;
            frame->ip = ip;
            if (!msVmSetupCall(vm, argc + 1)) {
              vm->errLine = chunkLineAt(chunk, instrOff);
              goto dispatchExit;
            }
            frame = &vm->frames[vm->frameCount - 1];
            chunk = frame->chunk;
            code = chunk->code;
            consts = chunk->consts;
            ip = frame->ip;
            NEXT();
          }
          RUNTIME_ERROR("TypeError: bound method is not callable");
        }
```

（该分派插在原 bound 分派位置，替换原 `MsNativeFn fn = ...` 整段。）

- [ ] **Step 4: 全量测试守护**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug`
Expected: 156/156 全绿（重构无行为变化）。

- [ ] **Step 5: Commit**

```bash
git add src/obj/obj.h src/obj/obj.c src/obj/obj_func.h src/obj/obj_func.c src/vm/vm.c
git commit -m "♻️ refactor(obj): generalize bound method to MsValue and add function owner (T020)"
```

---

### Task 3: 编译器——类声明编译 + this 占 slot 0 + binder 隐藏 this

**Files:**
- Modify: `src/compiler/chunk.h:77`（MS_CHUNK_METHOD）
- Modify: `src/compiler/opcodes.h:65-69`（NEW_CLASS 注释更新 + OP_GET_SUPER）
- Modify: `src/compiler/chunk.c`（msOpNames/msOpLayouts 表——GET_SUPER 追加）
- Modify: `src/binder/binder.c`（bindFuncCommon hasThis + AST_THIS 解析 + super 放行说明）
- Modify: `src/compiler/compiler.c`（compileClassDecl + AST_THIS + compileAssign MEMBER 目标 + super 编码；此任务先做类声明编译与 AST_THIS，super 编码归 Task 5）
- Test: `tests/unit/test_class.c` 追加编译级用例

- [ ] **Step 1: 写失败测试（类声明编译产物 + this 语义）**

test_class.c 的 tests[] 前追加用例（并加入数组）：

```c
// ---------- 编译层（Task 3）----------

TEST(classDeclCompiles) {  // 类声明编译通过：无诊断、产出 NEW_CLASS/DEFINE_METHOD/INHERIT_INIT
  RunResult r = runSrc("class Point {\n    var x, y\n    func __init__(a, b) {\n        this.x = a\n"
                       "        this.y = b\n    }\n    func len() -> this.x + this.y\n}\n");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
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
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build --config Debug --target test_class; ctest --test-dir build -C Debug -R "^class$" --output-on-failure`
Expected: 三个新用例全 FAIL——`classDeclCompiles` compileOk=false（编译器无 AST_CLASS_DECL 语句分支，报 "statement form is not supported yet"）；`thisOutsideMethodRejected` 现状 `print(this)` 可编译通过（binder 对 AST_THIS 无检查）故断言 `!r.compileOk` FAIL；`thisCapturedInClosureCompiles` 同因类声明不支持而 FAIL。

- [ ] **Step 3: 实现 binder 与编译器**

`src/compiler/chunk.h` 77 行区：

```c
#define MS_CHUNK_GENERATOR 1u
#define MS_CHUNK_ASYNC 2u
#define MS_CHUNK_METHOD 4u  // T020：方法帧（slot 0 = this；paramCount 含 this）
```

`src/compiler/opcodes.h` 类指令区（替换 NEW_CLASS 注释并追加 GET_SUPER）：

```c
  OP_NEW_CLASS,      // <const name> <u8 baseCount>：bases 值依序压栈后消费
  OP_DEFINE_METHOD,  // <const name> <u8 flags(MS_DEFM_*)>：栈 [class, value] → [class]
  OP_INHERIT_INIT,   // 栈 [class] → [class]：构建 MRO（冲突报 SyntaxError 等价）
  OP_GET_ATTR,       // <const name> <u8 icSlot>（T022）
  OP_SET_ATTR,       // <const name>：栈 [obj, value] → [value]
  OP_GET_SUPER,      // <const name> <u8 icSlot>：栈 [this] → [bound/值]（定义类 MRO 下一类起查）
```

在 `src/compiler/chunk.c` 的 msOpNames 表相应位置加 `"GET_SUPER"`、msOpLayouts 加 `OPL_CONST_U8`（跟随 opcodes.h 枚举序——NEW_CLASS 布局改为 OPL_CONST_U8、SET_ATTR 保持 OPL_CONST；打开 chunk.c 对照枚举逐项核对）。

`src/binder/binder.c`：

1. `bindFuncCommon` 签名加 `bool hasThis`；`Scope* fn = pushScope(...)` 与 isAsync 设置后：

```c
  if (hasThis) {  // T020：方法帧 this 占 slot 0（隐藏声明；AST_THIS 按名解析到此）
    declareStrict(b, "this", 4, false, n);
  }
```

2. 三个调用点更新：AST_FUNC_DECL（482 行区）与 AST_FUNC_EXPR（1033 行区）传 `false`；AST_CLASS_DECL 成员（505 行区）传 `!(m->as.funcDecl.flags & AST_FUNC_STATIC) != false`——写为：

```c
        } else {
          bindFuncCommon(b, m, m->as.funcDecl.flags, m->as.funcDecl.params, m->as.funcDecl.paramCount,
                         m->as.funcDecl.body, m->as.funcDecl.exprBody,
                         (m->as.funcDecl.flags & AST_FUNC_STATIC) == 0);
        }
```

3. bindExpr 的 `case AST_THIS:`（1037 行区）替换原空分支：

```c
    case AST_THIS: {  // T020：按隐藏 "this" 声明解析（方法直层 LOCAL/嵌套 UPVALUE 捕获自动成立）
      Resolved r = resolveName(b, "this", 4);
      if (!r.found || (r.def->owner->kind == SCOPE_MODULE && r.sym->slot < 0)) {
        bindErr(b, n, 4, MSE_BIND_THIS_OUTSIDE, "'this' is only allowed inside a method");
        break;
      }
      if (r.hops == 0) {
        n->bindKind = AST_BIND_LOCAL;
        n->bindSlot = r.sym->slot;
        break;
      }
      n->bindKind = AST_BIND_UPVALUE;
      n->bindSlot = r.sym->slot;
      n->bindUpvalDepth = r.hops;
      recordCapture(b, &r);
      break;
    }
```

binder.h MseBindCode 追加 `MSE_BIND_THIS_OUTSIDE = 1022,  // this 仅方法体内（T020）`。

`src/compiler/compiler.c`：

1. DEFINE_METHOD flags 宏（opcodes.h 尾部 `#endif` 前加）：

```c
// T020 类成员登记 flags（OP_DEFINE_METHOD 第二操作数）
#define MS_DEFM_STATIC 1u     // 值入 statics（static var/const、类体 const、static func）
#define MS_DEFM_DUNDER 2u     // dunder 方法名标记（T021 消费）
#define MS_DEFM_FIELD 4u      // 登记 fieldNames（非 static var 声明）
#define MS_DEFM_FIELD_DEF 8u  // 值入 fieldDefaults（带初始化器字段）
```

2. `compileFuncProto` 签名加 `bool isMethod`；`proto->paramCount = paramCount;` 改为：

```c
  proto->paramCount = paramCount + (isMethod ? 1 : 0);  // T020：方法 slot 0 = this
  int32_t nParams = proto->paramCount;
  if (nParams > 0) {
    proto->params = malloc(sizeof(MsParamName) * (size_t)nParams);
    MS_ASSERT(proto->params != NULL);
  }
  if (isMethod) {  // 名字表首位 this（dump/__params__ 内省呈现）
    proto->params[0].name = "this";
    proto->params[0].len = 4;
  }
```

参数填充循环下标改为 `for (int32_t i = 0; i < paramCount; i++) { proto->params[i + (isMethod ? 1 : 0)] = ... }`（保留原校验逻辑不动，仅写入位偏移）；`proto->flags |= MS_CHUNK_METHOD;`（isMethod 时，与 GENERATOR 设置同区）；临时槽基线 `int32_t base = paramCount;` 改 `int32_t base = proto->paramCount;`。

3. `compileFunctionValue` 签名加 `bool isMethod`，透传 compileFuncProto；物化分支 `if (!hasCaptures)` 改为 `if (!hasCaptures && !isMethod)`（方法恒闭包化——帧 closure 保 owner 回溯）：

```c
static void compileFunctionValue(FuncState* fs, const AstNode* n, bool isMethod) {
  ...  // proto 编译与默认参压栈同现状
  if (!hasCaptures && !isMethod) {
    emitOp(fs, OP_MAKE_FUNCTION, n->line);
    emitByte(fs, (ms_u8)(proto->defaultParamCount | (proto->hasRest ? 0x80 : 0)));
  } else {  // 捕获函数与类方法（T020：帧 closure 恒持——GET_SUPER 经 fn->owner）
    emitConstOp(fs, OP_CLOSURE, k, n->line);
  }
}
```

既有调用点（compileFuncDecl 与 AST_FUNC_EXPR）传 `false`。

4. compileExpr 加 `case AST_THIS:`（放 AST_IDENT 附近）：

```c
    case AST_THIS: {  // T020：binder 已按隐藏声明标注（LOCAL=方法直层 / UPVALUE=嵌套捕获）
      if (n->bindKind == AST_BIND_LOCAL) {
        localOp(fs, OP_LOAD_LOCAL, n->bindSlot, n->line);
      } else if (n->bindKind == AST_BIND_UPVALUE) {
        int32_t idx = findUpvalByName(fs, "this", 4);
        if (idx < 0) {  // 防御（binder 捕获记录已保证存在）
          compErr(fs, n, 4, "upvalue capture is not registered");
          emitOp(fs, OP_LOAD_NIL, n->line);
          break;
        }
        upvalOp(fs, OP_LOAD_UPVAL, idx, n->line);
      } else {  // binder 已诊断（防御占位）
        compErr(fs, n, 4, "'this' is only allowed inside a method");
        emitOp(fs, OP_LOAD_NIL, n->line);
      }
      break;
    }
```

5. 新增 `compileClassDecl`（compileStmt 前定义；compileFuncDecl 之后）：

```c
// 类声明编译（T020，21 §3.4/06 §1-§4）：bases 压栈 → NEW_CLASS → 逐成员 DEFINE_METHOD →
// INHERIT_INIT（MRO）→ 类名落盘。成员求值序 = 声明序（字段默认/static 初值类创建期求值一次）
static void compileClassDecl(FuncState* fs, AstNode* n) {
  for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
    compileExpr(fs, n->as.classDecl.bases[i]);
  }
  int32_t k = nameConst(fs, n->as.classDecl.name, n->as.classDecl.nameLen);
  emitConstOp(fs, OP_NEW_CLASS, k, n->line);
  emitByte(fs, (ms_u8)n->as.classDecl.baseCount);
  for (int32_t i = 0; i < n->as.classDecl.memberCount; i++) {
    AstNode* m = n->as.classDecl.members[i];
    if (m->kind == AST_FUNC_DECL) {
      bool isStatic = (m->as.funcDecl.flags & AST_FUNC_STATIC) != 0;
      compileFunctionValue(fs, m, !isStatic);
      int32_t mk = nameConst(fs, m->as.funcDecl.name, m->as.funcDecl.nameLen);
      ms_u8 flags = isStatic ? MS_DEFM_STATIC : 0u;
      int32_t nl = m->as.funcDecl.nameLen;
      if (nl > 4 && m->as.funcDecl.name[0] == '_' && m->as.funcDecl.name[1] == '_' &&
          m->as.funcDecl.name[nl - 2] == '_' && m->as.funcDecl.name[nl - 1] == '_') {
        flags |= MS_DEFM_DUNDER;  // __x__ 形态标记（T021 消费）
      }
      emitConstOp(fs, OP_DEFINE_METHOD, mk, m->line);
      emitByte(fs, flags);
    } else {  // AST_VAR_DECL 字段/类属性
      bool isStatic = (m->as.varDecl.flags & (AST_VAR_STATIC | AST_VAR_CONST)) != 0;
      if (m->as.varDecl.init != NULL) {
        compileExpr(fs, m->as.varDecl.init);
      } else {
        emitOp(fs, OP_LOAD_NIL, m->line);
      }
      int32_t mk = nameConst(fs, m->as.varDecl.name, m->as.varDecl.nameLen);
      ms_u8 flags = isStatic ? MS_DEFM_STATIC
                             : (ms_u8)(MS_DEFM_FIELD | (m->as.varDecl.init != NULL ? MS_DEFM_FIELD_DEF : 0));
      emitConstOp(fs, OP_DEFINE_METHOD, mk, m->line);
      emitByte(fs, flags);
    }
  }
  emitOp(fs, OP_INHERIT_INIT, n->line);
  finishDeclStore(fs, n);
}
```

compileStmt 的 `case AST_FUNC_DECL:` 后加 `case AST_CLASS_DECL: compileClassDecl(fs, n); break;`。

6. compileAssign 的 IDENT 分支前加 MEMBER 目标（覆盖普通与复合）：

```c
  if (target->kind == AST_MEMBER) {  // T020：obj.name = v（复合同展开 GET→OP→SET）
    int32_t k = nameConst(fs, target->as.memberExpr.name, target->as.memberExpr.nameLen);
    compileExpr(fs, target->as.memberExpr.obj);
    if (n->as.assign.op != AST_OP_NONE) {
      emitConstOp(fs, OP_GET_ATTR, k, n->line);
      emitByte(fs, 0);  // icSlot（T022）
      compileExpr(fs, n->as.assign.value);
      emitOp(fs, binaryOpCode(n->as.assign.op), n->line);
    } else {
      compileExpr(fs, n->as.assign.value);
    }
    emitConstOp(fs, OP_SET_ATTR, k, n->line);
    return;
  }
```

7. storeUnpackTarget（1086 行报错位）加 MEMBER 分支（解构目标含 MEMBER）：

```c
  if (t->kind == AST_MEMBER) {  // T020：解构目标 obj.name
    compileExpr(fs, t->as.memberExpr.obj);
    int32_t k = nameConst(fs, t->as.memberExpr.name, t->as.memberExpr.nameLen);
    emitOp(fs, OP_ROT, t->line);
    emitConstOp(fs, OP_SET_ATTR, k, t->line);
    return;
  }
```

（解构逆序消费时值在栈顶：栈 [obj, v] → ROT → [v, obj]？——按 SET_INDEX 同款三槽序核对：SET_INDEX 消费 [v obj idx]。SET_ATTR 只两操作数 [obj v]。storeUnpackTarget 语境：值已在栈顶，先编 obj 再 ROT 得 [obj, v]？ROT 三元旋 转 a b c → b c a。栈 [v, obj]（obj 后压）ROT → [obj, v]？ a=v b=obj → ROT 后 b a? 不对——ROT 需三值。直接顺序：先 compileExpr(obj) 得 [v, obj]，再 SWAP（二元交换）得 [obj, v]。用 OP_SWAP：`emitOp(fs, OP_SWAP, t->line);` 替换上例 ROT。）

8. scanMaxLocalSlot/scanDirectCaptures 已含 AST_CLASS_DECL/AST_MEMBER 分支（现状即可，无需改）。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure; ctest --test-dir build -C Debug`
Expected: class 6 用例过（3 值层 + 3 编译层）；全量 156 绿（编译器改动不破坏既有——类声明此前不可执行，无回归面）。
注意：classDeclCompiles 若失败，读 `--dump-ast`/诊断定位（常见：chunk.c 名表与枚举错位——disasm 用）。

- [ ] **Step 5: Commit**

```bash
git add src/compiler/chunk.h src/compiler/chunk.c src/compiler/opcodes.h src/compiler/compiler.c src/binder/binder.h src/binder/binder.c tests/unit/test_class.c
git commit -m "✨ feat(compiler): compile class decls with this-as-slot-0 methods (T020)"
```

---

### Task 4: VM——类创建指令（NEW_CLASS/DEFINE_METHOD/INHERIT_INIT 基路径）

**Files:**
- Create: `src/vm/class_ops.c`（MRO 线性化 + 类查找公共面——本任务先 MRO + 查找）
- Create: `src/vm/class_ops.h`
- Modify: `src/vm/vm.c`（三指令 handler + SET_HANDLER）
- Modify: `CMakeLists.txt`（ms_vm 加源文件）
- Test: `tests/unit/test_class.c` 追加

- [ ] **Step 1: 写失败测试**

test_class.c 追加（tests[] 同步登记）：

```c
// ---------- VM 类创建（Task 4）----------

TEST(classCreationRuns) {  // 类建立：type(A) == "class"、方法已登记（未继承路径）
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

TEST(fieldDefaultEvaluatedOnce) {  // 类字段默认：类创建期求值（var x = 1 只算一次）
  RunResult r = runSrc(
      "var n = 0\nfunc mk() { n += 1; return n }\nclass C {\n    var x = mk()\n}\nprint(C, n)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "<class C> 1\n", r.out);  // print(类) 形态（StrForm 类分支随本任务接入）
  free(r.out);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build --config Debug --target test_class; ctest --test-dir build -C Debug -R "^class$" --output-on-failure`
Expected: 新 3 用例 FAIL——VM 无 handler（unimplemented 陷阱 → 运行时错误）。

- [ ] **Step 3: 实现 class_ops 与三指令**

创建 `src/vm/class_ops.h`：

```c
// 类运行时操作（T020，06 §2/22 §6）：MRO 线性化 + 属性查找公共面（vm.c/builtins.c 共用）
#ifndef INCLUDE_MS_VM_CLASS_OPS_H_
#define INCLUDE_MS_VM_CLASS_OPS_H_

#include <stdbool.h>

#include "obj/obj_class.h"
#include "vm/vm.h"

// MRO 线性化并写 klass->mro（[本类] + mixins 逆序 + 基类 MRO；去重保首；
// 逐 base 验证其 mro 为结果子序列）。冲突/类型错误/OOM：置错误槽返回 false
bool msClassBuildMro(MsVM* vm, MsObjClass* klass);

// 实例属性查找（06 §4）：字段 → MRO methods（物化 bound 出参）→ MRO statics。
// 命中写 *out（isMethod=true 表示 *out 为函数值需物化 bound）返回 true；未命中 false
bool msInstanceFindAttr(const MsObjInstance* inst, const MsObjString* name, MsValue* out, bool* isMethod);

// 类属性查找：MRO 各级 methods（isMethod=true）→ statics（isMethod=false）；未命中 false
bool msClassFindAttr(const MsObjClass* klass, const MsObjString* name, MsValue* out, bool* isMethod);

// super 查找：start 之后（不含）首个 methods/statics 命中类（06 §2；start 为定义类）
bool msClassFindAttrAfter(const MsObjClass* start, const MsObjString* name, MsValue* out, bool* isMethod);

#endif  // INCLUDE_MS_VM_CLASS_OPS_H_
```

创建 `src/vm/class_ops.c`：

```c
// 类运行时操作实现（T020）：MRO（拼接去重 + 子序列验证）与查找序（06 §4）
#include "vm/class_ops.h"

#include <string.h>

#include "obj/obj_string.h"

static MsObjList* listWith(MsVM* vm, MsObjClass* head, const MsObjList* tailFrom, int32_t tailSkip) {
  MsObjList* l = msObjListNew(vm);
  if (l == NULL) {
    return NULL;
  }
  if (!msObjListPush(vm, l, msValueObj(&head->head))) {
    return NULL;
  }
  if (tailFrom != NULL) {
    for (int32_t i = tailSkip; i < tailFrom->count; i++) {
      if (!msObjListPush(vm, l, tailFrom->items[i])) {
        return NULL;
      }
    }
  }
  return l;
}

bool msClassBuildMro(MsVM* vm, MsObjClass* klass) {
  if (klass->bases->count == 0) {  // 无基类：mro = [self]
    klass->mro = listWith(vm, klass, NULL, 0);
    if (klass->mro == NULL) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
      vm->hasError = true;
    }
    return klass->mro != NULL;
  }
  // bases 类型校验（编译产物为 LOAD_GLOBAL 类名——非类值在此暴露）
  for (int32_t i = 0; i < klass->bases->count; i++) {
    if (!msIsClass(&klass->bases->items[i])) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: base class %d of '%s' is not a class", (int)i,
               klass->name->bytes);
      vm->hasError = true;
      return false;
    }
  }
  // bases 清单内部重复 → 冲突
  for (int32_t i = 0; i < klass->bases->count; i++) {
    for (int32_t j = i + 1; j < klass->bases->count; j++) {
      if (klass->bases->items[i].as.obj == klass->bases->items[j].as.obj) {
        snprintf(vm->errMsg, sizeof(vm->errMsg),
                 "SyntaxError: duplicate base class in '%s'", klass->name->bytes);
        vm->hasError = true;
        return false;
      }
    }
  }
  MsObjClass* base = (MsObjClass*)klass->bases->items[0].as.obj;
  // seq = [本类] + mixins 逆序 + 基类 MRO
  MsObjList* seq = listWith(vm, klass, NULL, 0);
  if (seq == NULL) {
    goto oom;
  }
  for (int32_t i = klass->bases->count - 1; i >= 1; i--) {  // mixins 逆序
    if (!msObjListPush(vm, seq, klass->bases->items[i])) {
      goto oom;
    }
  }
  for (int32_t i = 0; i < base->mro->count; i++) {
    if (!msObjListPush(vm, seq, base->mro->items[i])) {
      goto oom;
    }
  }
  // 去重保首次
  MsObjList* mro = msObjListNew(vm);
  if (mro == NULL) {
    goto oom;
  }
  for (int32_t i = 0; i < seq->count; i++) {
    bool dup = false;
    for (int32_t j = 0; j < mro->count; j++) {
      if (mro->items[j].as.obj == seq->items[i].as.obj) {
        dup = true;
        break;
      }
    }
    if (!dup && !msObjListPush(vm, mro, seq->items[i])) {
      goto oom;
    }
  }
  // 逐 base 验证：其 mro 须为结果子序列（保序）——顺序矛盾即 C3 冲突
  for (int32_t b = 0; b < klass->bases->count; b++) {
    MsObjClass* bc = (MsObjClass*)klass->bases->items[b].as.obj;
    int32_t at = 0;
    for (int32_t i = 0; i < bc->mro->count; i++) {
      while (at < mro->count && mro->items[at].as.obj != bc->mro->items[i].as.obj) {
        at++;
      }
      if (at >= mro->count) {
        snprintf(vm->errMsg, sizeof(vm->errMsg),
                 "SyntaxError: cannot create a consistent MRO for class '%s'", klass->name->bytes);
        vm->hasError = true;
        return false;
      }
      at++;
    }
  }
  klass->mro = mro;
  return true;
oom:
  snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
  vm->hasError = true;
  return false;
}

static bool mapGet(const MsObjMap* m, const MsObjString* name, MsValue* out) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapGet(m, &key, out) == MS_MAP_OK;
}

bool msInstanceFindAttr(const MsObjInstance* inst, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (msShapeFieldGet(inst, name, out)) {  // shape.h
    *isMethod = false;
    return true;
  }
  return msClassFindAttr(inst->klass, name, out, isMethod);
}

bool msClassFindAttr(const MsObjClass* klass, const MsObjString* name, MsValue* out, bool* isMethod) {
  const MsObjList* mro = klass->mro != NULL ? klass->mro : NULL;
  if (mro == NULL) {
    return false;  // 防御：INHERIT_INIT 前不可达（编译器保证）
  }
  for (int32_t i = 0; i < mro->count; i++) {
    MsObjClass* c = (MsObjClass*)mro->items[i].as.obj;
    if (mapGet(c->methods, name, out)) {
      *isMethod = true;
      return true;
    }
    if (mapGet(c->statics, name, out)) {
      *isMethod = false;
      return true;
    }
  }
  return false;
}

bool msClassFindAttrAfter(const MsObjClass* start, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (start->mro == NULL) {
    return false;
  }
  int32_t idx = -1;
  for (int32_t i = 0; i < start->mro->count; i++) {
    if (start->mro->items[i].as.obj == (const MsObj*)start) {
      idx = i;
      break;
    }
  }
  for (int32_t i = idx + 1; i < start->mro->count; i++) {
    MsObjClass* c = (MsObjClass*)start->mro->items[i].as.obj;
    if (mapGet(c->methods, name, out)) {
      *isMethod = true;
      return true;
    }
    if (mapGet(c->statics, name, out)) {
      *isMethod = false;
      return true;
    }
  }
  return false;
}
```

vm.c（include 区加 `#include "vm/class_ops.h"`）：

1. SET_HANDLER 区（GET_ATTR 行后）加：

```c
  SET_HANDLER(OP_NEW_CLASS);
  SET_HANDLER(OP_DEFINE_METHOD);
  SET_HANDLER(OP_INHERIT_INIT);
  SET_HANDLER(OP_SET_ATTR);
```

（OP_GET_SUPER 归 Task 5。）

2. dispatch 体加三指令（OP_GET_ATTR handler 前后均可；新增代码块放 OP_CALL_METHOD 前）：

```c
      OPCODE(OP_NEW_CLASS) {  // <const name> <u8 baseCount>：栈 [b1..bn] → [class]
        READ_OPERAND(k);
        int32_t n = code[ip++];
        MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
        if (name == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsObjClass* klass = msObjClassNew(vm, name);
        if (klass == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        vm->stack[vm->stackTop - n] = msValueObj(&klass->head);  // 类即刻 root（bases 仍在栈）
        for (int32_t i = 0; i < n; i++) {
          if (!msObjListPush(vm, klass->bases, vm->stack[vm->stackTop - n + 1 + i])) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
        }
        vm->stackTop -= n;  // 弹 bases（类已在栈顶）
        NEXT();
      }

      OPCODE(OP_DEFINE_METHOD) {  // <const name> <u8 flags>：栈 [class, value] → [class]
        READ_OPERAND(k);
        ms_u8 flags = code[ip++];
        MsObjClass* klass = (MsObjClass*)vm->stack[vm->stackTop - 2].as.obj;
        MsValue value = vm->stack[vm->stackTop - 1];
        MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
        if (name == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsValue key = msValueObj(&name->head);
        MsObjMap* table = klass->methods;
        if ((flags & MS_DEFM_STATIC) != 0) {
          table = klass->statics;
        } else if ((flags & MS_DEFM_FIELD) != 0) {
          table = NULL;  // 字段登记不走统一 put（见下）
        }
        bool ok;
        if (table != NULL) {
          ok = msObjMapPut(vm, table, &key, &value) == MS_MAP_OK;
        } else {
          ok = msObjListPush(vm, klass->fieldNames, key);
          if (ok && (flags & MS_DEFM_FIELD_DEF) != 0) {
            ok = msObjMapPut(vm, klass->fieldDefaults, &key, &value) == MS_MAP_OK;
          }
        }
        if (!ok) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        if (table == klass->methods && msIsCallableObj(&value)) {  // 普通方法回填 owner（super）
          msCallableFn(&value)->owner = klass;
        }
        vm->stackTop--;  // 弹 value（class 留栈顶）
        NEXT();
      }

      OPCODE(OP_INHERIT_INIT) {  // 栈 [class] → [class]：MRO 构建（冲突 = SyntaxError 等价）
        MsObjClass* klass = (MsObjClass*)vm->stack[vm->stackTop - 1].as.obj;
        if (!msClassBuildMro(vm, klass)) {
          vm->errLine = chunkLineAt(chunk, instrOff);
          goto dispatchExit;  // 错误槽已置（SyntaxError/TypeError/OOM）
        }
        NEXT();
      }
```

3. value.c StrForm：`strFormDepth` 的 OBJ 分支（MS_OBJ_SET 后）加：

```c
    if (v->as.obj->type == MS_OBJ_CLASS) {  // T020：类形态 <class Name>
      const MsObjClass* c = (const MsObjClass*)v->as.obj;
      char buf[96];
      int len = snprintf(buf, sizeof(buf), "<class %.*s>", (int)c->name->byteLen, c->name->bytes);
      return msObjStringNew(vm, buf, len);
    }
    if (v->as.obj->type == MS_OBJ_INSTANCE) {  // __str__/__repr__ 归 T021
      const MsObjInstance* i = (const MsObjInstance*)v->as.obj;
      char buf[96];
      int len = snprintf(buf, sizeof(buf), "<%.*s instance>", (int)i->klass->name->byteLen,
                         i->klass->name->bytes);
      return msObjStringNew(vm, buf, len);
    }
```

（reprFormDepth 同款两分支——instance 的 repr 先与 str 同形态，T021 分化。）

CMakeLists.txt（generator.c 行后）：

```cmake
# T020：类运行时操作（MRO/查找/实例化）
target_sources(ms_vm PRIVATE src/vm/class_ops.c)
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure; ctest --test-dir build -C Debug`
Expected: class 9 用例全过；全量 156 绿。

- [ ] **Step 5: Commit**

```bash
git add src/vm/class_ops.h src/vm/class_ops.c src/vm/vm.c src/vm/value.c CMakeLists.txt tests/unit/test_class.c
git commit -m "✨ feat(vm): run NEW_CLASS/DEFINE_METHOD/INHERIT_INIT with MRO linearization (T020)"
```

---

### Task 5: VM——实例化 + this 调用 + GET_ATTR/SET_ATTR（Animal 全用例）

**Files:**
- Modify: `src/vm/vm.c`（OP_CALL CLASS 分支 + OP_CALL_METHOD INSTANCE 分支 + OP_GET_ATTR class 路径 + OP_SET_ATTR）
- Test: `tests/unit/test_class.c` 追加

- [ ] **Step 1: 写失败测试（06 §1 Animal 原例 + 动态字段 + 遮蔽）**

test_class.c 追加：

```c
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
      "print(d.describe != nil, d.age)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true 1\n", r.out);
  free(r.out);
}

TEST(fieldDefaultsCopied) {  // 声明字段默认：无 init 置 nil、有 init 拷贝；__init__ 可覆盖
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

TEST(attrMissingError) {  // 未命中 AttributeError
  RunResult r = runSrc("class C {\n}\nvar c = C()\nprint(c.nope)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "AttributeError") != NULL);
  free(r.out);
}

TEST(thisCapturedInClosure) {  // 方法内闭包捕获 this（upvalue 路径）
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
```

- [ ] **Step 2: 跑测试确认失败**

Run: `ctest --test-dir build -C Debug -R "^class$" --output-on-failure`（先 build test_class）
Expected: 新 8 用例全 FAIL（TypeError: 'class' is not callable 等）。

- [ ] **Step 3: 实现 VM 路径**

vm.c 三处改动：

1. **OP_CALL 的 CLASS 分支**（插在 `MS_VAL_BOUND_METHOD` 分派之前）：

```c
        if (callee.kind == MS_VAL_OBJ && callee.as.obj->type == MS_OBJ_CLASS) {  // T020：实例化
          MsObjClass* klass = (MsObjClass*)callee.as.obj;
          MsObjInstance* inst = msObjInstanceNew(vm, klass);
          if (inst == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - argc - 1] = msValueObj(&inst->head);  // callee 槽 ← 实例（args 仍 root）
          for (int32_t i = 0; i < klass->fieldNames->count; i++) {  // 声明字段置默认（nil/默认值）
            MsValue dflt = msValueNil();
            MsObjString* fname = (MsObjString*)klass->fieldNames->items[i].as.obj;
            MsValue key = msValueObj(&fname->head);
            (void)msObjMapGet(klass->fieldDefaults, &key, &dflt);  // 未命中保持 nil
            if (!msShapeFieldSet(vm, inst, fname, &dflt)) {
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
          }
          MsValue initFn;
          bool isMethod;
          if (msClassFindAttr(klass, (MsObjString*)msInternBytes(vm, "__init__", 8), &initFn, &isMethod) &&
              isMethod) {
            if (vm->callDepth >= MS_VM_MAX_CALL_DEPTH) {
              RUNTIME_ERROR("StackOverflowError: call nesting limit exceeded (%d)", MS_VM_MAX_CALL_DEPTH);
            }
            // 栈形 [instance, a1..an] → [fn, instance, a1..an]，同步执行（返回值忽略）
            if (!msVmPush(vm, msValueNil())) {  // 上移占位
              goto dispatchExit;
            }
            MsValue* slot0 = &vm->stack[vm->stackTop - argc - 2];
            memmove(slot0 + 1, slot0, sizeof(MsValue) * (size_t)(argc + 1));
            *slot0 = initFn;
            frame->ip = ip;
            vm->callDepth++;
            if (!msVmSetupCall(vm, argc + 1)) {
              vm->callDepth--;
              vm->errLine = chunkLineAt(chunk, instrOff);
              goto dispatchExit;
            }
            msVmRunFrames(vm);  // 嵌套执行至 __init__ RETURN（entry 收口；返回值在栈顶）
            vm->callDepth--;
            if (vm->hasError) {
              goto dispatchExit;
            }
            vm->stackTop--;  // 弹 __init__ 返回值（忽略；栈顶即 instance）
            frame = &vm->frames[vm->frameCount - 1];
            chunk = frame->chunk;
            code = chunk->code;
            consts = chunk->consts;
            ip = frame->ip;
            NEXT();
          }
          if (argc != 0) {  // 无 __init__ 但传参
            RUNTIME_ERROR("ArgumentError: %.*s() expects no arguments, got %d", (int)klass->name->byteLen,
                          klass->name->bytes, argc);
          }
          vm->stackTop -= argc;  // 弹 args（instance 即结果）
          NEXT();
        }
```

2. **OP_CALL_METHOD 的 class 路径**（methodRefLookup 之前插）：

```c
        if (args[0].kind == MS_VAL_OBJ && args[0].as.obj->type == MS_OBJ_INSTANCE) {  // T020
          MsObjInstance* inst = (MsObjInstance*)args[0].as.obj;
          MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
          if (name == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          MsValue found;
          bool isMethod;
          if (msInstanceFindAttr(inst, name, &found, &isMethod) && isMethod && msIsCallableObj(&found)) {
            if (!msVmPush(vm, msValueNil())) {  // 上移占位
              goto dispatchExit;
            }
            MsValue* slot0 = &vm->stack[vm->stackTop - argc - 2];
            memmove(slot0 + 1, slot0, sizeof(MsValue) * (size_t)(argc + 1));
            *slot0 = found;  // [fn, recv, a1..an]
            frame->ip = ip;
            if (!msVmSetupCall(vm, argc + 1)) {
              vm->errLine = chunkLineAt(chunk, instrOff);
              goto dispatchExit;
            }
            frame = &vm->frames[vm->frameCount - 1];
            chunk = frame->chunk;
            code = chunk->code;
            consts = chunk->consts;
            ip = frame->ip;
            NEXT();
          }
          if (!msInstanceFindAttr(inst, name, &found, &isMethod)) {  // 属性未命中
            RUNTIME_ERROR("AttributeError: '%s' has no attribute '%.*s'",
                          inst->klass->name->bytes, (int)consts[k].as.text.len, consts[k].as.text.start);
          }
          // 字段/类属性值（可调用则调用；不可调用走通用 TypeError）
          if (!msIsCallableObj(&found) && found.kind != MS_VAL_NATIVE && found.kind != MS_VAL_FUNCTION) {
            RUNTIME_ERROR("TypeError: '%s' attribute '%.*s' is not callable", inst->klass->name->bytes,
                          (int)consts[k].as.text.len, consts[k].as.text.start);
          }
          vm->stack[vm->stackTop - argc - 1] = found;  // recv 槽 ← 值，args 原位
          int32_t plainArgc = argc;
          if (!msVmSetupCall(vm, plainArgc)) {
            vm->errLine = chunkLineAt(chunk, instrOff);
            goto dispatchExit;
          }
          frame = &vm->frames[vm->frameCount - 1];
          chunk = frame->chunk;
          code = chunk->code;
          consts = chunk->consts;
          ip = frame->ip;
          NEXT();
        }
```

3. **OP_GET_ATTR 前插 class 路径**（现有函数内省/methodRefLookup 之前；接收者已在栈顶 root）：

```c
        if (recv.kind == MS_VAL_OBJ && recv.as.obj->type == MS_OBJ_INSTANCE) {  // T020：实例属性
          MsObjInstance* inst = (MsObjInstance*)recv.as.obj;
          MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
          if (name == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          MsValue found;
          bool isMethod;
          if (msInstanceFindAttr(inst, name, &found, &isMethod)) {
            if (!isMethod) {
              vm->stack[vm->stackTop - 1] = found;
              NEXT();
            }
            msVmPush(vm, found);  // fn 落栈 root（物化 bound 分配窗口）
            if (vm->hasError) {
              goto dispatchExit;
            }
            MsObjBoundMethod* bound = msObjBoundNew(vm, recv, found);
            if (bound == NULL) {
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            vm->stack[vm->stackTop - 2] = msValueObj(&bound->head);
            vm->stackTop--;
            NEXT();
          }
          RUNTIME_ERROR("AttributeError: '%s' has no attribute '%.*s'", inst->klass->name->bytes,
                        (int)consts[k].as.text.len, consts[k].as.text.start);
        }
        if (recv.kind == MS_VAL_OBJ && recv.as.obj->type == MS_OBJ_CLASS) {  // T020：类属性/未绑定方法
          MsObjClass* klass = (MsObjClass*)recv.as.obj;
          MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
          if (name == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          MsValue found;
          bool isMethod;
          if (msClassFindAttr(klass, name, &found, &isMethod)) {  // 未绑定函数原值（显式传递用）
            vm->stack[vm->stackTop - 1] = found;
            NEXT();
          }
          RUNTIME_ERROR("AttributeError: class '%s' has no attribute '%.*s'", klass->name->bytes,
                        (int)consts[k].as.text.len, consts[k].as.text.start);
        }
```

4. **OP_SET_ATTR handler**（新增；SET_HANDLER 已在 Task 4 注册）：

```c
      OPCODE(OP_SET_ATTR) {  // <const name>：栈 [obj, value] → [value]
        READ_OPERAND(k);
        MsValue value = vm->stack[vm->stackTop - 1];
        if (vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_INSTANCE) {  // 实例字段写（首赋创建）
          MsObjInstance* inst = (MsObjInstance*)vm->stack[vm->stackTop - 2].as.obj;
          MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
          if (name == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          if (!msShapeFieldSet(vm, inst, name, &value)) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 2] = value;  // obj 槽 ← 值（弹 obj 保 SET 序）
          vm->stackTop--;
          NEXT();
        }
        if (vm->stack[vm->stackTop - 2].kind == MS_VAL_OBJ &&
            vm->stack[vm->stackTop - 2].as.obj->type == MS_OBJ_CLASS) {  // 类属性写（可新建）
          MsObjClass* klass = (MsObjClass*)vm->stack[vm->stackTop - 2].as.obj;
          MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
          if (name == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          MsValue key = msValueObj(&name->head);
          if (msObjMapPut(vm, klass->statics, &key, &value) != MS_MAP_OK) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 2] = value;
          vm->stackTop--;
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' does not support attribute assignment",
                      msValueTypeName(&vm->stack[vm->stackTop - 2]));
      }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure; ctest --test-dir build -C Debug`
Expected: class 17 用例全过；全量 156 绿。
（若 thisCapturedInClosure 失败：检查 binder recordCapture 的 "this" 名匹配与编译器 findUpvalByName("this")。）

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c tests/unit/test_class.c
git commit -m "✨ feat(vm): instantiate classes with this-dispatch and attr access (T020)"
```

---

### Task 6: 继承/MRO/super（Impl/mixin/C3 冲突）

**Files:**
- Modify: `src/compiler/compiler.c`（super 编码：AST_MEMBER/AST_CALL 特判 + FuncState.isMethod）
- Modify: `src/vm/vm.c`（OP_GET_SUPER handler + SET_HANDLER）
- Test: `tests/unit/test_class.c` 追加

- [ ] **Step 1: 写失败测试（06 §2 用例）**

test_class.c 追加：

```c
// ---------- 继承与 super（Task 6，06 §2）----------

TEST(inheritanceSuperCall) {  // 06 §2 Impl.call：super.call() 沿 MRO
  RunResult r = runSrc(
      "class Base {\n"
      "    func who() -> \"base\"\n"
      "    func call() -> \"Base.call\"\n"
      "}\n"
      "trait Logger {\n"
      "    func log(msg) { print(\"[log] ${msg}\") }\n"
      "}\n"
      "class Impl : Base, Logger {\n"
      "    func who() -> \"impl\"\n"
      "    func call() -> \"${super.call()} + Impl.call\"\n"
      "}\n"
      "var i = Impl()\n"
      "print(i.who())\n"
      "print(i.call())\n"
      "i.log(\"hi\")");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "impl\nBase.call + Impl.call\n[log] hi\n", r.out);
  free(r.out);
}

TEST(mixinReverseOrder) {  // 双 mixin 同名方法：后声明者近（MRO mixin 逆序）
  RunResult r = runSrc(
      "trait M1 {\n"
      "    func tag() -> \"m1\"\n"
      "}\n"
      "trait M2 {\n"
      "    func tag() -> \"m2\"\n"
      "}\n"
      "class C : Base2, M1, M2 {\n"
      "}\n"
      "class Base2 {\n"
      "}\n"
      "var c = C()\n"
      "print(c.tag())");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "m2\n", r.out);
  free(r.out);
}

TEST(methodInheritedFromBase) {  // 基类方法直接可用（this 绑定子类实例）
  RunResult r = runSrc(
      "class Base {\n"
      "    func hello() -> \"base-hello\"\n"
      "}\n"
      "class Sub : Base {\n"
      "}\n"
      "var s = Sub()\n"
      "print(s.hello())");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "base-hello\n", r.out);
  free(r.out);
}

TEST(superInitChain) {  // super.__init__(...) 显式基类构造（惯例首行）
  RunResult r = runSrc(
      "class Base {\n"
      "    var kind\n"
      "    func __init__() { this.kind = \"base\" }\n"
      "}\n"
      "class Sub : Base {\n"
      "    func __init__() {\n"
      "        super.__init__()\n"
      "        this.kind = \"${this.kind}+sub\"\n"
      "    }\n"
      "}\n"
      "var s = Sub()\n"
      "print(s.kind)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "base+sub\n", r.out);
  free(r.out);
}

TEST(mroConflictSyntaxError) {  // C3 冲突：X/Y 顺序矛盾 → SyntaxError
  RunResult r = runSrc(
      "class X {\n"
      "}\n"
      "class Y {\n"
      "}\n"
      "class A : X, Y {\n"
      "}\n"
      "class B : Y, X {\n"
      "}\n"
      "class C : A, B {\n"
      "}");
  EXPECT(ctx, r.compileOk);  // 前端/编译期通过（模块体执行才报）
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "SyntaxError") != NULL);
  free(r.out);
}

TEST(superOutsideMethodRejected) {  // 模块层 super.method → 编译期诊断
  RunResult r = runSrc("print(super.who())");
  EXPECT(ctx, !r.compileOk);
  free(r.out);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `ctest --test-dir build -C Debug -R "^class$" --output-on-failure`（先 build）
Expected: inheritance/super 系 FAIL（"undefined name: super" 或运行时错误）；mroConflict FAIL（无错误抛出）；methodInheritedFromBase 可能已过（MRO 无继承时仅在 Task 5 路径）——以实际为准记录。

- [ ] **Step 3: 实现 super 编码与 handler**

compiler.c：

1. FuncState 加 `bool isMethod;`（结构体末尾）；compileFuncProto 构造 inner FuncState 的初始化行加 `false`——即 `FuncState inner = {...现有字段..., .isMethod = isMethod};`（C11 指定初始化器追加；现有按序初始化则在末尾补 `isMethod` 值）。同时模块级 compilerCompile 的 FuncState 初始化补 `false`。

2. super 判定 helper（compileExpr 前）：

```c
// super 引用判定（T020，06 §2）：IDENT 名 "super" 且未解析为局部/上值（BIND_GLOBAL）
static bool isSuperRef(const AstNode* obj) {
  return obj->kind == AST_IDENT && obj->bindKind == AST_BIND_GLOBAL && obj->as.ident.nameLen == 5 &&
         memcmp(obj->as.ident.name, "super", 5) == 0;
}
```

3. compileExpr 的 `case AST_MEMBER:` 改为（super 分支前置）：

```c
    case AST_MEMBER: {
      const AstNode* obj = n->as.memberExpr.obj;
      if (isSuperRef(obj)) {  // T020：super.name → this + GET_SUPER（物化 bound/取值）
        if (!fs->isMethod) {
          compErr(fs, n, 5, "'super' is only allowed inside a method");
          emitOp(fs, OP_LOAD_NIL, n->line);
          break;
        }
        localOp(fs, OP_LOAD_LOCAL, 0, n->line);  // this（方法直层限定）
        int32_t k = nameConst(fs, n->as.memberExpr.name, n->as.memberExpr.nameLen);
        emitConstOp(fs, OP_GET_SUPER, k, n->line);
        emitByte(fs, 0);  // icSlot（T022）
        break;
      }
      compileExpr(fs, n->as.memberExpr.obj);
      int32_t k = nameConst(fs, n->as.memberExpr.name, n->as.memberExpr.nameLen);
      emitConstOp(fs, OP_GET_ATTR, k, n->line);
      emitByte(fs, 0);  // icSlot（T022）
      break;
    }
```

4. AST_CALL 的 MEMBER-callee 特判分支（发 CALL_METHOD 处，930-958 行区）前置 super 检查：callee 是 `super.name` 时改走通用调用路径——在 CALL_METHOD 特判条件中排除 super：

```c
      if (n->as.call.callee->kind == AST_MEMBER && !isSuperRef(n->as.call.callee->as.memberExpr.obj)) {
        ...现有 CALL_METHOD 发射不变...
      }
```

（super.name(args) 由 AST_MEMBER 编译为 GET_SUPER 物化 bound → 通用 OP_CALL 调用。）

5. AST_IDENT 编译防御（loadIdent 的 GLOBAL 路径前，compileExpr 的 AST_IDENT case 内）：

```c
    case AST_IDENT:
      if (isSuperRef(n)) {  // 裸 super 引用（非 MEMBER obj）→ 编译期拒绝
        compErr(fs, n, 5, "'super' is only allowed inside a method");
        emitOp(fs, OP_LOAD_NIL, n->line);
        break;
      }
      loadIdent(fs, n);
      break;
```

vm.c：

6. SET_HANDLER 加 `SET_HANDLER(OP_GET_SUPER);`；handler（OP_GET_ATTR 后）：

```c
      OPCODE(OP_GET_SUPER) {  // <const name> <u8 ic>：栈 [this] → [bound/值]（定义类 MRO 下一类起）
        READ_OPERAND(k);
        (void)code[ip++];
        MsValue recv = vm->stack[vm->stackTop - 1];
        MsObjClosure* cl = frame->closure;  // 方法帧恒闭包（编译器保证）
        if (cl == NULL || cl->fn->owner == NULL) {
          RUNTIME_ERROR("RuntimeError: 'super' outside method");
        }
        MsObjClass* owner = cl->fn->owner;
        MsObjString* name = msInternBytes(vm, consts[k].as.text.start, consts[k].as.text.len);
        if (name == NULL) {
          RUNTIME_ERROR("RuntimeError: out of memory");
        }
        MsValue found;
        bool isMethod;
        if (msClassFindAttrAfter(owner, name, &found, &isMethod)) {
          if (!isMethod) {
            vm->stack[vm->stackTop - 1] = found;
            NEXT();
          }
          msVmPush(vm, found);  // fn root（物化窗口）
          if (vm->hasError) {
            goto dispatchExit;
          }
          MsObjBoundMethod* bound = msObjBoundNew(vm, recv, found);
          if (bound == NULL) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          vm->stack[vm->stackTop - 2] = msValueObj(&bound->head);
          vm->stackTop--;
          NEXT();
        }
        RUNTIME_ERROR("AttributeError: 'super' has no attribute '%.*s'", (int)consts[k].as.text.len,
                      consts[k].as.text.start);
      }
```

7. ms.c runScript 的退出码路由（178 行区）：

```c
      if (vm.hasError) {
        fprintf(stderr, "%s:%d: %s\n", path, vm.errLine > 0 ? vm.errLine : 0, vm.errMsg);
        // T020：模块体执行期的编译期等价错误（MRO 冲突）按编译错退出码路由
        exitCode = strncmp(vm.errMsg, "SyntaxError:", 12) == 0 ? 3 : 1;
      }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure; ctest --test-dir build -C Debug`
Expected: class 23 用例全过；全量 156 绿。

- [ ] **Step 5: Commit**

```bash
git add src/compiler/compiler.c src/vm/vm.c tools/ms.c tests/unit/test_class.c
git commit -m "✨ feat(vm): inherit via C3-checked MRO with super dispatch (T020)"
```

---

### Task 7: static 类属性 + bound 提取/显式传递 + instanceof/type

**Files:**
- Modify: `src/vm/builtins.c`（instanceof 注册 + isinstance 分支 + type 分支）
- Test: `tests/unit/test_class.c` 追加

- [ ] **Step 1: 写失败测试（06 §3 §4 + instanceof）**

test_class.c 追加：

```c
// ---------- static/绑定方法/instanceof（Task 7，06 §3 §4）----------

TEST(staticCounterShared) {  // 06 §4 原例：static var 跨实例共享 + 类外读写
  RunResult r = runSrc(
      "class Counter {\n"
      "    static var total = 0\n"
      "    static func describe() -> \"counters, total=${Counter.total}\"\n"
      "    const VERSION = \"1.0\"\n"
      "}\n"
      "Counter.total += 1\n"
      "Counter.total += 1\n"
      "print(Counter.describe())\n"
      "print(Counter.VERSION)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "counters, total=2\n1.0\n", r.out);
  free(r.out);
}

TEST(staticNoThisButSubclassVisible) {  // static 方法无 this 直调 + 子类可见
  RunResult r = runSrc(
      "class B {\n"
      "    static func tag() -> \"b\"\n"
      "}\n"
      "class S : B {\n"
      "}\n"
      "print(B.tag())\n"
      "print(S.tag())");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "b\nb\n", r.out);
  free(r.out);
}

TEST(boundMethodExtraction) {  // var m = obj.method; m() 保持 this（06 §3）
  RunResult r = runSrc(
      "class Greeter {\n"
      "    var name\n"
      "    func __init__(name) { this.name = name }\n"
      "    func greet() -> \"hi ${this.name}\"\n"
      "}\n"
      "var g = Greeter(\"ms\")\n"
      "var m = g.greet\n"
      "print(m())\n"
      "print(callable(m))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "hi ms\ntrue\n", r.out);
  free(r.out);
}

TEST(explicitReceiverPassing) {  // Base.method(this, args) 显式传递等价（06 §3）
  RunResult r = runSrc(
      "class Base {\n"
      "    func twice(x) -> x * 2\n"
      "}\n"
      "class Sub : Base {\n"
      "    func twice(x) -> Base.twice(this, x) + 1\n"
      "}\n"
      "var s = Sub()\n"
      "print(s.twice(5))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "11\n", r.out);
  free(r.out);
}

TEST(instanceofAlongMro) {  // instanceof 沿 MRO：子类/基类/mixin 真、无关类假（06 §2）
  RunResult r = runSrc(
      "class Base {\n"
      "}\n"
      "trait Mix {\n"
      "}\n"
      "class Sub : Base, Mix {\n"
      "}\n"
      "class Other {\n"
      "}\n"
      "var s = Sub()\n"
      "print(instanceof(s, Sub), instanceof(s, Base), instanceof(s, Mix), instanceof(s, Other))\n"
      "print(isinstance(s, Sub), isinstance(s, \"int\"))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true true true false\ntrue false\n", r.out);
  free(r.out);
}

TEST(typeReturnsClassName) {  // type(instance) 返回类名（06 DoD）
  RunResult r = runSrc(
      "class Animal {\n"
      "}\n"
      "var a = Animal()\n"
      "print(type(a))\n"
      "print(str(a))");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "Animal\n<Animal instance>\n", r.out);
  free(r.out);
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `ctest --test-dir build -C Debug -R "^class$" --output-on-failure`（先 build）
Expected: staticCounterShared FAIL（Counter.total += 1 路径——复合赋值 GET_ATTR 类属性未实现为 class 路径已就绪应过；describe 的 static func 调用 Class.name() 走 OP_CALL CLASS 分支会当实例化——预期 FAIL）；instanceof FAIL（未定义名）；其余视现状。

- [ ] **Step 3: 实现内置与 static 调用路径**

1. vm.c **OP_CALL 无需 static 短路分支**——实现说明（无代码改动）：`Counter.describe()` 编译为 GET_ATTR（Counter.describe）→ OP_CALL。GET_ATTR CLASS 路径（Task 5 已实现）按 MRO 查 methods → statics，返回 statics 里的函数原值，随后 OP_CALL 走普通脚本函数调用 ✓。同理 `Base.twice(this, x)` 显式传递由 GET_ATTR CLASS 返回未绑定函数后常规调用 ✓。若 Step 1 中 staticCounterShared 失败于 `Counter.total += 1`（复合赋值），排查 GET_ATTR/SET_ATTR 的 CLASS 路径。

2. builtins.c：`nativeType`（164 行区）在取 typeName 前加类名分支：

```c
  if (msIsInstance(&a)) {  // T020：type(instance) 返回类名（06 DoD）
    const MsObjInstance* i = (const MsObjInstance*)a.as.obj;
    return msValueObj((MsObj*)i->klass->name);
  }
```

3. builtins.c：`typeNameMatch` 后加 instanceof 并改 nativeIsinstance。nativeIsinstance 的 `if (msIsString(t))` 前插：

```c
  if (t->kind == MS_VAL_OBJ && t->as.obj->type == MS_OBJ_CLASS) {  // T020：类对象沿 MRO
    if (args[0].kind != MS_VAL_OBJ || args[0].as.obj->type != MS_OBJ_INSTANCE) {
      return msValueBool(false);
    }
    const MsObjClass* k = ((const MsObjInstance*)args[0].as.obj)->klass;
    return msValueBool(msClassMroContains(k, (const MsObjClass*)t->as.obj));
  }
```

新内置（nativeCallable 旁）：

```c
// instanceof(x, T)：T 为类对象沿 MRO 判定（06 §2）；x 非实例 false；T 非类 TypeError
static MsValue nativeInstanceof(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    bErr(vm, "ArgumentError: instanceof expects 2 arguments");
    return msValueNil();
  }
  const MsValue* t = &args[1];
  if (t->kind != MS_VAL_OBJ || t->as.obj->type != MS_OBJ_CLASS) {
    bErr(vm, "TypeError: instanceof expects a class");
    return msValueNil();
  }
  if (args[0].kind != MS_VAL_OBJ || args[0].as.obj->type != MS_OBJ_INSTANCE) {
    return msValueBool(false);
  }
  const MsObjClass* k = ((const MsObjInstance*)args[0].as.obj)->klass;
  return msValueBool(msClassMroContains(k, (const MsObjClass*)t->as.obj));
}
```

注册表（`{"isinstance", nativeIsinstance}` 行区）加 `{"instanceof", nativeInstanceof},`。builtins.c include 区加 `#include "obj/obj_class.h"` 与 `#include "vm/class_ops.h"`。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; ctest --test-dir build -C Debug -R "^class$" --output-on-failure; ctest --test-dir build -C Debug`
Expected: class 29 用例全过；全量 156 绿。
排查提示：staticCounterShared 若失败于 `Counter.total += 1`——GET_ATTR CLASS 已返回 statics 值、SET_ATTR CLASS 已写 ✓；若失败于 `Counter.VERSION`（const 入 statics）查 DEFINE_METHOD flags 组合。

- [ ] **Step 5: Commit**

```bash
git add src/vm/builtins.c tests/unit/test_class.c
git commit -m "✨ feat(vm): add class semantics for isinstance/instanceof/type and static access (T020)"
```

---

### Task 8: GC 压力 + fixtures + 文档状态收尾

**Files:**
- Test: `tests/unit/test_class.c` 追加 GC 压力用例
- Create: `tests/fixtures/class/animal.ms` + `.out`、`mixin.ms` + `.out`、`static_counter.ms` + `.out`、`mro_conflict.ms` + `.code` + `.err` + `.out`、`bound_method.ms` + `.out`
- Modify: `docs/tasks/T020-classes-inheritance.md`（状态 ✅ + DoD 勾选）、`docs/tasks/README.md`（总表 T020 状态）

- [ ] **Step 1: 写 GC 压力失败测试**

test_class.c 追加：

```c
// ---------- GC 与收尾（Task 8）----------

TEST(classGcUnderPressure) {  // 低阈值下大量实例/类/绑定方法创建：可达者存活
  RunResult r = runSrcHeapCap(
      "class P {\n"
      "    var x, y\n"
      "    func __init__(a) { this.x = a; this.y = a * 2 }\n"
      "    func sum() -> this.x + this.y\n"
      "}\n"
      "var keep = P(1)\n"
      "var km = keep.sum\n"
      "var junk = nil\n"
      "for i in range(3000) { junk = P(i) }\n"
      "print(km(), keep.x, junk.sum())",
      "262144");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "3 1 8997\n", r.out);
  free(r.out);
}
```

- [ ] **Step 2: 跑测试确认状态**

Run: `cmake --build build --config Debug --target test_class; ctest --test-dir build -C Debug -R "^class$" --output-on-failure`
Expected: PASS（GC 标记已完备时直接绿；若 FAIL 按泄漏点修 mark 面——典型：boundMark 已泛化、CLASS mro NULL 检查）。若直接 PASS，记录后进入 Step 3（该用例作为回归钉保留）。

- [ ] **Step 3: 落盘 fixtures**

`tests/fixtures/class/animal.ms`：

```ms
class Animal {
    var name, age

    func __init__(name, age = 1) {
        this.name = name
        this.age = age
    }

    func speak() -> "..."
    func describe() -> "${this.name} (${this.age})"
}

var cat = Animal("Cat", 3)
var dog = Animal("Dog")
print(cat.describe())
print(dog.describe())
print(cat.speak())
```

`tests/fixtures/class/animal.out`：

```text
Cat (3)
Dog (1)
...
```

`tests/fixtures/class/mixin.ms`：

```ms
class Base {
    func who() -> "base"
    func call() -> "Base.call"
}

trait Logger {
    func log(msg) { print("[log] ${msg}") }
}

class Impl : Base, Logger {
    func who() -> "impl"
    func call() -> "${super.call()} + Impl.call"
}

var i = Impl()
print(i.who())
print(i.call())
i.log("mixin attached")
```

`tests/fixtures/class/mixin.out`：

```text
impl
Base.call + Impl.call
[log] mixin attached
```

`tests/fixtures/class/static_counter.ms`：

```ms
class Counter {
    static var total = 0
    static func describe() -> "counters, total=${Counter.total}"
    const VERSION = "1.0"
}

class NamedCounter : Counter {
}

Counter.total += 1
Counter.total += 2
print(Counter.describe())
print(Counter.VERSION)
print(NamedCounter.describe())
```

`tests/fixtures/class/static_counter.out`：

```text
counters, total=3
1.0
counters, total=3
```

`tests/fixtures/class/mro_conflict.ms`：

```ms
class X {
}

class Y {
}

class A : X, Y {
}

class B : Y, X {
}

class C : A, B {
}
```

`tests/fixtures/class/mro_conflict.out`（空文件：无 stdout）。

`tests/fixtures/class/mro_conflict.code`：

```text
3
```

`tests/fixtures/class/mro_conflict.err`：

```text
SyntaxError: cannot create a consistent MRO for class 'C'
```

`tests/fixtures/class/bound_method.ms`：

```ms
class Greeter {
    var name

    func __init__(name) {
        this.name = name
    }

    func greet() -> "hi ${this.name}"
    func viaBound() -> this.greet()
}

var g = Greeter("ms")
var m = g.greet
print(m())
print(g.viaBound())
print(Base == nil)
```

注意：`Base == nil` 引用未定义名会运行时错——删除该行。最终 `bound_method.ms`：

```ms
class Greeter {
    var name

    func __init__(name) {
        this.name = name
    }

    func greet() -> "hi ${this.name}"
    func viaBound() -> this.greet()
}

var g = Greeter("ms")
var m = g.greet
print(m())
print(g.viaBound())
```

`tests/fixtures/class/bound_method.out`：

```text
hi ms
hi ms
```

- [ ] **Step 4: 全量验证（含新 fixtures 自动注册）**

Run: `cmake --build build --config Debug 2>&1 | Select-Object -Last 3; cmake -S . -B build 2>&1 | Select-Object -Last 1; ctest --test-dir build -C Debug`
Expected: 161 unit/fixture 项全绿（156 + class 单测 + 5 fixtures——数量以 `ctest -N` 实际为准全过）。
mro_conflict 验证点：退出码 3 + stderr 含 SyntaxError（.code/.err 由 runner 比对）。

- [ ] **Step 5: 更新任务文档状态并提交**

`docs/tasks/T020-classes-inheritance.md`：头部 `- 状态: 未开始` 改 `✅ 已完成（2026-11-07）`；§4 DoD 五项全部勾选 `[x]`。
`docs/tasks/README.md` 总表 T020 行 `⬜` 改 `✅`。

```bash
git add tests/unit/test_class.c tests/fixtures/class docs/tasks/T020-classes-inheritance.md docs/tasks/README.md
git commit -m "🧪 test(fixtures): add class conformance fixtures and mark T020 done"
```

---

## 完成后验证（Definition of Done）

1. `ctest --test-dir build -C Debug` 全绿（基线 155 + 新增单测/fixture）。
2. 06 §1-§4 四个 fixture（animal/mixin/static_counter/bound_method）golden 输出正确。
3. mro_conflict 退出码 3 + SyntaxError。
4. GC 压力用例（低阈值 3000 实例）通过。
5. `git log --oneline` 含 8 个任务提交（T020 系列）。
6. AGENTS.md 规约满足：UTF-8/LF/无尾随空白、构建唯一 `build/`、单 `ms` 目标、gitmoji 提交。

## 备注（执行者须知）

- Windows/MSVC 本机无 ASan preset（asan 为 GCC/Clang 条件配置）；GC 正确性以 classGcUnderPressure + classGcMarkChildren 双钉守护，ASan 门禁留 CI。
- `chunk.c` 的 msOpNames/msOpLayouts 表与 opcodes.h 枚举序强耦合：改 opcodes.h 后必须同步两表（Task 3 Step 3.2），否则 disasm/--trace 越界。
- vm.c dispatch 宏区 SET_HANDLER 与 OPCODE 体是两处注册点，新增 handler 两处都要加。
- 所有 memmove 上移技法要求先 `msVmPush(vm, msValueNil())` 占位（扩容窗口内无裸栈指针），再移动——顺序颠倒会产生悬垂指针。
