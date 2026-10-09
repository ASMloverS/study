# T005 Scanner 字符串与 rune 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现普通字符串（转义校验 + `${}` 插值切分）、原始字符串（反引号）、rune 字面量，达成词法阶段出口（`docs/language/01-lexical.md` 全部词法用例覆盖）。

**Architecture:** Scanner 增加固定深度"域栈"（STR 字符串域 / INTERP 插值域），主循环按栈顶域分发；普通字符串切分为 `STRING(") (STR_PART|INTERP_BEGIN … INTERP_END)* STRING(")` token 流（STRING 复用为首尾定界符）。转义仅校验、不解码（前端零分配），rune 经新增公共函数 `msRuneLiteralValue` 取码点。`ms <file>` run 路径先行词法检查，有诊断则退出码 3（27 §10）。

**Tech Stack:** C11 / CMake≥3.16 / MSVC(Windows)+Ninja(Linux CI) / ctest

**Spec:** `docs/tasks/T005-scanner-string-rune.md` + `docs/language/01-lexical.md` §3.3 §3.4 §7

## 已确认设计决策（用户已批准）

1. **Token 流映射**：复用 `TK_STRING` 作首尾定界符（不新增 STR_BEGIN/STR_END）；新类型仅 `STR_PART / INTERP_BEGIN / INTERP_END / RAW_STR`（与任务 §3 清单一致）。无插值串也统一分解。
2. **STR_PART 保留原始字节**：转义仅校验（非法发 MSE），不解码；解码归 T010/T013（零分配约束）。
3. **rune 值**：`MsToken` 无值字段，新增 `msRuneLiteralValue(const MsToken*) -> uint32_t` 公共解码函数。
4. **ms.c run 路径加词法检查**：读文件→扫描→诊断打印 stderr→退出码 3；无诊断返回 0（真实执行 T011 接入）。
5. **普通字符串单行**：裸换行 = 未闭合错误（同 Go）；仅原始字符串可跨行。

## 文件清单

| 动作 | 文件 | 职责 |
|------|------|------|
| 修改 | `src/scanner/token.h` | X-macro 增 4 个 token 类型 |
| 修改 | `src/scanner/scanner.h` | MSE 11-16 错误码、LexFrame 域栈、msRuneLiteralValue 声明 |
| 修改 | `src/scanner/scanner.c` | 域栈状态机：scanStrDomain/scanEscape/scanRawString/scanRune |
| 修改 | `tools/ms.c` | run 路径词法检查 + 退出码 3 |
| 新增 | `tests/unit/test_scanner_string.c` | 单测（§5.1 用例表全覆盖） |
| 修改 | `tests/CMakeLists.txt` | 注册 scanner_string |
| 新增 | `tests/conformance/01-lex-03{,b}-*,05-*,06-*,err-*` | 4 golden + 3 err 用例 |
| 修改 | `docs/tasks/T005-scanner-string-rune.md`、`docs/tasks/README.md` | 状态流转 ⬜→🚧→✅ |

## 关键状态机（实现参照）

```text
主循环 scannerNext:
  栈顶 == STR   → scanStrDomain：内容即 token 文本；边界有 3 类
                   - '"'  → 段先出 → 闭合 STRING，弹栈
                   - '${' → 段先出 → INTERP_BEGIN，压 INTERP 帧
                   - 裸 \n/EOF → 段先出 → 诊断 + 零宽恢复 STRING，弹栈（\n 不吞）
                   - '\'  → scanEscape 校验（坏字节并入段）
  栈顶 == INTERP 或顶层 → 原主循环；差异：
                   - '}' 且本域 brace==0 → INTERP_END，弹栈（复位下层 STR 段起点）
                   - '"' 压 STR 帧；'\'' scanRune；'`' scanRawString
                   - '\n' 的 ASI 增加条件 depth==0（01 §7 细则 2）
                   - 收口处 parenDepth 照常（细则 1 延续），INTERP 帧另计 brace 配对
  EOF 且栈非空   → 逐帧弹栈诊断（INTERP 在主循环 c==0 分支，STR 在 scanStrDomain）
```

## Task 1: 域栈基建 + 普通字符串（无插值）

**Files:** Modify `src/scanner/token.h`、`src/scanner/scanner.h`、`src/scanner/scanner.c`、`tests/CMakeLists.txt`；Create `tests/unit/test_scanner_string.c`

- [ ] **Step 1.1** `tests/CMakeLists.txt` 追加一行：`ms_add_test(scanner_string ms_scanner)`

- [ ] **Step 1.2** 写失败测试 `tests/unit/test_scanner_string.c`（完整框架 + 首批用例）：

```c
// T005：字符串/插值/原始串/rune 单测（01-lexical.md §3.3 §3.4 §7 细则 2）
#include <stdbool.h>
#include <string.h>

#include "scanner/scanner.h"
#include "test_harness.h"

#define MAX_TOKS 256

typedef struct TokList {
  MsToken items[MAX_TOKS];
  int32_t count;
} TokList;

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

TEST(stringEmptyAndPlain) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\" \"abc\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[1], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[2], TK_STRING, "\""));
  EXPECT(ctx, tokIs(&toks.items[3], TK_STR_PART, "abc"));
  EXPECT(ctx, tokIs(&toks.items[4], TK_STRING, "\""));
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringDollarWithoutBraceIsContent) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a$b$\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "a$b$"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringMultibyteColumns) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\xe4\xb8\xad\xe6\x96\x87\" x", &toks, &diags);  // "中文" x
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "\xe4\xb8\xad\xe6\x96\x87"));
  EXPECT(ctx, tokIs(&toks.items[3], TK_ID, "x"));
  EXPECT_EQ(ctx, 7, toks.items[3].col);  // col 按 rune 计（01 §8）
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringAdjacentNotConcatenated) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a\" \"b\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_STR_PART, TK_STRING, TK_STRING, TK_STR_PART, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 7, toks.count);
  for (int i = 0; i < 7; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringAsiAfterEnd) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a\"\nx", &toks, &diags);
  EXPECT_EQ(ctx, TK_SEMICOLON, toks.items[3].type);
  EXPECT_EQ(ctx, TK_ID, toks.items[4].type);
  EXPECT_EQ(ctx, 6, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}
```

（main 与后续用例随任务追加，`main` 首版只调用本任务用例）

- [ ] **Step 1.3** 构建运行确认失败（TK_STR_PART 未定义 → 编译失败即红）：
  `cmake --build build --config Debug --target test_scanner_string`

- [ ] **Step 1.4** 最小实现。`src/scanner/token.h` X(RUNE) 后追加：

```c
  X(STR_PART)                                \
  X(INTERP_BEGIN)                            \
  X(INTERP_END)                              \
  X(RAW_STR)                                 \
```

`src/scanner/scanner.h`：枚举追加 11-16；新增域栈与解码声明：

```c
  MSE_ESCAPE = 11,            // 非法/残缺转义（\q、\x4、\u12、\u{}）
  MSE_ESCAPE_VALUE = 12,      // \u{} 值超 0x10FFFF 或代理区段
  MSE_STRING_UNTERMINATED = 13, // 普通串未闭合（裸换行/EOF）或插值域 EOF 未闭合
  MSE_RAW_UNTERMINATED = 14,  // 原始串到 EOF 未闭合
  MSE_RUNE_INVALID = 15,      // rune 空/多字符/未闭合
  MSE_LEX_DEPTH = 16,         // 字符串插值嵌套超 MS_LEX_MAX_DEPTH
```

```c
// 字符串/插值嵌套域（T005）：普通串内 ${}、插值内再嵌字符串（同规则）
typedef enum LexDomain { LEX_TOP, LEX_STR, LEX_INTERP } LexDomain;

#define MS_LEX_MAX_DEPTH 32

typedef struct LexFrame {
  LexDomain domain;
  int32_t brace;                    // INTERP：表达式内未闭合 { 计数（找本域闭合 }）
  int32_t openLine, openCol;       // 域开符号位置（诊断定位）
  int32_t partStart, partLine, partCol;  // STR：当前常量段起点（插值跨行后更新）
} LexFrame;
```

Scanner 结构体内 `docLine` 后追加：`LexFrame frames[MS_LEX_MAX_DEPTH]; int32_t depth;`
声明区追加：`uint32_t msRuneLiteralValue(const MsToken* tok);`

`src/scanner/scanner.c` 主体改造（pushStr/pushInterp/popInterp/emitStrPart/scanStrDomain + scannerNext 分发），完整代码见"关键状态机"与 Task 2-7 各函数；本任务实现 pushStr + scanStrDomain 的 `"`/内容分支（`${`、`\` 分支占位不触发，测试不含这些输入）。scannerInit 补 `s->depth = 0;`。canEndStatement 增 `case TK_RAW_STR:`（本任务顺带，rune 测试前生效即可）。

- [ ] **Step 1.5** 构建 + `ctest --test-dir build -C Debug -R scanner_string` 全绿；全量 `-R "scanner|token|smoke|runner|common"` 无回归
- [ ] **Step 1.6** 提交：`🧪 test(scanner): add string literal skeleton with domain stack`

## Task 2: 转义校验 + 未闭合恢复

- [ ] **Step 2.1** 追加失败测试：

```c
TEST(stringEscapeFullTable) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"\\n\\r\\t\\\\\\\"\\'\\0\\x41\\u{4E2D}\"", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "\\n\\r\\t\\\\\\\"\\'\\0\\x41\\u{4E2D}"));
  EXPECT_EQ(ctx, 4, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringEscapeMalformed) {
  struct {
    const char* src;
    int code;
  } cases[] = {
      {"\"\\q\"", MSE_ESCAPE},
      {"\"\\x4\"", MSE_ESCAPE},
      {"\"\\xG1\"", MSE_ESCAPE},
      {"\"\\u\"", MSE_ESCAPE},
      {"\"\\u12\"", MSE_ESCAPE},
      {"\"\\u{}\"", MSE_ESCAPE},
      {"\"\\u{1234567}\"", MSE_ESCAPE},
      {"\"\\u{110000}\"", MSE_ESCAPE_VALUE},
      {"\"\\u{D800}\"", MSE_ESCAPE_VALUE},
      {"\"\\u{DFFF}\"", MSE_ESCAPE_VALUE},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    MsDiagSink diags = {0};
    TokList toks;
    scanAll(cases[i].src, &toks, &diags);
    EXPECT(ctx, toks.count >= 3);  // STRING [STR_PART] STRING EOF 结构完整
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, cases[i].code, (int)diags.items[0].code);
  }
}

TEST(stringUnterminatedNewlineRecovers) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"abc\nx", &toks, &diags);
  static const MsTokenType types[] = {TK_STRING, TK_STR_PART, TK_STRING, TK_SEMICOLON, TK_ID, TK_EOF};
  EXPECT_EQ(ctx, 6, toks.count);
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
}

TEST(stringUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"abc", &toks, &diags);
  static const MsTokenType types[] = {TK_STRING, TK_STR_PART, TK_STRING, TK_EOF};
  EXPECT_EQ(ctx, 4, toks.count);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
}
```

- [ ] **Step 2.2** 红灯确认后实现 `scanEscape` + scanStrDomain 的 `\`/裸换行/EOF 分支：

```c
// \ 起始转义（01 §3.3）：\n \r \t \\ \" \' \0 \xHH \u{XXXXXX}
// 合法消费并返回 true；非法发 MSE 诊断后按形状恢复消费（\<换行> 仅吃反斜杠，换行留给域判定）
static bool scanEscape(Scanner* s, uint32_t* outValue) {
  int32_t start = s->pos;
  int32_t line = s->line, col = s->col;
  advanceRune(s);
  uint8_t c = peek(s);
  switch (c) {
    case 'n': *outValue = '\n'; advanceRune(s); return true;
    case 'r': *outValue = '\r'; advanceRune(s); return true;
    case 't': *outValue = '\t'; advanceRune(s); return true;
    case '\\': *outValue = '\\'; advanceRune(s); return true;
    case '"': *outValue = '"'; advanceRune(s); return true;
    case '\'': *outValue = '\''; advanceRune(s); return true;
    case '0': *outValue = 0; advanceRune(s); return true;
    case '\n':
      addDiag(s, MSE_ESCAPE, line, col, 1, "unknown escape sequence");
      *outValue = 0xFFFD;
      return false;
    case 'x': {
      advanceRune(s);
      int32_t digits = 0;
      uint32_t value = 0;
      while (digits < 2 && digitOfBase(peek(s), 16) >= 0) {
        value = value * 16 + (uint32_t)digitOfBase(peek(s), 16);
        advanceRune(s);
        digits++;
      }
      if (digits == 2) {
        *outValue = value;
        return true;
      }
      addDiag(s, MSE_ESCAPE, line, col, s->pos - start, "\\x expects two hex digits");
      *outValue = 0xFFFD;
      return false;
    }
    case 'u': {
      advanceRune(s);
      if (peek(s) != '{') {
        addDiag(s, MSE_ESCAPE, line, col, s->pos - start, "\\u expects '{'");
        *outValue = 0xFFFD;
        return false;
      }
      advanceRune(s);
      uint32_t value = 0;
      int32_t digits = 0;
      while (digits < 6 && digitOfBase(peek(s), 16) >= 0) {
        value = value * 16 + (uint32_t)digitOfBase(peek(s), 16);
        advanceRune(s);
        digits++;
      }
      if (digits > 0 && peek(s) == '}') {
        advanceRune(s);
        if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
          addDiag(s, MSE_ESCAPE_VALUE, line, col, s->pos - start, "\\u{} value out of Unicode range");
          *outValue = 0xFFFD;
          return false;
        }
        *outValue = value;
        return true;
      }
      addDiag(s, MSE_ESCAPE, line, col, s->pos - start, "\\u{} expects 1-6 hex digits and '}'");
      if (peek(s) == '}') {
        advanceRune(s);  // \u{}：吞掉空括号
      }
      *outValue = 0xFFFD;
      return false;
    }
    default:
      addDiag(s, MSE_ESCAPE, line, col, s->pos - start + 1, "unknown escape sequence");
      advanceRune(s);  // 非法字符一并吞掉（EOF 时为空操作）
      *outValue = 0xFFFD;
      return false;
  }
}
```

```c
    if (c == '\n' || c == 0) {  // 裸换行/EOF：未闭合（普通串单行）；换行不吞，可触发 ASI
      addDiag(s, MSE_STRING_UNTERMINATED, frame->openLine, frame->openCol, 1, "unterminated string literal");
      makeToken(s, out, TK_STRING, s->pos, s->line, s->col);  // 恢复性 STR_END（零宽）
      out->len = 0;
      s->depth--;
      return;
    }
    if (c == '\\') {
      uint32_t value = 0;
      scanEscape(s, &value);  // 非法已诊断，坏字节并入当前段
      continue;
    }
```

- [ ] **Step 2.3** 绿灯 + 无回归
- [ ] **Step 2.4** 提交：`✨ feat(scanner): validate string escapes with recovery`

## Task 3: 插值切分 + 嵌套 + ASI 抑制

- [ ] **Step 3.1** 追加失败测试：

```c
TEST(stringInterpSplit) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a = ${a}, sum = ${a + b}\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING,      TK_STR_PART, TK_INTERP_BEGIN, TK_ID,        TK_INTERP_END,
      TK_STR_PART,    TK_INTERP_BEGIN, TK_ID,       TK_PLUS,      TK_ID,
      TK_INTERP_END,  TK_STRING,   TK_EOF,
  };
  EXPECT_EQ(ctx, 13, toks.count);
  for (int i = 0; i < 13; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT(ctx, tokIs(&toks.items[1], TK_STR_PART, "a = "));
  EXPECT(ctx, tokIs(&toks.items[5], TK_STR_PART, ", sum = "));
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpNewlineNoAsi) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${1 +\n2}b\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_INT, TK_PLUS, TK_INT, TK_INTERP_END,
      TK_STR_PART, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 9, toks.count);  // 插值内换行无 ;（01 §7 细则 2）
  for (int i = 0; i < 9; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT(ctx, tokIs(&toks.items[6], TK_STR_PART, "b"));
  EXPECT_EQ(ctx, 2, toks.items[6].line);  // 段起点跟随插值内换行
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpBraceMatching) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${ {1: 2} }\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_LBRACE, TK_INT, TK_COLON, TK_INT, TK_RBRACE,
      TK_INTERP_END, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 10, toks.count);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpNested) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${\"in${x}\"}\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_STRING, TK_STR_PART, TK_INTERP_BEGIN, TK_ID,
      TK_INTERP_END, TK_STRING, TK_INTERP_END, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 11, toks.count);
  for (int i = 0; i < 11; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpNestedThreeDeep) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"${ \"${ '${a}' }\" }\"", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_INTERP_BEGIN, TK_STRING, TK_INTERP_BEGIN, TK_STRING, TK_INTERP_BEGIN,
      TK_RUNE, TK_INTERP_END, TK_STRING, TK_INTERP_END, TK_STRING, TK_INTERP_END,
      TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 14, toks.count);
  for (int i = 0; i < 14; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(stringInterpUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("\"a${x", &toks, &diags);
  static const MsTokenType types[] = {
      TK_STRING, TK_STR_PART, TK_INTERP_BEGIN, TK_ID, TK_STRING, TK_EOF,
  };
  EXPECT_EQ(ctx, 6, toks.count);
  for (int i = 0; i < 6; i++) {
    EXPECT_EQ(ctx, types[i], toks.items[i].type);
  }
  EXPECT_EQ(ctx, 2, diags.count);  // 插值域 + 字符串域各一
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_STRING_UNTERMINATED, (int)diags.items[1].code);
}

TEST(stringInterpDepthCapped) {
  MsDiagSink diags = {0};
  TokList toks;
  char src[MS_LEX_MAX_DEPTH * 4 + 64];
  int32_t n = 0;
  for (int32_t i = 0; i < MS_LEX_MAX_DEPTH + 1; i++) {
    memcpy(src + n, "\"${", 3);
    n += 3;
  }
  memcpy(src + n, "x", 1);
  n += 1;
  for (int32_t i = 0; i < MS_LEX_MAX_DEPTH + 1; i++) {
    memcpy(src + n, "}\"", 2);
    n += 2;
  }
  src[n] = '\0';
  scanAll(src, &toks, &diags);
  bool sawDepth = false;
  for (int32_t i = 0; i < diags.count; i++) {
    sawDepth = sawDepth || diags.items[i].code == MSE_LEX_DEPTH;
  }
  EXPECT(ctx, sawDepth);
  EXPECT(ctx, toks.count >= 2);
}
```

注：`stringInterpNestedThreeDeep` 依赖 Task 5 的 rune；若按序执行本测试先红后绿于 Task 5，或将其挪至 Task 5 后执行。

- [ ] **Step 3.2** 实现：scanStrDomain 的 `${` 分支 + pushInterp/popInterp + 主循环 `}` 闭合/ASI depth 条件/EOF 弹栈（见"关键状态机"）：

```c
// INTERP 域收尾：弹出并复位下层 STR 域的常量段起点
static void popInterp(Scanner* s) {
  s->depth--;
  if (s->depth > 0 && s->frames[s->depth - 1].domain == LEX_STR) {
    LexFrame* str = &s->frames[s->depth - 1];
    str->partStart = s->pos;
    str->partLine = s->line;
    str->partCol = s->col;
  }
}
```

主循环 '\n' 分支条件改：`if (s->pendingAsi && s->parenDepth == 0 && s->depth == 0)`；
分发表在 `}` 判定后依次增 `"`/`` ` ``/`'` 三分支；收口处追加 INTERP 帧大括号配对计数。

- [ ] **Step 3.3** 绿灯 + 无回归
- [ ] **Step 3.4** 提交：`✨ feat(scanner): split string interpolation into token segments`

## Task 4: 原始字符串

- [ ] **Step 4.1** 追加失败测试：

```c
TEST(rawStringPreservedVerbatim) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`a\\t${x}\\n`", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`a\\t${x}\\n`"));  // 字节级原样（含反引号）
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(rawStringMultilineBytes) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`a\nb` x", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`a\nb`"));
  EXPECT_EQ(ctx, 6, toks.items[0].len);  // 6 字节：跨行换行原样保留
  EXPECT(ctx, tokIs(&toks.items[1], TK_ID, "x"));
  EXPECT_EQ(ctx, 2, toks.items[1].line);
  EXPECT_EQ(ctx, 4, toks.items[1].col);
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(rawStringUnterminatedEof) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("`abc", &toks, &diags);
  EXPECT(ctx, tokIs(&toks.items[0], TK_RAW_STR, "`abc"));
  EXPECT_EQ(ctx, 2, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_RAW_UNTERMINATED, (int)diags.items[0].code);
}
```

- [ ] **Step 4.2** 实现：

```c
// 原始字符串（01 §3.3）：`...` 无转义无插值，跨行保留；token 文本含首尾反引号
static void scanRawString(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  advanceRune(s);  // 开 `
  while (s->pos < s->len && peek(s) != '`') {
    advanceRune(s);
  }
  if (s->pos < s->len) {
    advanceRune(s);  // 闭 `
  } else {
    addDiag(s, MSE_RAW_UNTERMINATED, line, col, 1, "unterminated raw string literal");
  }
  makeToken(s, out, TK_RAW_STR, start, line, col);
}
```

- [ ] **Step 4.3** 绿灯 + 无回归
- [ ] **Step 4.4** 提交：`✨ feat(scanner): scan raw string literals verbatim`

## Task 5: rune 字面量 + 取值函数

- [ ] **Step 5.1** 追加失败测试：

```c
TEST(runeFormsAndValues) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("'a' '\\n' '\\x41' '\\u{4E2D}' '\xe4\xb8\xad'", &toks, &diags);
  EXPECT_EQ(ctx, 6, toks.count);  // 5 RUNE + EOF
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(ctx, TK_RUNE, toks.items[i].type);
  }
  EXPECT_EQ(ctx, 97, (int)msRuneLiteralValue(&toks.items[0]));
  EXPECT_EQ(ctx, 10, (int)msRuneLiteralValue(&toks.items[1]));
  EXPECT_EQ(ctx, 65, (int)msRuneLiteralValue(&toks.items[2]));
  EXPECT_EQ(ctx, 0x4E2D, (int)msRuneLiteralValue(&toks.items[3]));
  EXPECT_EQ(ctx, 0x4E2D, (int)msRuneLiteralValue(&toks.items[4]));
  EXPECT_EQ(ctx, 27, toks.items[4].col);  // col 按 rune 计
  EXPECT_EQ(ctx, 0, diags.count);
}

TEST(runeInvalidForms) {
  struct {
    const char* src;
    const char* text;
    int code;
  } cases[] = {
      {"''", "''", MSE_RUNE_INVALID},
      {"'ab'", "'ab'", MSE_RUNE_INVALID},
      {"'a", "'a", MSE_RUNE_INVALID},
      {"'\\q'", "'\\q'", MSE_ESCAPE},
  };
  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    MsDiagSink diags = {0};
    TokList toks;
    scanAll(cases[i].src, &toks, &diags);
    EXPECT(ctx, tokIs(&toks.items[0], TK_RUNE, cases[i].text));
    EXPECT_EQ(ctx, 1, diags.count);
    EXPECT_EQ(ctx, cases[i].code, (int)diags.items[0].code);
  }
}

TEST(runeValueHelperGuards) {
  MsDiagSink diags = {0};
  TokList toks;
  scanAll("x ''", &toks, &diags);
  EXPECT_EQ(ctx, 0xFFFD, (int)msRuneLiteralValue(&toks.items[0]));  // 非 RUNE
  EXPECT_EQ(ctx, 0xFFFD, (int)msRuneLiteralValue(&toks.items[1]));  // 空 rune 文本
  EXPECT_EQ(ctx, 3, toks.count);
  EXPECT_EQ(ctx, 1, diags.count);
}
```

- [ ] **Step 5.2** 实现：

```c
// rune 字面量（01 §3.4）：'a' '\n' '\x41' '\u{4E2D}' '中'；空/多字符/未闭合发 MSE_RUNE_INVALID
// 恢复：仍产出 RUNE token（多字符吞到闭引号，未闭合停在行尾/EOF）
static void scanRune(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  advanceRune(s);  // 开 '
  uint8_t c = peek(s);
  if (c == '\'') {
    advanceRune(s);
    addDiag(s, MSE_RUNE_INVALID, line, col, 2, "rune literal cannot be empty");
  } else if (c == '\n' || c == 0) {
    addDiag(s, MSE_RUNE_INVALID, line, col, 1, "unterminated rune literal");
  } else {
    if (c == '\\') {
      uint32_t value = 0;
      scanEscape(s, &value);  // 非法转义在此诊断（MSE_ESCAPE）
    } else {
      advanceRune(s);  // 单 rune（多字节一并）
    }
    c = peek(s);
    if (c == '\'') {
      advanceRune(s);
    } else {  // 多字符或未闭合：吞到闭 ' / 换行 / EOF
      while (s->pos < s->len && peek(s) != '\'' && peek(s) != '\n') {
        advanceRune(s);
      }
      if (peek(s) == '\'') {
        advanceRune(s);
        addDiag(s, MSE_RUNE_INVALID, line, col, s->pos - start, "rune literal must contain exactly one character");
      } else {
        addDiag(s, MSE_RUNE_INVALID, line, col, s->pos - start, "unterminated rune literal");
      }
    }
  }
  makeToken(s, out, TK_RUNE, start, line, col);
}
```

```c
// RUNE token 文本解码为码点（扫描已校验；未知/残缺文本回退 0xFFFD）——T006 解析器复用
uint32_t msRuneLiteralValue(const MsToken* tok) {
  if (tok->type != TK_RUNE || tok->len < 3) {
    return 0xFFFD;
  }
  const char* p = tok->start + 1;
  int32_t avail = tok->len - 2;
  if (p[0] == '\\') {
    uint8_t c = (uint8_t)p[1];
    switch (c) {
      case 'n': return '\n';
      case 'r': return '\r';
      case 't': return '\t';
      case '\\': return '\\';
      case '"': return '"';
      case '\'': return '\'';
      case '0': return 0;
      case 'x': {
        if (avail < 4) {
          return 0xFFFD;
        }
        int hi = digitOfBase((uint8_t)p[2], 16), lo = digitOfBase((uint8_t)p[3], 16);
        return hi < 0 || lo < 0 ? 0xFFFD : (uint32_t)(hi * 16 + lo);
      }
      case 'u': {
        if (avail < 4 || p[2] != '{') {
          return 0xFFFD;
        }
        uint32_t value = 0;
        int32_t digits = 0;
        for (int32_t i = 3; i < avail && p[i] != '}'; i++) {
          int d = digitOfBase((uint8_t)p[i], 16);
          if (d < 0 || digits >= 6) {
            return 0xFFFD;
          }
          value = value * 16 + (uint32_t)d;
          digits++;
        }
        if (digits == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
          return 0xFFFD;
        }
        return value;
      }
      default: return 0xFFFD;
    }
  }
  int32_t width = 0;
  return utf8Decode(p, avail, &width);
}
```

- [ ] **Step 5.3** 绿灯 + 无回归（含 Task 3 挂起的 `stringInterpNestedThreeDeep`）
- [ ] **Step 5.4** 提交：`✨ feat(scanner): scan rune literals with codepoint value helper`

## Task 6: ms run 路径词法检查（退出码 3）

- [ ] **Step 6.1** `tools/ms.c`：占位 run 分支替换（slurpFile/normalizeLf/msDiagFormat 复用本文件既有函数）：

```c
// run 占位（T002）：T005 起先做词法检查——诊断打印 stderr、退出码 3（27 §10 编译错误；真实执行 T011 接入）
static int runScript(const char* path) {
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
  }
  for (int32_t i = 0; i < diags.count; i++) {
    char buf[512];
    msDiagFormat(&diags.items[i], src, buf, sizeof(buf));
    fputs(buf, stderr);
  }
  free(src);
  return diags.count > 0 ? 3 : 0;
}
```

`main` 中 `if (argc == 2 && isMsPath(argv[1])) { return runScript(argv[1]); }`

- [ ] **Step 6.2** 手工验证：`build\Debug\ms.exe tests\fixtures\smoke\hello.ms` → 退出码 0 无输出；临时坏文件 → stderr 含 `error[MSE0`、退出码 3
- [ ] **Step 6.3** 提交：`✨ feat(cli): lex-check scripts on run with compile-error exit code`

## Task 7: conformance 用例（4 golden + 3 err）

- [ ] **Step 7.1** 落盘 4 个 golden 用例（.ms + .args=`--dump-tokens` + .out）。.out 由手工推导（下表），执行时与实际 dump 逐行核对后方可落盘——差异须先判定实现 bug 还是推导笔误：

`01-lex-03-string-escapes.ms`（避免尾随空格 token）：

```text
"\n\r\t\\\"\0\x41\u{4E2D}"
""
"a$b"
```

期望 token 序列（列号按 rune）：L1 `1:1 STRING "` / `1:2 STR_PART \n\r\t\\\"\0\x41\u{4E2D}`（26 字符）/ `1:28 STRING "` / `1:29 SEMICOLON`；L2 `2:1 STRING "` / `2:2 STRING "` / `2:3 SEMICOLON`；L3 `3:1 STRING "` / `3:2 STR_PART a$b` / `3:5 STRING "` / `3:6 SEMICOLON`；`4:1 EOF`。

`01-lex-03b-string-interp.ms`：

```text
"a=${a}, sum=${a + b}"
"${"in${x}"}"
"${1 +
2}"
```

期望：L1 `1:1 STRING "` / `1:2 STR_PART a=` / `1:4 INTERP_BEGIN` / `1:6 ID a` / `1:7 INTERP_END` / `1:8 STR_PART , sum=` / `1:14 INTERP_BEGIN` / `1:16 ID a` / `1:18 PLUS` / `1:20 ID b` / `1:21 INTERP_END` / `1:22 STRING "` / `1:23 SEMICOLON`；L2 `2:1 STRING` / `2:2 INTERP_BEGIN` / `2:4 STRING` / `2:5 STR_PART in` / `2:7 INTERP_BEGIN` / `2:9 ID x` / `2:10 INTERP_END` / `2:11 STRING` / `2:12 INTERP_END` / `2:13 STRING` / `2:14 SEMICOLON`；L3-4 `3:1 STRING` / `3:2 INTERP_BEGIN` / `3:4 INT 1` / `3:6 PLUS` /（换行无 ;）/ `4:1 INT 2` / `4:2 INTERP_END` / `4:3 STRING` / `4:4 SEMICOLON`；`5:1 EOF`。

`01-lex-05-raw-string.ms`：

```text
`a\t${x}\n`
`line1
line2` x
```

期望：`1:1 RAW_STR \`a\t${x}\n\`` / `1:12 SEMICOLON` / `2:1 RAW_STR \`line1␤line2\``（跨行）/ `3:7 ID x` / `3:9 SEMICOLON` / `4:1 EOF`。

`01-lex-06-rune.ms`：

```text
'a' '\n' '\x41' '\u{4E2D}' '中'
```

期望：`1:1/1:5/1:10/1:17/1:27 RUNE` × 5 / `1:30 SEMICOLON` / `2:1 EOF`。

- [ ] **Step 7.2** 落盘 3 个 err 用例（无 .args，.out 为空文件，.code=`3`，.err=`error[MSE0`）：
  - `01-lex-err-01-escape.ms`：内容 `"\q"`
  - `01-lex-err-02-unterminated.ms`：内容 `"abc` + 换行 + `` `abc ``
  - `01-lex-err-03-rune.ms`：内容 `''` + 换行 + `'ab'`
- [ ] **Step 7.3** `ctest --test-dir build -C Debug -L conformance` 全绿（新增 7 例 + 既有 5 例）
- [ ] **Step 7.4** 提交：`🧪 test(conformance): add string/rune lex cases with error exits`

## Task 8: 全量验证 + 文档收尾

- [ ] **Step 8.1** 全量 `ctest --test-dir build -C Debug --output-on-failure` 全绿（预期 11 + 1 单测 + 7 conformance = 19 项）
- [ ] **Step 8.2** clang-format 校验改动文件（本机可用则 `clang-format --dry-run --Werror`，否则以 120 列/2 空格人工核对）；如本机有 clang-format，对 5 个 C 文件执行
- [ ] **Step 8.3** Release 构建亦绿：`cmake --build build --config Release` + `ctest --test-dir build -C Release`
- [ ] **Step 8.4** 文档状态流转：T005 文档头部"未开始"→"已完成"、DoD 全勾；`docs/tasks/README.md` T005 行 ⬜→✅
- [ ] **Step 8.5** 提交：`✅ docs(tasks): mark T005 done`
- [ ] **Step 8.6** 收尾（finishing-a-development-branch）：向用户报告并选择合并方式

## 验证命令汇总

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Debug -R scanner_string
build\Debug\ms.exe --dump-tokens tests\conformance\01-lex-03-string-escapes.ms
build\Debug\ms.exe <bad.ms>; echo "exit=$LASTEXITCODE"   # 期望 3
```

## 风险对照（任务 §6）

- 插值内注释+ASI 组合：主循环 prefix（注释/空白）对 INTERP 域照常，ASI 因 depth>0 抑制——由 `stringInterpNewlineNoAsi` 锁定。
- `\u{}` 多位十六进制按 rune 计列：advanceRune 逐 rune 前进，hex 均 ASCII 1 列/字符。
- `--dump-tokens` 保留不动（T038 复用）。

