# T003 Token 定义与诊断基础设施 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付 Scanner/Parser/Binder 共用的前端基座：`MsTokenType` 枚举 + 37 关键字表、`MsDiagnostic`/`MsDiagSink` 诊断结构与 `msDiagFormat` 格式化。

**Architecture:** 纯数据结构 + 查表 + 格式化，零动态分配（结构一律借用源缓冲指针）。`src/scanner/token.{h,c}` 编入既有 `ms_scanner` 静态库；单测挂到 T002 的 `ms_add_test` 框架（扩展其支持追加链接库）。

**Tech Stack:** C11（MSVC /W4 /WX 与 GCC/Clang -Wall -Wextra -Werror -Wshadow 双门禁）、CMake ≥ 3.16、CTest。

**规范输入:** `docs/tasks/T003-token-diagnostic.md`（含测试表）、`docs/language/01-lexical.md` §2 §4 §8、`docs/language/20-architecture.md` §1、`docs/language/28-style-c.md`。

**工作目录:** worktree `C:\Workspace\Repositories\study\.worktrees\t003-token-diagnostic\aicode\mslang-cc`（分支 `t003-token-diagnostic`，基线 ctest 4/4 绿）。

**约定:** 列号按 rune 计（1-based，非字节列）；诊断输出 `file:line:col: error[MSE%04u]: message` + 源码行 + `^` 指示（`^` 数 = len，最小 1）；`MS_DIAG_MAX = 20`，超出仅累计 `overflow`。

---

## 文件结构

| 文件 | 动作 | 职责 |
|------|------|------|
| `src/scanner/token.h` | 新增 | Token/诊断全部类型与 3 个公共函数声明（自包含头） |
| `src/scanner/token.c` | 新增 | 关键字表 + 查找、诊断收集、格式化（含 2 个 static 辅助） |
| `tests/unit/test_token_diag.c` | 新增 | TDD 三循环的单测（关键字号 / 收集器 / 格式化） |
| `CMakeLists.txt` | 修改 | `ms_scanner` 库追加 `token.c` |
| `cmake/Tests.cmake` | 修改 | `ms_add_test` 支持 `${ARGN}` 追加链接库 |
| `tests/CMakeLists.txt` | 修改 | 注册 `ms_add_test(token_diag ms_scanner)` |
| `docs/tasks/T003-token-diagnostic.md` | 修改 | DoD 勾选 + 状态 ✅（收尾） |
| `docs/tasks/README.md` | 修改 | T003 状态 🚧 → ✅（收尾） |

TDD 循环划分（每循环 RED → 验证失败 → GREEN → 验证通过 → commit）：

- 循环 1：关键字查找（stub 返回 `TK_ID` → 表实现）
- 循环 2：诊断收集器（stub 空操作 → 计数实现）
- 循环 3：诊断格式化（stub 空操作 → 格式化实现）

---

### Task 1: 脚手架 + 循环 1 RED（关键字测试失败）

**Files:**
- Create: `src/scanner/token.h`、`src/scanner/token.c`、`tests/unit/test_token_diag.c`
- Modify: `CMakeLists.txt`、`cmake/Tests.cmake`、`tests/CMakeLists.txt`

- [ ] **Step 1.1: 写 token.h（完整类型契约）**

```c
// Scanner/Parser/Binder 共用前端基座：Token、关键字、诊断（T003）
// 结构一律借用指针（源缓冲由调用方持有），前端零分配；col/len 按 rune 计（1-based）
// 错误码段：01 词法 MSE0xxx、03/04 语法 MSE1xxx、02 类型/运行时 MSE2xxx，后续任务沿用
#ifndef INCLUDE_MS_SCANNER_TOKEN_H_
#define INCLUDE_MS_SCANNER_TOKEN_H_

#include <stddef.h>
#include <stdint.h>

// 37 保留字单列（01-lexical.md §2）+ 标识符/字面量 + 全部运算符标点（01 §4）+ EOF
typedef enum MsTokenType {
  TK_EOF,
  TK_ID,
  TK_INT,
  TK_FLOAT,
  TK_BIGINT,
  TK_STRING,
  TK_RUNE,
  TK_FUNC,
  TK_VAR,
  TK_CONST,
  TK_IF,
  TK_ELSE,
  TK_FOR,
  TK_WHILE,
  TK_SWITCH,
  TK_CASE,
  TK_DEFAULT,
  TK_BREAK,
  TK_CONTINUE,
  TK_RETURN,
  TK_CLASS,
  TK_TRAIT,
  TK_THIS,
  TK_TRUE,
  TK_FALSE,
  TK_NIL,
  TK_AND,
  TK_OR,
  TK_NOT,
  TK_IN,
  TK_IS,
  TK_TRY,
  TK_CATCH,
  TK_FINALLY,
  TK_THROW,
  TK_DEFER,
  TK_GO,
  TK_CHAN,
  TK_ASYNC,
  TK_AWAIT,
  TK_IMPORT,
  TK_FROM,
  TK_AS,
  TK_YIELD,
  TK_PLUS,
  TK_MINUS,
  TK_STAR,
  TK_SLASH,
  TK_PERCENT,
  TK_STAR_STAR,
  TK_EQ_EQ,
  TK_BANG_EQ,
  TK_LT,
  TK_LT_EQ,
  TK_GT,
  TK_GT_EQ,
  TK_AMP_AMP,
  TK_PIPE_PIPE,
  TK_BANG,
  TK_AMP,
  TK_PIPE,
  TK_CARET,
  TK_LT_LT,
  TK_GT_GT,
  TK_TILDE,
  TK_EQ,
  TK_PLUS_EQ,
  TK_MINUS_EQ,
  TK_STAR_EQ,
  TK_SLASH_EQ,
  TK_PERCENT_EQ,
  TK_STAR_STAR_EQ,
  TK_AMP_EQ,
  TK_PIPE_EQ,
  TK_CARET_EQ,
  TK_LT_LT_EQ,
  TK_GT_GT_EQ,
  TK_COLON_EQ,
  TK_ARROW,
  TK_LARROW,
  TK_QUESTION,
  TK_COLON,
  TK_DOT,
  TK_COMMA,
  TK_SEMICOLON,
  TK_LPAREN,
  TK_RPAREN,
  TK_LBRACKET,
  TK_RBRACKET,
  TK_LBRACE,
  TK_RBRACE,
} MsTokenType;

typedef struct MsToken {
  MsTokenType type;
  const char* start;  // 源内指针（源缓冲由调用方持有）
  int32_t len;        // 字节长
  const char* file;   // 文件路径（intern）
  int32_t line, col;  // 1-based，col 按 rune 计
  int32_t offset;     // 字节偏移
} MsToken;

typedef enum MsDiagLevel {
  MS_DIAG_ERROR,
  MS_DIAG_WARNING,
} MsDiagLevel;

// 一次编译最多报 20 条，超出仅计数（20-architecture.md §1）
#define MS_DIAG_MAX 20

typedef struct MsDiagnostic {
  MsDiagLevel level;
  uint32_t code;  // MSExxxx 的数字部分
  const char* file;
  int32_t line, col, len;
  const char* message;
} MsDiagnostic;

typedef struct MsDiagSink {
  MsDiagnostic items[MS_DIAG_MAX];
  int32_t count, overflow;
} MsDiagSink;

// 关键字大小写敏感精确匹配（输入不必 NUL 结尾）；未命中返回 TK_ID
MsTokenType msKeywordLookup(const char* start, int32_t len);

// 收集一条诊断；超过 MS_DIAG_MAX 丢弃并累计 overflow
void msDiagAdd(MsDiagSink* sink, const MsDiagnostic* diag);

// 格式化到调用方缓冲：file:line:col: error[MSE%04u]: message + 源码行 + ^ 指示
// source 为 NULL 或行越界时仅输出头部行；容量不足时截断（保持 NUL 结尾）
void msDiagFormat(const MsDiagnostic* diag, const char* source, char* out, size_t outCap);

#endif
```

- [ ] **Step 1.2: 写 token.c stub（可编译、无实现逻辑）**

```c
// 关键字表与诊断格式化（T003）；查表零分配，格式化写入调用方缓冲
#include "scanner/token.h"

// TDD RED 占位：循环 1-3 依次以真实实现替换
MsTokenType msKeywordLookup(const char* start, int32_t len) {
  (void)start;
  (void)len;
  return TK_ID;
}

void msDiagAdd(MsDiagSink* sink, const MsDiagnostic* diag) {
  (void)sink;
  (void)diag;
}

void msDiagFormat(const MsDiagnostic* diag, const char* source, char* out, size_t outCap) {
  (void)diag;
  (void)source;
  (void)out;
  (void)outCap;
}
```

- [ ] **Step 1.3: 写 test_token_diag.c（仅循环 1 用例）**

```c
// T003：关键字表 / 诊断收集器 / 诊断格式化（col 按 rune 计，^ 对齐 UTF-8 安全）
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner/token.h"
#include "test_harness.h"

TEST(keywordLookupHits) {
  EXPECT_EQ(ctx, TK_FUNC, msKeywordLookup("func", 4));
  EXPECT_EQ(ctx, TK_TRAIT, msKeywordLookup("trait", 5));
  EXPECT_EQ(ctx, TK_AWAIT, msKeywordLookup("await", 5));
  EXPECT_EQ(ctx, TK_NOT, msKeywordLookup("not", 3));
  EXPECT_EQ(ctx, TK_YIELD, msKeywordLookup("yield", 5));
}

TEST(keywordLookupNonKeywords) {
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("type", 4));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("struct", 6));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("package", 7));
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("Func", 4));   // 大小写敏感
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("变量", 6));   // 非 ASCII 标识符（6 字节）
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("funcs", 5));  // 前缀更长
  EXPECT_EQ(ctx, TK_ID, msKeywordLookup("fu", 2));     // 前缀更短
}

int main(void) {
  TestCtx ctx = {0};
  keywordLookupHits(&ctx);
  keywordLookupNonKeywords(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
```

- [ ] **Step 1.4: 构建接线（3 处 CMake 修改）**

`CMakeLists.txt` 在 `foreach(...)` 循环块之后（`add_executable(ms tools/ms.c)` 之前）插入：

```cmake
# scanner：占位循环之外追加 T003 起的实际源文件（后续任务按目录继续 target_sources）
target_sources(ms_scanner PRIVATE src/scanner/token.c)
```

`cmake/Tests.cmake` 的 `ms_add_test` 函数体中 `target_link_libraries` 一行改为（其余不动）：

```cmake
  target_link_libraries(test_${name} PRIVATE ms_common ms_plat ${ARGN})
```

`tests/CMakeLists.txt` 追加一行：

```cmake
ms_add_test(token_diag ms_scanner)
```

- [ ] **Step 1.5: 构建并验证 RED**

```powershell
cmake --build build --config Debug --target test_token_diag
ctest --test-dir build -C Debug -R token_diag --output-on-failure
```

预期：构建成功；测试 **Failed**，输出含 `keywordLookupHits` 的 `expected 4 got 1` 风格失败（stub 恒返 `TK_ID`），`keywordLookupNonKeywords` 通过。

---

### Task 2: 循环 1 GREEN（关键字表实现）

**Files:**
- Modify: `src/scanner/token.c`

- [ ] **Step 2.1: 用真实实现替换 msKeywordLookup stub**

在 `#include` 区之后、stub 之前加入表与实现，并删除 stub 版 `msKeywordLookup`：

```c
typedef struct MsKeywordEntry {
  const char* name;
  MsTokenType type;
} MsKeywordEntry;

// 01-lexical.md §2 恰好 37 保留字（字典序）；struct/interface/package/type 不是关键字
static const MsKeywordEntry kKeywords[] = {
    {"and", TK_AND},          {"as", TK_AS},           {"async", TK_ASYNC},
    {"await", TK_AWAIT},      {"break", TK_BREAK},     {"case", TK_CASE},
    {"catch", TK_CATCH},      {"chan", TK_CHAN},       {"class", TK_CLASS},
    {"const", TK_CONST},      {"continue", TK_CONTINUE}, {"default", TK_DEFAULT},
    {"defer", TK_DEFER},      {"else", TK_ELSE},       {"false", TK_FALSE},
    {"finally", TK_FINALLY},  {"for", TK_FOR},         {"from", TK_FROM},
    {"func", TK_FUNC},        {"go", TK_GO},           {"if", TK_IF},
    {"import", TK_IMPORT},    {"in", TK_IN},           {"is", TK_IS},
    {"nil", TK_NIL},          {"not", TK_NOT},         {"or", TK_OR},
    {"return", TK_RETURN},    {"switch", TK_SWITCH},   {"this", TK_THIS},
    {"throw", TK_THROW},      {"trait", TK_TRAIT},     {"true", TK_TRUE},
    {"try", TK_TRY},          {"var", TK_VAR},         {"while", TK_WHILE},
    {"yield", TK_YIELD},
};

_Static_assert(sizeof(kKeywords) / sizeof(kKeywords[0]) == 37, "01-lexical.md §2 fixes exactly 37 keywords");

MsTokenType msKeywordLookup(const char* start, int32_t len) {
  for (size_t i = 0; i < sizeof(kKeywords) / sizeof(kKeywords[0]); i++) {
    const char* name = kKeywords[i].name;
    if ((int32_t)strlen(name) == len && memcmp(start, name, (size_t)len) == 0) {
      return kKeywords[i].type;
    }
  }
  return TK_ID;
}
```

- [ ] **Step 2.2: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_token_diag
ctest --test-dir build -C Debug -R token_diag --output-on-failure
```

预期：**Passed**。

- [ ] **Step 2.3: Commit**

```powershell
git add src/scanner/token.h src/scanner/token.c tests/unit/test_token_diag.c CMakeLists.txt cmake/Tests.cmake tests/CMakeLists.txt
git commit -m "✨ feat(scanner): add token types and keyword table"
```

---

### Task 3: 循环 2 RED→GREEN（诊断收集器）

**Files:**
- Modify: `tests/unit/test_token_diag.c`、`src/scanner/token.c`

- [ ] **Step 3.1: 追加失败测试（test_token_diag.c）**

在 `keywordLookupNonKeywords` 之后新增：

```c
TEST(diagSinkStoresAndOverflows) {
  MsDiagSink sink = {0};
  for (int i = 0; i < 25; i++) {
    MsDiagnostic diag = {MS_DIAG_ERROR, (uint32_t)i, "f.ms", 1, 1, 1, "m"};
    msDiagAdd(&sink, &diag);
  }
  EXPECT_EQ(ctx, 20, sink.count);
  EXPECT_EQ(ctx, 5, sink.overflow);
  EXPECT_EQ(ctx, 0, sink.items[0].code);
  EXPECT_EQ(ctx, 19, sink.items[19].code);
}
```

并在 `main` 中 `keywordLookupNonKeywords(&ctx);` 之后注册 `diagSinkStoresAndOverflows(&ctx);`。

- [ ] **Step 3.2: 验证 RED**

```powershell
cmake --build build --config Debug --target test_token_diag
ctest --test-dir build -C Debug -R token_diag --output-on-failure
```

预期：**Failed**，`sink.count expected 20 got 0`（stub 空操作）。

- [ ] **Step 3.3: 实现 msDiagAdd（替换 stub）**

```c
void msDiagAdd(MsDiagSink* sink, const MsDiagnostic* diag) {
  if (sink->count >= MS_DIAG_MAX) {
    sink->overflow++;
    return;
  }
  sink->items[sink->count++] = *diag;
}
```

- [ ] **Step 3.4: 验证 GREEN**

同 Step 3.2 命令。预期：**Passed**。

- [ ] **Step 3.5: Commit**

```powershell
git add tests/unit/test_token_diag.c src/scanner/token.c
git commit -m "✨ feat(scanner): add diagnostic sink with overflow counting"
```

---

### Task 4: 循环 3 RED→GREEN（诊断格式化）

**Files:**
- Modify: `tests/unit/test_token_diag.c`、`src/scanner/token.c`

- [ ] **Step 4.1: 追加失败测试（test_token_diag.c）**

在 `diagSinkStoresAndOverflows` 之后新增三个 TEST，并全部注册进 `main`：

```c
TEST(diagFormatMatchesDesign) {
  MsDiagnostic diag = {MS_DIAG_ERROR, 1, "f.ms", 2, 5, 3, "msg"};
  const char* source = "line one\nlet msg here ok\nline three\n";
  char out[256];
  msDiagFormat(&diag, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "f.ms:2:5: error[MSE0001]: msg\nlet msg here ok\n    ^^^\n", out);
}

TEST(diagFormatMultibyteColIsRuneBased) {
  // "a 中 b"：中占 3 字节，b 的 rune 列 = 4；^ 缩进按 rune 列而非字节列
  MsDiagnostic diag = {MS_DIAG_ERROR, 2, "f.ms", 2, 4, 1, "bad"};
  const char* source = "x = 1\na 中 b\n";
  char out[256];
  msDiagFormat(&diag, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "f.ms:2:4: error[MSE0002]: bad\na 中 b\n   ^\n", out);
}

TEST(diagFormatEdgeCases) {
  const char* source = "first line\n";
  char out[256];
  // len<1 时 ^ 最少 1 个；warning 级别小写
  MsDiagnostic zeroLen = {MS_DIAG_WARNING, 100, "w.ms", 1, 1, 0, "wmsg"};
  msDiagFormat(&zeroLen, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "w.ms:1:1: warning[MSE0100]: wmsg\nfirst line\n^\n", out);
  // 无源码：仅头部行
  MsDiagnostic noSrc = {MS_DIAG_ERROR, 3, "s.ms", 9, 9, 9, "no src"};
  msDiagFormat(&noSrc, NULL, out, sizeof(out));
  EXPECT_STREQ(ctx, "s.ms:9:9: error[MSE0003]: no src\n", out);
  // 行号越界：仅头部行
  msDiagFormat(&noSrc, source, out, sizeof(out));
  EXPECT_STREQ(ctx, "s.ms:9:9: error[MSE0003]: no src\n", out);
  // 缓冲不足：截断且 NUL 结尾、头部前缀保留
  MsDiagnostic diag = {MS_DIAG_ERROR, 4, "f.ms", 1, 1, 1, "trunc"};
  char tiny[8];
  msDiagFormat(&diag, source, tiny, sizeof(tiny));
  EXPECT(ctx, strlen(tiny) < sizeof(tiny));
  EXPECT(ctx, strncmp(tiny, "f.ms:1", 6) == 0);
}
```

- [ ] **Step 4.2: 验证 RED**

```powershell
cmake --build build --config Debug --target test_token_diag
ctest --test-dir build -C Debug -R token_diag --output-on-failure
```

预期：**Failed**，格式化期望字符串与空串 `""` 不符（stub 未写入）。

- [ ] **Step 4.3: 实现 msDiagFormat（替换 stub，含 2 个 static 辅助）**

```c
// 追加格式化片段到 out，容量不足时截断（保持 NUL 结尾）；调用方保证 *pos < outCap
static void diagAppend(char* out, size_t outCap, size_t* pos, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(out + *pos, outCap - *pos, fmt, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  size_t advance = (size_t)written;
  if (advance > outCap - *pos - 1) {
    advance = outCap - *pos - 1;
  }
  *pos += advance;
}

// 定位 lineNum（1-based）所在行；返回行首指针并写 end 为行尾（不含换行），越界返回 NULL
static const char* diagFindLine(const char* source, int32_t lineNum, const char** end) {
  const char* line = source;
  for (int32_t i = 1; i < lineNum; i++) {
    line = strchr(line, '\n');
    if (line == NULL) {
      return NULL;
    }
    line++;
  }
  const char* lineEnd = strchr(line, '\n');
  *end = lineEnd != NULL ? lineEnd : line + strlen(line);
  return line;
}

void msDiagFormat(const MsDiagnostic* diag, const char* source, char* out, size_t outCap) {
  if (outCap == 0) {
    return;
  }
  const char* levelName = diag->level == MS_DIAG_WARNING ? "warning" : "error";
  size_t pos = 0;
  diagAppend(out, outCap, &pos, "%s:%d:%d: %s[MSE%04u]: %s\n", diag->file, diag->line, diag->col,
             levelName, diag->code, diag->message);
  if (source == NULL) {
    return;
  }
  const char* lineEnd = NULL;
  const char* line = diagFindLine(source, diag->line, &lineEnd);
  if (line == NULL) {
    return;
  }
  diagAppend(out, outCap, &pos, "%.*s\n", (int)(lineEnd - line), line);
  int32_t indent = diag->col > 1 ? diag->col - 1 : 0;
  int32_t carets = diag->len > 0 ? diag->len : 1;
  for (int32_t i = 0; i < indent; i++) {
    diagAppend(out, outCap, &pos, " ");
  }
  for (int32_t i = 0; i < carets; i++) {
    diagAppend(out, outCap, &pos, "^");
  }
  diagAppend(out, outCap, &pos, "\n");
}
```

token.c 的 include 区需补 `#include <stdarg.h>`（stdio/string 由格式化使用，一并补上）。

- [ ] **Step 4.4: 验证 GREEN**

同 Step 4.2 命令。预期：**Passed**。

- [ ] **Step 4.5: Commit**

```powershell
git add tests/unit/test_token_diag.c src/scanner/token.c
git commit -m "✨ feat(scanner): add diagnostic formatter with UTF-8-safe caret"
```

---

### Task 5: 全量验证 + DoD 收尾

**Files:**
- Modify: `docs/tasks/T003-token-diagnostic.md`、`docs/tasks/README.md`

- [ ] **Step 5.1: 全量构建 + 全部测试**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

预期：全部 **Passed**（原 4 项 + token_diag）。

- [ ] **Step 5.2: DoD 核对（T003 §4）**

- 关键字表恰好 37 项（`_Static_assert` 编译期锁定），大小写敏感（`Func` 用例）
- `msDiagFormat` 输出与设计一致（`^^^` 对齐、rune 列用例）
- 超 20 条后 overflow 计数正确（count==20、overflow==5 用例）
- 单测全绿；无内存分配依赖（借用指针，无 malloc）

勾选 T003 文档 DoD 四项，状态改 `已完成`；README 总表 T003 改 `✅`。

- [ ] **Step 5.3: Commit**

```powershell
git add docs/tasks/T003-token-diagnostic.md docs/tasks/README.md
git commit -m "✅ docs(tasks): mark T003 done"
```

---

## 风险

- MSVC /W4 /WX 与 GCC 双门禁：新代码避免 size_t/int32_t 隐式窄化（显式 cast），`%.*s` 用 `(int)` 强转 ptrdiff。
- 列号语义已按任务文档锁定为 rune 列，多字节用例先行防回退。
- `EXPECT_EQ` 实参二次求值：测试内只传纯函数调用与常量。
