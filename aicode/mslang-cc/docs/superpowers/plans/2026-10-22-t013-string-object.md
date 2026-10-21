# T013 string 对象：UTF-8 与方法族 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 MsObjString（UTF-8 不可变、FNV-1a 32/64、ASCII 快路径、惰性 runeIndex、intern 表）、`s[i]`/切片/拼接/重复、10 §2 全部 string 方法（实例方法表机制）、GET/SET_INDEX 与 FORMAT_STRING 插值，并完成 MS_VAL_STRING → MS_VAL_OBJ 值迁移。

**Architecture:** 三层推进——先纯数据层（obj_string + intern + 最小 list），再值迁移（vm.h/value.c/vm.c/bridge/常量池缓存与 CONST_RAWSTRING/CONST_INTERP），最后指令与方法层（编译器新路径 + VM 新 opcode + str_methods 方法族）。语义决定见 `docs/superpowers/specs/2026-10-22-t013-string-object-design.md`（D1-D10）。

**Tech Stack:** C11（MSVC /W4 /WX + GCC/Clang -Wall -Wextra -Werror -Wshadow）、CMake ≥3.16、ctest、clang ASan（preset asan）。

**分支:** `t013-string-object`（worktree `.worktrees/t013-string-object`）。构建：`cmake --build build`；测试：`ctest --test-dir build`。

**关键语义决定（设计文档 D1-D10 摘要）:**

- runeIndex 在创建时预留尾空间、惰性填充、随对象单块 free（无 GC 析构钩子）。
- MS_VAL_STRING 退役；string 相等 = hash/len 短路 + memcmp（值相等）；排序 memcmp。
- CONST_STRING（转义原文，运行时解码+intern+VM 缓存）/ CONST_RAWSTRING（原始串不解码）/ CONST_INTERP（字面段编译期预解码）三分。
- 方法机制：MsObjNative + MsObjBoundMethod + string 方法表；CALL_METHOD 零分配直调（args[0]=接收者），GET_ATTR 物化 bound 对象。
- split/join 依赖最小 MsObjList 桥（BUILD_LIST + GET_INDEX + str/repr；其余归 T014）。
- MS_VAL_SLICE 过渡值（i64 三元组 + mask，栈上瞬态）；msSliceRange 规范算法供 T014 复用。
- UTF-8/转义解码入 ms_common；unicode_case.h 由 gen_unicode_table.py 扩展生成。

**文件结构:**

- 修改 `src/common/common.h`、`src/common/common.c` — UTF-8 decode/encode + 转义解码（D9）
- 新增 `src/obj/obj_string.h`、`src/obj/obj_string.c` — MsObjString 数据层 + rune 索引/切片 + str 迭代器
- 新增 `src/obj/intern.c`（声明入 obj_string.h）— intern 表
- 新增 `src/obj/obj_list.h`、`src/obj/obj_list.c` — 最小 list 桥（D6）
- 新增 `src/obj/unicode_case.h`（生成物入库）— 简单大小写映射
- 修改 `src/obj/obj.h`、`src/obj/obj.c` — MsObjNative/MsObjBoundMethod + 工厂
- 修改 `src/vm/vm.h` — kind 迁移 + MS_VAL_SLICE + 新声明
- 修改 `src/vm/value.c` — equal/order/typeName/FromConst/StrForm/ReprForm
- 修改 `src/vm/vm.c` — intern 接线、markFn 注册、新 opcode 执行、arithBinary 拼接/重复
- 修改 `src/vm/builtins_bridge.c` — print obj 路径 + len/str/repr
- 新增 `src/vm/str_methods.c` — 方法表 + 29 方法
- 修改 `src/compiler/chunk.h`、`src/compiler/chunk.c` — ConstEntry 缓存字段、CONST_RAWSTRING/CONST_INTERP、chunkAddConstInterp、chunkFree
- 修改 `src/compiler/compiler.c` — AST_STRING/INDEX/SLICE/MEMBER/CALL 编译、index 赋值
- 修改 `src/compiler/disasm.c` — 新常量 kind 渲染
- 修改 `tools/gen_unicode_table.py` — 大小写表生成
- 修改 `CMakeLists.txt`、`tests/CMakeLists.txt` — 源文件与测试注册
- 修改 `tests/unit/test_common.c`、`tests/unit/test_vm_basic.c` — 迁移适配
- 新增 `tests/unit/test_obj_string.c` — 任务书 §5.1 全表
- 新增 `tests/fixtures/string/{index_slice,methods,interp,type_err}.*`
- 修改 `docs/tasks/T013-string-object.md`、`docs/tasks/README.md` — 状态

---

### Task 1: common — UTF-8 与转义解码

**Files:**
- Modify: `src/common/common.h`
- Modify: `src/common/common.c`
- Test: `tests/unit/test_common.c`

- [ ] **Step 1.1: 写失败测试** — `tests/unit/test_common.c` 追加（include 区已有 test_harness.h；若无则补）：

```c
TEST(utf8RoundTrip) {  // T013：编码后解码还原码点与宽度
  char buf[4];
  struct { uint32_t cp; int32_t width; } cases[] = {
      {'a', 1}, {0xE9, 2}, {0x4E2D, 3}, {0x1F600, 4}, {0xFFFD, 3},
  };
  for (int i = 0; i < 5; i++) {
    int32_t w = msUtf8Encode(cases[i].cp, buf);
    EXPECT_EQ(ctx, cases[i].width, w);
    int32_t dw = 0;
    EXPECT_EQ(ctx, cases[i].cp, msUtf8Decode(buf, w, &dw));
    EXPECT_EQ(ctx, cases[i].width, dw);
  }
  EXPECT_EQ(ctx, 3, msUtf8Encode(0x7FFFFFFF, buf));  // 越界码点 → U+FFFD（3 字节）
}

TEST(utf8DecodeInvalid) {  // 截断/坏续字节/代理区/超界 → U+FFFD 宽 1（与 scanner 同规则）
  int32_t w = 0;
  EXPECT_EQ(ctx, 0xFFFD, msUtf8Decode("\xE4\xB8", 2, &w));
  EXPECT_EQ(ctx, 1, w);
  EXPECT_EQ(ctx, 0xFFFD, msUtf8Decode("\xED\xA0\x80", 3, &w));  // 代理区
  EXPECT_EQ(ctx, 1, w);
  EXPECT_EQ(ctx, 0xFFFD, msUtf8Decode("\xC0\x80", 2, &w));  // 过长编码
  EXPECT_EQ(ctx, 1, w);
}

TEST(escapeDecode) {  // 01 §3.3 全转义集
  char dst[64];
  EXPECT_EQ(ctx, 5, msTextDecodeEscapes("a\\nb\\tc", 7, dst, sizeof(dst)));
  EXPECT_STREQ(ctx, "a\nb\tc", dst);
  EXPECT_EQ(ctx, 1, msTextDecodeEscapes("\\x41", 4, dst, sizeof(dst)));
  EXPECT_STREQ(ctx, "A", dst);
  EXPECT_EQ(ctx, 3, msTextDecodeEscapes("\\u{4E2D}", 8, dst, sizeof(dst)));
  EXPECT_EQ(ctx, 0, memcmp("\xE4\xB8\xAD", dst, 3));
  EXPECT_EQ(ctx, 3, msTextDecodeEscapes("a\\'b\\\"c\\\\d", 10, dst, sizeof(dst)));
  EXPECT_STREQ(ctx, "a'b\"c\\d", dst);
  EXPECT_EQ(ctx, 1, msTextDecodeEscapes("\\0", 2, dst, sizeof(dst)));
  EXPECT_EQ(ctx, 0, dst[0]);
  EXPECT_EQ(ctx, 1, msTextDecodeEscapes("\\r", 2, dst, sizeof(dst)));
  EXPECT_EQ(ctx, '\r', dst[0]);
}
```

main() tests 数组末尾追加 `utf8RoundTrip, utf8DecodeInvalid, escapeDecode,`；文件头补 `#include <stdint.h>`（如无）。

- [ ] **Step 1.2: 跑测试确认失败** — `cmake --build build --target test_common` 预期编译错误（msUtf8Encode 等未声明）。

- [ ] **Step 1.3: 实现** — `common.h` 声明区追加：

```c
// ---- UTF-8 与转义（T013，01 §3.3；scanner 静态副本不动，此处为共享版） ----
// 解码 p 处一个 rune 并写回字节宽 width；非法/截断/代理区/超长序列按 1 字节返回 0xFFFD
uint32_t msUtf8Decode(const char* p, int32_t avail, int32_t* width);
// 编码 cp 到 out（容量 ≥4）；越界/代理区码点写 U+FFFD；返回字节宽 1-4
int32_t msUtf8Encode(uint32_t cp, char* out);
// 解码 01 §3.3 转义序列（\n \r \t \\ \" \' \0 \xHH \u{...}）；返回解码长度，cap 不足返回 -1
// 源文本经 scanner 校验后到达，非法形态按字面拷贝（防御路径）
int32_t msTextDecodeEscapes(const char* src, int32_t len, char* dst, int32_t cap);
```

`common.c` 末尾追加实现：`msUtf8Decode` 与 scanner.c:7-43 的 `utf8Decode` 同规则（逐字节拷贝逻辑，禁直接引用 scanner 静态函数）；`msUtf8Encode` 按标准 1-4 字节编码；`msTextDecodeEscapes` 逐字符扫描——`\\` 后按 scanner.c:311-404 的转义表分派（`\x` 取 2 个十六进制位、`\u{` 取 1-6 位十六进制后 `}`、单字符查表、未知形态原样拷贝反斜杠与字符）。

- [ ] **Step 1.4: 构建 + 测试** — `cmake --build build --target test_common && ctest --test-dir build -R ^common$` 通过；随后全量 `ctest --test-dir build` 无回归。

- [ ] **Step 1.5: 提交** — `git add -A && git commit -m "✨ feat(common): shared UTF-8 codec and escape decoder for T013"`

---

### Task 2: MsObjString 核心数据层

**Files:**
- Create: `src/obj/obj_string.h`
- Create: `src/obj/obj_string.c`
- Modify: `CMakeLists.txt`（ms_obj 追加源文件）
- Create: `tests/unit/test_obj_string.c`（骨架 + 本任务用例）
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 2.1: 写失败测试** — 新建 `tests/unit/test_obj_string.c`：

```c
// T013：string 对象单测（docs/tasks/T013 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_string.h"
#include "test_harness.h"
#include "vm/vm.h"

TEST(createAndHash) {  // 创建即定 hash/isAscii/charLen（22 §3）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "abc", 3);
  EXPECT(ctx, s != NULL);
  EXPECT(ctx, s->head.type == MS_OBJ_STRING);
  EXPECT_EQ(ctx, 3, (long long)s->byteLen);
  EXPECT_EQ(ctx, 3, (long long)s->charLen);
  EXPECT(ctx, s->isAscii);
  EXPECT(ctx, s->hash != 0 && s->hash64 != 0);
  EXPECT_EQ(ctx, 0, memcmp("abc", s->bytes, 3));
  MsObjString* zh = msObjStringNew(&vm, "中文", 6);
  EXPECT(ctx, !zh->isAscii);
  EXPECT_EQ(ctx, 6, (long long)zh->byteLen);
  EXPECT_EQ(ctx, 2, (long long)zh->charLen);
  EXPECT(ctx, zh->runeIndex == NULL);  // 惰性未构建
  msVmFree(&vm);
}

TEST(hashEqualConcat) {  // "abc" vs "ab"+"c"：hash 相等、值相等、指针不同
  MsVM vm;
  msVmInit(&vm);
  MsObjString* a = msObjStringNew(&vm, "abc", 3);
  MsObjString* b = msObjStringNew(&vm, "ab", 2);
  MsObjString* c = msObjStringConcat(&vm, b, a->bytes + 2, 1);
  EXPECT(ctx, a != c);
  EXPECT_EQ(ctx, (long long)a->hash, (long long)c->hash);
  EXPECT_EQ(ctx, (long long)a->hash64, (long long)c->hash64);
  EXPECT(ctx, msObjStringEqual(a, c));
  EXPECT(ctx, !msObjStringEqual(a, b));
  EXPECT(ctx, msObjStringCompare(a, c) == 0 && msObjStringCompare(b, a) < 0 &&
                  msObjStringCompare(c, b) > 0);
  msVmFree(&vm);
}

TEST(fromEscapedCreatesDecoded) {
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringFromEscaped(&vm, "a\\u{4E2D}b", 9);
  EXPECT_EQ(ctx, 6, (long long)s->byteLen);
  EXPECT_EQ(ctx, 3, (long long)s->charLen);
  EXPECT_EQ(ctx, 0, memcmp("a\xE4\xB8\xADb", s->bytes, 6));
  msVmFree(&vm);
}
```

main() 与 tests 数组按 test_gc_basic.c 模板（先只挂这三个）。

- [ ] **Step 2.2: 注册测试** — `tests/CMakeLists.txt` 追加 `ms_add_test(obj_string ms_obj ms_gc ms_vm ms_compiler ms_binder)`；构建确认链接失败（obj_string.h 不存在）。

- [ ] **Step 2.3: 实现 obj_string.h** — 全量（FAM 布局 + API，注释写明 D1 依据）：

```c
// MsObjString（T013，22 §3）：UTF-8 不可变存储、FNV-1a 32/64、ASCII 快路径、惰性 runeIndex
#ifndef INCLUDE_MS_OBJ_OBJ_STRING_H_
#define INCLUDE_MS_OBJ_OBJ_STRING_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef struct MsObjString {
  MsObj head;
  ms_u32 hash;      // FNV-1a-32（相等短路）
  ms_u64 hash64;    // FNV-1a-64（map 键，T015 对接）
  ms_u32 byteLen;
  ms_u32 charLen;   // 创建时一次扫描定（ASCII 时 = byteLen）
  bool isAscii;
  int32_t* runeIndex;  // 惰性：非 ASCII 首次按需构建；指向对象内 bytes+byteLen 保留区（D1）
  char bytes[];        // FAM；非 ASCII 时尾随 (charLen+1)*4 字节 runeIndex 空间
} MsObjString;

// 运行时新建（拷贝 bytes；不 intern）
MsObjString* msObjStringNew(MsVM* vm, const char* bytes, int32_t len);
// 拼接（a + bytes[0..lenB)）；失败（长度溢出/分配失败）返回 NULL
MsObjString* msObjStringConcat(MsVM* vm, const MsObjString* a, const char* bytesB, int32_t lenB);
// 重复 n 次（n <= 0 返回空串）；失败返回 NULL
MsObjString* msObjStringRepeat(MsVM* vm, const MsObjString* a, ms_i64 n);
// 解码 01 §3.3 转义原文后新建（src 借用）
MsObjString* msObjStringFromEscaped(MsVM* vm, const char* src, int32_t len);

bool msObjStringEqual(const MsObjString* a, const MsObjString* b);  // hash/len 短路 → memcmp
int32_t msObjStringCompare(const MsObjString* a, const MsObjString* b);  // 字典序（<0/0/>0）

// rune 访问（负索引换算；越界返回 false）
bool msObjStringRuneAt(MsVM* vm, MsObjString* s, ms_i64 i, ms_u32* out);
// 惰性构建并返回 rune 索引表（charLen+1 项，末项 = byteLen 哨兵；ASCII 串返回 NULL）
const int32_t* msObjStringRuneIndex(MsVM* vm, MsObjString* s);

// 规范切片算法（T014 list 复用；Python 语义）。step==0 返回 false（调用方报 ValueError）
bool msSliceRange(ms_i64 len, bool hasA, ms_i64 a, bool hasB, ms_i64 b, bool hasC, ms_i64 c,
                  ms_i64* startOut, ms_i64* stopOut, ms_i64* stepOut);
// 按 rune 索引切片（端点省略由 has* 标记）；step==0 返回 NULL 且 *errStepZero 置 true
MsObjString* msObjStringSlice(MsVM* vm, MsObjString* s, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                              bool hasC, ms_i64 c, bool* errStepZero);
// 字节偏移 → rune 索引（byteOffset 必须落在 rune 边界；否则最近前驱边界）
int32_t msObjStringRuneAtByte(const MsObjString* s, int32_t byteOffset);

// ---- string 迭代器（chars/bytes；MS_OBJ_ITERATOR tag，T017 泛化壳） ----
typedef struct MsObjStrIter {
  MsObj head;
  MsObjString* str;  // markChildren 标记（D8）
  ms_u32 pos;        // chars：字节游标；bytes：字节下标
  bool byteMode;
} MsObjStrIter;

MsObjStrIter* msObjStrIterNew(MsVM* vm, MsObjString* s, bool byteMode);
bool msStrIterNext(MsVM* vm, MsObjStrIter* it, MsValue* out);  // 耗尽返回 false

// ---- intern 表（intern.c；挂 MsVM，PERMANENT 代） ----
typedef struct MsInternTable {
  MsObjString** slots;
  size_t cap;  // 2^n
  size_t count;
} MsInternTable;

void msInternInit(MsInternTable* t);
void msInternFree(MsInternTable* t);  // 仅释放槽位数组（对象归堆统一释放）
MsObjString* msInternBytes(MsVM* vm, const char* bytes, int32_t len);  // 存在即返回既有指针

static inline bool msIsString(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_STRING;
}

#endif  // INCLUDE_MS_OBJ_OBJ_STRING_H_
```

- [ ] **Step 2.4: 实现 obj_string.c（本任务范围）** — 创建/拼接/重复/FromEscaped/Equal/Compare：

创建核心：`newString(vm, bytes, len, extraTail)` 静态助手——`msObjAlloc(vm, MS_OBJ_STRING, sizeof(MsObjString) + len + extraTail)`；一次遍历同时算 FNV-1a 32/64（`0x811C9DC5/0x01000193` 与 `0xCBF29CE484222325/0x100000001B3`）、isAscii、charLen（非 ASCII 用 msUtf8Decode 计宽，坏序列按 1 字节计 1 char）；非 ASCII 时 extraTail = `(charLen+1)*sizeof(int32_t)`，`runeIndex = NULL`。`msObjStringNew` = newString(0)；Concat/Repeat 先检总长 ≤ INT32_MAX 再拼缓冲走 newString；FromEscaped 先 `msTextDecodeEscapes`（栈缓冲放不下时 malloc）再 newString。Equal：`a == b` 短路 → hash 不等 false → byteLen 不等 false → memcmp。Compare：memcmp 短长度 + 前缀规则（memcmp 结果，等长才 0）。

- [ ] **Step 2.5: CMake 接线** — CMakeLists.txt `target_sources(ms_obj PRIVATE src/obj/obj_string.c)`（T013 注释）。

- [ ] **Step 2.6: 构建 + 测试** — `cmake --build build --target test_obj_string && ctest --test-dir build -R ^obj_string$` 通过。

- [ ] **Step 2.7: 提交** — `git add -A && git commit -m "✨ feat(obj): MsObjString core with FNV-1a hashing and UTF-8 scan"`

---

### Task 3: rune 索引与规范切片

**Files:**
- Modify: `src/obj/obj_string.c`
- Test: `tests/unit/test_obj_string.c`

- [ ] **Step 3.1: 写失败测试** — 追加：

```c
TEST(runeIndexAndRuneAt) {  // "中文ab"[1] = 20025；ASCII 无表直查
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "中文ab", 8);
  ms_u32 cp = 0;
  EXPECT(ctx, msObjStringRuneAt(&vm, s, 1, &cp));
  EXPECT_EQ(ctx, 20025, (long long)cp);  // '文'
  EXPECT(ctx, msObjStringRuneAt(&vm, s, 2, &cp) && cp == 'a');
  EXPECT(ctx, msObjStringRuneAt(&vm, s, -1, &cp) && cp == 'b');
  EXPECT(ctx, msObjStringRuneAt(&vm, s, -3, &cp) && cp == 20013);  // '中'
  EXPECT(ctx, !msObjStringRuneAt(&vm, s, 4, &cp));   // 越界
  EXPECT(ctx, !msObjStringRuneAt(&vm, s, -5, &cp));
  const int32_t* ri = msObjStringRuneIndex(&vm, s);
  EXPECT(ctx, ri != NULL);
  EXPECT_EQ(ctx, 0, ri[0]);
  EXPECT_EQ(ctx, 3, ri[1]);
  EXPECT_EQ(ctx, 6, ri[2]);
  EXPECT_EQ(ctx, 8, ri[5]);  // 末哨兵 = byteLen
  EXPECT_EQ(ctx, 1, msObjStringRuneAtByte(s, 3));
  EXPECT_EQ(ctx, 2, msObjStringRuneAtByte(s, 6));
  MsObjString* ascii = msObjStringNew(&vm, "abc", 3);
  EXPECT(ctx, msObjStringRuneIndex(&vm, ascii) == NULL);  // ASCII 快路径不建表
  EXPECT(ctx, msObjStringRuneAt(&vm, ascii, 1, &cp) && cp == 'b');
  msVmFree(&vm);
}

TEST(sliceRangeCanonical) {  // 规范化（T014 复用）
  ms_i64 st, sp, stp;
  EXPECT(ctx, msSliceRange(5, true, 1, true, 4, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 1, st); EXPECT_EQ(ctx, 4, sp); EXPECT_EQ(ctx, 1, stp);
  EXPECT(ctx, msSliceRange(5, false, 0, false, 0, true, -1, &st, &sp, &stp));
  EXPECT_EQ(ctx, 4, st); EXPECT_EQ(ctx, -1, sp); EXPECT_EQ(ctx, -1, stp);
  EXPECT(ctx, msSliceRange(5, true, -3, false, 0, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 2, st); EXPECT_EQ(ctx, 5, sp);
  EXPECT(ctx, msSliceRange(3, true, 10, false, 0, false, 0, &st, &sp, &stp));
  EXPECT_EQ(ctx, 3, st);  // 越界钳位
  EXPECT(ctx, !msSliceRange(5, false, 0, false, 0, true, 0, &st, &sp, &stp));  // 步进 0
}

TEST(stringSlice) {  // "a中b"：[0:2]="a中" [::2]="ab" [::-1]="b中a"
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "a中b", 7);
  bool zero = false;
  MsObjString* r = msObjStringSlice(&vm, s, true, 0, true, 2, false, 0, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 4 && memcmp("a\xE4\xB8", r->bytes, 4) == 0);
  r = msObjStringSlice(&vm, s, false, 0, false, 0, true, 2, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 2 && memcmp("ab", r->bytes, 2) == 0);
  r = msObjStringSlice(&vm, s, false, 0, false, 0, true, -1, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 7 && memcmp("b\xE4\xB8\xADa", r->bytes, 7) == 0);
  r = msObjStringSlice(&vm, s, true, 0, true, 0, false, 0, &zero);
  EXPECT(ctx, r != NULL && r->byteLen == 0);  // 空切片
  EXPECT(ctx, msObjStringSlice(&vm, s, false, 0, false, 0, true, 0, &zero) == NULL && zero);
  msVmFree(&vm);
}
```

tests 数组追加三项；构建确认失败（RuneAt/Slice 未实现——链接错误）。

- [ ] **Step 3.2: 实现** — obj_string.c 追加：

`msObjStringRuneIndex`：非 ASCII 且 NULL 时遍历 bytes 填保留区 `(int32_t*)(s->bytes + s->byteLen)`（每 rune 起始偏移 + 末哨兵 byteLen），返回指针。`msObjStringRuneAt`：负索引 += charLen 后越界检；ASCII 直接 `bytes[i]`；否则查表 `(unsigned char)bytes[ri[i]]`（<0x80）或 msUtf8Decode 于该偏移。`msObjStringRuneAtByte`：ASCII → byteOffset；否则建表后二分（upper_bound 前驱）。`msSliceRange` 按 D7 伪码。`msObjStringSlice`：len=charLen 走 msSliceRange → 计数；结果缓冲 malloc；ASCII 直接字节步进拷贝，非 ASCII 查表取每 rune 字节段（步进负时反向）；msObjStringNew 后 free 缓冲。

- [ ] **Step 3.3: 构建 + 测试** — `ctest --test-dir build -R ^obj_string$` 通过。

- [ ] **Step 3.4: 提交** — `git commit -am "✨ feat(obj): lazy rune index and canonical slice for strings"`

---

### Task 4: intern 表

**Files:**
- Create: `src/obj/intern.c`
- Modify: `src/vm/vm.c`（msVmInit/msVmFree 接线）

- [ ] **Step 4.1: 写失败测试** — 追加：

```c
TEST(internDedup) {  // 同字面量两处出现指针相等（DoD）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* a = msInternBytes(&vm, "hello", 5);
  MsObjString* b = msInternBytes(&vm, "hello", 5);
  EXPECT(ctx, a == b);
  EXPECT(ctx, a->head.flags & MS_OBJ_PERMANENT);
  MsObjString* c = msObjStringNew(&vm, "hello", 5);
  EXPECT(ctx, a != c);  // 运行时产物不强制 intern
  gcCollectAll(&vm);    // permanent 无 root 也存活
  EXPECT(ctx, a == msInternBytes(&vm, "hello", 5));
  msInternBytes(&vm, "", 0);  // 空串可 intern
  msVmFree(&vm);
}

TEST(internGrowth) {  // 负载再哈希：大批量 intern 后仍正确去重
  MsVM vm;
  msVmInit(&vm);
  char buf[32];
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof(buf), "str-%d", i);
    EXPECT(ctx, msInternBytes(&vm, buf, n) == msInternBytes(&vm, buf, n));
  }
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof(buf), "str-%d", i);
    EXPECT_EQ(ctx, 0, memcmp(buf, msInternBytes(&vm, buf, n)->bytes, (size_t)n));
  }
  msVmFree(&vm);
}
```

- [ ] **Step 4.2: 实现 intern.c** — 初始 cap 256 线性探测（idx = hash & (cap-1)，命中条件 hash 相等 + byteLen 相等 + memcmp）；miss 时 `msObjStringNew` + `flags |= MS_OBJ_PERMANENT` 插入；count > cap*3/4 → cap*2 再哈希。`msInternInit` 置零；`msInternFree` free slots。

- [ ] **Step 4.3: vm.c 接线** — `struct MsVM` 增 `MsInternTable intern;`（vm.h，前向 `typedef struct MsInternTable MsInternTable;`）；`msVmInit` 尾部 `msInternInit(&vm->intern);`；`msVmFree` 在 gcHeapFree 之后 `msInternFree(&vm->intern);`（先释放对象后释放槽数组，无 deref）。vm.c include `obj/obj_string.h`。

- [ ] **Step 4.4: 构建 + 全量测试** — `ctest --test-dir build` 无回归。

- [ ] **Step 4.5: 提交** — `git commit -am "✨ feat(obj): per-VM intern table with permanent generation"`

---

### Task 5: 值迁移 — MS_VAL_STRING 退役

**Files:**
- Modify: `src/vm/vm.h`、`src/vm/value.c`、`src/vm/vm.c`、`src/vm/builtins_bridge.c`
- Modify: `src/compiler/chunk.h`、`src/compiler/chunk.c`、`src/compiler/compiler.c`、`src/compiler/disasm.c`
- Modify: `tests/unit/test_vm_basic.c`

- [ ] **Step 5.1: 写失败测试** — test_vm_basic.c：改 `valuePredicateFamily`（假指针不再做 typeName/equal deref——OBJ 路径现读 obj->type），并新增：

```c
TEST(stringValueMigration) {  // T013：string 即 MS_VAL_OBJ；== 值相等；< 字典序
  RunResult r = runSrc("var a = \"abc\"\nvar b = \"ab\" + \"c\"\nprint(a == b)\nprint(a < b)\n"
                       "print(len(a))\nprint(\"x\\u{4E2D}\".charLen())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "true\nfalse\n3\n2\n", r.out);
  free(r.out);
}
```

（charLen 方法在 Task 8 才有——本步先仅保留前三行 `true\nfalse\n3\n`，Step 5.7 落地后同批把 charLen 行留给 Task 8 再加。）

valuePredicateFamily 改造：删除假指针的 `msValueEqual(&o,&o)`/typeName/`!msValueEqual(&o,&o2)` 断言，改为 VM 内真实对象：

```c
TEST(valuePredicateFamily) {  // T012 五 tag 判型族 + T013 OBJ 分派（typeName deref obj->type）
  MsValue n = msValueNil();
  EXPECT(ctx, msIsNil(&n) && !msIsObj(&n));
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "s", 1);
  MsValue sv = msValueObj(&s->head);
  EXPECT(ctx, msIsObj(&sv));
  EXPECT_STREQ(ctx, "string", msValueTypeName(&sv));
  EXPECT(ctx, msValueIsTruthy(&sv));
  MsValue o2 = msValueObj((MsObj*)(uintptr_t)0x20);
  EXPECT(ctx, !msValueEqual(&sv, &o2));  // string vs 非 string → false（不 deref o2? 需 deref 判型）
  msVmFree(&vm);
}
```

注意：equal 的 OBJ 路径实现为"读 a->obj->type 与 b->obj->type 分派"，o2 假指针会被 deref——改为 `(uintptr_t)0x20` 指针不可 deref，**此断言换成真实对象对**：用两个不同 string 值比较。最终以实现落地的真实分派为准调整（原则：假指针只用于 msIsObj 不 deref 判型）。

- [ ] **Step 5.2: vm.h** — 删 `MS_VAL_STRING` 与 `as.str` 成员；kind 注释更新（STRING 已退役）。union 增 `MS_VAL_SLICE`：

```c
  MS_VAL_RANGE_ITER,  // 过渡：range 惰性迭代器（MsObjIter 归 T017）
  MS_VAL_SLICE,       // 过渡：切片描述（BUILD_SLICE → GET_INDEX 栈上瞬态；T014/T015 复用）
```

```c
    struct {
      ms_i64 cur, stop, step;
    } range;
    struct {
      ms_i64 start, end, step;
      ms_u8 mask;  // bit0/1/2：start/end/step 显式给出
    } slice;
```

声明区：`msValueFromConst` 改签 `MsValue msValueFromConst(MsVM* vm, const ConstEntry* e, bool* okOut);`；追加 `MsObjString* msValueStrForm(MsVM* vm, const MsValue* v);`、`MsObjString* msValueReprForm(MsVM* vm, const MsValue* v);`（Task 11 实装，本任务先占位返回 "<object>" 串——**不**：声明与实现同任务落，见 Task 11；本任务 vm.h 只加声明会未定义——改为 Task 11 一并加声明。本任务不动这两行）。

- [ ] **Step 5.3: chunk.h/chunk.c** — ConstKind 增 `CONST_RAWSTRING`、`CONST_INTERP`；ConstEntry 增缓存与 interp 载荷：

```c
typedef struct InterpSeg {
  bool isExpr;         // true：表达式段（值运行时前置压栈）
  const char* start;   // false：字面段（编译期已解码；chunk 拥有，chunkFree 释放）
  int32_t len;
} InterpSeg;

typedef struct ConstEntry {
  ConstKind kind;
  MsObj* strObj;  // T013：STRING/RAWSTRING 值缓存（intern 产物；永久代）
  struct MsVM* strVm;  // 缓存归属 VM（跨 VM 复跑按此失效）
  union {
    ...既有...
    struct {
      InterpSeg* segs;
      int32_t count;
    } interp;
  } as;
} ConstEntry;
```

`struct MsVM;` 前向声明（chunk.h 不能 include vm.h——vm.h 已 include chunk.h，会环）。chunk.c：`constEquals` RAWSTRING 与 STRING 同 memcmp 分支（kind 不同天然不去重）、INTERP 恒 false；`chunkAddConstText` 调整注释（STRING/BIGINT/RAWSTRING）；新增：

```c
int32_t chunkAddConstInterp(Chunk* c, const InterpSeg* segs, int32_t count) {
  InterpSeg* own = malloc(sizeof(InterpSeg) * (size_t)count);
  MS_ASSERT(own != NULL);
  memcpy(own, segs, sizeof(InterpSeg) * (size_t)count);
  ConstEntry e = {CONST_INTERP, NULL, NULL, {0}};
  e.as.interp.segs = own;
  e.as.interp.count = count;
  MS_ARRAY_PUSH(c->consts, c->constsLen, c->constsCap, e);  // 恒追加（不去重）
  return c->constsLen - 1;
}
```

`chunkFree`：CONST_PROTO 分支后加 CONST_INTERP 分支 `free(c->consts[i].as.interp.segs);`。**注意**：ConstEntry 各构造点（chunkAddConstInt/Float/Bool/Rune/Text/Proto 与 compiler.c 手写初始化）必须零初始化 strObj/strVm——现有 `{KIND, {0}}` 形态改为 `{KIND, NULL, NULL, {0}}` 或逐个确认指定初始化器覆盖。

- [ ] **Step 5.4: value.c** — 删 MS_VAL_STRING case（equal/typeName/toStr/FromConst）；equal OBJ 路径：

```c
    case MS_VAL_OBJ: {
      if (b->kind != MS_VAL_OBJ) {
        return false;
      }
      if (a->as.obj->type == MS_OBJ_STRING && b->as.obj->type == MS_OBJ_STRING) {
        return msObjStringEqual((const MsObjString*)a->as.obj, (const MsObjString*)b->as.obj);
      }
      return a->as.obj == b->as.obj;  // 其余身份（list 值等价 T014）
    }
```

order 增 string/string → `msObjStringCompare`；typeName MS_VAL_OBJ 分派（string/list/function=BOUND_METHOD|NATIVE/iterator=ITERATOR，default "object"；需要 obj_string.h/obj.h include）；`msValueFromConst(vm, e, ok)`：CONST_STRING → 缓存命中（`e->strVm == vm && e->strObj != NULL`）直接返回；否则 `msObjStringFromEscaped` + `msInternBytes`（intern 后丢弃临时串）缓存；CONST_RAWSTRING 同路径但不解码（`msInternBytes(vm, e->as.text.start, len)`）；BIGINT 同现状。

- [ ] **Step 5.5: vm.c / builtins_bridge.c / disasm.c / compiler.c** — vm.c LOAD_CONST 传 vm；print：`msIsString(&args[i])` → fwrite string bytes；arithBinary 占位分支（vm.c:232）删除（Task 6 接管，此刻 string+string 落 generic TypeError）。disasm.c：CONST_RAWSTRING 复用 CONST_STRING 渲染；CONST_INTERP 渲染 `"<interp %d segs>"`。compiler.c `tryStringConst`：`n->as.strLit.raw` → `chunkAddConstText(..., CONST_RAWSTRING, ...)`（raw 单段）。

- [ ] **Step 5.6: 构建修余波** — 全量构建；`rg -n "MS_VAL_STRING|as\.str\b" src tests` 清零；既有测试（valuePredicateFamily 已改）全绿。**注意** runShortCircuitValues 等 print 字符串用例必须仍绿。

- [ ] **Step 5.7: 全量测试 + 提交** — `ctest --test-dir build` 全绿后 `git commit -am "♻️ refactor(vm): retire MS_VAL_STRING, strings become heap objects via intern"`

---

### Task 6: 拼接 / 重复 / TypeError

**Files:**
- Modify: `src/vm/vm.c`（arithBinary）

- [ ] **Step 6.1: 写失败测试** — test_vm_basic.c 追加：

```c
TEST(stringConcatRepeat) {  // 02 §4：+ 拼接、* 重复、"a"+1 TypeError
  RunResult r = runSrc("print(\"ab\" + \"c\")\nprint(\"ab\" * 3)\nprint(\"x\" * 0 == \"\")");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "abc\nababab\ntrue\n", r.out);
  free(r.out);
  r = runSrc("print(\"a\" + 1)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("print(\"a\" * \"b\")");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  free(r.out);
}
```

- [ ] **Step 6.2: 实现** — arithBinary 数值分支后、generic TypeError 前：

```c
  if (k == BA_ADD && a.kind == MS_VAL_OBJ && b.kind == MS_VAL_OBJ &&
      a.as.obj->type == MS_OBJ_STRING && b.as.obj->type == MS_OBJ_STRING) {
    const MsObjString* sa = (const MsObjString*)a.as.obj;
    const MsObjString* sb = (const MsObjString*)b.as.obj;
    MsObjString* r = msObjStringConcat(vm, sa, sb->bytes, (int32_t)sb->byteLen);
    if (r == NULL) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: string concatenation result too large");
      return false;
    }
    *out = msValueObj((MsObj*)r);
    return true;
  }
  if (k == BA_MUL && a.kind == MS_VAL_OBJ && a.as.obj->type == MS_OBJ_STRING &&
      b.kind == MS_VAL_I64) {
    MsObjString* s = msObjStringRepeat(vm, (const MsObjString*)a.as.obj, b.as.i);
    if (s != NULL) {
      *out = msValueObj((MsObj*)s);
      return true;
    }
    snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: string repetition result too large");
    return false;
  }
```

（`"a" + 1` 等混合形态落既有 generic TypeError 分支——02 §8 无隐式转换。）

- [ ] **Step 6.3: 构建 + 测试 + 提交** — `git commit -am "✨ feat(vm): string concatenation and repeat operators"`

---

### Task 7: 最小 MsObjList 桥

**Files:**
- Create: `src/obj/obj_list.h`、`src/obj/obj_list.c`
- Modify: `CMakeLists.txt`、`src/vm/vm.c`（BUILD_LIST + GET_INDEX list 路径 + markFn 注册）、`src/compiler/compiler.c`（AST_LIST）

- [ ] **Step 7.1: 写失败测试** — test_obj_string.c 追加（list 是 string 方法的桥，同文件测）：

```c
TEST(minimalListBridge) {  // D6：BUILD_LIST 数据面——append/count/GET_INDEX/str 形态
  MsVM vm;
  msVmInit(&vm);
  MsObjList* xs = msObjListNew(&vm);
  EXPECT(ctx, xs != NULL && xs->count == 0);
  msObjListPush(&vm, xs, msValueInt(1));
  msObjListPush(&vm, xs, msValueInt(2));
  EXPECT_EQ(ctx, 2, (long long)xs->count);
  EXPECT_EQ(ctx, 1, xs->items[0].as.i);
  EXPECT_EQ(ctx, 2, xs->items[1].as.i);
  MsValue lv = msValueObj(&xs->head);
  EXPECT_STREQ(ctx, "list", msValueTypeName(&lv));
  msVmFree(&vm);
}
```

test_vm_basic.c 追加（指令路径）：

```c
TEST(buildListAndIndex) {
  RunResult r = runSrc("var xs = [1, 2, 3]\nprint(xs[1])\nprint(xs[-1])\nprint(len(xs))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "2\n3\n3\n", r.out);
  free(r.out);
  r = runSrc("var xs = [1]\nprint(xs[5])");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "IndexError") != NULL);
  free(r.out);
}
```

- [ ] **Step 7.2: 实现 obj_list.h/.c** — `{MsObj head; MsValue* items; int32_t count, cap;}`；`msObjListNew/msObjListPush`（倍增初 8，push 前调用方保证值已落栈或无分配窗口）；markChildren 遍历 items（静态函数 + 由 vm.c 注册）；`msObjListGet(xs, i)` 负索引 + 越界检。CMake 追加源文件。

- [ ] **Step 7.3: 编译器 AST_LIST** — compileExpr 增：

```c
    case AST_LIST: {
      for (int32_t i = 0; i < n->as.list.count; i++) {
        compileExpr(fs, n->as.list.items[i]);
      }
      emitOp(fs, OP_BUILD_LIST, n->line);
      emitByte(fs, (ms_u8)n->as.list.count);
      break;
    }
```

- [ ] **Step 7.4: VM** — SET_HANDLER 增 `OP_BUILD_LIST`；执行体：argc 次 pop 收集到新建 list（倒序填 items，逐个 pop——**GC 纪律**：先 alloc list 再逐值 pop 填入，pop 的值在值栈上直到填入，无分配窗口）；GET_INDEX 增 list 分支（int 索引/负索引，越界 `RUNTIME_ERROR("IndexError: list index out of range")`）。msVmInit 注册 `gcSetMarkFn(MS_OBJ_LIST, listMark)`。

- [ ] **Step 7.5: 构建 + 全量测试 + 提交** — `git commit -am "✨ feat(obj): minimal MsObjList bridge for string methods (BUILD_LIST/GET_INDEX)"`

---

### Task 8: 方法机制 + 首批方法（charLen/compareTo）

**Files:**
- Modify: `src/obj/obj.h`、`src/obj/obj.c`
- Create: `src/vm/str_methods.c`
- Modify: `CMakeLists.txt`、`src/vm/vm.c`（GET_ATTR/CALL_METHOD/CALL bound）、`src/compiler/compiler.c`（MEMBER 调用/取值）、`src/vm/vm.c`（markFn）

- [ ] **Step 8.1: 写失败测试** — test_vm_basic.c 追加：

```c
TEST(stringMethodCall) {  // CALL_METHOD 零分配路径
  RunResult r = runSrc("var s = \"hello\"\nprint(s.charLen())\nprint(\"x\".compareTo(\"y\"))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "5\n-1\n", r.out);
  free(r.out);
  r = runSrc("var f = \"a\".compareTo\nprint(f(\"b\"))");  // GET_ATTR 物化 bound 方法
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "-1\n", r.out);
  free(r.out);
  r = runSrc("print(\"a\".nope())");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "nope") != NULL);
  free(r.out);
}
```

- [ ] **Step 8.2: obj.h/obj.c** — 结构与工厂：

```c
typedef struct MsObjNative {
  MsObj head;
  MsNativeFn fn;
  const char* name;  // 借用方法表静态串
  int32_t nameLen;
} MsObjNative;

typedef struct MsObjBoundMethod {
  MsObj head;
  MsValue receiver;
  MsObjNative* method;
} MsObjBoundMethod;

MsObjNative* msObjNativeNew(MsVM* vm, MsNativeFn fn, const char* name, int32_t nameLen);
MsObjBoundMethod* msObjBoundNew(MsVM* vm, MsValue receiver, MsObjNative* method);
```

- [ ] **Step 8.3: str_methods.c 骨架 + 首批** — 方法表 + 错误助手 + 两方法：

```c
// string 方法族（T013，10 §2）：实例方法表机制——args[0] 恒为接收者（D5）
#include <string.h>

#include "obj/obj.h"
#include "obj/obj_string.h"
#include "vm/vm.h"

typedef struct StrMethodEntry {
  const char* name;
  MsNativeFn fn;
} StrMethodEntry;

static const StrMethodEntry kStrMethods[] = {
    {"charLen", strmCharLen},
    {"compareTo", strmCompareTo},
};

const MsObjNative* msStringMethodLookup(const char* name, int32_t len) {
  for (size_t i = 0; i < sizeof(kStrMethods) / sizeof(kStrMethods[0]); i++) {
    if ((int32_t)strlen(kStrMethods[i].name) == len && memcmp(kStrMethods[i].name, name, (size_t)len) == 0) {
      ...命中：需要 MsObjNative*——查表命中处惰性建永久对象或每次新建（见下）
    }
  }
  return NULL;
}
```

**物化策略**：CALL_METHOD 零分配直调（返回 fn 指针即可）；GET_ATTR 需要 MsObjNative 对象——`msStringMethodLookup` 返回 `{fn, name, len}` 值结构体（`StrMethodRef`），由调用方决定直调或物化。声明入 vm.h：`typedef struct {MsNativeFn fn; const char* name; int32_t nameLen;} MsMethodRef; bool msStringMethodLookup(const char* name, int32_t len, MsMethodRef* out);`

`strmCharLen`：`return msValueInt((ms_i64)((MsObjString*)args[0].as.obj)->charLen);`；`strmCompareTo`：参数校验 string（否则 TypeError 置槽）→ `msValueInt(msObjStringCompare(...))`。

- [ ] **Step 8.4: 编译器** — compileExpr AST_CALL 前插入 member 调用特例；AST_MEMBER：

```c
    case AST_MEMBER: {  // 方法取值（属性 GET_ATTR；string 方法物化 bound，其余 T020）
      compileExpr(fs, n->as.memberExpr.obj);
      int32_t k = nameConst(fs, n->as.memberExpr.name, n->as.memberExpr.nameLen);
      emitConstOp(fs, OP_GET_ATTR, k, n->line);
      emitByte(fs, 0);  // icSlot（T022）
      break;
    }
```

AST_CALL 分支改：

```c
    case AST_CALL: {
      if (n->as.call.callee->kind == AST_MEMBER) {  // s.m(args) → CALL_METHOD（21 §3.3）
        compileExpr(fs, n->as.call.callee->as.memberExpr.obj);
        for (int32_t i = 0; i < n->as.call.count; i++) {
          compileExpr(fs, n->as.call.args[i]);
        }
        int32_t k = nameConst(fs, n->as.call.callee->as.memberExpr.name,
                              n->as.call.callee->as.memberExpr.nameLen);
        emitConstOp(fs, OP_CALL_METHOD, k, n->line);
        emitByte(fs, 0);  // icSlot（T022）
        emitByte(fs, (ms_u8)n->as.call.count);
        break;
      }
      ...既有 IDENT 路径不变...
    }
```

- [ ] **Step 8.5: VM 三 opcode** — SET_HANDLER 增 OP_GET_ATTR/OP_CALL_METHOD；执行体：

```c
      OPCODE(OP_GET_ATTR) {  // string 方法物化 bound（类属性 T020）；<const name> <u8 ic>
        READ_OPERAND(k);
        (void)code[ip++];  // icSlot（T022）
        MsValue recv = pop(vm);
        MsMethodRef ref;
        if (msIsString(&recv) && msStringMethodLookup(consts[k].as.text.start, consts[k].as.text.len, &ref)) {
          MsObjNative* native = msObjNativeNew(vm, ref.fn, ref.name, ref.nameLen);
          if (native == NULL) RUNTIME_ERROR("RuntimeError: out of memory");
          MsObjBoundMethod* bound = msObjBoundNew(vm, recv, native);
          if (bound == NULL) RUNTIME_ERROR("RuntimeError: out of memory");
          push(vm, msValueObj(&bound->head));
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' has no attribute '%.*s'", msValueTypeName(&recv),
                      (int)consts[k].as.text.len, consts[k].as.text.start);
      }

      OPCODE(OP_CALL_METHOD) {  // <const name> <u8 ic> <u8 argc>：零分配直调（args[0]=接收者）
        READ_OPERAND(k);
        (void)code[ip++];  // icSlot（T022）
        int32_t argc = code[ip++];
        MsValue* args = &vm->stack[vm->stackTop - argc - 1];
        MsMethodRef ref;
        if (msIsString(&args[0]) &&
            msStringMethodLookup(consts[k].as.text.start, consts[k].as.text.len, &ref)) {
          MsValue r = ref.fn(vm, argc + 1, args);
          vm->stackTop -= argc + 1;
          push(vm, r);
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' has no method '%.*s'", msValueTypeName(&args[0]),
                      (int)consts[k].as.text.len, consts[k].as.text.start);
      }
```

OP_CALL 增 bound 分派（callee 槽改写 receiver 后原位调用，fn/receiver 先读入局部——D5）；msVmInit 注册 `gcSetMarkFn(MS_OBJ_BOUND_METHOD, ...)`（标 receiver + method）与 NATIVE（无 children，NULL 可不注册）。CMake：`target_sources(ms_vm PRIVATE src/vm/str_methods.c)`、`target_link_libraries(ms_vm PRIVATE ms_obj)`。

- [ ] **Step 8.6: 构建 + 测试** — 把 Task 5 留下的 `print("x\u{4E2D}".charLen())` 用例行补回（输出 `2`）。

- [ ] **Step 8.7: 提交** — `git commit -am "✨ feat(vm): instance method mechanism with bound natives for strings"`

---

### Task 9: 方法族全集（10 §2）+ Unicode 大小写表

**Files:**
- Modify: `tools/gen_unicode_table.py`（生成 unicode_case.h）
- Create: `src/obj/unicode_case.h`（生成物）
- Modify: `src/vm/str_methods.c`、`src/vm/vm.h`（msStringMethodLookup 已声明）
- Test: `tests/unit/test_obj_string.c`、`tests/unit/test_vm_basic.c`

- [ ] **Step 9.1: 生成脚本扩展** — gen_unicode_table.py 增 `emit_case_table`：遍历 0x80..0x10FFFF，`unicodedata` 之外直接用 `chr(cp).upper()/.lower()`（简单映射：结果长度 =1 且 ≠ 原字符才收）；写 `src/obj/unicode_case.h`（MsUnicodeCaseMap {from,to} 两张有序表 + 二分 `msUnicodeToUpper/msUnicodeToLower` + ASCII 快路径 + 与 unicode_table.h 相同头保护风格）；main 打印两表条目数（任务报告统计）。运行 `python tools/gen_unicode_table.py` 生成入库。

- [ ] **Step 9.2: 写失败测试** — test_obj_string.c 追加 C 级直调用例（receiver 构造 `MsValue recvArr[2]` 传方法 fn）：

```c
TEST(strMethodsFamily) {  // 10 §2 行为表（§5.1 未覆盖部分）
  MsVM vm;
  msVmInit(&vm);
  MsValue r;
  MsValue args[4];
  args[0] = msValueObj(&msObjStringNew(&vm, "a,b,c", 5)->head);
  args[1] = msValueObj(&msObjStringNew(&vm, ",", 1)->head);
  MsMethodRef m;
  EXPECT(ctx, msStringMethodLookup("split", 5, &m));
  r = m.fn(&vm, 2, args);
  EXPECT(ctx, r.kind == MS_VAL_OBJ && r.as.obj->type == MS_OBJ_LIST);
  EXPECT_EQ(ctx, 3, ((MsObjList*)r.as.obj)->count);
  args[0] = msValueObj(&msObjStringNew(&vm, "  x ", 4)->head);
  EXPECT(ctx, msStringMethodLookup("trim", 4, &m) && (r = m.fn(&vm, 1, args)).kind == MS_VAL_OBJ);
  EXPECT(ctx, msObjStringEqual((MsObjString*)r.as.obj, msInternBytes(&vm, "x", 1)));
  args[0] = msValueObj(&msObjStringNew(&vm, "5", 1)->head);
  args[1] = msValueInt(3);
  args[2] = msValueInt('0');
  EXPECT(ctx, msStringMethodLookup("padLeft", 7, &m) && (r = m.fn(&vm, 3, args)).kind == MS_VAL_OBJ);
  EXPECT(ctx, msObjStringEqual((MsObjString*)r.as.obj, msInternBytes(&vm, "005", 3)));
  args[0] = msValueObj(&msObjStringNew(&vm, "123", 3)->head);
  EXPECT(ctx, msStringMethodLookup("isDigit", 7, &m) && m.fn(&vm, 1, args).as.b);
  args[0] = msValueObj(&msObjStringNew(&vm, "12a", 3)->head);
  EXPECT(ctx, !m.fn(&vm, 1, args).as.b);
  args[0] = msValueObj(&msObjStringNew(&vm, "a", 1)->head);
  args[1] = msValueObj(&msObjStringNew(&vm, "b", 1)->head);
  EXPECT(ctx, msStringMethodLookup("join", 4, &m));
  // join 参数为 list：["a","b"] → "a,b"（§5.1 表）
  msVmFree(&vm);
}
```

（join 用例在 Task 7 list 桥上直接构造 `MsObjList` 填两个 string 值传入。）

test_vm_basic.c 追加端到端：

```c
TEST(stringMethodsEndToEnd) {  // 检索/变换/判断组合（02 §4 + 10 §2）
  RunResult r = runSrc("var s = \"Hello, World\"\n"
                       "print(s.indexOf(\"o\"))\nprint(s.lastIndexOf(\"o\"))\n"
                       "print(s.contains(\"World\"))\nprint(s.startsWith(\"Hell\"))\n"
                       "print(s.endsWith(\"rld\"))\nprint(s.toUpper())\n"
                       "print(s.replace(\"o\", \"0\"))\nprint(s.count(\"l\"))\n"
                       "print(s.slice(0, 5))\nprint(\"  hi  \".trim())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "4\n7\ntrue\ntrue\ntrue\nHELLO, WORLD\nHell0, W0rld\n3\nHello\nhi\n", r.out);
  free(r.out);
  r = runSrc("print(\"中文\".toUpper())\nprint(\"a\\u{4E2D}\".charLen())");
  EXPECT_STREQ(ctx, "中文\n2\n", r.out);  // 中文无大小写映射 → 原样
  free(r.out);
}
```

- [ ] **Step 9.3: 实现方法族** — str_methods.c 扩表至 29 项并实现（公共助手：`static const MsObjString* recvStr(vm, args)`、`argStr(vm, i, args)` 校验置槽、`argInt` 同、错误 `methodErr(vm, "TypeError: ...")` 置 hasError 返回 nil）。逐方法：

- `charLen/compareTo`（Task 8 已有）
- `chars/bytes` → `msValueObj(&msObjStrIterNew(vm, s, byteMode=false/true)->head)`
- `toUpper/toLower`：逐 rune `msUnicodeToUpper/Lower` 映射（ASCII 快路径在生成头内）→ 新串；无映射 rune 原样
- `split(sep[, n])`：sep 空串 → 按空白运行切分（isSpace 判定，首尾不产生空段）；否则字节搜索 sep（自实现 memmem，Windows 无 memmem）逐段切；n 给定且 >0 时最多 n 刀（尾段含剩余）；每段 `msObjStringNew` push 进新 list；段本身按 rune 边界（sep 匹配天然边界）
- `splitLines()`：按 `\n` 切（`\r\n` 归一？源已归一化 LF——按 `\n` 切，尾无 `\n` 则末行保留，空行保留）
- `join(xs)`：xs 必须 list（`TypeError: join expects a list`）；逐元素必须 string（`TypeError: join expects string elements`）；s + 元素交替拼接
- `contains/startsWith/endsWith(sub)`：字节搜索（endsWith 从尾部对齐比较）
- `indexOf(sub[, from])/lastIndexOf(sub)`：字节找到后 `msObjStringRuneAtByte` 转 rune 索引（from 为 rune 索引，经 runeIndex 表转字节起点）；未找到 -1
- `replace(old, new[, n])`：循环字节搜索替换；n 限次（缺省全换）；old 空串 → 原样返回（语义注记）
- `trim/trimLeft/trimRight([chars])`：缺省空白集（ASCII 空白）；给定 chars（string，按 rune 集合）时按集合剥离；按 rune 前后剥离
- `repeat(n)`：`msObjStringRepeat`
- `count(sub)`：非重叠计数；sub 空 → 0
- `padLeft(w[, ch])/padRight`：w 按 **rune 宽**（charLen 基准）；ch 缺省空格，给定必须单 rune string；重复填充至 w
- `slice(a[, b[, c]])`：`msObjStringSlice`（参数省略按调用 arity 映射 has* 位）
- `isDigit/isSpace/isAlpha`：逐 rune 判定全真才真（空串 false）；ASCII 精确 + 非 ASCII isAlpha 走 `msUnicodeIdStart` 剔 `_`（借 scanner/unicode_table.h；isDigit/isSpace 非 ASCII 暂 false，D9）
- 方法表条目按 10 §2 表序

- [ ] **Step 9.4: 构建 + 测试 + 提交** — `ctest --test-dir build` 全绿；`git commit -am "✨ feat(stdlib/strings): full Tier0 string method family with Unicode case tables"`

---

### Task 10: 索引/切片指令 — 编译与执行

**Files:**
- Modify: `src/compiler/compiler.c`（AST_INDEX/AST_SLICE + index 赋值目标）
- Modify: `src/vm/vm.c`（OP_GET_INDEX/OP_SET_INDEX/OP_BUILD_SLICE；SET_HANDLER 三项）

- [ ] **Step 10.1: 写失败测试** — test_vm_basic.c：

```c
TEST(indexAndSliceExec) {  // 02 §4 rune 语义 + 越界/步进 0 错误
  RunResult r = runSrc("var s = \"Hello, 世界\"\nprint(s[0])\nprint(s[7])\nprint(s[-1])\n"
                       "print(s[0:5])\nprint(s[::2])\nprint(s[::-1])\nprint(len(s))\nprint(s.charLen())");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "72\n19990\n30028\nHello\nHlo 世\n界世 ,olleH\n13\n9\n", r.out);
  free(r.out);
  r = runSrc("print(\"abc\"[5])");
  EXPECT(ctx, r.hasError && strstr(r.err, "IndexError") != NULL);
  free(r.out);
  r = runSrc("print(\"abc\"[::0])");
  EXPECT(ctx, r.hasError && strstr(r.err, "ValueError") != NULL);
  free(r.out);
  r = runSrc("var s = \"abc\"\ns[0] = 1");  // SET_INDEX string 报错
  EXPECT(ctx, r.hasError && strstr(r.err, "TypeError") != NULL);
  free(r.out);
  r = runSrc("print(\"abc\"[\"x\"])");
  EXPECT(ctx, r.hasError && strstr(r.err, "TypeError") != NULL);
  free(r.out);
}
```

（`s[-1]` = '界' U+754C = 30028；`[::2]` = H l o ␣ 世 = "Hlo 世"。）

- [ ] **Step 10.2: 编译器** — compileExpr 增：

```c
    case AST_INDEX: {
      compileExpr(fs, n->as.indexExpr.obj);
      compileExpr(fs, n->as.indexExpr.index);
      emitOp(fs, OP_GET_INDEX, n->line);
      break;
    }
    case AST_SLICE: {  // 切片对象不独立暴露：BUILD_SLICE 后立即 GET_INDEX（03 §3）
      compileExpr(fs, n->as.slice.obj);
      const AstNode* parts[3] = {n->as.slice.start, n->as.slice.end, n->as.slice.step};
      for (int i = 0; i < 3; i++) {
        if (parts[i] != NULL) {
          compileExpr(fs, parts[i]);
        } else {
          emitOp(fs, OP_LOAD_NIL, n->line);
        }
      }
      emitOp(fs, OP_BUILD_SLICE, n->line);
      emitByte(fs, 3);
      emitOp(fs, OP_GET_INDEX, n->line);
      break;
    }
```

compileAssign 非 IDENT 目标分支改：

```c
  if (target->kind == AST_INDEX) {  // obj[idx] = v（string 只读在运行时报）
    compileExpr(fs, target->as.indexExpr.obj);
    compileExpr(fs, target->as.indexExpr.index);
    compileExpr(fs, n->as.assign.value);
    emitOp(fs, OP_SET_INDEX, n->line);
    return;
  }
```

（保留其余不支持路径；复合赋值 index 目标维持不支持。）

- [ ] **Step 10.3: VM 执行** — SET_HANDLER 增三项；执行体：

```c
      OPCODE(OP_BUILD_SLICE) {  // <u8 argc=3>：start/end/step（nil = 省略）→ MS_VAL_SLICE
        (void)code[ip++];
        MsValue step = pop(vm), end = pop(vm), start = pop(vm);
        if ((start.kind != MS_VAL_NIL && start.kind != MS_VAL_I64) ||
            (end.kind != MS_VAL_NIL && end.kind != MS_VAL_I64) ||
            (step.kind != MS_VAL_NIL && step.kind != MS_VAL_I64)) {
          RUNTIME_ERROR("TypeError: slice indices must be int");
        }
        MsValue v = {MS_VAL_SLICE, {0}};
        v.as.slice.start = start.kind == MS_VAL_I64 ? start.as.i : 0;
        v.as.slice.end = end.kind == MS_VAL_I64 ? end.as.i : 0;
        v.as.slice.step = step.kind == MS_VAL_I64 ? step.as.i : 0;
        v.as.slice.mask = (ms_u8)((start.kind == MS_VAL_I64 ? 1 : 0) | (end.kind == MS_VAL_I64 ? 2 : 0) |
                                  (step.kind == MS_VAL_I64 ? 4 : 0));
        push(vm, v);
        NEXT();
      }

      OPCODE(OP_GET_INDEX) {
        MsValue idx = pop(vm);
        MsValue obj = pop(vm);
        if (msIsString(&obj)) {
          MsObjString* s = (MsObjString*)obj.as.obj;
          if (idx.kind == MS_VAL_I64) {
            ms_u32 cp = 0;
            if (!msObjStringRuneAt(vm, s, idx.as.i, &cp)) {
              RUNTIME_ERROR("IndexError: string index out of range");
            }
            push(vm, msValueInt((ms_i64)cp));
            NEXT();
          }
          if (idx.kind == MS_VAL_SLICE) {
            bool zero = false;
            MsObjString* r = msObjStringSlice(vm, s, idx.as.slice.mask & 1, idx.as.slice.start,
                                              idx.as.slice.mask & 2, idx.as.slice.end,
                                              idx.as.slice.mask & 4, idx.as.slice.step, &zero);
            if (r == NULL) {
              RUNTIME_ERROR(zero ? "ValueError: slice step must not be zero"
                                 : "OverflowError: string slice result too large");
            }
            push(vm, msValueObj(&r->head));
            NEXT();
          }
          RUNTIME_ERROR("TypeError: string indices must be int or slice");
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {
          if (idx.kind != MS_VAL_I64) {
            RUNTIME_ERROR("TypeError: list indices must be int");
          }
          MsValue item;
          if (!msObjListGet((MsObjList*)obj.as.obj, idx.as.i, &item)) {
            RUNTIME_ERROR("IndexError: list index out of range");
          }
          push(vm, item);
          NEXT();
        }
        RUNTIME_ERROR("TypeError: '%s' is not indexable", msValueTypeName(&obj));
      }

      OPCODE(OP_SET_INDEX) {
        MsValue val = pop(vm);
        MsValue idx = pop(vm);
        MsValue obj = pop(vm);
        if (msIsString(&obj)) {
          RUNTIME_ERROR("TypeError: 'string' does not support item assignment");
        }
        if (obj.kind == MS_VAL_OBJ && obj.as.obj->type == MS_OBJ_LIST) {
          RUNTIME_ERROR("RuntimeError: list item assignment executes in T014");
        }
        RUNTIME_ERROR("TypeError: '%s' does not support item assignment", msValueTypeName(&obj));
      }
```

**GC 纪律注记**：GET_INDEX string 切片路径在 pop 之后才分配——obj/idx 已在 C 局部，分配触发回收时 s 无 root！修正：先 peek 两槽（不 pop），完成分配后再统一 `stackTop -= 2`：

```c
      OPCODE(OP_GET_INDEX) {  // GC 纪律：peek 求值完成后再弹栈（分配窗口内保 root）
        MsValue idx = vm->stack[vm->stackTop - 1];
        MsValue obj = vm->stack[vm->stackTop - 2];
        ...全部成功路径在此处 vm->stackTop -= 2; push(result); NEXT();...
      }
```

（错误路径 RUNTIME_ERROR 直接 unwind 无需弹栈；OP_BUILD_SLICE/SET_INDEX 无分配无此问题；CALL_METHOD 同理 peek 形态——Task 8 实现时注意。）

- [ ] **Step 10.4: 构建 + 测试 + 提交** — `git commit -am "✨ feat(vm): GET/SET_INDEX and BUILD_SLICE with rune semantics"`

---

### Task 11: str/repr/len 内置 + FORMAT_STRING 插值

**Files:**
- Modify: `src/vm/value.c`、`src/vm/vm.h`（StrForm/ReprForm）
- Modify: `src/vm/builtins_bridge.c`（len/str/repr + print 统一走 str 形态）
- Modify: `src/compiler/compiler.c`（AST_STRING 插值路径）
- Test: `tests/unit/test_vm_basic.c`

- [ ] **Step 11.1: 写失败测试**：

```c
TEST(strReprLenBuiltins) {
  RunResult r = runSrc("print(str(1) + str(0.5))\nprint(repr(\"a\\nb\"))\nprint(str(nil))\n"
                       "print(len(\"abc\"))\nprint(len([1, 2]))");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "10.5\n\"a\\nb\"\nnil\n3\n2\n", r.out);  // repr 输出带引号转义
  free(r.out);
  r = runSrc("print(len(1))");
  EXPECT(ctx, r.hasError && strstr(r.err, "TypeError") != NULL);
  free(r.out);
}

TEST(formatStringInterp) {  // 03 §9：任意表达式 + 嵌套 + str 形态
  RunResult r = runSrc("print(\"a = ${1 + 2}, s = ${\"in${\"n\"}\"}\")");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "a = 3, s = inn\n", r.out);
  free(r.out);
  r = runSrc("var x = 4\nprint(\"${x * 2} and ${nil} and ${1.5}\")");
  EXPECT_STREQ(ctx, "8 and nil and 1.5\n", r.out);
  free(r.out);
}
```

- [ ] **Step 11.2: StrForm/ReprForm（value.c）** — vm.h 声明；实现：动态缓冲助手（MS_ARRAY_PUSH char）。`msValueStrForm`：string → 自身；list → `[` + 元素 `msValueReprForm` 逗号空格 + `]`（空 list `[]`）；标量 → msValueToStr buf 包裹；NATIVE/BOUND → "<native fn>"/"<bound method>"；FUNCTION/RANGE_ITER 沿 msValueToStr 形态。`msValueReprForm`：string → `"` + 逐字节转义（\\ \" \n \t \r，其余 <0x20 或 >0x7E 按 \xHH）+ `"`（与 disasm printConstValue CONST_STRING 同构，可提公共小函数或各写——各写，层不同）；list → 元素全 repr；标量同 str。

- [ ] **Step 11.3: builtins_bridge** — print 非 string 分支改 `msValueStrForm` 后 fwrite；新内置：

```c
// len(x)：string 字节 / list 元素（10 §1；其余容器 T014+）
static MsValue nativeLen(MsVM* vm, int32_t argc, const MsValue* args) { ... }
// str(x)/repr(x)：形态转换（02 §8 显式转换；__str__/__repr__ 归 T021）
static MsValue nativeStr(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) { bridgeErr(vm, "ArgumentError: str expects 1 argument"); return msValueNil(); }
  return msValueObj((MsObj*)msValueStrForm(vm, &args[0]));
}
```

`msBuiltinLookup` 增 `len/str/repr` 三项（memcmp 同 print/range 模式）。

- [ ] **Step 11.4: FORMAT_STRING 编译** — compiler.c AST_STRING else 分支改：

```c
      } else {  // 插值：字面段编译期解码入 CONST_INTERP；表达式段依次压栈后 FORMAT_STRING
        InterpSeg* segs = malloc(sizeof(InterpSeg) * (size_t)n->as.strLit.count);
        MS_ASSERT(segs != NULL);
        for (int32_t i = 0; i < n->as.strLit.count; i++) {
          const AstStrSeg* src = &n->as.strLit.segs[i];
          if (src->expr != NULL) {
            segs[i] = (InterpSeg){true, NULL, 0};
          } else {
            char* decoded = malloc((size_t)src->len);  // 解码只会收缩（\u{...} 源更长）
            MS_ASSERT(decoded != NULL);
            int32_t dlen = msTextDecodeEscapes(src->start, src->len, decoded, src->len);
            segs[i] = (InterpSeg){false, decoded, dlen};
          }
        }
        int32_t k = chunkAddConstInterp(fs->chunk, segs, n->as.strLit.count);
        for (int32_t i = 0; i < n->as.strLit.count; i++) {
          if (segs[i].isExpr) {
            compileExpr(fs, n->as.strLit.segs[i].expr);
          } else {
            free((void*)segs[i].start);  // const 已拷贝
          }
        }
        free(segs);
        emitConstOp(fs, OP_FORMAT_STRING, k, n->line);
      }
```

（tryStringConst 顶部加 `if (n->as.strLit.raw)` 保持单段 raw 常量路径——插值不可能出现在 raw。）

- [ ] **Step 11.5: FORMAT_STRING 执行（vm.c）** — SET_HANDLER 增；**GC 纪律：全程 peek，构建完成后再弹**：

```c
      OPCODE(OP_FORMAT_STRING) {  // <const k-interp>：表达式值已在栈顶区，消费后产单串
        READ_OPERAND(k);
        const InterpSeg* segs = consts[k].as.interp.segs;
        int32_t count = consts[k].as.interp.count;
        int32_t nExpr = 0;
        for (int32_t i = 0; i < count; i++) {
          nExpr += segs[i].isExpr ? 1 : 0;
        }
        const MsValue* exprs = &vm->stack[vm->stackTop - nExpr];  // 栈上 root
        char* buf = NULL;
        int32_t len = 0, cap = 0;
        int32_t next = 0;
        for (int32_t i = 0; i < count; i++) {
          if (segs[i].isExpr) {
            MsObjString* form = msValueStrForm(vm, &exprs[next++]);  // string 自身；其余新建（栈 root 保活）
            MS_ARRAY_PUSH_N(buf, len, cap, form->bytes, (int32_t)form->byteLen);  // 追加助手
          } else {
            MS_ARRAY_PUSH_N(buf, len, cap, segs[i].start, segs[i].len);
          }
        }
        MsObjString* result = msObjStringNew(vm, buf ? buf : "", len);
        free(buf);
        if (result == NULL) RUNTIME_ERROR("RuntimeError: out of memory");
        vm->stackTop -= nExpr;
        push(vm, msValueObj(&result->head));
        NEXT();
      }
```

（`MS_ARRAY_PUSH_N(buf, len, cap, ptr, n)` 为 vm.c 内新静态助手/宏——逐字节倍增追加；注意 msValueStrForm 的产物若非 args 自身则仅 buf 持引用，但 buf 为 C 堆无 GC——产物在下一次 StrForm 分配前已拷贝完毕，无窗口。）

- [ ] **Step 11.6: 构建 + 测试 + 提交** — `git commit -am "✨ feat(vm): str/repr/len builtins and FORMAT_STRING interpolation"`

---

### Task 12: chars()/bytes() 迭代器

**Files:**
- Modify: `src/vm/vm.c`（GET_ITER/FOR_ITER 分派 + markFn 注册）
- Test: `tests/unit/test_obj_string.c`、`tests/unit/test_vm_basic.c`

- [ ] **Step 12.1: 写失败测试** — test_obj_string.c：

```c
TEST(strIterators) {  // chars/bytes 迭代与 charLen/s[i] 自洽（DoD）
  MsVM vm;
  msVmInit(&vm);
  MsObjString* s = msObjStringNew(&vm, "a中b", 5);
  MsObjStrIter* it = msObjStrIterNew(&vm, s, false);
  MsValue v;
  EXPECT(ctx, msStrIterNext(&vm, it, &v) && v.as.i == 'a');
  EXPECT(ctx, msStrIterNext(&vm, it, &v) && v.as.i == 0x4E2D);
  EXPECT(ctx, msStrIterNext(&vm, it, &v) && v.as.i == 'b');
  EXPECT(ctx, !msStrIterNext(&vm, it, &v));
  MsValue keep = msValueObj(&it->head);  // 迭代器经栈保活
  vm.stack = malloc(sizeof(MsValue));
  vm.stackTop = 1;
  vm.stack[0] = keep;
  gcCollectAll(&vm);  // markChildren 标 str
  EXPECT(ctx, it->str == s);  // 误收则 ASan UAF
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  msVmFree(&vm);
}
```

test_vm_basic.c：

```c
TEST(forInStringChars) {
  RunResult r = runSrc("var n = 0\nfor c in \"a中b\".chars() {\n  n = n + 1\n}\nprint(n)\n"
                       "var t = 0\nfor b in \"中\".bytes() {\n  t = t + b\n}\nprint(t)");
  EXPECT(ctx, r.compileOk);
  EXPECT_STREQ(ctx, "3\n585\n", r.out);  // 'a' '中' 'b' 3 字符；0xE4+0xB8+0xAD = 585
  free(r.out);
}
```

（核对：0xE4=228, 0xB8=184, 0xAD=173，和 = 585——期望行写 `3\n585\n`。）

- [ ] **Step 12.2: 实现** — obj_string.c：`msObjStrIterNew`（markChildren 由 vm.c 注册的 str-iter markFn 标 `it->str`）；`msStrIterNext`：byteMode → `pos < byteLen` 出 `bytes[pos++]`；rune → `pos < byteLen` 时 msUtf8Decode 于 pos 前进。vm.c：GET_ITER 增 string 分支（peek→建迭代器→pop/push；分配窗口 string 在栈上）；FOR_ITER 顶部增 `MS_VAL_OBJ && type == MS_OBJ_ITERATOR` 分支（`msStrIterNext` 有值压值/耗尽 pop+跳 exit——注意 push 扩容后迭代器槽重取，与 range 路径同构）；msVmInit 注册 `gcSetMarkFn(MS_OBJ_ITERATOR, strIterMark)`（测试自注册覆盖纪律见 test_gc_basic 注释——**冲突处理**：strIterMark 内部判 `obj->size == sizeof(MsObjStrIter)`? 不引入魔法：test_gc_basic 已自行覆盖注册，此处注册为生产默认，语义互斥不混用）。

- [ ] **Step 12.3: 构建 + 测试 + 提交** — `git commit -am "✨ feat(vm): chars/bytes iterators wired into GET_ITER/FOR_ITER"`

---

### Task 13: fixtures + GC 压力 + ASan 门禁

**Files:**
- Create: `tests/fixtures/string/index_slice.ms` + `.out`
- Create: `tests/fixtures/string/methods.ms` + `.out`
- Create: `tests/fixtures/string/interp.ms` + `.out`
- Create: `tests/fixtures/string/type_err.ms` + `.out` + `.code` + `.err`
- Modify: `tests/unit/test_obj_string.c`（GC 压力）

- [ ] **Step 13.1: index_slice.ms**（任务书 §5.2 原文）：

```ms
var s = "Hello, 世界"
print(s[0])            // 72
print(s[7])            // 19990
print(s[0:5])          // Hello
print(s[::-1])         // 界世 ,olleH
print(len(s))          // 13
print(s.charLen())     // 9
```

`.out`：

```text
72
19990
Hello
界世 ,olleH
13
9
```

- [ ] **Step 13.2: methods.ms**（golden——先手写期望，跑 diff 校准后落盘）：

```ms
var s = "a,b,c"
var parts = s.split(",")
print(parts)
print(s.split(",", 1))
print(s.indexOf("b"))
print(s.contains("b"))
print(s.toUpper())
print("Hello".replace("l", "L"))
print("aXbXc".count("X"))
print("5".padLeft(3, '0'))
print("  hi  ".trim())
print("12a".isDigit())
```

`.out` 期望：

```text
["a", "b", "c"]
["a", "b,c"]
2
true
A,B,C
HeLLo
2
005
hi
false
```

（`split(",", 1)` 一刀 → `["a", "b,c"]`；list str 形态 = 元素 repr 逗号空格。）

- [ ] **Step 13.3: interp.ms**：

```ms
print("a = ${1 + 2}, s = ${"in${"n"}"}")
var x = 10
print("x = ${x}, double = ${x * 2}, nested list = ${[x, "s"]}")
```

`.out`：

```text
a = 3, s = inn
x = 10, double = 20, nested list = [10, "s"]
```

- [ ] **Step 13.4: type_err.ms** — `print("a" + 1)`；`.out` 为空文件；`.code` 内容 `1`；`.err` 首行 `TypeError: unsupported operand types for '+'`。

- [ ] **Step 13.5: GC 压力单测** — test_obj_string.c 追加：

```c
TEST(gcStressStrings) {  // DoD：循环分配无泄漏（ASan 门禁验证）；小阈值下堆稳定
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=65536");
#else
  setenv("MS_GC_HEAP_MAX", "65536", 1);
#endif
  MsVM vm;
  msVmInit(&vm);
  MsObjString* base = msObjStringNew(&vm, "seed-", 5);
  vm.stack = malloc(sizeof(MsValue));  // base 落栈保活
  vm.stackTop = 1;
  vm.stack[0] = msValueObj(&base->head);
  size_t maxSeen = 0;
  char buf[32];
  for (int i = 0; i < 20000; i++) {
    int n = snprintf(buf, sizeof(buf), "%d", i);
    MsObjString* s = msObjStringConcat(&vm, base, buf, n);
    EXPECT(ctx, s != NULL);
    vm.stack[0] = msValueObj(&s->head);  // 上一轮产物失引用（拼接产生 runeIndex 惰性表也随对象回收）
    if (i % 7 == 0) {
      (void)msObjStringSlice(&vm, s, false, 0, true, 1, true, 2, &(bool){false});
    }
    size_t now = gcHeapBytes(&vm);
    if (now > maxSeen) maxSeen = now;
  }
  EXPECT(ctx, maxSeen < 65536 + 4096);
  free(vm.stack);
  vm.stack = NULL;
  vm.stackTop = 0;
  msVmFree(&vm);
#ifdef _MSC_VER
  _putenv("MS_GC_HEAP_MAX=");
#else
  unsetenv("MS_GC_HEAP_MAX");
#endif
}
```

- [ ] **Step 13.6: Debug 全量 + ASan 全量** — `cmake --build build && ctest --test-dir build` 全绿；`cmake --preset asan && cmake --build build && ctest --test-dir build` 全绿（两 preset 共用 `build/`——仓库规则单一构建目录；跑完切回 `cmake --preset debug && cmake --build build`。Windows LSan 限制见 T012 备注，UAF/越界仍受控）。

- [ ] **Step 13.7: 提交** — `git commit -am "✨ test(string): fixtures, GC stress and ASan gate for T013"`

---

### Task 14: 收尾 — 文档状态 + 全量验证

**Files:**
- Modify: `docs/tasks/T013-string-object.md`（状态 ✅、DoD 勾选、备注补 unicode_case 行数与 D6/D9 决定）
- Modify: `docs/tasks/README.md`（T013 行 ✅）

- [ ] **Step 14.1: DoD 复核** — 逐项核对任务书 §4：02 §4/10 §2 用例（fixture+单测）、rune 语义（index_slice）、len/charLen/chars 自洽（Task 12）、intern 指针相等（Task 4）、插值（Task 11/13）、GC 压力 ASan（Task 13）。
- [ ] **Step 14.2: 文档更新** — 任务文档头部 `状态: 已完成`、DoD 全勾、§6 备注追加："unicode_case.h 上/下映射 N1/N2 条（生成脚本统计）"；"最小 list 桥落地 D6（T014 完成）"；"isDigit/isSpace 非 ASCII 暂 false（T034 复核）"。README 表 T013 行改 ✅。
- [ ] **Step 14.3: 终验** — `ctest --test-dir build` 全绿（99+新增项）；`git status` 干净；`git log --oneline` 检视提交序列。
- [ ] **Step 14.4: 提交** — `git commit -am "📝 docs(tasks): mark T013 done"`
- [ ] **Step 14.5: 分支收束** — 与用户确认后 merge 回 main（finishing-a-development-branch 流程）。

---

## 自查记录（Self-Review）

- 规格覆盖：02 §4（Task 3/6/10）、10 §2 全方法（Task 8/9）、intern/DoD（Task 4）、插值（Task 11）、GC（Task 13）、转义解码（Task 1/5）、原始串区分（Task 5 D3）、chars/bytes（Task 12）、错误形态（Task 6/10 fixtures）。间隙：无。
- 类型一致性：msValueFromConst(vm,...) 签名变更贯穿 Task 5；MsMethodRef 形态在 Task 8 定义、Task 9 沿用；msObjStringSlice 参数序 (hasA,a,hasB,b,hasC,c) 贯穿 Task 3/9/10。
- 已知风险：FORMAT_STRING/GET_INDEX 的 GC 纪律（peek-后-弹）在 Task 8/10/11 三处显式标注；test_gc_basic 的 MS_OBJ_ITERATOR markFn 覆盖纪律 Task 12 注明。




