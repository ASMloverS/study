# T004 Scanner 核心实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付 Scanner 主循环：标识符（Unicode）、整数（十/十六/八/二/下划线/`n` 后缀/溢出升 bigint）、浮点、运算符最长匹配、注释、换行归一化与 ASI 四细则；含 `ms --dump-tokens` 调试开关与 5 个 conformance 用例。字符串/rune/插值归 T005。

**Architecture:** 逐 token 拉取式扫描器（`scannerNext`），借用源缓冲指针零分配；Unicode 类别走生成表（L*/N*）+ ASCII 快路径；ASI 由"上一 token 可结束语句"状态 + `parenDepth` 在换行处回补 `;`。token 类型名经 X-macro 单一事实源导出。

**Tech Stack:** C11（MSVC /W4 /WX 双门禁之一）、CMake ≥ 3.16、CTest、Python 3.14（生成脚本，仅标准库 unicodedata）。

**规范输入:** `docs/tasks/T004-scanner-core.md`（含测试表）、`docs/language/01-lexical.md` §1 §3.1 §3.2 §4 §5 §6 §7 §9、`docs/language/28-style-c.md`。

**工作目录:** worktree `C:\Workspace\Repositories\study\.worktrees\t004-scanner-core\aicode\mslang-cc`（分支 `t004-scanner-core`，基线 ctest 5/5 绿）。

**已批准设计决策**（用户 2026-10-09 批准）:

1. `.5`（`.` 紧跟数字）→ MSE0006 + FLOAT 恢复 token；`5.`（数字+`.` 后非数字且非 ID 起始）→ MSE0005 + INT+DOT；`.` 后跟 ID 字符（`1.toString()`）→ 静默 INT+DOT（03 文法成员访问豁免）。
2. 错误码：0001 数字后跟 ID 字符、0002 指数缺数字、0003 溢出升 BIGINT、0004 下划线错位、0005 `5.`、0006 `.5`、0007 非法字符（含 `"` `'` `` ` ``，T005 接管）、0008 块注释、0009 基前缀缺数字、0010 数字越进制。
3. ASI：`;` 定位在换行符处（line:col，len=0）；`<-`/`return`/`throw` 行为由触发集合自然导出；**EOF 不补 `;`**（01 §7 字面）。
4. `--dump-tokens` 每行 `line:col TYPE[ 文本]`（TYPE=枚举名去 `TK_`，含 EOF 行）；诊断走 stderr，有错退出码 1；`MsTokenType` 改 X-macro 支撑 `msTokenTypeName`。
5. test_runner 扩展：可选 `.args`（空白分隔参数插在 ms 与脚本间）；`.err` 从首行前缀改为 **stderr 子串匹配**（路径无关）；同步更新 conformance README。
6. `0B` 大写不识别（spec 仅 `0b`，落到 0001）；`///` 在 Scanner 记 `docLine`；unicode 表生成物入库 + CMake 手动目标 `gen_unicode_table`。

---

## 文件结构

| 文件 | 动作 | 职责 |
|------|------|------|
| `src/scanner/token.h` | 修改 | `MsTokenType` 枚举改 X-macro 形式（值序不变），声明 `msTokenTypeName` |
| `src/scanner/token.c` | 修改 | 实现 `msTokenTypeName` |
| `src/scanner/scanner.h` | 新增 | Scanner 结构、`scannerInit`/`scannerNext`、MSE0xxx 错误码枚举 |
| `src/scanner/scanner.c` | 覆盖 | 扫描主循环（utf8 解码/注释/标识符/数字/运算符/ASI） |
| `src/scanner/unicode_table.h` | 新增 | 生成物：L* 与 L*\|N* 区间表 + 二分 + ASCII 快路径 |
| `tools/gen_unicode_table.py` | 新增 | 生成上述头文件（unicodedata，标准库） |
| `tools/ms.c` | 修改 | `--dump-tokens` 子命令（读文件 + LF 归一化 + dump + 诊断） |
| `tools/test_runner.c` | 修改 | `splitArgs` + `.args` 注入 + `.err` 子串匹配 |
| `tests/unit/test_scanner_core.c` | 新增 | 单测（按任务递增） |
| `tests/unit/test_runner_selftest.c` | 修改 | `splitArgs` 自测 |
| `CMakeLists.txt` | 修改 | `gen_unicode_table` 自定义目标 |
| `tests/CMakeLists.txt` | 修改 | 注册 `ms_add_test(scanner_core ms_scanner)` |
| `tests/conformance/01-lex-{01,02,04,07,09}-*.ms` + `.args`/`.out`/`.err`/`.code` | 新增 | conformance 金集 |
| `tests/conformance/README.md` | 修改 | 契约补充 `.args`/`.err` 子串 |
| `docs/tasks/T004-scanner-core.md`、`docs/tasks/README.md` | 修改 | 状态流转与 DoD 勾选 |

TDD 循环划分（每循环 RED → 验证失败 → GREEN → 验证通过 → commit）:

- 循环 1（Task 1）：token 类型名 X-macro
- 循环 2（Task 2）：骨架 + 注释 + 标识符/关键字 + Unicode 表
- 循环 3（Task 3）：数字（int/bigint/float + 全部数字诊断）
- 循环 4（Task 4）：运算符最长匹配
- 循环 5（Task 5）：ASI 四细则 + parenDepth
- 循环 6（Task 6）：ms --dump-tokens + runner 扩展 + conformance
- 收尾（Task 7）：全量验证 + DoD + 状态 ✅

---

### Task 1: token 类型名 X-macro（msTokenTypeName）

**Files:**
- Create: `tests/unit/test_scanner_core.c`
- Modify: `src/scanner/token.h`、`src/scanner/token.c`、`tests/CMakeLists.txt`

- [ ] **Step 1.1: RED——建测试（仅含 token.h 依赖）并注册**

`tests/unit/test_scanner_core.c`：

```c
// T004：Scanner 核心单测（token 名表/标识符/数字/运算符/ASI/注释）
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "scanner/token.h"
#include "test_harness.h"

TEST(tokenTypeNameMatchesEnum) {
  EXPECT_STREQ(ctx, "ID", msTokenTypeName(TK_ID));
  EXPECT_STREQ(ctx, "STAR_STAR_EQ", msTokenTypeName(TK_STAR_STAR_EQ));
  EXPECT_STREQ(ctx, "SEMICOLON", msTokenTypeName(TK_SEMICOLON));
  EXPECT_STREQ(ctx, "LARROW", msTokenTypeName(TK_LARROW));
  EXPECT_STREQ(ctx, "EOF", msTokenTypeName(TK_EOF));
}

int main(void) {
  TestCtx ctx = {0};
  tokenTypeNameMatchesEnum(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
```

`tests/CMakeLists.txt` 追加一行：

```cmake
ms_add_test(scanner_core ms_scanner)
```

- [ ] **Step 1.2: 验证 RED**

```powershell
cmake --build build --config Debug --target test_scanner_core
```

预期：编译失败，`msTokenTypeName` 未声明（C2065/C3861）。

- [ ] **Step 1.3: GREEN——token.h 枚举改 X-macro（值序不变）+ token.c 实现**

`token.h` 中整个 `typedef enum MsTokenType {...}` 替换为：

```c
// X-macro 单一事实源：token.h 枚举与 token.c 名表共用（新增类型只改此清单）
#define MS_TOKEN_LIST                       \
  X(EOF)                                    \
  X(ID)                                     \
  X(INT)                                    \
  X(FLOAT)                                  \
  X(BIGINT)                                 \
  X(STRING)                                 \
  X(RUNE)                                   \
  X(FUNC)                                   \
  X(VAR)                                    \
  X(CONST)                                  \
  X(IF)                                     \
  X(ELSE)                                   \
  X(FOR)                                    \
  X(WHILE)                                  \
  X(SWITCH)                                 \
  X(CASE)                                   \
  X(DEFAULT)                                \
  X(BREAK)                                  \
  X(CONTINUE)                               \
  X(RETURN)                                 \
  X(CLASS)                                  \
  X(TRAIT)                                  \
  X(THIS)                                   \
  X(TRUE)                                   \
  X(FALSE)                                  \
  X(NIL)                                    \
  X(AND)                                    \
  X(OR)                                     \
  X(NOT)                                    \
  X(IN)                                     \
  X(IS)                                     \
  X(TRY)                                    \
  X(CATCH)                                  \
  X(FINALLY)                                \
  X(THROW)                                  \
  X(DEFER)                                  \
  X(GO)                                     \
  X(CHAN)                                   \
  X(ASYNC)                                  \
  X(AWAIT)                                  \
  X(IMPORT)                                 \
  X(FROM)                                   \
  X(AS)                                     \
  X(YIELD)                                  \
  X(PLUS)                                   \
  X(MINUS)                                  \
  X(STAR)                                   \
  X(SLASH)                                  \
  X(PERCENT)                                \
  X(STAR_STAR)                              \
  X(EQ_EQ)                                  \
  X(BANG_EQ)                                \
  X(LT)                                     \
  X(LT_EQ)                                  \
  X(GT)                                     \
  X(GT_EQ)                                  \
  X(AMP_AMP)                                \
  X(PIPE_PIPE)                              \
  X(BANG)                                   \
  X(AMP)                                    \
  X(PIPE)                                   \
  X(CARET)                                  \
  X(LT_LT)                                  \
  X(GT_GT)                                  \
  X(TILDE)                                  \
  X(EQ)                                     \
  X(PLUS_EQ)                                \
  X(MINUS_EQ)                               \
  X(STAR_EQ)                                \
  X(SLASH_EQ)                               \
  X(PERCENT_EQ)                             \
  X(STAR_STAR_EQ)                           \
  X(AMP_EQ)                                 \
  X(PIPE_EQ)                                \
  X(CARET_EQ)                               \
  X(LT_LT_EQ)                               \
  X(GT_GT_EQ)                               \
  X(COLON_EQ)                               \
  X(ARROW)                                  \
  X(LARROW)                                 \
  X(QUESTION)                               \
  X(COLON)                                  \
  X(DOT)                                    \
  X(COMMA)                                  \
  X(SEMICOLON)                              \
  X(LPAREN)                                 \
  X(RPAREN)                                 \
  X(LBRACKET)                               \
  X(RBRACKET)                               \
  X(LBRACE)                                 \
  X(RBRACE)

typedef enum MsTokenType {
#define X(name) TK_##name,
  MS_TOKEN_LIST
#undef X
} MsTokenType;
```

枚举原注释行保留在 `#define` 之上。`token.h` 函数声明区追加：

```c
// 类型名（枚举名去 TK_，如 "ID"/"STAR_STAR_EQ"）；非法值返回 "?"
const char* msTokenTypeName(MsTokenType type);
```

`token.c` 文件末尾追加：

```c
const char* msTokenTypeName(MsTokenType type) {
  switch (type) {
#define X(name)  \
  case TK_##name: \
    return #name;
    MS_TOKEN_LIST
#undef X
    default:
      return "?";
  }
}
```

- [ ] **Step 1.4: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_scanner_core test_token_diag
ctest --test-dir build -C Debug -R "scanner_core|token_diag" --output-on-failure
```

预期：两项均 **Passed**（值序未变，T003 用例不回归）。

- [ ] **Step 1.5: Commit**

```powershell
git add src/scanner/token.h src/scanner/token.c tests/unit/test_scanner_core.c tests/CMakeLists.txt
git commit -m "✨ feat(scanner): expose token type names via X-macro list"
```

---

### Task 2: Scanner 骨架 + 标识符/注释 + Unicode 表

**Files:**
- Create: `tools/gen_unicode_table.py`、`src/scanner/unicode_table.h`（生成）、`src/scanner/scanner.h`
- Modify: `src/scanner/scanner.c`（覆盖占位）、`CMakeLists.txt`、`tests/unit/test_scanner_core.c`

- [ ] **Step 2.1: 写 `tools/gen_unicode_table.py`（上半：区间收集与表发射）**

```python
#!/usr/bin/env python3
# 生成 src/scanner/unicode_table.h（T004）：ID_START = L*、ID_CONTINUE = L*|N* 的码点区间表
# 生成物入库；改脚本或升级 Python（unicodedata 随版本变化）后手动重跑：
#   cmake --build build --config Debug --target gen_unicode_table
import sys
import unicodedata
from pathlib import Path

MAX_CP = 0x10FFFF
LETTERS = ("Lu", "Ll", "Lt", "Lm", "Lo")
NUMBERS = ("Nd", "Nl", "No")


def collect_ranges(categories: tuple[str, ...]) -> list[tuple[int, int]]:
    """收集给定 Unicode 类别的连续码点区间（ASCII 段走 C 快路径，不进表）。"""
    wanted = frozenset(categories)
    ranges: list[tuple[int, int]] = []
    start = -1
    prev = -1
    for cp in range(0x80, MAX_CP + 1):
        if unicodedata.category(chr(cp)) in wanted:
            if start < 0:
                start = cp
            prev = cp
        elif start >= 0:
            ranges.append((start, prev))
            start = -1
    if start >= 0:
        ranges.append((start, prev))
    return ranges


def emit_table(name: str, ranges: list[tuple[int, int]]) -> list[str]:
    lines = [f"static const MsUnicodeRange {name}[] = {{"]
    for lo, hi in ranges:
        lines.append(f"    {{0x{lo:X}, 0x{hi:X}}},")
    lines.append("};")
    return lines
```

- [ ] **Step 2.2: 续写 `tools/gen_unicode_table.py`（下半：模板与入口）**

```python
HEADER = """\
// 自动生成（tools/gen_unicode_table.py，Unicode {version}）：请勿手改
// ID_START = L*、ID_CONTINUE = L*|N*（01-lexical.md §1；ASCII 走快路径不进表）
#ifndef INCLUDE_MS_SCANNER_UNICODE_TABLE_H_
#define INCLUDE_MS_SCANNER_UNICODE_TABLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MsUnicodeRange {{
  uint32_t lo, hi;
}} MsUnicodeRange;

"""

FOOTER = """\
// 区间表二分查找
static bool msUnicodeInRange(const MsUnicodeRange* ranges, size_t count, uint32_t cp) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < ranges[mid].lo) {
      hi = mid;
    } else if (cp > ranges[mid].hi) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

// ID_START：Unicode 字母或 '_'（ASCII 快路径）
static bool msUnicodeIdStart(uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
  }
  return msUnicodeInRange(MS_UNICODE_ID_START, sizeof(MS_UNICODE_ID_START) / sizeof(MS_UNICODE_ID_START[0]), cp);
}

// ID_CONTINUE：ID_START | Unicode 数字（ASCII 快路径）
static bool msUnicodeIdContinue(uint32_t cp) {
  if (cp < 0x80) {
    return msUnicodeIdStart(cp) || (cp >= '0' && cp <= '9');
  }
  return msUnicodeInRange(MS_UNICODE_ID_CONTINUE, sizeof(MS_UNICODE_ID_CONTINUE) / sizeof(MS_UNICODE_ID_CONTINUE[0]), cp);
}

#endif  // INCLUDE_MS_SCANNER_UNICODE_TABLE_H_
"""


def main() -> None:
    default_out = Path(__file__).resolve().parents[1] / "src" / "scanner" / "unicode_table.h"
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_out
    start_ranges = collect_ranges(LETTERS)
    continue_ranges = collect_ranges(LETTERS + NUMBERS)
    parts = [HEADER.format(version=unicodedata.unidata_version), "// L*：ID_START"]
    parts.extend(emit_table("MS_UNICODE_ID_START", start_ranges))
    parts.extend(["", "// L* | N*：ID_CONTINUE"])
    parts.extend(emit_table("MS_UNICODE_ID_CONTINUE", continue_ranges))
    parts.extend(["", FOOTER])
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out_path}: {len(start_ranges)} start ranges, {len(continue_ranges)} continue ranges")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2.3: CMake 自定义目标 + 生成头文件**

`CMakeLists.txt` 在 `target_sources(ms_scanner PRIVATE src/scanner/token.c)` 之后追加：

```cmake
# T004：Unicode 类别表再生成（生成物已入库；改脚本/升 Python 后手动构建本目标）
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(gen_unicode_table
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_unicode_table.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "regenerate src/scanner/unicode_table.h"
    VERBATIM)
endif()
```

生成（脚本自写文件，无 shell 重定向）：

```powershell
python tools\gen_unicode_table.py
```

预期：输出 `wrote src\scanner\unicode_table.h: N start ranges, M continue ranges`（N≈700+，M≈850+），文件为 ASCII、LF。

- [ ] **Step 2.4: 写 `src/scanner/scanner.h`（完整契约）**

```c
// Scanner 主循环（T004）：标识符/数字/运算符/ASI；字符串/rune/插值归 T005
// 前端零分配：一律借用源缓冲指针；源须已 LF 归一化（读取层完成，01-lexical.md）
#ifndef INCLUDE_MS_SCANNER_SCANNER_H_
#define INCLUDE_MS_SCANNER_SCANNER_H_

#include <stdbool.h>
#include <stdint.h>

#include "scanner/token.h"

// 词法错误码（MSE0xxx 段，01-lexical.md；诊断 code 字段取枚举值）
typedef enum MseLexCode {
  MSE_INT_TRAILING = 1,        // 数字后紧跟标识符字符（123abc / 0x1Fg）
  MSE_FLOAT_EXPONENT = 2,      // 指数缺数字（1e / 1e+）
  MSE_INT_OVERFLOW = 3,        // 整数超 int64，升 BIGINT 占位（T016 精确解析）
  MSE_UNDERSCORE = 4,          // 下划线分组错位（1_ / 1__0）
  MSE_FLOAT_TRAILING_DOT = 5,  // 5. 缺小数位（. 后非数字且非成员访问）
  MSE_FLOAT_LEADING_DOT = 6,   // .5 缺整数部
  MSE_UNEXPECTED_CHAR = 7,     // 非法字符（含 " ' `；字符串/rune 归 T005）
  MSE_BLOCK_COMMENT = 8,       // /* */ 块注释（01 §5 非法）
  MSE_BASE_PREFIX = 9,         // 基前缀后缺数字（0x / 0o）
  MSE_DIGIT_BASE = 10,         // 数字超出进制（0o8 / 0b2）
} MseLexCode;

typedef struct Scanner {
  const char* src;    // 已 LF 归一化的 UTF-8 源（调用方持有）
  int32_t len, pos;
  int32_t line, col;  // 下一未读 rune 的位置（1-based，col 按 rune 计）
  int32_t parenDepth; // ()[]{} 合计深度，>0 抑制 ASI（01 §7 细则 1）
  const char* file;
  MsDiagSink* diags;
  bool pendingAsi;    // 上一 token 可结束语句，换行且 depth==0 时回补 ;
  int32_t docLine;    // 最近一条 /// 文档注释行号（-1 无），供 ms doc（T042）
} Scanner;

// diags 由调用方持有（可为 NULL，仅免诊断场景）
void scannerInit(Scanner* s, const char* src, int32_t len, const char* file, MsDiagSink* diags);

// 产出下一 token（含 ASI 合成的 ;）；EOF 时写 TK_EOF 并返回 false
bool scannerNext(Scanner* s, MsToken* out);

#endif  // INCLUDE_MS_SCANNER_SCANNER_H_
```

- [ ] **Step 2.5: 覆盖 `src/scanner/scanner.c`（上半：UTF-8 与游标基座）**

```c
// Scanner 主循环（T004）：标识符/数字/运算符/ASI；字符串/rune/插值归 T005
#include "scanner/scanner.h"

#include "scanner/unicode_table.h"

// 解码 p 处一个 rune 并写回字节宽 width；非法/截断/代理区/超长序列按 1 字节返回 0xFFFD
static uint32_t utf8Decode(const char* p, int32_t avail, int32_t* width) {
  uint8_t b = (uint8_t)p[0];
  if (b < 0x80) {
    *width = 1;
    return b;
  }
  int32_t n = 0;
  uint32_t cp = 0;
  if ((b & 0xE0) == 0xC0 && avail >= 2) {
    n = 2;
    cp = b & 0x1F;
  } else if ((b & 0xF0) == 0xE0 && avail >= 3) {
    n = 3;
    cp = b & 0x0F;
  } else if ((b & 0xF8) == 0xF0 && avail >= 4) {
    n = 4;
    cp = b & 0x07;
  } else {
    *width = 1;
    return 0xFFFD;
  }
  for (int32_t i = 1; i < n; i++) {
    uint8_t cont = (uint8_t)p[i];
    if ((cont & 0xC0) != 0x80) {
      *width = 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    *width = 1;
    return 0xFFFD;
  }
  *width = n;
  return cp;
}

static uint8_t peek(const Scanner* s) {
  return s->pos < s->len ? (uint8_t)s->src[s->pos] : 0;
}

static uint8_t peekAt(const Scanner* s, int32_t off) {
  int32_t i = s->pos + off;
  return i < s->len ? (uint8_t)s->src[i] : 0;
}

static void addDiag(Scanner* s, uint32_t code, int32_t line, int32_t col, int32_t len, const char* message) {
  if (s->diags == NULL) {
    return;
  }
  MsDiagnostic diag = {MS_DIAG_ERROR, code, s->file, line, col, len, message};
  msDiagAdd(s->diags, &diag);
}

static void makeToken(const Scanner* s, MsToken* out, MsTokenType type, int32_t start, int32_t line, int32_t col) {
  out->type = type;
  out->start = s->src + start;
  out->len = s->pos - start;
  out->file = s->file;
  out->line = line;
  out->col = col;
  out->offset = start;
}

// 前进一个 rune：\n 计行重置列，其余列 +1（col 按 rune 计，tab 记 1 列）
static void advanceRune(Scanner* s) {
  if (s->pos >= s->len) {
    return;
  }
  int32_t width = 0;
  utf8Decode(s->src + s->pos, s->len - s->pos, &width);
  if (s->src[s->pos] == '\n') {
    s->line++;
    s->col = 1;
  } else {
    s->col++;
  }
  s->pos += width;
}
```

- [ ] **Step 2.6: 续写 `src/scanner/scanner.c`（下半：注释/标识符/主循环 v1）**

```c
// // 行注释：吃到行尾（不含 \n）；/// 文档注释透出行号供 ms doc（T042）
static void skipLineComment(Scanner* s) {
  if (peekAt(s, 2) == '/') {
    s->docLine = s->line;
  }
  do {
    advanceRune(s);
  } while (s->pos < s->len && s->src[s->pos] != '\n');
}

// /* */ 块注释非法（01 §5）：报 MSE 后跳到闭合或 EOF 恢复
static void skipBlockComment(Scanner* s) {
  addDiag(s, MSE_BLOCK_COMMENT, s->line, s->col, 2, "block comments are not allowed; use //");
  advanceRune(s);
  advanceRune(s);
  while (s->pos < s->len) {
    if (peek(s) == '*' && peekAt(s, 1) == '/') {
      advanceRune(s);
      advanceRune(s);
      return;
    }
    advanceRune(s);
  }
}

// 标识符：ID_START 起始，ID_CONTINUE 续行；命中关键字表改写类型
static void scanIdentifier(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  while (s->pos < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    bool ok = s->pos == start ? msUnicodeIdStart(cp) : msUnicodeIdContinue(cp);
    if (!ok) {
      break;
    }
    advanceRune(s);
  }
  makeToken(s, out, TK_ID, start, line, col);
  out->type = msKeywordLookup(out->start, out->len);
}

void scannerInit(Scanner* s, const char* src, int32_t len, const char* file, MsDiagSink* diags) {
  s->src = src;
  s->len = len;
  s->pos = 0;
  s->line = 1;
  s->col = 1;
  s->parenDepth = 0;
  s->file = file;
  s->diags = diags;
  s->pendingAsi = false;
  s->docLine = -1;
}

bool scannerNext(Scanner* s, MsToken* out) {
  for (;;) {
    uint8_t c = peek(s);
    if (c == 0) {
      makeToken(s, out, TK_EOF, s->pos, s->line, s->col);
      out->len = 0;
      return false;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advanceRune(s);
      continue;
    }
    if (c == '/' && peekAt(s, 1) == '/') {
      skipLineComment(s);
      continue;
    }
    if (c == '/' && peekAt(s, 1) == '*') {
      skipBlockComment(s);
      continue;
    }
    int32_t line = s->line, col = s->col;
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    if (cp != 0 && msUnicodeIdStart(cp)) {
      scanIdentifier(s, out);
      return true;
    }
    // 数字与运算符由 Task 3/4 落地；此前一律按非法字符跳过恢复
    addDiag(s, MSE_UNEXPECTED_CHAR, line, col, width, "unexpected character");
    advanceRune(s);
  }
}
```

- [ ] **Step 2.7: RED——测试先行（改 include、加辅助与 4 个用例）**

`tests/unit/test_scanner_core.c` 的 `#include "scanner/token.h"` 改为 `#include "scanner/scanner.h"`；在 TEST 区前插入辅助：

```c
#define MAX_TOKS 256

typedef struct TokList {
  MsToken items[MAX_TOKS];
  int32_t count;
} TokList;

// 扫描到 EOF（EOF 一并入表，便于断言位置与“EOF 不补分号”）；返回 Scanner 以便检查内部态
static Scanner scanAll(const char* src, TokList* toks, MsDiagSink* diags) {
  toks->count = 0;
  Scanner s;
  scannerInit(&s, src, (int32_t)strlen(src), "t.ms", diags);
  MsToken tok;
  while (scannerNext(&s, &tok)) {
    toks->items[toks->count++] = tok;
  }
  toks->items[toks->count++] = tok;
  return s;
}

static bool tokIs(const MsToken* t, MsTokenType type, const char* text) {
  return t->type == type && (int32_t)strlen(text) == t->len && memcmp(t->start, text, (size_t)t->len) == 0;
}
```

```c
TEST(scanIdentifierAndKeywords) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("func 变量 type", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_FUNC, "func"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "变量"));
  EXPECT_EQ(ctx, 6, toks.items[1].col);  // col 按 rune 计
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "type"));  // struct/interface/package/type 非保留字
  EXPECT_EQ(ctx, 4, toks.count);  // + EOF
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanCommentsExposeDocLine) {
  MsDiagSink diags = {0};
  TokList toks;
  Scanner s = scanAll("/// doc\n// c\nx", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "x"));
  EXPECT_EQ(ctx, 2, toks.items[0].line);
  EXPECT_EQ(ctx, 1, s.docLine);
  EXPECT_EQ(ctx, 2, toks.count);  // x + EOF
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanBlockCommentDiagnosed) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("/* x */y", &toks, &diags);  // Task 2 用 ID；Task 3 改回 "/* x */1" 验 INT 恢复
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "y"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_BLOCK_COMMENT, (int)diags.items[0].code);
}

TEST(scanUnexpectedCharSkipped) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a # b", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "a"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "b"));
  EXPECT_EQ(ctx, 3, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_UNEXPECTED_CHAR, (int)diags.items[0].code);
}
```

注意：`scanBlockCommentDiagnosed` 本任务用 `"/* x */y"`（ID），Task 3 落地数字后改为 `"/* x */1"` 复核 INT 恢复路径。

- [ ] **Step 2.8: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Passed**（4 用例全绿；数字/运算符输入尚走 unexpected 分支，由后续任务覆盖）。

- [ ] **Step 2.9: Commit**

```powershell
git add tools/gen_unicode_table.py src/scanner/unicode_table.h src/scanner/scanner.h src/scanner/scanner.c CMakeLists.txt tests/unit/test_scanner_core.c
git commit -m "✨ feat(scanner): scan identifiers and comments with unicode table"
```

---

### Task 3: 数字字面量（int/bigint/float + 诊断）

**Files:**
- Modify: `src/scanner/scanner.c`、`tests/unit/test_scanner_core.c`

- [ ] **Step 3.1: RED——追加数字用例（先加 tokIntValue 辅助，紧随 tokIs 之后）**

```c
// token 文本去下划线后按进制求值（验证数字语义）
static uint64_t tokIntValue(const MsToken* t) {
  char buf[128];
  int32_t n = 0;
  for (int32_t i = 0; i < t->len && n < 127; i++) {
    if (t->start[i] != '_') {
      buf[n++] = t->start[i];
    }
  }
  buf[n] = '\0';
  int base = 10;
  if (buf[0] == '0' && buf[1] == 'x') {
    base = 16;
  } else if (buf[0] == '0' && buf[1] == 'o') {
    base = 8;
  } else if (buf[0] == '0' && buf[1] == 'b') {
    base = 2;
  }
  return strtoull(base == 10 ? buf : buf + 2, NULL, base);
}
```

（文件头需补 `#include <stdlib.h>`。）新增 TEST：

```c
TEST(scanIntForms) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("1_000_000", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "1_000_000"));
  EXPECT_EQ(ctx, 1000000, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntRadix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("0x1F 0o17 0b101", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "0x1F"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_INT, "0o17"));
  EXPECT(ctx, tokIs(&toks.items[2], TK_INT, "0b101"));
  EXPECT_EQ(ctx, 31, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 15, (int)tokIntValue(&toks.items[1]));
  EXPECT_EQ(ctx, 5, (int)tokIntValue(&toks.items[2]));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntUppercasePrefix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("0X1F 0O17", &toks, &diags);
  EXPECT_EQ(ctx, 31, (int)tokIntValue(&toks.items[0]));
  EXPECT_EQ(ctx, 15, (int)tokIntValue(&toks.items[1]));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanBigintSuffix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("123n 0xFFn", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_BIGINT, "123n"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_BIGINT, "0xFFn"));
  EXPECT_EQ(ctx, 3, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanFloatForms) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("3.14 1e9 1E-3 2.5e+10 1_000.5", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_FLOAT, "3.14"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_FLOAT, "1e9"));
  EXPECT(ctx, tokIs(&toks.items[2], TK_FLOAT, "1E-3"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_FLOAT, "2.5e+10"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_FLOAT, "1_000.5"));
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanIntOverflowToBigint) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("9223372036854775808", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_BIGINT, "9223372036854775808"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_INT_OVERFLOW, (int)diags.items[0].code);
}
```

- [ ] **Step 3.2: RED（续）——非法数字表驱动用例与成员豁免**

```c
TEST(scanNumericErrors) {
  struct {
    const char* src;
    MsTokenType type;
    const char* text;
    int code;
  } cases[] = {
      {"123abc", TK_INT, "123", MSE_INT_TRAILING},
      {"1e", TK_FLOAT, "1e", MSE_FLOAT_EXPONENT},
      {"1e+", TK_FLOAT, "1e+", MSE_FLOAT_EXPONENT},
      {".5", TK_FLOAT, ".5", MSE_FLOAT_LEADING_DOT},
      {"5.", TK_INT, "5", MSE_FLOAT_TRAILING_DOT},
      {"1_", TK_INT, "1_", MSE_UNDERSCORE},
      {"1__0", TK_INT, "1__0", MSE_UNDERSCORE},
      {"0xG", TK_INT, "0x", MSE_BASE_PREFIX},
      {"0o8", TK_INT, "0o", MSE_DIGIT_BASE},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    MsDiagSink diags = {0};
    TokList toks;
    scanAll(cases[i].src, &toks, &diags);
    EXPECT(ctx, tokIs(&toks.items[0], cases[i].type, cases[i].text));
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, cases[i].code, (int)diags.items[0].code);
  }
  // 5. 的恢复补完：INT 之后还有 DOT
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("5.", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[1], TK_DOT, "."));
  EXPECT_EQ(ctx, 3, toks.count);
}

TEST(scanMemberAccessExemptFromDotDiag) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("5.toString", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_INT, "5"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_DOT, "."));
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "toString"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}
```

同时把 Task 2 的 `scanBlockCommentDiagnosed` 输入改回 `"/* x */1"`、首断言改 `tokIs(&toks.items[0], TK_INT, "1")`（数字现已可恢复）。全部新 TEST 注册进 main。

- [ ] **Step 3.3: 验证 RED**

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Failed**——数字 token 全部被判 `MSE_UNEXPECTED_CHAR`（`scanIntForms` 等首先因 `diags.count != 0` 失败）。

- [ ] **Step 3.4: GREEN——scanner.c 加入数字扫描（上：辅助函数）**

插入位置：`advanceRune` 之后、`skipLineComment` 之前。

```c
static int digitOfBase(uint8_t c, int base) {
  if (c >= '0' && c <= '9') {
    return c - '0' < base ? c - '0' : -1;
  }
  if (base == 16) {
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
  }
  return -1;
}

// 连续消费 base 进制数字与下划线分组（错位下划线报 MSE_UNDERSCORE 后继续恢复）；
// 溢出 uint64 时置 *overflow 并保持近似值（BIGINT 占位语义，T016 接管精确值）
static uint64_t scanDigits(Scanner* s, int base, bool* overflow) {
  uint64_t value = 0;
  bool sawDigit = false;
  for (;;) {
    uint8_t c = peek(s);
    int d = digitOfBase(c, base);
    if (d >= 0) {
      if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
        *overflow = true;
      } else {
        value = value * (uint64_t)base + (uint64_t)d;
      }
      sawDigit = true;
      advanceRune(s);
    } else if (c == '_') {
      if (!sawDigit || digitOfBase(peekAt(s, 1), base) < 0) {
        addDiag(s, MSE_UNDERSCORE, s->line, s->col, 1, "underscore must separate digits");
      }
      advanceRune(s);
    } else {
      break;
    }
  }
  return value;
}

// 数字后紧跟 . 且 . 后非数字时：判断是否成员访问（1.toString，03 §3 后缀成员）；
// 是则豁免诊断（DOT 留给主循环产出），否则按 5. 报缺小数位
static bool isMemberContinuation(const Scanner* s) {
  uint8_t c = peekAt(s, 1);
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
    return true;
  }
  if (c >= 0x80 && s->pos + 1 < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos + 1, s->len - s->pos - 1, &width);
    return msUnicodeIdStart(cp);
  }
  return false;
}
```

- [ ] **Step 3.5: GREEN（续）——scanNumber 主体**

插入位置：`scanIdentifier` 之后、`scannerInit` 之前。

```c
// 数字字面量：十/十六(0x 0X)/八(0o 0O)/二(0b)进制、下划线分组、n 后缀、浮点（. 与 e/E）
// 恢复策略：诊断后仍产出 token（01 §3/§9；溢出升 BIGINT 由 T016 接管精确值）
static void scanNumber(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  int base = 10;
  uint8_t n1 = peekAt(s, 1);
  if (peek(s) == '0') {
    if (n1 == 'x' || n1 == 'X') {
      base = 16;
    } else if (n1 == 'o' || n1 == 'O') {
      base = 8;
    } else if (n1 == 'b') {
      base = 2;
    }
  }
  if (base != 10) {
    advanceRune(s);
    advanceRune(s);
    uint8_t d = peek(s);
    if (digitOfBase(d, base) < 0 && d != '_') {
      if (digitOfBase(d, 10) >= 0) {
        addDiag(s, MSE_DIGIT_BASE, line, col, s->pos - start, "digit out of range for literal base");
      } else {
        addDiag(s, MSE_BASE_PREFIX, line, col, s->pos - start, "digits expected after base prefix");
      }
      makeToken(s, out, TK_INT, start, line, col);
      return;
    }
  }
  bool overflow = false;
  uint64_t value = scanDigits(s, base, &overflow);
  bool isFloat = false;
  if (base == 10 && peek(s) == '.') {
    if (digitOfBase(peekAt(s, 1), 10) >= 0) {
      isFloat = true;
      advanceRune(s);
      scanDigits(s, 10, &overflow);
    } else if (!isMemberContinuation(s)) {
      addDiag(s, MSE_FLOAT_TRAILING_DOT, s->line, s->col, 1, "fractional digits expected after '.'");
    }
  }
  if (base == 10 && (peek(s) == 'e' || peek(s) == 'E')) {
    int32_t off = (peekAt(s, 1) == '+' || peekAt(s, 1) == '-') ? 2 : 1;
    if (digitOfBase(peekAt(s, off), 10) >= 0) {
      isFloat = true;
      while (off-- > 0) {
        advanceRune(s);
      }
      scanDigits(s, 10, &overflow);
    } else {
      // 1e / 1e+：残缺指数，e 与符号并入恢复 FLOAT
      advanceRune(s);
      if (peek(s) == '+' || peek(s) == '-') {
        advanceRune(s);
      }
      addDiag(s, MSE_FLOAT_EXPONENT, line, col, s->pos - start, "exponent expects digits");
      makeToken(s, out, TK_FLOAT, start, line, col);
      return;
    }
  }
  bool isBig = false;
  if (!isFloat && peek(s) == 'n') {
    isBig = true;
    advanceRune(s);
  }
  if (s->pos < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    if (cp != 0 && msUnicodeIdContinue(cp)) {
      addDiag(s, MSE_INT_TRAILING, s->line, s->col, width, "identifier character cannot follow number");
    }
  }
  if (isFloat) {
    makeToken(s, out, TK_FLOAT, start, line, col);
  } else if (isBig || overflow || value > (uint64_t)INT64_MAX) {
    if (!isBig) {
      addDiag(s, MSE_INT_OVERFLOW, line, col, s->pos - start, "integer literal exceeds int64 range; promoted to bigint");
    }
    makeToken(s, out, TK_BIGINT, start, line, col);
  } else {
    makeToken(s, out, TK_INT, start, line, col);
  }
}
```

- [ ] **Step 3.6: GREEN（续）——scannerNext 分发接入数字**

`scannerNext` 中 `if (cp != 0 && msUnicodeIdStart(cp))` 分支之前插入两个数字分支（`.5` 前导点优先）：

```c
    int32_t start = s->pos;
    if (c == '.' && digitOfBase(peekAt(s, 1), 10) >= 0) {
      // .5：缺整数部（01 §3.2），恢复为 FLOAT
      addDiag(s, MSE_FLOAT_LEADING_DOT, line, col, 1, "integer part expected before '.'");
      advanceRune(s);
      bool overflow = false;
      scanDigits(s, 10, &overflow);
      makeToken(s, out, TK_FLOAT, start, line, col);
      return true;
    }
    if (digitOfBase(c, 10) >= 0) {
      scanNumber(s, out);
      return true;
    }
```

（原 `int32_t line = s->line, col = s->col;` 声明保留在两分支之前；原 `width`/`cp` 声明移入其后的标识符分支。）

- [ ] **Step 3.7: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Passed**（Task 2 的 4 例 + 数字 9 例全绿）。

- [ ] **Step 3.8: Commit**

```powershell
git add src/scanner/scanner.c tests/unit/test_scanner_core.c
git commit -m "✨ feat(scanner): scan numeric literals with diagnostics"
```

---

### Task 4: 运算符与标点（最长匹配）

**Files:**
- Modify: `src/scanner/scanner.c`、`tests/unit/test_scanner_core.c`

- [ ] **Step 4.1: RED——最长匹配与全矩阵用例**

```c
TEST(scanOperatorMaxMunch) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a **= b x := y <- ch", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_ID, "a"));
  EXPECT(ctx, tokIs(&toks.items[1], TK_STAR_STAR_EQ, "**="));
  EXPECT(ctx, tokIs(&toks.items[2], TK_ID, "b"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_ID, "x"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_COLON_EQ, ":="));
  EXPECT(ctx, tokIs(&toks.items[5], TK_ID, "y"));
  EXPECT(ctx, tokIs(&toks.items[6], TK_LARROW, "<-"));
  EXPECT(ctx, tokIs(&toks.items[7], TK_ID, "ch"));
  EXPECT_EQ(ctx, 9, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(scanOperatorMatrix) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("+ += - -= -> * *= ** **= / /= % %= = == ! != < <= << <<= > >= >> >>= "
          "& && &= | || |= ^ ^= ~ ? : := . , ; ( ) [ ] { }",
          &toks, &diags);
  static const MsTokenType types[] = {
      TK_PLUS,         TK_PLUS_EQ,     TK_MINUS,        TK_MINUS_EQ,     TK_ARROW,
      TK_STAR,         TK_STAR_EQ,     TK_STAR_STAR,    TK_STAR_STAR_EQ, TK_SLASH,
      TK_SLASH_EQ,     TK_PERCENT,     TK_PERCENT_EQ,   TK_EQ,           TK_EQ_EQ,
      TK_BANG,         TK_BANG_EQ,     TK_LT,           TK_LT_EQ,        TK_LT_LT,
      TK_LT_LT_EQ,     TK_GT,          TK_GT_EQ,        TK_GT_GT,        TK_GT_GT_EQ,
      TK_AMP,          TK_AMP_AMP,     TK_AMP_EQ,       TK_PIPE,         TK_PIPE_PIPE,
      TK_PIPE_EQ,      TK_CARET,       TK_CARET_EQ,     TK_TILDE,        TK_QUESTION,
      TK_COLON,        TK_COLON_EQ,    TK_DOT,          TK_COMMA,        TK_SEMICOLON,
      TK_LPAREN,       TK_RPAREN,      TK_LBRACKET,     TK_RBRACKET,     TK_LBRACE,
      TK_RBRACE,
  };
  EXPECT_EQ(ctx, (int)(sizeof(types) / sizeof(types[0])), toks.count - 1);  // 46 运算符 + EOF
  EXPECT_EQ(ctx, 0, diags.count);
  for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, TK_EOF, toks.items[toks.count - 1].type);
}
```

注册进 main 后验证 RED：

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Failed**（运算符被判非法字符，`diags.count` 非 0）。

- [ ] **Step 4.2: GREEN——scanOperator（首字符分发 + 2/3 字符前瞻）**

插入位置：`scanNumber` 之后、`scannerInit` 之前。

```c
// 运算符/标点：首字符分发 + 最长匹配（**= 优先于 ** 与 *=；<- 归一为一个 token）
static bool scanOperator(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  uint8_t c = peek(s);
  uint8_t n1 = peekAt(s, 1);
  uint8_t n2 = peekAt(s, 2);
  MsTokenType type;
  int32_t width;
  switch (c) {
    case '+': type = n1 == '=' ? TK_PLUS_EQ : TK_PLUS; width = n1 == '=' ? 2 : 1; break;
    case '-':
      if (n1 == '=') { type = TK_MINUS_EQ; width = 2; }
      else if (n1 == '>') { type = TK_ARROW; width = 2; }
      else { type = TK_MINUS; width = 1; }
      break;
    case '*':
      if (n1 == '*') { type = n2 == '=' ? TK_STAR_STAR_EQ : TK_STAR_STAR; width = n2 == '=' ? 3 : 2; }
      else if (n1 == '=') { type = TK_STAR_EQ; width = 2; }
      else { type = TK_STAR; width = 1; }
      break;
    case '/': type = n1 == '=' ? TK_SLASH_EQ : TK_SLASH; width = n1 == '=' ? 2 : 1; break;
    case '%': type = n1 == '=' ? TK_PERCENT_EQ : TK_PERCENT; width = n1 == '=' ? 2 : 1; break;
    case '=': type = n1 == '=' ? TK_EQ_EQ : TK_EQ; width = n1 == '=' ? 2 : 1; break;
    case '!': type = n1 == '=' ? TK_BANG_EQ : TK_BANG; width = n1 == '=' ? 2 : 1; break;
    case '<':
      if (n1 == '<') { type = n2 == '=' ? TK_LT_LT_EQ : TK_LT_LT; width = n2 == '=' ? 3 : 2; }
      else if (n1 == '=') { type = TK_LT_EQ; width = 2; }
      else if (n1 == '-') { type = TK_LARROW; width = 2; }
      else { type = TK_LT; width = 1; }
      break;
    case '>':
      if (n1 == '>') { type = n2 == '=' ? TK_GT_GT_EQ : TK_GT_GT; width = n2 == '=' ? 3 : 2; }
      else if (n1 == '=') { type = TK_GT_EQ; width = 2; }
      else { type = TK_GT; width = 1; }
      break;
    case '&':
      if (n1 == '&') { type = TK_AMP_AMP; width = 2; }
      else if (n1 == '=') { type = TK_AMP_EQ; width = 2; }
      else { type = TK_AMP; width = 1; }
      break;
    case '|':
      if (n1 == '|') { type = TK_PIPE_PIPE; width = 2; }
      else if (n1 == '=') { type = TK_PIPE_EQ; width = 2; }
      else { type = TK_PIPE; width = 1; }
      break;
    case '^': type = n1 == '=' ? TK_CARET_EQ : TK_CARET; width = n1 == '=' ? 2 : 1; break;
    case '~': type = TK_TILDE; width = 1; break;
    case '?': type = TK_QUESTION; width = 1; break;
    case ':': type = n1 == '=' ? TK_COLON_EQ : TK_COLON; width = n1 == '=' ? 2 : 1; break;
    case '.': type = TK_DOT; width = 1; break;
    case ',': type = TK_COMMA; width = 1; break;
    case ';': type = TK_SEMICOLON; width = 1; break;
    case '(': type = TK_LPAREN; width = 1; break;
    case ')': type = TK_RPAREN; width = 1; break;
    case '[': type = TK_LBRACKET; width = 1; break;
    case ']': type = TK_RBRACKET; width = 1; break;
    case '{': type = TK_LBRACE; width = 1; break;
    case '}': type = TK_RBRACE; width = 1; break;
    default: return false;
  }
  for (int32_t i = 0; i < width; i++) {
    advanceRune(s);
  }
  makeToken(s, out, type, start, line, col);
  return true;
}
```

- [ ] **Step 4.3: GREEN（续）——分发接入**

`scannerNext` 标识符分支之后、非法字符分支之前插入：

```c
    if (scanOperator(s, out)) {
      return true;
    }
```

- [ ] **Step 4.4: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Passed**。

- [ ] **Step 4.5: Commit**

```powershell
git add src/scanner/scanner.c tests/unit/test_scanner_core.c
git commit -m "✨ feat(scanner): scan operators with maximal munch"
```

---

### Task 5: ASI 与 parenDepth

**Files:**
- Modify: `src/scanner/scanner.c`、`tests/unit/test_scanner_core.c`

- [ ] **Step 5.1: RED——四细则正反用例（01 §7）**

```c
TEST(asiBasic) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a\nb", &toks, &diags);
  EXPECT_EQ(ctx, TK_ID, toks.items[0].type);
  EXPECT(ctx, tokIs(&toks.items[1], TK_SEMICOLON, ""));
  EXPECT_EQ(ctx, 1, toks.items[1].line);
  EXPECT_EQ(ctx, 2, toks.items[1].col);  // ; 定位在换行符处
  EXPECT_EQ(ctx, TK_ID, toks.items[2].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[3].type);  // EOF 不补分号
  EXPECT_EQ(ctx, 4, toks.count);
}

TEST(asiParenSuppress) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("f(\na\n)", &toks, &diags);
  EXPECT_EQ(ctx, 5, toks.count);  // f ( a ) EOF，括号内换行不产生分号（细则 1）
  EXPECT_EQ(ctx, TK_RPAREN, toks.items[3].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[4].type);
}

TEST(asiLarrowSuppress) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("ch <-\n1", &toks, &diags);
  EXPECT_EQ(ctx, 4, toks.count);  // ch <- 1 EOF，行尾 <- 抑制（细则 3）
  EXPECT_EQ(ctx, TK_LARROW, toks.items[1].type);
  EXPECT_EQ(ctx, TK_INT, toks.items[2].type);
}

TEST(asiReturnBreaksStatement) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("return\nx", &toks, &diags);
  EXPECT_EQ(ctx, TK_RETURN, toks.items[0].type);
  EXPECT(ctx, tokIs(&toks.items[1], TK_SEMICOLON, ""));
  EXPECT_EQ(ctx, 7, toks.items[1].col);  // return 后换行立即断句（细则 4）
  EXPECT_EQ(ctx, TK_ID, toks.items[2].type);
  EXPECT_EQ(ctx, 4, toks.count);
}
```

```c
TEST(asiLiteralAndKeywordTriggers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("true\nnil\nthis\n1.5\n2n\nbreak\ncontinue\nthrow\ndefer\nyield", &toks, &diags);
  EXPECT_EQ(ctx, 20, toks.count);  // 10 token + 9 ; + EOF（结尾无换行）
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(ctx, TK_SEMICOLON, toks.items[2 * i + 1].type);
  }
  EXPECT_EQ(ctx, TK_YIELD, toks.items[18].type);
  EXPECT_EQ(ctx, TK_EOF, toks.items[19].type);
}

TEST(asiClosingBracketTriggers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a[1]\nb\n{\nc\n}\n", &toks, &diags);
  static const MsTokenType types[] = {
      TK_ID,     TK_LBRACKET, TK_INT,      TK_RBRACKET, TK_SEMICOLON,
      TK_ID,     TK_SEMICOLON, TK_LBRACE,  TK_ID,       TK_RBRACE,
      TK_SEMICOLON, TK_EOF,
  };
  EXPECT_EQ(ctx, 12, toks.count);
  for (int i = 0; i < 12; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
}

TEST(asiParenDepthMixed) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("x = ([1])\ny", &toks, &diags);
  static const MsTokenType types[] = {
      TK_ID, TK_EQ, TK_LPAREN, TK_LBRACKET, TK_INT, TK_RBRACKET, TK_RPAREN,
      TK_SEMICOLON, TK_ID, TK_EOF,
  };
  EXPECT_EQ(ctx, 10, toks.count);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 10, toks.items[7].col);  // ; 位于 ([1]) 之后的换行处
}

TEST(asiOperatorNoSemicolon) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("a +\nb", &toks, &diags);
  EXPECT_EQ(ctx, 4, toks.count);  // a + b EOF，行尾运算符不触发
  EXPECT_EQ(ctx, TK_PLUS, toks.items[1].type);
}
```

注册进 main，验证 RED：

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Failed**（`asiBasic` 期望 4 个 token 实得 3——无分号插入）。

- [ ] **Step 5.2: GREEN——canEndStatement + 换行 ASI 分支 + 统一收口**

`scanOperator` 之后加入：

```c
// 01 §7：换行前最后 token 属于该集合时回补分号（<- 与运算符天然不在集合内）
static bool canEndStatement(MsTokenType type) {
  switch (type) {
    case TK_ID:
    case TK_INT:
    case TK_FLOAT:
    case TK_BIGINT:
    case TK_STRING:
    case TK_RUNE:
    case TK_BREAK:
    case TK_CONTINUE:
    case TK_RETURN:
    case TK_THROW:
    case TK_DEFER:
    case TK_YIELD:
    case TK_TRUE:
    case TK_FALSE:
    case TK_NIL:
    case TK_THIS:
    case TK_RPAREN:
    case TK_RBRACKET:
    case TK_RBRACE:
      return true;
    default:
      return false;
  }
}
```

`scannerNext` 改造三处：

1. 空白分支拆出 `\n` 单独处理（ASI 发射，位置=换行符，len=0）：

```c
    if (c == '\n') {
      if (s->pendingAsi && s->parenDepth == 0) {
        makeToken(s, out, TK_SEMICOLON, s->pos, s->line, s->col);
        out->len = 0;
        s->pendingAsi = false;
        return true;
      }
      advanceRune(s);
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      advanceRune(s);
      continue;
    }
```

2. EOF 分支返回前不动（EOF 不补 `;`）。

3. 各真实 token 的 `return true` 收口为函数尾统一处理（括号深度 + ASI 触发态）：把 `.5`、数字、标识符、运算符四个分支的 `return true;` 删除，改为落至函数末尾：

```c
    if (out->type == TK_LPAREN || out->type == TK_LBRACKET || out->type == TK_LBRACE) {
      s->parenDepth++;
    } else if ((out->type == TK_RPAREN || out->type == TK_RBRACKET || out->type == TK_RBRACE) && s->parenDepth > 0) {
      s->parenDepth--;
    }
    s->pendingAsi = canEndStatement(out->type);
    return true;
```

（非法字符分支的 `continue` 保持不变。）

- [ ] **Step 5.3: 验证 GREEN**

```powershell
cmake --build build --config Debug --target test_scanner_core
ctest --test-dir build -C Debug -R scanner_core --output-on-failure
```

预期：**Passed**（累计 23 用例全绿）。

- [ ] **Step 5.4: Commit**

```powershell
git add src/scanner/scanner.c tests/unit/test_scanner_core.c
git commit -m "✨ feat(scanner): insert semicolons via ASI rules"
```

---

### Task 6: ms --dump-tokens + runner .args/.err + conformance

**Files:**
- Modify: `tools/test_runner.c`、`tests/unit/test_runner_selftest.c`、`tools/ms.c`
- Create: `tests/conformance/01-lex-{01,02,04,07,09}-*.ms` + 伴生 `.args`/`.out`（09 另含 `.err`/`.code`）
- Modify: `tests/conformance/README.md`

- [ ] **Step 6.1: runner——splitArgs（置于 `#ifndef MS_TEST_RUNNER_NO_MAIN` 之前，自测可达）**

```c
#define RUNNER_MAX_EXTRA_ARGS 16

// 就地切分空白分隔参数（写入 argvOut，指针指向 text 内部）；返回个数
static int splitArgs(char* text, const char* argvOut[], int maxArgs) {
  int count = 0;
  char* p = text;
  while (*p != '\0' && count < maxArgs) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    argvOut[count++] = p;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
      p++;
    }
    if (*p != '\0') {
      *p++ = '\0';
    }
  }
  return count;
}
```

- [ ] **Step 6.2: runner——spawnCapture 改收 argv（Windows 拼命令行 / POSIX execv）**

签名改 `static SpawnResult spawnCapture(char* const* argv, int timeoutSec)`；Windows 版命令行拼接替换原 snprintf 一行：

```c
  char command[RUNNER_MAX_PATH * 2];
  size_t pos = 0;
  for (int i = 0; argv[i] != NULL; i++) {
    int written = snprintf(command + pos, sizeof(command) - pos, "%s\"%s\"", i == 0 ? "" : " ", argv[i]);
    if (written < 0 || (size_t)written >= sizeof(command) - pos) {
      break;
    }
    pos += (size_t)written;
  }
```

POSIX 版 `execl(exe, exe, script, (char*)NULL);` 替换为 `execv(argv[0], argv);`。

- [ ] **Step 6.3: runner——runCase 读 .args、组装 argv、.err 改子串匹配**

`spawnCapture(msExe, casePath, timeoutSec)` 调用处替换为：

```c
  char* extraArgsText = NULL;
  const char* extraArgs[RUNNER_MAX_EXTRA_ARGS];
  int extraArgCount = 0;
  if (joinPath(path, sizeof(path), base, ".args")) {
    extraArgsText = slurpFile(path, &expectedLen);
    if (extraArgsText != NULL) {
      extraArgCount = splitArgs(extraArgsText, extraArgs, RUNNER_MAX_EXTRA_ARGS);
    }
  }
  char* spawnArgv[RUNNER_MAX_EXTRA_ARGS + 3];
  spawnArgv[0] = (char*)msExe;
  for (int i = 0; i < extraArgCount; i++) {
    spawnArgv[1 + i] = (char*)extraArgs[i];
  }
  spawnArgv[1 + extraArgCount] = (char*)casePath;
  spawnArgv[2 + extraArgCount] = NULL;

  SpawnResult run = spawnCapture(spawnArgv, timeoutSec);
```

`.err` 校验块替换为（首行为针，stderr 任意位置命中即过——路径无关）：

```c
    if (expectedErr != NULL && expectedErr[0] != '\0') {
      normalizeLf(expectedErr);
      char* nl = strchr(expectedErr, '\n');
      if (nl != NULL) {
        *nl = '\0';
      }
      const char* actualErr = bufText(&run.err);
      if (strstr(actualErr, expectedErr) == NULL) {
        printf("FAIL %s: stderr missing \"%s\"\n", casePath, expectedErr);
        printDiffLine(stdout, "actual  ", actualErr, 1);
        failed = 1;
      }
    }
```

收尾 `free(expectedErr);` 之后补 `free(extraArgsText);`。文件头契约注释行改为：

```c
// 可选 .code（期望退出码，缺省 0）、.err（期望 stderr 子串）、.timeout（秒，缺省 10）、
// .args（空白分隔参数，插在 ms 与脚本之间，如 --dump-tokens）
```

- [ ] **Step 6.4: selftest——splitArgs 用例（test_runner_selftest.c）**

新增两个 TEST 并注册：

```c
TEST(splitArgsSplitsWhitespace) {
  char text[] = " --dump-tokens \t foo\r\n";
  const char* args[RUNNER_MAX_EXTRA_ARGS];
  EXPECT_EQ(ctx, 2, splitArgs(text, args, RUNNER_MAX_EXTRA_ARGS));
  EXPECT_STREQ(ctx, "--dump-tokens", args[0]);
  EXPECT_STREQ(ctx, "foo", args[1]);
}

TEST(splitArgsEmptyYieldsNone) {
  char text[] = " \r\n\t ";
  const char* args[RUNNER_MAX_EXTRA_ARGS];
  EXPECT_EQ(ctx, 0, splitArgs(text, args, RUNNER_MAX_EXTRA_ARGS));
}
```

```powershell
cmake --build build --config Debug --target test_runner_selftest test_runner
ctest --test-dir build -C Debug -R runner_selftest --output-on-failure
```

预期：**Passed**（原 7 例 + 新 2 例）。

- [ ] **Step 6.5: ms.c——文件读取与 LF 归一化（include 区补 stdlib.h，加 scanner/scanner.h）**

`printUsage` 增补一行命令说明；`isMsPath` 之后追加：

```c
// 读取整个文件到 NUL 结尾缓冲；失败返回 NULL
static char* slurpFile(const char* path, size_t* outLen) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  size_t cap = 4096;
  size_t len = 0;
  char* data = malloc(cap);
  if (data == NULL) {
    fclose(file);
    return NULL;
  }
  for (;;) {
    if (len == cap) {
      cap *= 2;
      char* grown = realloc(data, cap);
      if (grown == NULL) {
        free(data);
        fclose(file);
        return NULL;
      }
      data = grown;
    }
    size_t got = fread(data + len, 1, cap - len, file);
    len += got;
    if (got == 0) {
      break;
    }
  }
  fclose(file);
  char* shrunk = realloc(data, len + 1);
  if (shrunk == NULL) {
    free(data);
    return NULL;
  }
  shrunk[len] = '\0';
  *outLen = len;
  return shrunk;
}

// 就地 \r\n / \r → \n（01-lexical.md：读取层归一化，扫描器只见 \n）；返回归一化后长度
static size_t normalizeLf(char* text) {
  char* src = text;
  char* dst = text;
  while (*src != '\0') {
    if (*src == '\r') {
      src++;
      if (*src == '\n') {
        src++;
      }
      *dst++ = '\n';
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
  return (size_t)(dst - text);
}
```

- [ ] **Step 6.6: ms.c——printToken/dumpTokens 与 main 接线**

```c
static void printToken(const MsToken* tok) {
  printf("%d:%d %s", tok->line, tok->col, msTokenTypeName(tok->type));
  if (tok->len > 0) {
    printf(" %.*s", (int)tok->len, tok->start);
  }
  printf("\n");
}

// --dump-tokens：token 流到 stdout，诊断到 stderr；有错退出码 1
static int dumpTokens(const char* path) {
  size_t len = 0;
  char* src = slurpFile(path, &len);
  if (src == NULL) {
    fprintf(stderr, "ms: cannot read %s\n", path);
    return 2;
  }
  len = normalizeLf(src);
  MsDiagSink diags = {0};
  Scanner s;
  scannerInit(&s, src, (int32_t)len, path, &diags);
  MsToken tok;
  while (scannerNext(&s, &tok)) {
    printToken(&tok);
  }
  printToken(&tok);  // EOF 行
  for (int32_t i = 0; i < diags.count; i++) {
    char buf[512];
    msDiagFormat(&diags.items[i], src, buf, sizeof(buf));
    fputs(buf, stderr);
  }
  if (diags.overflow > 0) {
    fprintf(stderr, "ms: %d more diagnostics suppressed\n", (int)diags.overflow);
  }
  free(src);
  return diags.count > 0 ? 1 : 0;
}
```

`main` 在 run 占位分支之前插入：

```c
  if (argc == 3 && strcmp(argv[1], "--dump-tokens") == 0 && isMsPath(argv[2])) {
    return dumpTokens(argv[2]);
  }
```

构建自检：

```powershell
cmake --build build --config Debug --target ms
build\Debug\ms.exe --dump-tokens tests\fixtures\smoke\hello.ms
```

预期：注释脚本仅输出 EOF 行（如 `2:1 EOF`），退出码 0。

- [ ] **Step 6.7: Commit（runner 与 ms 分两笔；README 随 6.10 的 conformance 一并提交）**

```powershell
git add tools/test_runner.c tests/unit/test_runner_selftest.c
git commit -m "✨ feat(tools): support .args injection and substring .err in test runner"
git add tools/ms.c
git commit -m "✨ feat(cli): add ms --dump-tokens debug switch"
```

- [ ] **Step 6.8: conformance 用例（每个 .ms 配 `.args`（内容 `--dump-tokens`）与同名 `.out`）**

`tests/conformance/01-lex-01-int-forms.ms`：

```
1_000_000
0x1F 0o17 0b101
123n 0xFFn
42
```

`01-lex-01-int-forms.out`：

```
1:1 INT 1_000_000
1:10 SEMICOLON
2:1 INT 0x1F
2:6 INT 0o17
2:11 INT 0b101
2:16 SEMICOLON
3:1 BIGINT 123n
3:6 BIGINT 0xFFn
3:11 SEMICOLON
4:1 INT 42
4:3 SEMICOLON
5:1 EOF
```

`tests/conformance/01-lex-02-float-forms.ms`：

```
3.14 1e9 1E-3
2.5e+10 1_000.5
```

`01-lex-02-float-forms.out`：

```
1:1 FLOAT 3.14
1:6 FLOAT 1e9
1:10 FLOAT 1E-3
1:14 SEMICOLON
2:1 FLOAT 2.5e+10
2:9 FLOAT 1_000.5
2:16 SEMICOLON
3:1 EOF
```

`tests/conformance/01-lex-04-operators.ms`：

```
a **= b
x := y <- ch
p <<= q >>= r
m and n or not k
t ? u : v
```

`01-lex-04-operators.out`：

```
1:1 ID a
1:3 STAR_STAR_EQ **=
1:7 ID b
1:8 SEMICOLON
2:1 ID x
2:3 COLON_EQ :=
2:6 ID y
2:8 LARROW <-
2:11 ID ch
2:13 SEMICOLON
3:1 ID p
3:3 LT_LT_EQ <<=
3:7 ID q
3:9 GT_GT_EQ >>=
3:13 ID r
3:14 SEMICOLON
4:1 ID m
4:3 AND and
4:7 ID n
4:9 OR or
4:12 NOT not
4:16 ID k
4:17 SEMICOLON
5:1 ID t
5:3 QUESTION ?
5:5 ID u
5:7 COLON :
5:9 ID v
5:10 SEMICOLON
6:1 EOF
```

`tests/conformance/01-lex-07-asi-rules.ms`：

```
a
b
f(
  a
)
ch <-
  1
return
  x
if (a) {
  b = 1
}
```

`01-lex-07-asi-rules.out`：

```
1:1 ID a
1:2 SEMICOLON
2:1 ID b
2:2 SEMICOLON
3:1 ID f
3:2 LPAREN (
4:3 ID a
5:1 RPAREN )
5:2 SEMICOLON
6:1 ID ch
6:4 LARROW <-
7:3 INT 1
7:4 SEMICOLON
8:1 RETURN return
8:7 SEMICOLON
9:3 ID x
9:4 SEMICOLON
10:1 IF if
10:4 LPAREN (
10:5 ID a
10:6 RPAREN )
10:8 LBRACE {
11:3 ID b
11:5 EQ =
11:7 INT 1
12:1 RBRACE }
12:2 SEMICOLON
13:1 EOF
```

`tests/conformance/01-lex-09-numeric-ambiguity.ms`（含非法输入；另加 `.err` 内容 `error[MSE0`、`.code` 内容 `1`）：

```
123abc 1e 0x1F 5. .5
1..2 0o8 0b2 0xG 1_ 1__0
9223372036854775808
```

`01-lex-09-numeric-ambiguity.out`（诊断共 12 条走 stderr，stdout 只有 token）：

```
1:1 INT 123
1:4 ID abc
1:8 FLOAT 1e
1:11 INT 0x1F
1:16 INT 5
1:17 DOT .
1:19 FLOAT .5
1:21 SEMICOLON
2:1 INT 1
2:2 DOT .
2:3 FLOAT .2
2:6 INT 0o
2:8 INT 8
2:10 INT 0b
2:12 INT 2
2:14 INT 0x
2:16 ID G
2:18 INT 1_
2:21 INT 1__0
2:25 SEMICOLON
3:1 BIGINT 9223372036854775808
3:20 SEMICOLON
4:1 EOF
```

`tests/conformance/README.md` 契约行替换为：

```markdown
- 契约：同目录同名 `.out`（期望 stdout，UTF-8、LF）；可选 `.code`（期望退出码，缺省 0）、`.err`（期望 stderr 子串：取 `.err` 首行，在 stderr 任意位置命中即通过）、`.timeout`（秒，缺省 10）、`.args`（空白分隔参数，插在 ms 与脚本之间，如 `--dump-tokens`）。比对前实际与期望输出统一 LF 归一化。
```

- [ ] **Step 6.9: 验证 conformance（reconfigure 后 5 例全绿）**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug -R conformance --output-on-failure
```

预期：5 例 **Passed**。若有 diff：逐条对照 01-lexical.md 判定是 golden 写错（修 golden 并复核语义）还是实现错（修实现）；禁止"照抄实际输出"式放行。

- [ ] **Step 6.10: Commit**

```powershell
git add tests/conformance
git commit -m "🧪 test(conformance): add 01-lex token dump cases"
```

---

### Task 7: 全量验证 + DoD 收尾

**Files:**
- Modify: `docs/tasks/T004-scanner-core.md`、`docs/tasks/README.md`

- [ ] **Step 7.1: 全量构建 + 全部测试**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

预期：全部 **Passed**（基线 5 项 + scanner_core + runner_selftest（扩 2 例）+ conformance 5 例 = 12 项）。

- [ ] **Step 7.2: DoD 核对（T004 §4）**

- 01 §3.1/§3.2/§4/§7 示例 token 化正确（conformance 5 例绿）
- 37 关键字与运算符最大匹配无歧义（`scanOperatorMatrix` 46 项 + `scanOperatorMaxMunch`）
- ASI 四细则正反用例通过（`asi*` 8 例）
- 非法数字（`123abc`、`1e`、`.5`、`5.`）产生 MSE0xxx（`scanNumericErrors` + conformance 09）
- `parenDepth` 混合嵌套 `([{ }])` 抑制正确（`asiParenDepthMixed`）

勾选 T004 文档 DoD 五项，头部状态改 `已完成`；README 总表 T004 改 `✅`。

- [ ] **Step 7.3: Commit**

```powershell
git add docs/tasks/T004-scanner-core.md docs/tasks/README.md
git commit -m "✅ docs(tasks): mark T004 done"
```

---

## 风险

- MSVC /W4 /WX：`size_t`/`int32_t` 窄化需显式 cast（`%.*s` 用 `(int)`、u64 溢出判定全程 `(uint64_t)`）；本地仅 MSVC 门禁，GCC/Clang -Wshadow -Wconversion 由 CI/后续补验（当前仓库无 workflow）。
- golden 生成纪律：期望输出必须手写并对照 spec 复核，diff 时先判责再改；禁止直接把实际输出落盘为 golden。
- `5.` + 空格 + 标识符（`5 . x` 稀疏写法）会误报 MSE0005——已批准的已知局限（成员访问紧贴语义）。
- 生成表体积约 1500-1900 行属预期；`msUnicodeIdStart/Continue` 为头内 static 函数，仅 scanner.c 包含，无多重定义。
- 嵌套 `}` 多余闭合时 `parenDepth` 下限钳 0（防负深度反转抑制逻辑）。

## 计划自查（writing-plans Self-Review）

1. 覆盖：T004 §2 全部要点（结构体/关键字/数字/浮点/运算符/注释/ASI/换行归一化）与 §5 测试表逐行对应——`1_000_000`、进制、`123n 0xFFn`、浮点 5 形态、`a **= b`、`x := y <- ch`、ASI 四则、注释、`变量 = 1` 均有落点；§5.2 五个 conformance 文件齐备。
2. 占位符：无 TBD/TODO；所有代码完整给出。
3. 类型一致：`Scanner` 字段、`MseLexCode` 枚举名、`splitArgs`/`spawnCapture` 签名在各任务间一致；`scanAll` 返回 `Scanner`。

---
