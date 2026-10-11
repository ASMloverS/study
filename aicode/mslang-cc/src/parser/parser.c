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

// 同 errTok，但以显式位置报错（token 已被消费的场景）
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
  match(p, TK_STRING);  // 闭 STRING（scanner 已对未闭合串内部补闭引号）
  AstNode* node = astNewNode(p->arena, AST_STRING, line, col);
  node->as.strLit.raw = false;
  node->as.strLit.count = count;
  node->as.strLit.segs = astArenaAlloc(p->arena, sizeof(AstStrSeg) * (size_t)count);
  if (count > 0) {
    memcpy(node->as.strLit.segs, tmp, sizeof(AstStrSeg) * (size_t)count);
  }
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
      // 剥反引号；未闭合 raw（scanner 已报 MSE0xxx）长度钳位非负
      int32_t segLen = p->cur.len >= 2 ? p->cur.len - 2 : 0;
      const char* segStart = p->cur.start + (p->cur.len >= 2 ? 1 : 0);
      node->as.strLit.segs[0] = (AstStrSeg){NULL, segStart, segLen};
      advance(p);
      return node;
    }
    default:
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "expected expression");
      return NULL;
  }
}

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

// ---------------- 语句 ----------------

static AstNode* parseStatement(Parser* p);

// 简单语句收尾：; / } / EOF / 后继行 token 任一（04 §10 换行即语句）
// 块内换行不产 ; （01 §7 细则 1），故以 token 行号识别语句边界
static bool endSimpleStmt(Parser* p, int32_t stmtLine) {
  if (match(p, TK_SEMICOLON)) {
    return true;
  }
  if (check(p, TK_RBRACE) || check(p, TK_EOF)) {
    return true;
  }
  if (p->cur.line > stmtLine) {
    return true;
  }
  errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected newline or ';' after statement");
  return false;
}

// 语句序列（模块体/块体共用）：孤立 ; 静默跳过（04 §10）；错误语句同步后继续
static AstNode** parseStmtList(Parser* p, int32_t* outCount, MsTokenType end) {
  AstNode** tmp = NULL;
  int32_t count = 0, cap = 0;
  while (!check(p, end) && !check(p, TK_EOF)) {
    if (match(p, TK_SEMICOLON)) {
      continue;
    }
    if (check(p, TK_RBRACE) && end != TK_RBRACE) {
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "unmatched '}'");
      advance(p);
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
  if (count > 0) {
    memcpy(items, tmp, sizeof(AstNode*) * (size_t)count);
  }
  free(tmp);
  *outCount = count;
  return items;
}

// 块语句：{ 开新作用域（04 §10）
static AstNode* parseBlock(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{'");  // 块必须花括号（04 §3）
    return NULL;
  }
  int32_t count = 0;
  pushScope(p);
  AstNode** items = parseStmtList(p, &count, TK_RBRACE);
  popScope(p);
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
  if (!endSimpleStmt(p, line)) {
    return NULL;
  }
  return node;
}

static AstNode* parseStatement(Parser* p) {
  if (check(p, TK_LBRACE)) {
    return parseBlock(p);  // 块语句（自带边界，无需 ASI）
  }
  if (check(p, TK_VAR) || check(p, TK_CONST)) {
    ms_u8 flags = check(p, TK_CONST) ? AST_VAR_CONST : 0;
    AstNode* node = parseVarDecl(p, flags);
    if (node != NULL && !endSimpleStmt(p, node->line)) {
      return NULL;
    }
    return node;
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
  pushScope(p);
  AstNode** items = parseStmtList(p, &count, TK_EOF);
  popScope(p);
  AstNode* node = astNewNode(p->arena, AST_MODULE, line, col);
  node->as.list.items = items;
  node->as.list.count = count;
  return node;
}
