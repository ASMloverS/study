# T006 AST 与基础语句实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 mslang 的 AST 节点集（arena 分配）与递归下降 Parser：`var`/`const`/`:=` 声明、赋值族（普通/复合/索引/成员/元组/解构）、表达式语句、`if/else if/else`、块作用域，以及语句级错误恢复。

**Architecture:** 单遍递归下降（20-architecture §1），Parser 内嵌 Scanner 逐 token 拉取。AST 节点与数组全部取自 bump 式 arena（整树一次性释放），文本一律借用源缓冲指针。Parser 维护轻量作用域栈，仅做「同作用域重复声明」与「顶层 `:=`」两个语法检查。错误恢复 = 语句级 panic + 同步至 `;`/`}` 边界。

**Tech Stack:** C11（MSVC /W4 /WX + GCC/Clang -Wall -Wextra -Werror）、CMake ≥3.16、仓库自带 test_harness 断言宏。设计决策见 `docs/superpowers/specs/2026-10-11-t006-ast-basic-statements-design.md`。

**工作目录（worktree）:** `C:\Workspace\Repositories\study\.worktrees\t006-ast-basic-statements\aicode\mslang-cc`（分支 `t006-ast-basic-statements`）。下文所有相对路径均基于此目录；命令为 PowerShell。

**构建/测试命令：**

```powershell
cmake --build build --config Debug --target test_parser_basic   # 单测目标
& build\tests\Debug\test_parser_basic.exe                        # 直接运行（输出 all tests passed）
ctest --test-dir build -C Debug -R parser_basic --output-on-failure
ctest --test-dir build -C Debug                                  # 全量（交付前）
```

---

## 文件结构

| 文件 | 职责 | 任务 |
|------|------|------|
| `src/parser/ast.h`（新增） | AstKind 全集枚举（含 T007/T008 扩展位）、AstNode 载荷 union、AstStrSeg、AstBinOp、AstArena API | T1 定稿 |
| `src/parser/parser.h`（新增） | Parser API + MseSyntaxCode（MSE1000-1008） | T2 定稿 |
| `src/parser/parser.c`（覆盖占位） | arena 实现 + 作用域栈 + 递归下降解析 | T1-T6 递增 |
| `tests/unit/test_parser_basic.c`（新增） | 单测（内嵌源码片段断言 AST） | T1-T6 递增 |
| `CMakeLists.txt`（修改） | ms_parser 链接 ms_scanner | T2 |
| `tests/CMakeLists.txt`（修改） | 注册 parser_basic | T1 |
| `docs/tasks/T006-ast-basic-statements.md`、`docs/tasks/README.md`（修改） | 状态标记 | T0 / T7 |

任务序列：T0 状态标记 → T1 arena+AST 头 → T2 骨架+字面量 → T3 后缀索引/成员 → T4 声明+作用域 → T5 赋值族+比较 → T6 if+错误恢复+统计 → T7 验证收尾。每个任务 = 失败测试 → 实现 → 通过 → 提交。

---

### Task 0: 标记任务开工

**Files:**
- Modify: `docs/tasks/T006-ast-basic-statements.md:6`（`- 状态: 未开始` → `- 状态: 进行中`）
- Modify: `docs/tasks/README.md:27`（T006 行 `⬜` → `🚧`）

- [ ] **Step 0.1:** 两处状态改为 🚧，提交：`git commit -m "🚧 docs(tasks): mark T006 in progress"`（仅这两文件）。

---

### Task 1: AST 头与 arena

**Files:**
- Create: `src/parser/ast.h`
- Modify: `src/parser/parser.c`（覆盖占位，先只含 arena 实现）
- Modify: `tests/CMakeLists.txt`（注册测试）
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 1.1: 写失败测试** — 创建 `tests/unit/test_parser_basic.c`：

```c
// T006：Parser 声明/基础语句单测（docs/tasks/T006 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "parser/parser.h"
#include "test_harness.h"

// 解析整模块：返回 AST_MODULE 根（调用方负责 arena 销毁）
static AstNode* parseSrc(const char* src, AstArena* arena, MsDiagSink* diags) {
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", arena, diags);
  return parserParseModule(&p);
}

TEST(arenaAllocAndStats) {
  AstArena arena;
  astArenaInit(&arena);
  EXPECT_EQ(ctx, 0, (int)astArenaBytesUsed(&arena));
  EXPECT_EQ(ctx, 0, arena.nodeCount);
  AstNode* node = astNewNode(&arena, AST_MODULE, 3, 7);
  EXPECT_EQ(ctx, AST_MODULE, node->kind);
  EXPECT_EQ(ctx, 3, node->line);
  EXPECT_EQ(ctx, 7, node->col);
  EXPECT(ctx, node->as.list.items == NULL);  // 载荷清零
  EXPECT_EQ(ctx, 1, arena.nodeCount);
  EXPECT(ctx, astArenaBytesUsed(&arena) >= sizeof(AstNode));
  EXPECT(ctx, astArenaAlloc(&arena, 100000) != NULL);  // 超大分配单独成块
  astArenaDestroy(&arena);
  EXPECT_EQ(ctx, 0, (int)astArenaBytesUsed(&arena));
  EXPECT_EQ(ctx, 0, arena.nodeCount);
}

int main(void) {
  TestCtx ctx = {0};
  arenaAllocAndStats(&ctx);
  if (ctx.failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d check(s) failed\n", ctx.failures);
  return 1;
}
```

- [ ] **Step 1.2:** `tests/CMakeLists.txt` 追加一行 `ms_add_test(parser_basic ms_parser)`。
- [ ] **Step 1.3: 验证失败** — `cmake --build build --config Debug --target test_parser_basic` → 编译失败（找不到 `parser/parser.h`）。
- [ ] **Step 1.4: 实现** — 创建 `src/parser/ast.h`：

```c
// AST 节点集与区段分配器（T006）：全语言节点枚举一次定义到位，T007/T008 仅补载荷
// 节点与数组一律取自 arena（整树随 arena 一次性释放）；文本一律借用源缓冲指针
#ifndef INCLUDE_MS_PARSER_AST_H_
#define INCLUDE_MS_PARSER_AST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/common.h"

typedef enum AstKind {
  AST_MODULE,       // 程序体：as.list
  AST_BLOCK,        // 块语句 { }：as.list
  AST_VAR_DECL,     // var/const/:= 声明：as.varDecl（flags 区分形态）
  AST_ASSIGN,       // 赋值（普通/复合/元组目标）：as.assign
  AST_DESTRUCTURE,  // 含 *rest 的解构赋值：as.destructure
  AST_EXPR_STMT,    // 表达式语句：as.exprStmt
  AST_IF,           // if / else if / else：as.ifStmt
  // T008 语句扩展位（本任务仅定义枚举位，解析后续任务填入）
  AST_FOR, AST_WHILE, AST_SWITCH, AST_SWITCH_CASE, AST_BREAK, AST_CONTINUE, AST_RETURN,
  AST_THROW, AST_DEFER, AST_SEND, AST_SELECT, AST_SELECT_CASE, AST_IMPORT, AST_FUNC_DECL,
  AST_CLASS_DECL,
  // 表达式
  AST_IDENT,    // as.ident
  AST_NIL,      // 无载荷
  AST_BOOL,     // as.boolLit
  AST_INT,      // as.intLit
  AST_FLOAT,    // as.floatLit
  AST_BIGINT,   // as.bigintLit（借用源文本，显式 n 后缀已剥离）
  AST_STRING,   // as.strLit（普通串段列表 / 原始串单段）
  AST_RUNE,     // as.runeLit
  AST_TUPLE,    // 逗号列表（元组字面量/元组赋值目标）：as.tuple
  AST_BINARY,   // as.binary（本任务仅比较级；完整优先级阶梯 T007）
  AST_INDEX,    // as.indexExpr（后缀 [e]，赋值目标用）
  AST_MEMBER,   // as.memberExpr（后缀 .name）
  // T007 表达式扩展位（本任务仅定义枚举位）
  AST_UNARY, AST_LOGIC, AST_TERNARY, AST_CALL, AST_SLICE, AST_LIST, AST_MAP, AST_SET,
  AST_COMPREHENSION, AST_GEN_EXPR, AST_FUNC_EXPR, AST_RECV, AST_AWAIT, AST_GO,
} AstKind;

// 二元运算符：复合赋值展开与 AST_BINARY 共用；token → op 映射见 parser.c
typedef enum AstBinOp {
  AST_OP_NONE = 0,
  AST_OP_ADD, AST_OP_SUB, AST_OP_MUL, AST_OP_DIV, AST_OP_MOD, AST_OP_POW,
  AST_OP_BIT_AND, AST_OP_BIT_OR, AST_OP_BIT_XOR, AST_OP_SHL, AST_OP_SHR,
  AST_OP_EQ, AST_OP_NE, AST_OP_LT, AST_OP_LE, AST_OP_GT, AST_OP_GE,
  AST_OP_AND, AST_OP_OR,  // 短路逻辑（T007 AST_LOGIC 载荷）
} AstBinOp;

// var 声明形态标志（as.varDecl.flags 位域）
enum {
  AST_VAR_SHORT = 1,  // x := e
  AST_VAR_CONST = 2,  // const x = e
};

typedef struct AstNode AstNode;

// 字符串段：expr != NULL 为插值段，否则为字面段（借用源文本，转义原文保留）
typedef struct AstStrSeg {
  AstNode* expr;
  const char* start;
  int32_t len;
} AstStrSeg;

struct AstNode {
  AstKind kind;
  int32_t line, col;  // 构造起始 token 位置（1-based，col 按 rune 计）
  union {
    struct { AstNode** items; int32_t count; } list;  // MODULE/BLOCK 语句序列
    struct { const char* name; int32_t nameLen; ms_u8 flags; AstNode* init; } varDecl;
    struct { AstNode* target; AstNode* value; AstBinOp op; } assign;  // op==NONE 普通赋值
    struct { AstNode** targets; int32_t count; int32_t restIndex; AstNode* value; } destructure;
    struct { AstNode* expr; } exprStmt;
    struct { AstNode* cond; AstNode* thenBlock; AstNode* elseNode; } ifStmt;
    struct { const char* name; int32_t nameLen; } ident;
    struct { bool b; } boolLit;
    struct { ms_i64 v; } intLit;
    struct { double v; } floatLit;
    struct { const char* start; int32_t len; } bigintLit;
    struct { bool raw; AstStrSeg* segs; int32_t count; } strLit;
    struct { uint32_t cp; } runeLit;
    struct { AstNode** items; int32_t count; } tuple;
    struct { AstNode* lhs; AstNode* rhs; AstBinOp op; } binary;
    struct { AstNode* obj; AstNode* index; } indexExpr;
    struct { AstNode* obj; const char* name; int32_t nameLen; } memberExpr;
  } as;
};

// 区段分配器：整棵 AST 一次性释放；8 字节对齐（节点内最长成员为指针/double/i64）
typedef struct AstArenaBlock {
  struct AstArenaBlock* next;
  size_t used, cap;  // cap 为载荷容量（不含块头）
} AstArenaBlock;

typedef struct AstArena {
  AstArenaBlock* head;  // 当前分配块（链向更早块）
  size_t bytesUsed;     // 累计分配字节（含对齐垫片；快照测试辅助，DoD §4）
  int32_t nodeCount;    // astNewNode 累计节点数
} AstArena;

void astArenaInit(AstArena* arena);
void astArenaDestroy(AstArena* arena);
void* astArenaAlloc(AstArena* arena, size_t bytes);
AstNode* astNewNode(AstArena* arena, AstKind kind, int32_t line, int32_t col);
AstNode** astNewNodeArray(AstArena* arena, int32_t count);
size_t astArenaBytesUsed(const AstArena* arena);

#endif  // INCLUDE_MS_PARSER_AST_H_
```

- [ ] **Step 1.5: 实现** — 覆盖 `src/parser/parser.c`（本任务仅 arena 部分 + 最小 parser.h 依赖；parser.h 在 Task 2 落盘，此处先创建仅含 init/parseModule 骨架的头）：

`src/parser/parser.h`（Task 1 最小版，Task 2 扩充错误码与字段）：

```c
// 递归下降 Parser（T006）：声明/基础语句 + 最小表达式集（见 docs/tasks/T006 §2）
// 错误恢复：语句级 panic + 同步至 ; 或 } 边界；单次编译 ≤ MS_DIAG_MAX 条诊断
#ifndef INCLUDE_MS_PARSER_PARSER_H_
#define INCLUDE_MS_PARSER_PARSER_H_

#include <stdbool.h>
#include <stdint.h>

#include "parser/ast.h"
#include "scanner/scanner.h"

// 语法错误码（MSE1xxx 段，03/04 文档；诊断 code 字段取枚举值）
typedef enum MseSyntaxCode {
  MSE_SYN_UNEXPECTED = 1000,      // 意外 token（含缺表达式）
  MSE_SYN_EXPECTED = 1001,        // 缺少期望的语法成分（消息指明）
  MSE_SYN_SHORT_TOPLEVEL = 1002,  // := 出现在模块顶层（04 §1）
  MSE_SYN_IF_PAREN = 1003,        // if 条件加了括号（04 §3，Go 风格强制）
  MSE_SYN_DUP_DECL = 1004,        // 同作用域重复声明（04 §1）
  MSE_SYN_DESTRUCT_STARS = 1005,  // 解构目标 * 多于一个（04 §2）
  MSE_SYN_BAD_TARGET = 1006,      // 非法赋值目标 / 短声明目标
  MSE_SYN_CHAINED_CMP = 1007,     // 比较运算符不可链式（03 优先级表）
  MSE_SYN_STAR_NO_ASSIGN = 1008,  // * 目标只能出现在解构赋值
} MseSyntaxCode;

typedef struct ParserScope ParserScope;

typedef struct Parser {
  Scanner scan;         // 内嵌：逐 token 拉取（20-architecture §1）
  MsToken cur;          // 当前 lookahead（单 token，不可回退）
  AstArena* arena;
  MsDiagSink* diags;    // 可为 NULL（免诊断场景）
  ParserScope* scope;   // 声明查重作用域链（完整解析在 T009 Binder）
  bool panic;           // 恢复中：屏蔽级联诊断直至同步点
} Parser;

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena,
                MsDiagSink* diags);
AstNode* parserParseModule(Parser* p);  // 失败也返回部分树（诊断在 sink）

#endif  // INCLUDE_MS_PARSER_PARSER_H_
```

`src/parser/parser.c`（Task 1 版 = arena 实现 + 占位 API；Task 2 起扩充解析逻辑）：

```c
// 递归下降 Parser 与 AST arena（T006）：声明/基础语句 + 最小表达式集
// 见 docs/tasks/T006-ast-basic-statements.md 与 docs/language/04-statements.md §1-§3 §10
#include "parser/parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// ---------------- AST arena ----------------

#define AST_ARENA_BLOCK_PAYLOAD 4096  // 单块载荷容量（块头另计）

void astArenaInit(AstArena* arena) {
  arena->head = NULL;
  arena->bytesUsed = 0;
  arena->nodeCount = 0;
}

void astArenaDestroy(AstArena* arena) {
  AstArenaBlock* block = arena->head;
  while (block != NULL) {
    AstArenaBlock* next = block->next;
    free(block);
    block = next;
  }
  arena->head = NULL;
  arena->bytesUsed = 0;
  arena->nodeCount = 0;
}

void* astArenaAlloc(AstArena* arena, size_t bytes) {
  bytes = (bytes + 7) & ~(size_t)7;  // 8 字节对齐（malloc 返回值对齐得以保持）
  if (arena->head == NULL || arena->head->cap - arena->head->used < bytes) {
    size_t cap = bytes > AST_ARENA_BLOCK_PAYLOAD ? bytes : AST_ARENA_BLOCK_PAYLOAD;
    AstArenaBlock* block = malloc(sizeof(AstArenaBlock) + cap);
    MS_ASSERT(block != NULL);  // OOM 设计上致命（与 MS_ARRAY_PUSH 约定一致）
    block->next = arena->head;
    block->used = 0;
    block->cap = cap;
    arena->head = block;
  }
  void* payload = (char*)(arena->head + 1) + arena->head->used;
  arena->head->used += bytes;
  arena->bytesUsed += bytes;
  return payload;
}

AstNode* astNewNode(AstArena* arena, AstKind kind, int32_t line, int32_t col) {
  AstNode* node = astArenaAlloc(arena, sizeof(AstNode));
  node->kind = kind;
  node->line = line;
  node->col = col;
  memset(&node->as, 0, sizeof(node->as));  // 载荷默认清零（init/items 等 NULL）
  arena->nodeCount++;
  return node;
}

AstNode** astNewNodeArray(AstArena* arena, int32_t count) {
  AstNode** items = astArenaAlloc(arena, sizeof(AstNode*) * (size_t)count);
  memset(items, 0, sizeof(AstNode*) * (size_t)count);
  return items;
}

size_t astArenaBytesUsed(const AstArena* arena) { return arena->bytesUsed; }

// ---------------- Parser API（Task 2 起充实） ----------------

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena,
                MsDiagSink* diags) {
  scannerInit(&p->scan, src, len, file, diags);
  p->arena = arena;
  p->diags = diags;
  p->scope = NULL;
  p->panic = false;
  advance(p);
}

AstNode* parserParseModule(Parser* p) {
  AstNode* node = astNewNode(p->arena, AST_MODULE, p->cur.line, p->cur.col);
  node->as.list.count = 0;
  return node;
}
```

注意：Task 1 的 parser.c 中 `advance` 尚未定义，`parserInit` 暂不调用它（去掉 `advance(p);` 一行），Task 2 引入。即 Task 1 版本 parserInit 末尾无 advance 调用。

- [ ] **Step 1.6: 验证通过** — 构建 + 运行 `test_parser_basic` → `all tests passed`；`ctest --test-dir build -C Debug` 全绿（19+1=20 项）。
- [ ] **Step 1.7: 提交** — `git add` 后提交 `✨ feat(parser): add AST arena and full node kind set`。

---

### Task 2: Parser 骨架 + 字面量表达式语句

**Files:**
- Modify: `src/parser/parser.c`（token 助手/诊断/恢复 + primary 字面量 + 语句序列）
- Modify: `CMakeLists.txt`（ms_parser 链接 ms_scanner）
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 2.1: 写失败测试** — test_parser_basic.c 的 `main` 前追加用例（并登记到 main）：

```c
TEST(parseEmptyModule) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("", &arena, &diags);
  EXPECT(ctx, mod != NULL && mod->kind == AST_MODULE);
  EXPECT_EQ(ctx, 0, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(parseExprStmtLiterals) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x\n42\n3.5\ntrue\nnil", &arena, &diags);
  EXPECT_EQ(ctx, 5, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode** s = mod->as.list.items;
  EXPECT_EQ(ctx, AST_EXPR_STMT, s[0]->kind);
  EXPECT_EQ(ctx, AST_IDENT, s[0]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, AST_INT, s[1]->as.exprStmt.expr->kind);
  EXPECT_EQ(ctx, 42, (int)s[1]->as.exprStmt.expr->as.intLit.v);
  EXPECT_EQ(ctx, AST_FLOAT, s[2]->as.exprStmt.expr->kind);
  EXPECT_F64_EQ(ctx, 3.5, s[2]->as.exprStmt.expr->as.floatLit.v);
  EXPECT_EQ(ctx, AST_BOOL, s[3]->as.exprStmt.expr->kind);
  EXPECT(ctx, s[3]->as.exprStmt.expr->as.boolLit.b);
  EXPECT_EQ(ctx, AST_NIL, s[4]->as.exprStmt.expr->kind);
  astArenaDestroy(&arena);
}

TEST(parseIntRadixForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("0x1F\n0o17\n0b101\n1_000", &arena, &diags);
  EXPECT_EQ(ctx, 4, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  int expect[] = {31, 15, 5, 1000};
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(ctx, expect[i], (int)mod->as.list.items[i]->as.exprStmt.expr->as.intLit.v);
  }
  astArenaDestroy(&arena);
}

TEST(parseBigintAndRune) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("99999999999999999999n\n'a'", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  AstNode* big = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIGINT, big->kind);
  EXPECT_EQ(ctx, 20, big->as.bigintLit.len);  // n 后缀已剥离
  EXPECT(ctx, memcmp(big->as.bigintLit.start, "99999999999999999999", 20) == 0);
  AstNode* rune = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_RUNE, rune->kind);
  EXPECT_EQ(ctx, 97, (int)rune->as.runeLit.cp);
  astArenaDestroy(&arena);
}

TEST(parseNodePositions) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x\n  y", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 1, mod->as.list.items[0]->line);
  EXPECT_EQ(ctx, 1, mod->as.list.items[0]->col);
  EXPECT_EQ(ctx, 2, mod->as.list.items[1]->line);
  EXPECT_EQ(ctx, 3, mod->as.list.items[1]->col);  // col 按 rune 计
  astArenaDestroy(&arena);
}

TEST(parseStringSegments) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var s = x", &arena, &diags);  // 占位：Task 4 前无声明，改用下式
  astArenaDestroy(&arena);
}
```

说明：Task 2 阶段尚无 `var`，字符串段用例改挂在表达式语句上（Task 4 再移入 var 初值）：

```c
TEST(parseStringLiterals) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("\"a${x}b\"\n\"\"\n`raw`", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* s = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, s->kind);
  EXPECT_EQ(ctx, 3, s->as.strLit.count);
  EXPECT(ctx, s->as.strLit.segs[0].expr == NULL);  // 字面段 "a"
  EXPECT_EQ(ctx, 1, s->as.strLit.segs[0].len);
  EXPECT_EQ(ctx, 'a', s->as.strLit.segs[0].start[0]);
  EXPECT_EQ(ctx, AST_IDENT, s->as.strLit.segs[1].expr->kind);  // 插值段
  EXPECT(ctx, s->as.strLit.segs[2].expr == NULL);  // 字面段 "b"
  EXPECT_EQ(ctx, 'b', s->as.strLit.segs[2].start[0]);
  AstNode* empty = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, empty->kind);
  EXPECT_EQ(ctx, 0, empty->as.strLit.count);
  AstNode* raw = mod->as.list.items[2]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_STRING, raw->kind);
  EXPECT(ctx, raw->as.strLit.raw);
  EXPECT_EQ(ctx, 1, raw->as.strLit.count);
  EXPECT_EQ(ctx, 4, raw->as.strLit.segs[0].len);  // 反引号已剥
  astArenaDestroy(&arena);
}

TEST(parseBareSemicolonsSilent) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x;\n;\n42", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);  // 孤立/尾随 ; 均静默消费（04 §10）
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(parseUnterminatedBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ x", &arena, &diags);
  EXPECT_EQ(ctx, 0, mod->as.list.count);  // 块失败被同步跳过
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_EXPECTED, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```

（`parseStringSegments` 占位用例删除，仅保留 `parseStringLiterals`。）

- [ ] **Step 2.2: 验证失败** — 构建失败或用例断言失败（模块 count=0、无 EXPR_STMT 等）。
- [ ] **Step 2.3:** `CMakeLists.txt` 在 scanner 的 `target_sources` 行之后追加：

```cmake
# T006：parser 逐 token 拉取，依赖 scanner
target_link_libraries(ms_parser PUBLIC ms_scanner)
```

- [ ] **Step 2.4: 实现** — parser.c 在 arena 段之后插入以下内容（替换 Task 1 的 `Parser API` 段；`advance` 现已定义，`parserInit` 恢复调用）：

```c
// ---------------- token 助手与诊断 ----------------

static bool check(const Parser* p, MsTokenType type) { return p->cur.type == type; }

static void advance(Parser* p) { scannerNext(&p->scan, &p->cur); }  // EOF 幂等

static bool match(Parser* p, MsTokenType type) {
  if (check(p, type)) {
    advance(p);
    return true;
  }
  return false;
}

static void diagPos(Parser* p, int32_t line, int32_t col, int32_t len, uint32_t code,
                    const char* message) {
  if (p->diags == NULL) {
    return;
  }
  MsDiagnostic diag = {MS_DIAG_ERROR, code, p->scan.file, line, col, len, message};
  msDiagAdd(p->diags, &diag);
}

// 进 panic 的错误：屏蔽级联诊断直至 synchronize（T006 §2 错误恢复）
static void errTok(Parser* p, const MsToken* tok, uint32_t code, const char* message) {
  if (p->panic) {
    return;
  }
  p->panic = true;
  diagPos(p, tok->line, tok->col, tok->len, code, message);
}

static void errPos(Parser* p, int32_t line, int32_t col, int32_t len, uint32_t code,
                   const char* message) {
  if (p->panic) {
    return;
  }
  p->panic = true;
  diagPos(p, line, col, len, code, message);
}

// 丢弃至语句边界：; 消费后停，} 停留（属外层块），EOF 终止（T006 §2）
static void synchronize(Parser* p) {
  p->panic = false;
  while (!check(p, TK_EOF)) {
    if (p->cur.type == TK_SEMICOLON) {
      advance(p);
      return;
    }
    if (p->cur.type == TK_RBRACE) {
      return;
    }
    advance(p);
  }
}

// ---------------- 数值求值 ----------------

// INT token 求值：剥下划线、按前缀进制累加（溢出已由 scanner 升 BIGINT，必不溢 i64）
static ms_i64 intTokenValue(const MsToken* tok) {
  int base = 10;
  int32_t i = 0;
  if (tok->len > 2 && tok->start[0] == '0') {
    if (tok->start[1] == 'x' || tok->start[1] == 'X') {
      base = 16;
      i = 2;
    } else if (tok->start[1] == 'o' || tok->start[1] == 'O') {
      base = 8;
      i = 2;
    } else if (tok->start[1] == 'b') {
      base = 2;
      i = 2;
    }
  }
  ms_u64 value = 0;
  for (; i < tok->len; i++) {
    char c = tok->start[i];
    if (c == '_') {
      continue;
    }
    int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
    value = value * (ms_u64)base + (ms_u64)digit;
  }
  return (ms_i64)value;
}

// FLOAT token 求值：剥下划线后 strtod（超长截断：病态长字面量不追求精确）
static double floatTokenValue(const MsToken* tok) {
  char buf[512];
  int32_t n = 0;
  for (int32_t i = 0; i < tok->len && n < 511; i++) {
    if (tok->start[i] != '_') {
      buf[n++] = tok->start[i];
    }
  }
  buf[n] = '\0';
  return strtod(buf, NULL);
}

// ---------------- 表达式（最小集，T006 §1） ----------------

static AstNode* parseExpr(Parser* p);

// 普通字符串段装配：STRING(开) (STR_PART | INTERP_BEGIN expr INTERP_END)* STRING(闭)
static AstNode* parseStringLit(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // 开 STRING
  AstStrSeg* tmp = NULL;
  int32_t count = 0, cap = 0;
  while (!check(p, TK_STRING) && !check(p, TK_EOF)) {
    if (check(p, TK_STR_PART)) {
      MS_ARRAY_PUSH(tmp, count, cap, ((AstStrSeg){NULL, p->cur.start, p->cur.len}));
      advance(p);
    } else if (match(p, TK_INTERP_BEGIN)) {
      AstNode* expr = parseExpr(p);
      if (expr == NULL) {
        free(tmp);
        return NULL;
      }
      if (!match(p, TK_INTERP_END)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}' to close interpolation");
        free(tmp);
        return NULL;
      }
      MS_ARRAY_PUSH(tmp, count, cap, ((AstStrSeg){expr, NULL, 0}));
    } else {
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "unexpected token in string literal");
      free(tmp);
      return NULL;
    }
  }
  match(p, TK_STRING);  // 闭 STRING（EOF 到此：scanner 已对未闭合串内部补闭引号）
  AstNode* node = astNewNode(p->arena, AST_STRING, line, col);
  node->as.strLit.raw = false;
  node->as.strLit.count = count;
  node->as.strLit.segs = astArenaAlloc(p->arena, sizeof(AstStrSeg) * (size_t)count);
  memcpy(node->as.strLit.segs, tmp, sizeof(AstStrSeg) * (size_t)count);
  free(tmp);
  return node;
}

static AstNode* parsePrimary(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  switch (p->cur.type) {
    case TK_ID: {
      AstNode* node = astNewNode(p->arena, AST_IDENT, line, col);
      node->as.ident.name = p->cur.start;
      node->as.ident.nameLen = p->cur.len;
      advance(p);
      return node;
    }
    case TK_NIL: {
      AstNode* node = astNewNode(p->arena, AST_NIL, line, col);
      advance(p);
      return node;
    }
    case TK_TRUE:
    case TK_FALSE: {
      AstNode* node = astNewNode(p->arena, AST_BOOL, line, col);
      node->as.boolLit.b = p->cur.type == TK_TRUE;
      advance(p);
      return node;
    }
    case TK_INT: {
      AstNode* node = astNewNode(p->arena, AST_INT, line, col);
      node->as.intLit.v = intTokenValue(&p->cur);
      advance(p);
      return node;
    }
    case TK_FLOAT: {
      AstNode* node = astNewNode(p->arena, AST_FLOAT, line, col);
      node->as.floatLit.v = floatTokenValue(&p->cur);
      advance(p);
      return node;
    }
    case TK_BIGINT: {
      AstNode* node = astNewNode(p->arena, AST_BIGINT, line, col);
      const char* start = p->cur.start;
      int32_t len = p->cur.len;
      if (len > 0 && start[len - 1] == 'n') {  // 显式 n 后缀剥离
        len--;
      }
      node->as.bigintLit.start = start;
      node->as.bigintLit.len = len;
      advance(p);
      return node;
    }
    case TK_RUNE: {
      AstNode* node = astNewNode(p->arena, AST_RUNE, line, col);
      node->as.runeLit.cp = msRuneLiteralValue(&p->cur);
      advance(p);
      return node;
    }
    case TK_STRING:
      return parseStringLit(p);
    case TK_RAW_STR: {
      AstNode* node = astNewNode(p->arena, AST_STRING, line, col);
      node->as.strLit.raw = true;
      node->as.strLit.count = 1;
      node->as.strLit.segs = astArenaAlloc(p->arena, sizeof(AstStrSeg));
      // 剥反引号；未闭合 raw（scanner 已报 MSE0xxx）时长度可能偏 1，容错保留
      node->as.strLit.segs[0] = (AstStrSeg){NULL, p->cur.start + 1, p->cur.len - 2};
      advance(p);
      return node;
    }
    default:
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "expected expression");
      return NULL;
  }
}

// 最小表达式：本任务仅 primary（后缀 Task 3、比较 Task 5、完整阶梯 T007）
static AstNode* parseExpr(Parser* p) { return parsePrimary(p); }

// ---------------- 语句 ----------------

static AstNode* parseStatement(Parser* p);

// 语句序列（模块体/块体共用）：孤立 ; 静默跳过（04 §10）；错误语句同步后继续
static AstNode** parseStmtList(Parser* p, int32_t* outCount, MsTokenType end) {
  AstNode** tmp = NULL;
  int32_t count = 0, cap = 0;
  while (!check(p, end) && !check(p, TK_EOF)) {
    if (match(p, TK_SEMICOLON)) {
      continue;
    }
    AstNode* stmt = parseStatement(p);
    if (stmt != NULL) {
      MS_ARRAY_PUSH(tmp, count, cap, stmt);
    } else {
      synchronize(p);
    }
  }
  AstNode** items = astNewNodeArray(p->arena, count);
  memcpy(items, tmp, sizeof(AstNode*) * (size_t)count);
  free(tmp);
  *outCount = count;
  return items;
}

// 块语句：{ 开新作用域（04 §10；作用域栈 Task 4 接入）
static AstNode* parseBlock(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{'");  // 块必须花括号（04 §3）
    return NULL;
  }
  int32_t count = 0;
  AstNode** items = parseStmtList(p, &count, TK_RBRACE);
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}'");  // EOF 截断
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_BLOCK, line, col);
  node->as.list.items = items;
  node->as.list.count = count;
  return node;
}

// 表达式语句（04 §10）：任意表达式 + ASI 换行即语句
static AstNode* parseSimpleStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  AstNode* expr = parseExpr(p);
  if (expr == NULL) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_EXPR_STMT, line, col);
  node->as.exprStmt.expr = expr;
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// 简单语句收尾：; / } / EOF 任一（ASI 产物与源码 ; 均静默接受，04 §10）
static bool endSimpleStmt(Parser* p) {
  if (match(p, TK_SEMICOLON)) {
    return true;
  }
  if (check(p, TK_RBRACE) || check(p, TK_EOF)) {
    return true;
  }
  errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected newline or ';' after statement");
  return false;
}

static AstNode* parseStatement(Parser* p) {
  if (check(p, TK_LBRACE)) {
    return parseBlock(p);  // 块语句（自带边界，无需 ASI）
  }
  return parseSimpleStmt(p);
}

// ---------------- Parser API ----------------

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena,
                MsDiagSink* diags) {
  scannerInit(&p->scan, src, len, file, diags);
  p->arena = arena;
  p->diags = diags;
  p->scope = NULL;
  p->panic = false;
  advance(p);  // 拉取首 token
}

AstNode* parserParseModule(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  int32_t count = 0;
  AstNode** items = parseStmtList(p, &count, TK_EOF);
  AstNode* node = astNewNode(p->arena, AST_MODULE, line, col);
  node->as.list.items = items;
  node->as.list.count = count;
  return node;
}
```

注意：`endSimpleStmt` 需前移到 `parseSimpleStmt` 之前（或加前向声明）。`parseSrc` 测试助手中的 `Parser p` 为栈变量，`p.scope` 在 Task 4 前恒 NULL。

- [ ] **Step 2.5: 验证通过** — 全部新用例通过，全量 ctest 绿。
- [ ] **Step 2.6: 提交** — `✨ feat(parser): parse module statements with literal expressions`。

---

### Task 3: 后缀索引/成员表达式

**Files:**
- Modify: `src/parser/parser.c`
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 3.1: 写失败测试：**

```c
TEST(parseIndexMemberPostfix) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[0]\nobj.f\nm[0].f", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* idx = mod->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_INDEX, idx->kind);
  EXPECT_EQ(ctx, AST_IDENT, idx->as.indexExpr.obj->kind);
  EXPECT_EQ(ctx, AST_INT, idx->as.indexExpr.index->kind);
  AstNode* mem = mod->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_MEMBER, mem->kind);
  EXPECT_EQ(ctx, AST_IDENT, mem->as.memberExpr.obj->kind);
  EXPECT_EQ(ctx, 1, mem->as.memberExpr.nameLen);  // f
  EXPECT_EQ(ctx, 'f', mem->as.memberExpr.name[0]);
  AstNode* mix = mod->as.list.items[2]->as.exprStmt.expr;  // 后缀链左结合
  EXPECT_EQ(ctx, AST_MEMBER, mix->kind);
  EXPECT_EQ(ctx, AST_INDEX, mix->as.memberExpr.obj->kind);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 3.2: 验证失败** — 断言失败（INDEX 未产出）。
- [ ] **Step 3.3: 实现** — `parseExpr` 与 `parsePrimary` 之间插入 `parsePostfix`，`parseExpr` 改为调用它：

```c
// 后缀链：[e] 索引 / .name 成员（调用/切片 T007）
static AstNode* parsePostfix(Parser* p) {
  AstNode* obj = parsePrimary(p);
  if (obj == NULL) {
    return NULL;
  }
  for (;;) {
    if (check(p, TK_LBRACKET)) {
      int32_t line = obj->line, col = obj->col;  // 构造起点取对象起点
      advance(p);
      AstNode* index = parseExpr(p);
      if (index == NULL) {
        return NULL;
      }
      if (!match(p, TK_RBRACKET)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_INDEX, line, col);
      node->as.indexExpr.obj = obj;
      node->as.indexExpr.index = index;
      obj = node;
    } else if (check(p, TK_DOT)) {
      int32_t line = obj->line, col = obj->col;
      advance(p);
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected member name after '.'");
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_MEMBER, line, col);
      node->as.memberExpr.obj = obj;
      node->as.memberExpr.name = p->cur.start;
      node->as.memberExpr.nameLen = p->cur.len;
      advance(p);
      obj = node;
    } else {
      return obj;
    }
  }
}

// 最小表达式：primary + 后缀（比较 Task 5、完整阶梯 T007）
static AstNode* parseExpr(Parser* p) { return parsePostfix(p); }
```

- [ ] **Step 3.4: 验证通过 → 提交** — `✨ feat(parser): parse index and member postfix expressions`。

---

### Task 4: var/const/:= 声明与作用域查重

**Files:**
- Modify: `src/parser/parser.c`（作用域栈 + parseVarDecl + parseSimpleStmt 短声明分支 + parseStatement 分派 + parseBlock/parseModule 推弹作用域）
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 4.1: 写失败测试：**

```c
TEST(parseVarDeclForms) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var y\nvar x = 10\nconst MAX = 100", &arena, &diags);
  EXPECT_EQ(ctx, 3, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* d0 = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, d0->kind);
  EXPECT(ctx, d0->as.varDecl.init == NULL);  // 无初值默认 nil（04 §1）
  EXPECT_EQ(ctx, 1, d0->as.varDecl.nameLen);
  EXPECT_EQ(ctx, 'y', d0->as.varDecl.name[0]);
  AstNode* d1 = mod->as.list.items[1];
  EXPECT_EQ(ctx, 0, d1->as.varDecl.flags);
  EXPECT_EQ(ctx, AST_INT, d1->as.varDecl.init->kind);
  EXPECT_EQ(ctx, 10, (int)d1->as.varDecl.init->as.intLit.v);
  AstNode* d2 = mod->as.list.items[2];
  EXPECT_EQ(ctx, AST_VAR_CONST, d2->as.varDecl.flags);
  EXPECT_EQ(ctx, 3, d2->as.varDecl.nameLen);  // MAX
  astArenaDestroy(&arena);
}

TEST(parseShortDeclInBlock) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ x := 20 }", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* block = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_BLOCK, block->kind);
  AstNode* decl = block->as.list.items[0];
  EXPECT_EQ(ctx, AST_VAR_DECL, decl->kind);
  EXPECT_EQ(ctx, AST_VAR_SHORT, decl->as.varDecl.flags);
  EXPECT_EQ(ctx, 20, (int)decl->as.varDecl.init->as.intLit.v);
  astArenaDestroy(&arena);
}

TEST(parseBlockScopingShadowOk) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ var x = 1 }\nvar x = 2", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);  // 不同作用域遮蔽合法（04 §1）
  EXPECT_EQ(ctx, 0, diags.count);
  astArenaDestroy(&arena);
}

TEST(errShortDeclAtModuleTop) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x := 1", &arena, &diags);
  EXPECT_EQ(ctx, 0, mod->as.list.count);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_SHORT_TOPLEVEL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 1, diags.items[0].line);
  astArenaDestroy(&arena);
}

TEST(errDuplicateDeclSameScope) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("{ var x\nx := 1 }", &arena, &diags);  // 04 §1 示例
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errDuplicateVarVarSameScope) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var x = 1\nvar x = 2", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DUP_DECL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 2, diags.items[0].line);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 4.2: 验证失败。**
- [ ] **Step 4.3: 实现** — parser.c 增加：
  1. 语句段之前加作用域栈：

```c
// ---------------- 声明查重作用域 ----------------

// 仅 parser 内查重（04 §1）：名称解析/遮蔽/upvalue 分类归 T009 Binder
typedef struct ParserName {
  const char* start;
  int32_t len;
} ParserName;

struct ParserScope {
  ParserName* names;
  int32_t count, cap;
  ParserScope* parent;
};

static void pushScope(Parser* p) {
  ParserScope* scope = malloc(sizeof(ParserScope));
  MS_ASSERT(scope != NULL);
  scope->names = NULL;
  scope->count = 0;
  scope->cap = 0;
  scope->parent = p->scope;
  p->scope = scope;
}

static void popScope(Parser* p) {
  ParserScope* scope = p->scope;
  p->scope = scope->parent;
  free(scope->names);
  free(scope);
}

static bool inModuleScope(const Parser* p) { return p->scope != NULL && p->scope->parent == NULL; }

// 登记声明；同作用域重名返回 true（调用方发 MSE_SYN_DUP_DECL）
static bool scopeDeclare(Parser* p, const char* start, int32_t len) {
  ParserScope* scope = p->scope;
  for (int32_t i = 0; i < scope->count; i++) {
    if (scope->names[i].len == len && memcmp(scope->names[i].start, start, (size_t)len) == 0) {
      return true;
    }
  }
  MS_ARRAY_PUSH(scope->names, scope->count, scope->cap, ((ParserName){start, len}));
  return false;
}
```

  2. 声明解析（作用域段之后）：

```c
// var/const 声明（04 §1）
static AstNode* parseVarDecl(Parser* p, ms_u8 flags) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // var / const
  if (!check(p, TK_ID)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected identifier");
    return NULL;
  }
  const char* name = p->cur.start;
  int32_t nameLen = p->cur.len;
  int32_t nameLine = p->cur.line, nameCol = p->cur.col;
  advance(p);
  AstNode* init = NULL;
  if (match(p, TK_EQ)) {
    init = parseExpr(p);
    if (init == NULL) {
      return NULL;
    }
  }
  if (scopeDeclare(p, name, nameLen)) {
    errPos(p, nameLine, nameCol, nameLen, MSE_SYN_DUP_DECL, "duplicate declaration in the same scope");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_VAR_DECL, line, col);
  node->as.varDecl.name = name;
  node->as.varDecl.nameLen = nameLen;
  node->as.varDecl.flags = flags;
  node->as.varDecl.init = init;
  return node;
}
```

  3. `parseSimpleStmt` 开头（parseExpr 之前）加短声明分支：

```c
  // x := e 短声明：仅单 IDENT 目标（04 §1）
  {
    AstNode* probe = NULL;  // 占位避免超前判断：先按表达式解析再回看 :=
  }
```

实际实现：`parseSimpleStmt` 改为先 `parseExpr`，随后（表达式语句回退之前）判断 `TK_COLON_EQ`：

```c
static AstNode* parseSimpleStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  AstNode* expr = parseExpr(p);
  if (expr == NULL) {
    return NULL;
  }
  // x := e 短声明：仅单 IDENT 目标；模块顶层非法（04 §1）
  if (check(p, TK_COLON_EQ)) {
    advance(p);
    if (expr->kind != AST_IDENT) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "short declaration target must be an identifier");
      return NULL;
    }
    AstNode* value = parseExpr(p);
    if (value == NULL) {
      return NULL;
    }
    if (inModuleScope(p)) {
      errPos(p, line, col, 1, MSE_SYN_SHORT_TOPLEVEL, "':=' is not allowed at module top level");
      return NULL;
    }
    if (scopeDeclare(p, expr->as.ident.name, expr->as.ident.nameLen)) {
      errPos(p, line, col, expr->as.ident.nameLen, MSE_SYN_DUP_DECL,
             "duplicate declaration in the same scope");
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_VAR_DECL, line, col);
    node->as.varDecl.name = expr->as.ident.name;
    node->as.varDecl.nameLen = expr->as.ident.nameLen;
    node->as.varDecl.flags = AST_VAR_SHORT;
    node->as.varDecl.init = value;
    return node;
  }
  AstNode* node = astNewNode(p->arena, AST_EXPR_STMT, line, col);
  node->as.exprStmt.expr = expr;
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}
```

  4. `parseStatement` 分派加 var/const；`parseBlock` 在语句序列前后 `pushScope/popScope`；`parserParseModule` 同样包 `pushScope/popScope`：

```c
static AstNode* parseStatement(Parser* p) {
  if (check(p, TK_LBRACE)) {
    return parseBlock(p);
  }
  if (check(p, TK_VAR) || check(p, TK_CONST)) {
    ms_u8 flags = check(p, TK_CONST) ? AST_VAR_CONST : 0;
    AstNode* node = parseVarDecl(p, flags);
    if (node != NULL && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  return parseSimpleStmt(p);
}
```

parseBlock 中 `parseStmtList` 调用改为：`pushScope(p); items = parseStmtList(...); popScope(p);`（RBRACE 检查在 pop 之后）。parserParseModule 同理。

- [ ] **Step 4.4: 验证通过 → 提交** — `✨ feat(parser): parse var/const/short declarations with scope checks`。

---

### Task 5: 赋值族与比较表达式

**Files:**
- Modify: `src/parser/parser.c`（parseExprList / 复合与普通赋值 / 元组目标 / 解构 / 比较级）
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 5.1: 写失败测试：**

```c
TEST(parseCompoundAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x += 1", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_ASSIGN, a->kind);  // 复合形态保留，编译期展开（T006 §2）
  EXPECT_EQ(ctx, AST_OP_ADD, a->as.assign.op);
  EXPECT_EQ(ctx, AST_IDENT, a->as.assign.target->kind);
  EXPECT_EQ(ctx, 1, (int)a->as.assign.value->as.intLit.v);
  astArenaDestroy(&arena);
}

TEST(parseIndexMemberAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("xs[0] = v\nobj.f = v", &arena, &diags);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, AST_INDEX, mod->as.list.items[0]->as.assign.target->kind);
  EXPECT_EQ(ctx, AST_MEMBER, mod->as.list.items[1]->as.assign.target->kind);
  EXPECT_EQ(ctx, AST_OP_NONE, mod->as.list.items[0]->as.assign.op);
  astArenaDestroy(&arena);
}

TEST(parseTupleSwapAssign) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, b = b, a", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* a = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_ASSIGN, a->kind);
  EXPECT_EQ(ctx, AST_TUPLE, a->as.assign.target->kind);   // 目标元组
  EXPECT_EQ(ctx, 2, a->as.assign.target->as.tuple.count);
  EXPECT_EQ(ctx, AST_TUPLE, a->as.assign.value->kind);    // 右值元组
  EXPECT_EQ(ctx, 2, a->as.assign.value->as.tuple.count);
  EXPECT_EQ(ctx, 'a', a->as.assign.target->as.tuple.items[0]->as.ident.name[0]);
  EXPECT_EQ(ctx, 'b', a->as.assign.target->as.tuple.items[1]->as.ident.name[0]);
  astArenaDestroy(&arena);
}

TEST(parseStarDestructure) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, *mid, z = xs", &arena, &diags);  // 04 §2
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* d = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_DESTRUCTURE, d->kind);
  EXPECT_EQ(ctx, 3, d->as.destructure.count);
  EXPECT_EQ(ctx, 1, d->as.destructure.restIndex);  // * 可在任意位置（04 §2）
  EXPECT_EQ(ctx, 'm', d->as.destructure.targets[1]->as.ident.name[0]);
  EXPECT_EQ(ctx, AST_IDENT, d->as.destructure.value->kind);
  astArenaDestroy(&arena);
}

TEST(errDestructureTwoStars) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("a, *b, *c = xs", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_DESTRUCT_STARS, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errBadAssignTarget) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("1 = 2", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}

TEST(errChainedComparison) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if x < y < z {}", &arena, &diags);  // 03 优先级表：不可链式
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_CHAINED_CMP, (int)diags.items[0].code);
  astArenaDestroy(&arena);
}
```

注：`errChainedComparison` 含 `if`，需在 Task 6 的 if 解析就位后才能整体通过；本任务先让非 if 部分（若改为 `var r = x < y`）可用：

```c
TEST(parseComparisonExpr) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var r = x == y", &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* init = mod->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BINARY, init->kind);
  EXPECT_EQ(ctx, AST_OP_EQ, init->as.binary.op);
  EXPECT_EQ(ctx, AST_IDENT, init->as.binary.lhs->kind);
  EXPECT_EQ(ctx, AST_IDENT, init->as.binary.rhs->kind);
  astArenaDestroy(&arena);
}
```

（`errChainedComparison` 用例写入文件但标注依赖 Task 6；Task 6 完成后转绿。）

- [ ] **Step 5.2: 验证失败。**
- [ ] **Step 5.3: 实现：**

  1. 运算符映射（数值求值段后）：

```c
static AstBinOp compoundOpOf(MsTokenType type) {
  switch (type) {
    case TK_PLUS_EQ: return AST_OP_ADD;
    case TK_MINUS_EQ: return AST_OP_SUB;
    case TK_STAR_EQ: return AST_OP_MUL;
    case TK_SLASH_EQ: return AST_OP_DIV;
    case TK_PERCENT_EQ: return AST_OP_MOD;
    case TK_STAR_STAR_EQ: return AST_OP_POW;
    case TK_AMP_EQ: return AST_OP_BIT_AND;
    case TK_PIPE_EQ: return AST_OP_BIT_OR;
    case TK_CARET_EQ: return AST_OP_BIT_XOR;
    case TK_LT_LT_EQ: return AST_OP_SHL;
    case TK_GT_GT_EQ: return AST_OP_SHR;
    default: return AST_OP_NONE;
  }
}

static AstBinOp comparisonOpOf(MsTokenType type) {
  switch (type) {
    case TK_EQ_EQ: return AST_OP_EQ;
    case TK_BANG_EQ: return AST_OP_NE;
    case TK_LT: return AST_OP_LT;
    case TK_LT_EQ: return AST_OP_LE;
    case TK_GT: return AST_OP_GT;
    case TK_GT_EQ: return AST_OP_GE;
    default: return AST_OP_NONE;
  }
}
```

  2. `parseExpr` 升级为比较级 + 新增 `parseExprList`（放在 parsePostfix 之后）：

```c
// 最小表达式：primary + 后缀 + 单级比较（04 §3 条件用；完整阶梯 T007）
static AstNode* parseExpr(Parser* p) {
  AstNode* lhs = parsePostfix(p);
  if (lhs == NULL) {
    return NULL;
  }
  AstBinOp op = comparisonOpOf(p->cur.type);
  if (op == AST_OP_NONE) {
    return lhs;
  }
  advance(p);
  AstNode* rhs = parsePostfix(p);
  if (rhs == NULL) {
    return NULL;
  }
  if (comparisonOpOf(p->cur.type) != AST_OP_NONE) {
    errTok(p, &p->cur, MSE_SYN_CHAINED_CMP, "comparison operators cannot be chained");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_BINARY, lhs->line, lhs->col);
  node->as.binary.lhs = lhs;
  node->as.binary.rhs = rhs;
  node->as.binary.op = op;
  return node;
}

// 逗号表达式列表 → 单个或 AST_TUPLE（赋值右值 / 元组表达式）
static AstNode* parseExprList(Parser* p) {
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  if (!check(p, TK_COMMA)) {
    return first;
  }
  AstNode** tmp = NULL;
  int32_t count = 0, cap = 0;
  MS_ARRAY_PUSH(tmp, count, cap, first);
  while (match(p, TK_COMMA)) {
    AstNode* expr = parseExpr(p);
    if (expr == NULL) {
      free(tmp);
      return NULL;
    }
    MS_ARRAY_PUSH(tmp, count, cap, expr);
  }
  AstNode* node = astNewNode(p->arena, AST_TUPLE, first->line, first->col);
  node->as.tuple.count = count;
  node->as.tuple.items = astNewNodeArray(p->arena, count);
  memcpy(node->as.tuple.items, tmp, sizeof(AstNode*) * (size_t)count);
  free(tmp);
  return node;
}
```

  3. `parseVarDecl` 的 init 改用 `parseExprList`（支持 `var t = a, b` 元组初值）。短声明 value 同改 `parseExprList`。
  4. `parseSimpleStmt` 升级为完整形态（目标列表 + 赋值/解构定形）：

```c
// 赋值目标合法性：标识符/索引/成员（04 §2）
static bool isAssignTarget(const AstNode* node) {
  return node->kind == AST_IDENT || node->kind == AST_INDEX || node->kind == AST_MEMBER;
}

static AstNode* newExprStmt(Parser* p, AstNode* expr, int32_t line, int32_t col) {
  AstNode* node = astNewNode(p->arena, AST_EXPR_STMT, line, col);
  node->as.exprStmt.expr = expr;
  return node;
}

// 表达式/赋值/短声明语句（04 §1 §2 §10）：先解析逗号列表，按后续 token 定形
static AstNode* parseSimpleStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool firstStar = match(p, TK_STAR);  // 语句起始 * 仅解构目标合法
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  // x := e 短声明（单 IDENT 目标）
  if (!firstStar && check(p, TK_COLON_EQ)) {
    advance(p);
    if (first->kind != AST_IDENT) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "short declaration target must be an identifier");
      return NULL;
    }
    AstNode* value = parseExprList(p);
    if (value == NULL) {
      return NULL;
    }
    if (inModuleScope(p)) {
      errPos(p, line, col, 1, MSE_SYN_SHORT_TOPLEVEL, "':=' is not allowed at module top level");
      return NULL;
    }
    if (scopeDeclare(p, first->as.ident.name, first->as.ident.nameLen)) {
      errPos(p, line, col, first->as.ident.nameLen, MSE_SYN_DUP_DECL,
             "duplicate declaration in the same scope");
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_VAR_DECL, line, col);
    node->as.varDecl.name = first->as.ident.name;
    node->as.varDecl.nameLen = first->as.ident.nameLen;
    node->as.varDecl.flags = AST_VAR_SHORT;
    node->as.varDecl.init = value;
    return node;
  }
  // 逗号列表：元组赋值 / 解构目标候选
  if (firstStar || check(p, TK_COMMA)) {
    TargetElem* tmp = NULL;
    int32_t count = 0, cap = 0;
    MS_ARRAY_PUSH(tmp, count, cap, ((TargetElem){first, firstStar}));
    while (match(p, TK_COMMA)) {
      bool starred = match(p, TK_STAR);
      AstNode* expr = parseExpr(p);
      if (expr == NULL) {
        free(tmp);
        return NULL;
      }
      MS_ARRAY_PUSH(tmp, count, cap, ((TargetElem){expr, starred}));
    }
    if (match(p, TK_EQ)) {
      AstNode* node = finishAssignTargets(p, tmp, count, line, col);
      free(tmp);
      if (node == NULL) {
        return NULL;
      }
      return node;
    }
    if (check(p, TK_COLON_EQ)) {
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "short declaration supports a single target");
      free(tmp);
      return NULL;
    }
    // 无赋值：元组表达式语句；* 只属于解构目标
    for (int32_t i = 0; i < count; i++) {
      if (tmp[i].starred) {
        errPos(p, tmp[i].expr->line, tmp[i].expr->col, 1, MSE_SYN_STAR_NO_ASSIGN,
               "starred target requires an assignment");
        free(tmp);
        return NULL;
      }
    }
    AstNode* tupleNode = astNewNode(p->arena, AST_TUPLE, line, col);
    tupleNode->as.tuple.count = count;
    tupleNode->as.tuple.items = astNewNodeArray(p->arena, count);
    for (int32_t i = 0; i < count; i++) {
      tupleNode->as.tuple.items[i] = tmp[i].expr;
    }
    free(tmp);
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return newExprStmt(p, tupleNode, line, col);
  }
  // 复合赋值：x += e（AST 保留复合形态，编译期展开读-算-写）
  AstBinOp op = compoundOpOf(p->cur.type);
  if (op != AST_OP_NONE) {
    advance(p);
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid assignment target");
      return NULL;
    }
    AstNode* value = parseExpr(p);
    if (value == NULL) {
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_ASSIGN, line, col);
    node->as.assign.target = first;
    node->as.assign.value = value;
    node->as.assign.op = op;
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // 普通赋值：target = value
  if (match(p, TK_EQ)) {
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid assignment target");
      return NULL;
    }
    AstNode* value = parseExprList(p);
    if (value == NULL) {
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_ASSIGN, line, col);
    node->as.assign.target = first;
    node->as.assign.value = value;
    node->as.assign.op = AST_OP_NONE;
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // 表达式语句
  if (firstStar) {
    errPos(p, line, col, 1, MSE_SYN_STAR_NO_ASSIGN, "starred target requires an assignment");
    return NULL;
  }
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return newExprStmt(p, first, line, col);
}
```

配套定义（parseSimpleStmt 之前）：

```c
typedef struct TargetElem {
  AstNode* expr;
  bool starred;
} TargetElem;

// = 后定形：目标合法性 + star 计数 → ASSIGN(元组目标) 或 DESTRUCTURE
static AstNode* finishAssignTargets(Parser* p, TargetElem* tmp, int32_t count, int32_t line,
                                    int32_t col) {
  AstNode* value = parseExprList(p);
  if (value == NULL) {
    return NULL;
  }
  int32_t restIndex = -1;
  int32_t starCount = 0;
  for (int32_t i = 0; i < count; i++) {
    if (tmp[i].starred) {
      starCount++;
      restIndex = i;
    }
  }
  if (starCount > 1) {
    errTok(p, &p->cur, MSE_SYN_DESTRUCT_STARS, "at most one starred target allowed");
    return NULL;
  }
  for (int32_t i = 0; i < count; i++) {
    if (!isAssignTarget(tmp[i].expr)) {
      errPos(p, tmp[i].expr->line, tmp[i].expr->col, 1, MSE_SYN_BAD_TARGET,
             "invalid assignment target");
      return NULL;
    }
  }
  if (starCount == 0) {
    AstNode* target = astNewNode(p->arena, AST_TUPLE, line, col);
    target->as.tuple.count = count;
    target->as.tuple.items = astNewNodeArray(p->arena, count);
    for (int32_t i = 0; i < count; i++) {
      target->as.tuple.items[i] = tmp[i].expr;
    }
    AstNode* node = astNewNode(p->arena, AST_ASSIGN, line, col);
    node->as.assign.target = target;
    node->as.assign.value = value;
    node->as.assign.op = AST_OP_NONE;
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  AstNode* node = astNewNode(p->arena, AST_DESTRUCTURE, line, col);
  node->as.destructure.count = count;
  node->as.destructure.restIndex = restIndex;
  node->as.destructure.value = value;
  node->as.destructure.targets = astNewNodeArray(p->arena, count);
  for (int32_t i = 0; i < count; i++) {
    node->as.destructure.targets[i] = tmp[i].expr;
  }
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}
```

- [ ] **Step 5.4: 验证通过（除 errChainedComparison 标注 Task 6）→ 提交** — `✨ feat(parser): parse assignment family and comparisons`。

---

### Task 6: if/else 链、错误恢复与 DoD 统计

**Files:**
- Modify: `src/parser/parser.c`（parseIfStmt + parseStatement 分派）
- Test: `tests/unit/test_parser_basic.c`

- [ ] **Step 6.1: 写失败测试：**

```c
TEST(parseIfElseChain) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if a { var x = 1 } else if b { var y = 2 } else { var z = 3 }",
                          &arena, &diags);
  EXPECT_EQ(ctx, 1, mod->as.list.count);
  EXPECT_EQ(ctx, 0, diags.count);
  AstNode* ifa = mod->as.list.items[0];
  EXPECT_EQ(ctx, AST_IF, ifa->kind);
  EXPECT_EQ(ctx, AST_IDENT, ifa->as.ifStmt.cond->kind);
  EXPECT_EQ(ctx, AST_BLOCK, ifa->as.ifStmt.thenBlock->kind);
  EXPECT_EQ(ctx, 1, ifa->as.ifStmt.thenBlock->as.list.count);
  AstNode* ifb = ifa->as.ifStmt.elseNode;
  EXPECT_EQ(ctx, AST_IF, ifb->kind);  // else if → 嵌套 IF（04 §3）
  EXPECT_EQ(ctx, AST_BLOCK, ifb->as.ifStmt.thenBlock->kind);
  EXPECT_EQ(ctx, AST_BLOCK, ifb->as.ifStmt.elseNode->kind);
  EXPECT_EQ(ctx, 1, ifb->as.ifStmt.elseNode->as.list.count);
  EXPECT(ctx, ifa->as.ifStmt.elseNode->as.ifStmt.elseNode->as.list.items[0]->as.varDecl.name[0] == 'z');
  astArenaDestroy(&arena);
}

TEST(errIfConditionParenthesized) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if (x) {}", &arena, &diags);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_IF_PAREN, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 就地恢复：if 仍完整产出
  EXPECT_EQ(ctx, AST_IF, mod->as.list.items[0]->kind);
  EXPECT_EQ(ctx, AST_IDENT, mod->as.list.items[0]->as.ifStmt.cond->kind);
  EXPECT_EQ(ctx, AST_BLOCK, mod->as.list.items[0]->as.ifStmt.thenBlock->kind);
  astArenaDestroy(&arena);
}

TEST(parseMultiErrorRecovery) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("x := 1\nif (y) {}\n1 = 2", &arena, &diags);
  EXPECT_EQ(ctx, 3, diags.count);  // 一次编译收集多条（DoD §3）
  EXPECT_EQ(ctx, MSE_SYN_SHORT_TOPLEVEL, (int)diags.items[0].code);
  EXPECT_EQ(ctx, MSE_SYN_IF_PAREN, (int)diags.items[1].code);
  EXPECT_EQ(ctx, MSE_SYN_BAD_TARGET, (int)diags.items[2].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 中间 if 恢复成功
  astArenaDestroy(&arena);
}

TEST(errElseOnNewLine) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("if a {\n}\nelse {}", &arena, &diags);  // Go 风格：else 须同行
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_SYN_UNEXPECTED, (int)diags.items[0].code);
  EXPECT_EQ(ctx, 1, mod->as.list.count);  // 第一条 if（无 else）保留
  astArenaDestroy(&arena);
}

TEST(arenaStatsAfterParse) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  AstNode* mod = parseSrc("var x = 1\nif a { var y }", &arena, &diags);
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 2, mod->as.list.count);
  EXPECT_EQ(ctx, 7, arena.nodeCount);  // module+var+int+if+ident+block+var（DoD §4）
  EXPECT(ctx, astArenaBytesUsed(&arena) >= sizeof(AstNode) * 7);
  astArenaDestroy(&arena);
}
```

- [ ] **Step 6.2: 验证失败。**
- [ ] **Step 6.3: 实现** — `parseIfStmt`（parseVarDecl 之后）+ 分派：

```c
// if / else if / else（04 §3）：条件不加括号（写括号 MSE1003）、块必须花括号
static AstNode* parseIfStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // if
  if (check(p, TK_LPAREN)) {
    diagPos(p, p->cur.line, p->cur.col, 1, MSE_SYN_IF_PAREN,
            "if condition must not be parenthesized");
    advance(p);  // 就地恢复：按无括号继续
  }
  AstNode* cond = parseExpr(p);
  if (cond == NULL) {
    return NULL;
  }
  match(p, TK_RPAREN);  // 括号错误路径的配对消费（正常路径遇不到）
  AstNode* thenBlock = parseBlock(p);
  if (thenBlock == NULL) {
    return NULL;
  }
  AstNode* elseNode = NULL;
  if (match(p, TK_ELSE)) {  // 须与 } 同行：换行已被 ASI 断开
    elseNode = check(p, TK_IF) ? parseIfStmt(p) : parseBlock(p);
    if (elseNode == NULL) {
      return NULL;
    }
  }
  AstNode* node = astNewNode(p->arena, AST_IF, line, col);
  node->as.ifStmt.cond = cond;
  node->as.ifStmt.thenBlock = thenBlock;
  node->as.ifStmt.elseNode = elseNode;
  return node;
}
```

`parseStatement` 分派加：`if (check(p, TK_IF)) { return parseIfStmt(p); }`（块分支之后）。

- [ ] **Step 6.4: 验证通过** — 全部用例（含 Task 5 挂起的 errChainedComparison）通过；全量 ctest 绿。
- [ ] **Step 6.5: 提交** — `✨ feat(parser): parse if chains with error recovery`。

---

### Task 7: 验证收尾

**Files:**
- Modify: `docs/tasks/T006-ast-basic-statements.md`（DoD 打勾 + 状态 ✅）
- Modify: `docs/tasks/README.md:27`（T006 → ✅）
- Modify（格式）: `src/parser/ast.h`、`src/parser/parser.h`、`src/parser/parser.c`、`tests/unit/test_parser_basic.c`

- [ ] **Step 7.1: clang-format** — `clang-format -i src\parser\ast.h src\parser\parser.h src\parser\parser.c tests\unit\test_parser_basic.c`；重建 + 全量 ctest 确认仍绿；若有格式改动提交 `🎨 style(parser): clang-format T006 sources`。
- [ ] **Step 7.2: DoD 核对**（对照 `docs/tasks/T006-ast-basic-statements.md` §4）：
  - [ ] 04 §1-§3 示例全部解析成功且 AST 形态正确（用例 parseVarDeclForms/parseShortDeclInBlock/parseCompoundAssign/parseIndexMemberAssign/parseTupleSwapAssign/parseStarDestructure/parseIfElseChain/parseComparisonExpr 覆盖）
  - [ ] 非法输入（顶层 `:=`、`if (x) {}`、同作用域重复声明、解构双星号）各发 MSE1xxx（errShortDeclAtModuleTop/errIfConditionParenthesized/errDuplicateDeclSameScope/errDestructureTwoStars）
  - [ ] 错误恢复：多错误文件一次编译收集多条诊断（parseMultiErrorRecovery）
  - [ ] AST arena 用量统计接口可用（arenaAllocAndStats/arenaStatsAfterParse）
- [ ] **Step 7.3: 更新任务文档** — T006 文档：头部 `状态: 进行中` → `已完成`、§4 四个 DoD 复选框打 `[x]`；README.md T006 行 `🚧` → `✅`。提交 `✅ docs(tasks): mark T006 done`。
- [ ] **Step 7.4: 最终全量验证** — `cmake --build build --config Debug` 全量重建 + `ctest --test-dir build -C Debug`（预期 20 项全绿）+ `git log --oneline` 检查提交序列。

---

## 自审记录

- **规格覆盖**：任务文档 §2 要点逐条有实现（arena/line-col/错误恢复/声明规则/赋值目标/复合保留/if 括号/块花括号/ASI 语句/裸 `;`）；§5.1 用例表 11 行全部有对应测试函数。
- **占位符扫描**：无 TBD/TODO；Task 5 的 errChainedComparison 依赖 Task 6 属显式声明的顺序依赖，非占位。
- **类型一致性**：AstNode union 成员名（list/varDecl/assign/destructure/exprStmt/ifStmt/ident/boolLit/intLit/floatLit/bigintLit/strLit/runeLit/tuple/binary/indexExpr/memberExpr）在测试与实现间一致；错误码枚举名与测试引用一致；`astNewNodeArray`/`astArenaBytesUsed` 签名一致。
- **已知边界**：`var a, b = 1, 2` 多目标声明不支持（文档未定义）；`:=` 多目标报 MSE1000；插值内仅支持最小表达式集（`${a + b}` 待 T007）。







