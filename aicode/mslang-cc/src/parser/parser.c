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
  node->bindKind = AST_BIND_NONE;  // T009：标注默认值（arena 复用内存可能残留，此处归零）
  node->bindFlags = 0;
  node->bindSlot = 0;
  node->bindUpvalDepth = 0;
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

static void advance(Parser* p) {
  p->prevLine = p->cur.line;
  scannerNext(&p->scan, &p->cur);  // EOF 幂等
}

static bool match(Parser* p, MsTokenType type) {
  if (check(p, type)) {
    advance(p);
    return true;
  }
  return false;
}

static void diagPos(Parser* p, int32_t line, int32_t col, int32_t len, uint32_t code, const char* message) {
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
static void errPos(Parser* p, int32_t line, int32_t col, int32_t len, uint32_t code, const char* message) {
  if (p->panic) {
    return;
  }
  p->panic = true;
  diagPos(p, line, col, len, code, message);
}

// 丢弃至语句边界：; 消费后停，} 停留（属外层块），EOF 终止；块内无 ; 时以换行为界（04 §10）
static void synchronize(Parser* p) {
  p->panic = false;
  int32_t startLine = p->cur.line;
  while (!check(p, TK_EOF)) {
    if (p->cur.type == TK_SEMICOLON) {
      advance(p);
      return;
    }
    if (p->cur.type == TK_RBRACE) {
      return;
    }
    if (p->cur.line > startLine) {
      return;  // 错误行之后的换行即新语句边界
    }
    advance(p);
  }
}

// 单 token 前瞻：快照推进再回滚（词法诊断屏蔽，由真实推进报告；仅用于判别，不产生副作用）
static MsTokenType peekType(Parser* p) {
  Scanner savedScan = p->scan;
  MsToken savedCur = p->cur;
  int32_t savedPrevLine = p->prevLine;
  p->scan.diags = NULL;
  MsToken next;
  scannerNext(&p->scan, &next);
  p->scan = savedScan;
  p->cur = savedCur;
  p->prevLine = savedPrevLine;
  return next.type;
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

static AstBinOp compoundOpOf(MsTokenType type) {
  switch (type) {
    case TK_PLUS_EQ:
      return AST_OP_ADD;
    case TK_MINUS_EQ:
      return AST_OP_SUB;
    case TK_STAR_EQ:
      return AST_OP_MUL;
    case TK_SLASH_EQ:
      return AST_OP_DIV;
    case TK_PERCENT_EQ:
      return AST_OP_MOD;
    case TK_STAR_STAR_EQ:
      return AST_OP_POW;
    case TK_AMP_EQ:
      return AST_OP_BIT_AND;
    case TK_PIPE_EQ:
      return AST_OP_BIT_OR;
    case TK_CARET_EQ:
      return AST_OP_BIT_XOR;
    case TK_LT_LT_EQ:
      return AST_OP_SHL;
    case TK_GT_GT_EQ:
      return AST_OP_SHR;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp comparisonOpOf(MsTokenType type) {
  switch (type) {
    case TK_EQ_EQ:
      return AST_OP_EQ;
    case TK_BANG_EQ:
      return AST_OP_NE;
    case TK_LT:
      return AST_OP_LT;
    case TK_LT_EQ:
      return AST_OP_LE;
    case TK_GT:
      return AST_OP_GT;
    case TK_GT_EQ:
      return AST_OP_GE;
    case TK_IS:
      return AST_OP_IS;
    case TK_IN:
      return AST_OP_IN;
    default:
      return AST_OP_NONE;
  }
}

// ---------------- 表达式（完整优先级阶梯，T007 §4.1） ----------------

static AstNode* parseExpr(Parser* p);
static AstNode* parseUnary(Parser* p);  // 与 parsePow 互递归（** 右结合吸收一元）
static AstNode* parseComprTail(Parser* p, AstNode* key, AstNode* value, int32_t compKind, MsTokenType closer,
                               int32_t line, int32_t col);  // 定义于 parseSliceRest 后（依赖 rejectGenExpr）
static bool rejectGenExpr(Parser* p, const AstNode* n);  // 定义在 parseCallArgs 前（parseStringLit 前置可见）
static AstNode* parseBlock(Parser* p);                   // 函数表达式体需要前置可见（定义在语句区）
static bool parseParamList(Parser* p, AstParam** outParams, int32_t* outCount);  // 声明/表达式共用（语句区）
static bool parseFuncBody(Parser* p, AstNode** outBody, AstNode** outExprBody);        // 同上
static AstNode* parseFuncDeclStmt(Parser* p, ms_u8 flags, int32_t line, int32_t col);  // 同上

// map 字面量键值对（parsePrimary 花括号分支暂存；定义须先于 parsePrimary）
typedef struct MapPair {
  AstNode* key;
  AstNode* value;
} MapPair;

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
      if (rejectGenExpr(p, expr)) {  // 插值段为直接操作数位置，GEN_EXPR 非法（03 §1）
        free(tmp);
        return NULL;
      }
      if (check(p, TK_FOR)) {  // 裸生成器形态在插值段同样非法（03 §1 位置限制）
        errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression is not allowed in interpolation");
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
    case TK_THIS: {  // this（06 §3；类体字段初始化合法性见 exprHasThis）
      AstNode* node = astNewNode(p->arena, AST_THIS, line, col);
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
    case TK_LPAREN: {
      advance(p);                 // (
      if (match(p, TK_RPAREN)) {  // () 空 tuple（02 §5.3）
        AstNode* node = astNewNode(p->arena, AST_TUPLE, line, col);
        node->as.tuple.count = 0;
        return node;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (check(p, TK_FOR)) {  // 生成器表达式（03 §1：惰性，位置限制见 rejectGenExpr）
        return parseComprTail(p, NULL, first, 0, TK_RPAREN, line, col);
      }
      if (!check(p, TK_COMMA)) {
        if (!match(p, TK_RPAREN)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
          return NULL;
        }
        return first;  // (expr) 分组（03 §10）
      }
      AstNode** tmp = NULL;
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, first);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RPAREN)) {
          break;  // 尾逗号 (1,)
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RPAREN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_TUPLE, line, col);
      node->as.tuple.count = count;
      node->as.tuple.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.tuple.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
    case TK_LBRACKET: {  // list 字面量 / list 推导式（03 §2）
      advance(p);
      if (match(p, TK_RBRACKET)) {
        AstNode* node = astNewNode(p->arena, AST_LIST, line, col);
        node->as.list.count = 0;
        return node;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (check(p, TK_FOR)) {
        return parseComprTail(p, NULL, first, AST_COMP_LIST, TK_RBRACKET, line, col);
      }
      AstNode** tmp = NULL;
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, first);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RBRACKET)) {
          break;  // 尾逗号
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RBRACKET)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_LIST, line, col);
      node->as.list.count = count;
      node->as.list.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.list.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
    case TK_LBRACE: {  // map/set 字面量与推导式（03 §2；歧义按首个子表达式后 token 定形）
      advance(p);
      if (match(p, TK_RBRACE)) {  // {} 空 map（02 §5）
        AstNode* node = astNewNode(p->arena, AST_MAP, line, col);
        node->as.mapLit.count = 0;
        return node;
      }
      AstNode* key = parseExpr(p);
      if (key == NULL) {
        return NULL;
      }
      if (check(p, TK_COLON)) {  // map 族
        advance(p);
        AstNode* value = parseExpr(p);
        if (value == NULL) {
          return NULL;
        }
        if (check(p, TK_FOR)) {
          return parseComprTail(p, key, value, AST_COMP_MAP, TK_RBRACE, line, col);
        }
        MapPair* tmp = NULL;
        int32_t count = 0, cap = 0;
        MS_ARRAY_PUSH(tmp, count, cap, ((MapPair){key, value}));
        while (match(p, TK_COMMA)) {
          if (check(p, TK_RBRACE)) {
            break;  // 尾逗号
          }
          AstNode* k = parseExpr(p);
          if (k == NULL) {
            free(tmp);
            return NULL;
          }
          if (!match(p, TK_COLON)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' in map literal");
            free(tmp);
            return NULL;
          }
          AstNode* v = parseExpr(p);
          if (v == NULL) {
            free(tmp);
            return NULL;
          }
          MS_ARRAY_PUSH(tmp, count, cap, ((MapPair){k, v}));
        }
        if (!match(p, TK_RBRACE)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}'");
          free(tmp);
          return NULL;
        }
        AstNode* node = astNewNode(p->arena, AST_MAP, line, col);
        node->as.mapLit.count = count;
        node->as.mapLit.keys = astNewNodeArray(p->arena, count);
        node->as.mapLit.values = astNewNodeArray(p->arena, count);
        for (int32_t i = 0; i < count; i++) {
          node->as.mapLit.keys[i] = tmp[i].key;
          node->as.mapLit.values[i] = tmp[i].value;
        }
        free(tmp);
        return node;
      }
      if (check(p, TK_FOR)) {  // set 推导式
        return parseComprTail(p, NULL, key, AST_COMP_SET, TK_RBRACE, line, col);
      }
      AstNode** tmp = NULL;  // set 字面量（{x} 单元素亦然）
      int32_t count = 0, cap = 0;
      MS_ARRAY_PUSH(tmp, count, cap, key);
      while (match(p, TK_COMMA)) {
        if (check(p, TK_RBRACE)) {
          break;
        }
        AstNode* e = parseExpr(p);
        if (e == NULL) {
          free(tmp);
          return NULL;
        }
        MS_ARRAY_PUSH(tmp, count, cap, e);
      }
      if (!match(p, TK_RBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}'");
        free(tmp);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_SET, line, col);
      node->as.list.count = count;
      node->as.list.items = astNewNodeArray(p->arena, count);
      if (count > 0) {
        memcpy(node->as.list.items, tmp, sizeof(AstNode*) * (size_t)count);
      }
      free(tmp);
      return node;
    }
    case TK_FUNC: {  // 函数表达式（05 §2）：与声明共用参数/体解析
      advance(p);    // line/col 取 parsePrimary 顶部（同一 func token）
      ms_u8 flags = 0;
      if (match(p, TK_STAR)) {
        flags |= AST_FUNC_GENERATOR;
      }
      AstParam* params = NULL;
      int32_t paramCount = 0;
      if (!parseParamList(p, &params, &paramCount)) {
        return NULL;
      }
      AstNode* body = NULL;
      AstNode* exprBody = NULL;
      if (!parseFuncBody(p, &body, &exprBody)) {
        free(params);
        return NULL;
      }
      AstNode* node = astNewNode(p->arena, AST_FUNC_EXPR, line, col);
      node->as.funcExpr.flags = flags;
      node->as.funcExpr.paramCount = paramCount;
      node->as.funcExpr.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
      if (paramCount > 0) {
        memcpy(node->as.funcExpr.params, params, sizeof(AstParam) * (size_t)paramCount);
      }
      node->as.funcExpr.body = body;
      node->as.funcExpr.exprBody = exprBody;
      free(params);
      return node;
    }
    default:
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "expected expression");
      return NULL;
  }
}

// GEN_EXPR 位置限制（03 §1）：仅调用唯一实参或赋值右值（带括号），其余直接操作数位置拒绝
static bool rejectGenExpr(Parser* p, const AstNode* n) {
  if (n == NULL || n->kind != AST_GEN_EXPR) {
    return false;
  }
  errPos(p, n->line, n->col, 1, MSE_SYN_GENEXPR_POS,
         "generator expression is only allowed as the sole call argument or an assignment right-hand side");
  return true;
}

typedef struct CallArg {
  AstNode* expr;
  bool spread;
} CallArg;

// 调用实参表（03 §4）：位置实参 + 至多一个尾随 * 展开；实参求值顺序=存放顺序
static AstNode* parseCallArgs(Parser* p, AstNode* callee, int32_t line, int32_t col) {
  advance(p);  // (
  CallArg* tmp = NULL;
  int32_t count = 0, cap = 0;
  bool closedByGenexpr = false;  // 裸生成器形态的尾随闭括号由 parseComprTail 消费
  while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
    bool star = match(p, TK_STAR);
    AstNode* arg = parseExpr(p);
    if (arg == NULL) {
      free(tmp);
      return NULL;
    }
    if (check(p, TK_FOR)) {  // 裸单实参生成器（03 §1）：唯一实参、不可展开
      if (count != 0) {      // 前面已有实参 → 非唯一实参（03 §1）
        errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression must be the sole argument");
        free(tmp);
        return NULL;
      }
      if (star) {
        errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression cannot be spread");
        free(tmp);
        return NULL;
      }
      arg = parseComprTail(p, NULL, arg, 0, TK_RPAREN, arg->line, arg->col);
      if (arg == NULL) {
        free(tmp);
        return NULL;
      }
      MS_ARRAY_PUSH(tmp, count, cap, ((CallArg){arg, false}));
      closedByGenexpr = true;
      break;  // 闭括号已随推导式消费，实参表结束
    }
    MS_ARRAY_PUSH(tmp, count, cap, ((CallArg){arg, star}));
    if (star && !check(p, TK_RPAREN)) {
      errTok(p, &p->cur, MSE_SYN_SPREAD_NOT_LAST, "'*' spread argument must be the last argument");
      free(tmp);
      return NULL;
    }
    if (arg->kind == AST_GEN_EXPR && (star || count > 1 || !check(p, TK_RPAREN))) {
      // GEN_EXPR 仅可作唯一实参且不得被 * 展开（括号形态后跟逗号亦在此拦截）
      errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression must be the sole argument");
      free(tmp);
      return NULL;
    }
    if (!match(p, TK_COMMA)) {
      break;
    }
  }
  if (!closedByGenexpr && !match(p, TK_RPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
    free(tmp);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_CALL, line, col);
  node->as.call.callee = callee;
  node->as.call.count = count;
  node->as.call.args = astNewNodeArray(p->arena, count);
  node->as.call.spread = astArenaAlloc(p->arena, sizeof(bool) * (size_t)count);
  for (int32_t i = 0; i < count; i++) {
    node->as.call.args[i] = tmp[i].expr;
    node->as.call.spread[i] = tmp[i].spread;
  }
  free(tmp);
  return node;
}

// 切片尾部：已消费 '[' 与可选 start，当前 token 为 ':'（03 §3 端点/步进均可省略）
static AstNode* parseSliceRest(Parser* p, AstNode* obj, AstNode* start, int32_t line, int32_t col) {
  advance(p);  // :
  AstNode* end = NULL;
  if (!check(p, TK_COLON) && !check(p, TK_RBRACKET)) {
    end = parseExpr(p);
    if (end == NULL) {
      return NULL;
    }
  }
  AstNode* step = NULL;
  if (match(p, TK_COLON) && !check(p, TK_RBRACKET)) {
    step = parseExpr(p);
    if (step == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_RBRACKET)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_SLICE, line, col);
  node->as.slice.obj = obj;
  node->as.slice.start = start;
  node->as.slice.end = end;
  node->as.slice.step = step;
  return node;
}

// 推导式/生成器公共尾部（03 §2）：(for 目标 in 迭代对象 | if 条件)*，末尾闭括号 closer
// compKind 取 AST_COMP_LIST/MAP/SET；0 为生成器表达式（AST_GEN_EXPR）
static AstNode* parseComprTail(Parser* p, AstNode* key, AstNode* value, int32_t compKind, MsTokenType closer,
                               int32_t line, int32_t col) {
  AstComprFor* fors = NULL;
  int32_t forCount = 0, forCap = 0;
  AstNode** conds = NULL;
  int32_t condCount = 0, condCap = 0;
  bool bad = false;
  while (!bad) {
    if (match(p, TK_FOR)) {
      AstNode** names = NULL;
      int32_t nameCount = 0, nameCap = 0;
      while (!bad) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected identifier in 'for' target");
          bad = true;
          break;
        }
        AstNode* id = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
        id->as.ident.name = p->cur.start;
        id->as.ident.nameLen = p->cur.len;
        advance(p);
        MS_ARRAY_PUSH(names, nameCount, nameCap, id);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (bad) {
        free(names);
        break;
      }
      if (!match(p, TK_IN)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'in' after 'for' target");
        free(names);
        bad = true;
        break;
      }
      AstNode* iter = parseExpr(p);
      if (iter == NULL || rejectGenExpr(p, iter)) {
        free(names);
        bad = true;
        break;
      }
      AstNode* target;
      if (nameCount == 1) {
        target = names[0];
      } else {  // for k, v in … → 元组目标
        target = astNewNode(p->arena, AST_TUPLE, names[0]->line, names[0]->col);
        target->as.tuple.count = nameCount;
        target->as.tuple.items = astNewNodeArray(p->arena, nameCount);
        for (int32_t i = 0; i < nameCount; i++) {
          target->as.tuple.items[i] = names[i];
        }
      }
      free(names);
      MS_ARRAY_PUSH(fors, forCount, forCap, ((AstComprFor){target, iter}));
    } else if (match(p, TK_IF)) {
      AstNode* cond = parseExpr(p);
      if (cond == NULL) {
        bad = true;
        break;
      }
      MS_ARRAY_PUSH(conds, condCount, condCap, cond);
    } else {
      break;
    }
  }
  if (!bad && !match(p, closer)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected closing bracket of comprehension");
    bad = true;
  }
  if (bad) {
    free(fors);
    free(conds);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, compKind == 0 ? AST_GEN_EXPR : AST_COMPREHENSION, line, col);
  node->as.compr.compKind = (ms_u8)compKind;
  node->as.compr.key = key;
  node->as.compr.value = value;
  node->as.compr.forCount = forCount;
  node->as.compr.fors = astArenaAlloc(p->arena, sizeof(AstComprFor) * (size_t)forCount);
  if (forCount > 0) {
    memcpy(node->as.compr.fors, fors, sizeof(AstComprFor) * (size_t)forCount);
  }
  node->as.compr.condCount = condCount;
  node->as.compr.conds = astNewNodeArray(p->arena, condCount);
  if (condCount > 0) {
    memcpy(node->as.compr.conds, conds, sizeof(AstNode*) * (size_t)condCount);
  }
  free(fors);
  free(conds);
  return node;
}

// 后缀链循环（左结合，03 优先级 14）：调用/索引/切片/成员
// 独立成函数供语句位函数表达式续接（裸 IIFE：func(…) { … }(…)）
static AstNode* parsePostfixFrom(Parser* p, AstNode* obj) {
  for (;;) {
    if (!check(p, TK_LPAREN) && !check(p, TK_LBRACKET) && !check(p, TK_DOT)) {
      return obj;  // 无后缀 token：结束（裸 GEN_EXPR 由此合法离开）
    }
    if (rejectGenExpr(p, obj)) {  // GEN_EXPR 后跟调用/索引/成员 → 位置非法
      return NULL;
    }
    if (check(p, TK_LPAREN)) {
      obj = parseCallArgs(p, obj, obj->line, obj->col);
      if (obj == NULL) {
        return NULL;
      }
    } else if (check(p, TK_LBRACKET)) {
      int32_t line = obj->line, col = obj->col;  // 构造起点取对象起点
      advance(p);
      if (check(p, TK_COLON)) {  // [:…] start 省略
        obj = parseSliceRest(p, obj, NULL, line, col);
        if (obj == NULL) {
          return NULL;
        }
        continue;
      }
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        return NULL;
      }
      if (check(p, TK_FOR)) {  // 索引位裸生成器（03 §1：非调用实参/赋值右值）
        errTok(p, &p->cur, MSE_SYN_GENEXPR_POS, "generator expression is not allowed in index");
        return NULL;
      }
      if (check(p, TK_COLON)) {
        obj = parseSliceRest(p, obj, first, line, col);
        if (obj == NULL) {
          return NULL;
        }
      } else {
        if (!match(p, TK_RBRACKET)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ']'");
          return NULL;
        }
        AstNode* node = astNewNode(p->arena, AST_INDEX, line, col);
        node->as.indexExpr.obj = obj;
        node->as.indexExpr.index = first;
        obj = node;
      }
    } else {
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
    }
  }
}

// 后缀链（左结合，03 优先级 14）：主表达式 + 调用/索引/切片/成员
static AstNode* parsePostfix(Parser* p) {
  AstNode* obj = parsePrimary(p);
  if (obj == NULL) {
    return NULL;
  }
  return parsePostfixFrom(p, obj);
}

// 二元构造（各级共用）：操作数不得为 GEN_EXPR
static AstNode* mkBinary(Parser* p, AstNode* lhs, AstBinOp op, AstNode* rhs) {
  if (rejectGenExpr(p, lhs) || rejectGenExpr(p, rhs)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_BINARY, lhs->line, lhs->col);
  node->as.binary.lhs = lhs;
  node->as.binary.rhs = rhs;
  node->as.binary.op = op;
  return node;
}

// 逻辑短路构造（AST_LOGIC 复用 binary 载荷，op 取 AND/OR）
static AstNode* mkLogic(Parser* p, AstNode* lhs, AstBinOp op, AstNode* rhs) {
  if (rejectGenExpr(p, lhs) || rejectGenExpr(p, rhs)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_LOGIC, lhs->line, lhs->col);
  node->as.binary.lhs = lhs;
  node->as.binary.rhs = rhs;
  node->as.binary.op = op;
  return node;
}

static AstNode* mkUnary(Parser* p, AstUnaryOp op, AstNode* operand, int32_t line, int32_t col) {
  if (rejectGenExpr(p, operand)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_UNARY, line, col);  // 取运算符位置
  node->as.unary.op = op;
  node->as.unary.operand = operand;
  return node;
}

static AstNode* parsePow(Parser* p) {
  AstNode* base = parsePostfix(p);
  if (base == NULL) {
    return NULL;
  }
  if (!match(p, TK_STAR_STAR)) {
    return base;
  }
  AstNode* rhs = parseUnary(p);  // 右结合；rhs 吸收一元（2 ** -3 合法）
  if (rhs == NULL) {
    return NULL;
  }
  return mkBinary(p, base, AST_OP_POW, rhs);
}

// 优先级 13 前缀（03 §8）：<-
// 接收 / await / go（复用 unary 载荷，op 恒 0）
static AstNode* parsePrefix13(Parser* p) {
  AstKind kind;
  switch (p->cur.type) {
    case TK_LARROW:
      kind = AST_RECV;
      break;
    case TK_AWAIT:
      kind = AST_AWAIT;
      break;
    case TK_GO:
      kind = AST_GO;
      break;
    default:
      return parsePow(p);
  }
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* operand = parsePrefix13(p);
  if (operand == NULL || rejectGenExpr(p, operand)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, kind, line, col);
  node->as.unary.operand = operand;
  return node;
}

static AstNode* parseUnary(Parser* p) {
  AstUnaryOp op;
  switch (p->cur.type) {
    case TK_MINUS:
      op = AST_UNOP_NEG;
      break;
    case TK_BANG:
    case TK_NOT:
      op = AST_UNOP_NOT;
      break;
    case TK_TILDE:
      op = AST_UNOP_INV;
      break;
    default:
      return parsePrefix13(p);
  }
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* operand = parseUnary(p);  // 一元在 ** 之下：-2 ** 2 → -(2 ** 2)
  if (operand == NULL) {
    return NULL;
  }
  return mkUnary(p, op, operand, line, col);
}

// 左结合二元层通用循环：next 为更高层，fromTok 无操作时返回 AST_OP_NONE
static AstNode* parseBinaryLevel(Parser* p, AstNode* (*next)(Parser*), AstBinOp (*fromTok)(MsTokenType)) {
  AstNode* lhs = next(p);
  if (lhs == NULL) {
    return NULL;
  }
  for (;;) {
    AstBinOp op = fromTok(p->cur.type);
    if (op == AST_OP_NONE) {
      return lhs;
    }
    advance(p);
    AstNode* rhs = next(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkBinary(p, lhs, op, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
}

static AstBinOp mulOpOf(MsTokenType type) {
  switch (type) {
    case TK_STAR:
      return AST_OP_MUL;
    case TK_SLASH:
      return AST_OP_DIV;
    case TK_PERCENT:
      return AST_OP_MOD;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp addOpOf(MsTokenType type) {
  switch (type) {
    case TK_PLUS:
      return AST_OP_ADD;
    case TK_MINUS:
      return AST_OP_SUB;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp shiftOpOf(MsTokenType type) {
  switch (type) {
    case TK_LT_LT:
      return AST_OP_SHL;
    case TK_GT_GT:
      return AST_OP_SHR;
    default:
      return AST_OP_NONE;
  }
}

static AstBinOp bitAndOpOf(MsTokenType type) { return type == TK_AMP ? AST_OP_BIT_AND : AST_OP_NONE; }

static AstBinOp bitXorOpOf(MsTokenType type) { return type == TK_CARET ? AST_OP_BIT_XOR : AST_OP_NONE; }

static AstBinOp bitOrOpOf(MsTokenType type) { return type == TK_PIPE ? AST_OP_BIT_OR : AST_OP_NONE; }

static AstNode* parseMul(Parser* p) { return parseBinaryLevel(p, parseUnary, mulOpOf); }

static AstNode* parseAdd(Parser* p) { return parseBinaryLevel(p, parseMul, addOpOf); }

static AstNode* parseShift(Parser* p) { return parseBinaryLevel(p, parseAdd, shiftOpOf); }

static AstNode* parseBitAnd(Parser* p) { return parseBinaryLevel(p, parseShift, bitAndOpOf); }

static AstNode* parseBitXor(Parser* p) { return parseBinaryLevel(p, parseBitAnd, bitXorOpOf); }

static AstNode* parseBitOr(Parser* p) { return parseBinaryLevel(p, parseBitXor, bitOrOpOf); }

// 比较（03 优先级 4）：单次、不可链式
static AstNode* parseCompare(Parser* p) {
  AstNode* lhs = parseBitOr(p);
  if (lhs == NULL) {
    return NULL;
  }
  AstBinOp op = comparisonOpOf(p->cur.type);
  if (op == AST_OP_NONE || (op == AST_OP_IN && p->noIn)) {
    return lhs;  // noIn：for 头部顶层 in 留给迭代形态判别（04 §4）
  }
  advance(p);
  AstNode* rhs = parseBitOr(p);
  if (rhs == NULL) {
    return NULL;
  }
  if (comparisonOpOf(p->cur.type) != AST_OP_NONE) {
    errTok(p, &p->cur, MSE_SYN_CHAINED_CMP, "comparison operators cannot be chained");
    return NULL;
  }
  return mkBinary(p, lhs, op, rhs);
}

// 逻辑与（03 优先级 3）：&& / and
static AstNode* parseAnd(Parser* p) {
  AstNode* lhs = parseCompare(p);
  if (lhs == NULL) {
    return NULL;
  }
  while (check(p, TK_AMP_AMP) || check(p, TK_AND)) {
    advance(p);
    AstNode* rhs = parseCompare(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkLogic(p, lhs, AST_OP_AND, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
  return lhs;
}

// 逻辑或（03 优先级 2）：|| / or
static AstNode* parseOr(Parser* p) {
  AstNode* lhs = parseAnd(p);
  if (lhs == NULL) {
    return NULL;
  }
  while (check(p, TK_PIPE_PIPE) || check(p, TK_OR)) {
    advance(p);
    AstNode* rhs = parseAnd(p);
    if (rhs == NULL) {
      return NULL;
    }
    lhs = mkLogic(p, lhs, AST_OP_OR, rhs);
    if (lhs == NULL) {
      return NULL;
    }
  }
  return lhs;
}

// 三元（03 §7）：右结合（分支递归本层），分支惰性求值由编译期保证
static AstNode* parseTernary(Parser* p) {
  AstNode* cond = parseOr(p);
  if (cond == NULL) {
    return NULL;
  }
  if (!match(p, TK_QUESTION)) {
    return cond;
  }
  AstNode* thenExpr = parseTernary(p);
  if (thenExpr == NULL) {
    return NULL;
  }
  if (!match(p, TK_COLON)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' in conditional expression");
    return NULL;
  }
  AstNode* elseExpr = parseTernary(p);
  if (elseExpr == NULL) {
    return NULL;
  }
  if (rejectGenExpr(p, cond) || rejectGenExpr(p, thenExpr) || rejectGenExpr(p, elseExpr)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_TERNARY, cond->line, cond->col);
  node->as.ternary.cond = cond;
  node->as.ternary.thenExpr = thenExpr;
  node->as.ternary.elseExpr = elseExpr;
  return node;
}

static AstNode* parseExpr(Parser* p) { return parseTernary(p); }

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
  if (count > 0) {
    memcpy(node->as.tuple.items, tmp, sizeof(AstNode*) * (size_t)count);
  }
  free(tmp);
  return node;
}

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
    init = parseExprList(p);
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

// 简单语句收尾：; / } / EOF / 换行任一（块内无 ASI 分号，换行按 04 §10 即语句边界）
static bool endSimpleStmt(Parser* p) {
  if (match(p, TK_SEMICOLON)) {
    return true;
  }
  if (check(p, TK_RBRACE) || check(p, TK_EOF)) {
    return true;
  }
  if (p->cur.line > p->prevLine) {
    return true;
  }
  errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected newline or ';' after statement");
  return false;
}

// 语句序列（模块体/块体共用）：孤立 ; 静默跳过（04 §10）；panic 语句同步后继续，无 panic 的 NULL 已就地恢复至语句边界
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
    } else if (p->panic) {
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

// if / else if / else（04 §3）：条件不加括号（写括号 MSE1003）、块必须花括号
static AstNode* parseIfStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // if
  bool hadParen = false;
  if (check(p, TK_LPAREN)) {
    diagPos(p, p->cur.line, p->cur.col, 1, MSE_SYN_IF_PAREN, "if condition must not be parenthesized");
    hadParen = true;
    advance(p);  // 就地恢复：按无括号继续
  }
  AstNode* cond = parseExpr(p);
  if (cond == NULL) {
    return NULL;
  }
  if (hadParen) {
    match(p, TK_RPAREN);  // 括号错误路径的配对消费（正常路径遇不到）
  }
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

// 值可省跳转语句（04 §6、05 §4 §5）：return/throw/yield（裸 return 返回 nil，裸 throw 为重抛，07 §3）
static AstNode* parseValueJumpStmt(Parser* p, AstKind kind) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* value = NULL;
  if (!check(p, TK_RBRACE) && !check(p, TK_SEMICOLON) && !check(p, TK_EOF) && p->cur.line == p->prevLine) {
    value = parseExprList(p);
    if (value == NULL) {
      return NULL;
    }
  }
  AstNode* node = astNewNode(p->arena, kind, line, col);
  node->as.exprStmt.expr = value;  // 载荷复用 exprStmt
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// return 语句（05 §5）：裸 return 返回 nil；多值 return a, b 即 tuple（完整函数族 T008）
static AstNode* parseReturnStmt(Parser* p) { return parseValueJumpStmt(p, AST_RETURN); }

// 参数表（05 §1）：普通 → 默认 → 至多一个 *rest
// 违序 MSE1012 就地诊断不中断（MSE1003 先例：泛 synchronize 会误吞函数体 } 报 unmatched）
static bool parseParamList(Parser* p, AstParam** outParams, int32_t* outCount) {
  if (!match(p, TK_LPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '(' after function name");
    return false;
  }
  AstParam* params = NULL;
  int32_t count = 0, cap = 0;
  bool seenDefault = false;
  bool seenRest = false;
  while (!check(p, TK_RPAREN) && !check(p, TK_EOF)) {
    bool rest = match(p, TK_STAR);
    if (!check(p, TK_ID)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected parameter name");
      goto fail;
    }
    AstParam param = {p->cur.start, p->cur.len, NULL, rest};
    int32_t nameLine = p->cur.line, nameCol = p->cur.col;  // 违序诊断定位在参数名 token
    int32_t nameLen = p->cur.len;
    advance(p);
    if (match(p, TK_EQ)) {
      param.defaultExpr = parseExpr(p);
      if (param.defaultExpr == NULL) {
        goto fail;
      }
      if (rest) {
        diagPos(p, nameLine, nameCol, nameLen, MSE_SYN_PARAM_ORDER, "rest parameter cannot have a default");
      } else {
        seenDefault = true;
      }
    }
    if (rest) {
      if (seenRest) {
        diagPos(p, nameLine, nameCol, nameLen, MSE_SYN_PARAM_ORDER, "at most one rest parameter allowed");
      }
      seenRest = true;
    } else if (seenRest) {
      diagPos(p, nameLine, nameCol, nameLen, MSE_SYN_PARAM_ORDER, "no parameter allowed after rest parameter");
    } else if (seenDefault && param.defaultExpr == NULL) {
      diagPos(p, nameLine, nameCol, nameLen, MSE_SYN_PARAM_ORDER,
              "parameter without default follows default parameter");
    }
    MS_ARRAY_PUSH(params, count, cap, param);
    if (!match(p, TK_COMMA)) {
      break;
    }
  }
  if (!match(p, TK_RPAREN)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')'");
    goto fail;
  }
  *outParams = params;
  *outCount = count;
  return true;
fail:
  free(params);
  return false;
}

// 函数体（05 §1）：{ } 块或 -> 表达式；新作用域 + funcDepth（defer 合法性，04 §7）
static bool parseFuncBody(Parser* p, AstNode** outBody, AstNode** outExprBody) {
  pushScope(p);
  p->funcDepth++;
  bool savedNoIn = p->noIn;
  p->noIn = false;  // 函数体回到正常表达式语境（for 头 noIn 不穿透，JS 同则）
  bool ok;
  if (match(p, TK_ARROW)) {
    *outBody = NULL;
    *outExprBody = parseExpr(p);
    ok = *outExprBody != NULL;
  } else {
    *outExprBody = NULL;
    *outBody = parseBlock(p);
    ok = *outBody != NULL;
  }
  p->noIn = savedNoIn;
  p->funcDepth--;
  popScope(p);
  return ok;
}

// 语句位函数表达式（05 §2）：func[*](params) 体 → 表达式语句（T007 probe F 回归）
static AstNode* parseFuncExprStmt(Parser* p, ms_u8 flags, int32_t line, int32_t col) {
  AstParam* params = NULL;
  int32_t paramCount = 0;
  if (!parseParamList(p, &params, &paramCount)) {
    return NULL;
  }
  AstNode* body = NULL;
  AstNode* exprBody = NULL;
  if (!parseFuncBody(p, &body, &exprBody)) {
    free(params);
    return NULL;
  }
  AstNode* fn = astNewNode(p->arena, AST_FUNC_EXPR, line, col);
  fn->as.funcExpr.flags = flags;
  fn->as.funcExpr.paramCount = paramCount;
  fn->as.funcExpr.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
  if (paramCount > 0) {
    memcpy(fn->as.funcExpr.params, params, sizeof(AstParam) * (size_t)paramCount);
  }
  fn->as.funcExpr.body = body;
  fn->as.funcExpr.exprBody = exprBody;
  free(params);
  fn = parsePostfixFrom(p, fn);  // 续接后缀链：裸 IIFE func(…) { … }(…)（T007 能力保持）
  if (fn == NULL) {
    return NULL;
  }
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_EXPR_STMT, line, col);
  node->as.exprStmt.expr = fn;
  return node;
}

// 函数声明（05 §1 §8）：[async] func[*] name(params) { } / -> expr
// func 后无 name 而直接 ( → 语句位函数表达式（05 §2，名称可省）
static AstNode* parseFuncDeclStmt(Parser* p, ms_u8 flags, int32_t line, int32_t col) {
  advance(p);  // func
  if (match(p, TK_STAR)) {
    flags |= AST_FUNC_GENERATOR;
  }
  if (!check(p, TK_ID)) {
    if (check(p, TK_LPAREN)) {
      return parseFuncExprStmt(p, flags, line, col);
    }
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected function name");
    return NULL;
  }
  const char* name = p->cur.start;
  int32_t nameLen = p->cur.len;
  advance(p);
  AstParam* params = NULL;
  int32_t paramCount = 0;
  if (!parseParamList(p, &params, &paramCount)) {
    return NULL;
  }
  AstNode* body = NULL;
  AstNode* exprBody = NULL;
  if (!parseFuncBody(p, &body, &exprBody)) {
    free(params);
    return NULL;
  }
  if (exprBody != NULL && !endSimpleStmt(p)) {  // 表达式体需语句终点（块体以 } 收尾，无需）
    free(params);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_FUNC_DECL, line, col);
  node->as.funcDecl.name = name;
  node->as.funcDecl.nameLen = nameLen;
  node->as.funcDecl.flags = flags;
  node->as.funcDecl.paramCount = paramCount;
  node->as.funcDecl.params = astArenaAlloc(p->arena, sizeof(AstParam) * (size_t)paramCount);
  if (paramCount > 0) {
    memcpy(node->as.funcDecl.params, params, sizeof(AstParam) * (size_t)paramCount);
  }
  node->as.funcDecl.body = body;
  node->as.funcDecl.exprBody = exprBody;
  free(params);
  return node;
}

// 赋值目标合法性：标识符/索引/成员（04 §2）
static bool isAssignTarget(const AstNode* node) {
  return node->kind == AST_IDENT || node->kind == AST_INDEX || node->kind == AST_MEMBER;
}

static AstNode* newExprStmt(Parser* p, AstNode* expr, int32_t line, int32_t col) {
  AstNode* node = astNewNode(p->arena, AST_EXPR_STMT, line, col);
  node->as.exprStmt.expr = expr;
  return node;
}

typedef struct TargetElem {
  AstNode* expr;
  bool starred;
} TargetElem;

// = 后定形：目标合法性 + star 计数 → ASSIGN(元组目标) 或 DESTRUCTURE
static AstNode* finishAssignTargets(Parser* p, TargetElem* tmp, int32_t count, int32_t line, int32_t col,
                                    bool terminate) {
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
      errPos(p, tmp[i].expr->line, tmp[i].expr->col, 1, MSE_SYN_BAD_TARGET, "invalid assignment target");
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
    if (terminate && !endSimpleStmt(p)) {
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
  if (terminate && !endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// 首表达式后定形：短声明 / send / 元组目标 / 复合赋值 / 普通赋值 / 表达式语句
// terminate=false 供 for 头部首段使用：不消费结尾 ;/换行（04 §4 判别需要，后续任务）
static AstNode* finishSimpleStmt(Parser* p, AstNode* first, bool firstStar, int32_t line, int32_t col, bool terminate) {
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
    if (inModuleScope(p) && !p->loopHeader) {  // for 头部循环域为块作用域（04 §4）：:= 合法
      errPos(p, line, col, 1, MSE_SYN_SHORT_TOPLEVEL, "':=' is not allowed at module top level");
      return NULL;
    }
    if (!p->loopHeader && scopeDeclare(p, first->as.ident.name, first->as.ident.nameLen)) {
      errPos(p, line, col, first->as.ident.nameLen, MSE_SYN_DUP_DECL, "duplicate declaration in the same scope");
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_VAR_DECL, line, col);
    node->as.varDecl.name = first->as.ident.name;
    node->as.varDecl.nameLen = first->as.ident.nameLen;
    node->as.varDecl.flags = AST_VAR_SHORT;
    node->as.varDecl.init = value;
    if (terminate && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // channel 发送语句：ch <- v（04 §8）
  if (!firstStar && check(p, TK_LARROW)) {
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid send target");
      return NULL;
    }
    advance(p);
    AstNode* value = parseExpr(p);
    if (value == NULL) {
      return NULL;
    }
    AstNode* node = astNewNode(p->arena, AST_SEND, line, col);
    node->as.sendStmt.chan = first;
    node->as.sendStmt.value = value;
    if (terminate && !endSimpleStmt(p)) {
      return NULL;
    }
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
      AstNode* node = finishAssignTargets(p, tmp, count, line, col, terminate);
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
    if (terminate && !endSimpleStmt(p)) {
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
    if (terminate && !endSimpleStmt(p)) {
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
    if (terminate && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  // 表达式语句
  if (firstStar) {
    errPos(p, line, col, 1, MSE_SYN_STAR_NO_ASSIGN, "starred target requires an assignment");
    return NULL;
  }
  if (terminate && !endSimpleStmt(p)) {
    return NULL;
  }
  return newExprStmt(p, first, line, col);
}

// 表达式/赋值/短声明语句（04 §1 §2 §10）：先解析逗号列表，按后续 token 定形
static AstNode* parseSimpleStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool firstStar = match(p, TK_STAR);  // 语句起始 * 仅解构目标合法
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  return finishSimpleStmt(p, first, firstStar, line, col, true);
}

// for 第三段（04 §4）：i++ / i-- 语法糖（语言无通用自增运算符，仅此处）→ 复合赋值
// 目标取后缀表达式而非完整阶梯：加法层会把 i++ 的首个 + 当二元运算符（rhs 遇第二个 + 必报错）
static AstNode* parseForPost(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  AstNode* first = parsePostfix(p);
  if (first == NULL) {
    return NULL;
  }
  bool inc = check(p, TK_PLUS) && peekType(p) == TK_PLUS;
  bool dec = check(p, TK_MINUS) && peekType(p) == TK_MINUS;
  if (inc || dec) {
    advance(p);
    advance(p);
    if (!isAssignTarget(first)) {
      errPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "invalid 'for' post target");
      return NULL;
    }
    AstNode* one = astNewNode(p->arena, AST_INT, line, col);
    one->as.intLit.v = 1;
    AstNode* node = astNewNode(p->arena, AST_ASSIGN, line, col);
    node->as.assign.target = first;
    node->as.assign.value = one;
    node->as.assign.op = inc ? AST_OP_ADD : AST_OP_SUB;
    return node;
  }
  // 非糖：其余简单语句形态（赋值/复合赋值/send）；后随体块 '{'，不作语句终止判定
  return finishSimpleStmt(p, first, false, line, col, false);
}

// for 首段（04 §4）：不消费段尾 ';'，交由形态判别（';' 三段式 / in 迭代 / '{' 条件式）
static AstNode* parseForInit(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool firstStar = match(p, TK_STAR);
  AstNode* first = parseExpr(p);
  if (first == NULL) {
    return NULL;
  }
  return finishSimpleStmt(p, first, firstStar, line, col, false);
}

// 三段式后两段（04 §4）：init 可 NULL；cond/post 省略时第二 ';' 随 '{' 可省
static AstNode* parseForTriRest(Parser* p, AstNode* node, AstNode* init) {
  advance(p);  // 首段后的 ';'
  node->as.forStmt.init = init;
  if (!check(p, TK_SEMICOLON) && !check(p, TK_LBRACE)) {
    node->as.forStmt.cond = parseExpr(p);
    if (node->as.forStmt.cond == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_SEMICOLON) && !check(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ';' in 'for' header");
    return NULL;
  }
  if (!check(p, TK_LBRACE)) {
    node->as.forStmt.post = parseForPost(p);
    if (node->as.forStmt.post == NULL) {
      return NULL;
    }
  }
  node->as.forStmt.body = parseBlock(p);
  if (node->as.forStmt.body == NULL) {
    return NULL;
  }
  return node;
}

// for-in 目标：IDENT 或全 IDENT 的 TUPLE（04 §4）
static bool isIdentListTarget(const AstNode* n) {
  if (n->kind == AST_IDENT) {
    return true;
  }
  if (n->kind != AST_TUPLE) {
    return false;
  }
  for (int32_t i = 0; i < n->as.tuple.count; i++) {
    if (n->as.tuple.items[i]->kind != AST_IDENT) {
      return false;
    }
  }
  return true;
}

// for 五形态（04 §4）：判别依据任务 §2（';' 三段式 / in 迭代 / '{' 无限或条件式）
static AstNode* parseForStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // for
  AstNode* node = astNewNode(p->arena, AST_FOR, line, col);
  node->as.forStmt.form = AST_FOR_TRI;
  if (check(p, TK_LBRACE)) {
    node->as.forStmt.form = AST_FOR_INF;
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  AstNode* init = NULL;
  if (!check(p, TK_SEMICOLON)) {  // 首段省略则直接三段式
    bool savedNoIn = p->noIn;
    p->noIn = true;
    bool savedLoopHeader = p->loopHeader;
    p->loopHeader = true;
    init = parseForInit(p);  // 不消费段尾 ';'（判别依赖）
    p->loopHeader = savedLoopHeader;
    p->noIn = savedNoIn;
    if (init == NULL) {
      return NULL;
    }
  }
  if (check(p, TK_IN)) {  // 迭代形态：目标取自首段表达式
    advance(p);
    AstNode* target = init->kind == AST_EXPR_STMT ? init->as.exprStmt.expr : NULL;
    node->as.forStmt.form = AST_FOR_IN;
    if (target == NULL || !isIdentListTarget(target)) {
      // 就地诊断不中断（parseParamList 先例）：errPos+NULL 会经 synchronize 吞块 } 误报 unmatched
      diagPos(p, line, col, 1, MSE_SYN_BAD_TARGET, "'for' target must be an identifier list");
    }
    node->as.forStmt.target = target;
    node->as.forStmt.iter = parseExpr(p);
    if (node->as.forStmt.iter == NULL) {
      return NULL;
    }
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  if (init != NULL && check(p, TK_LBRACE)) {  // 条件形态：首段即条件
    if (init->kind != AST_EXPR_STMT) {
      errTok(p, &p->cur, MSE_SYN_UNEXPECTED, "expected expression as 'for' condition");
      return NULL;
    }
    node->as.forStmt.form = AST_FOR_COND;
    node->as.forStmt.cond = init->as.exprStmt.expr;
    node->as.forStmt.body = parseBlock(p);
    return node->as.forStmt.body != NULL ? node : NULL;
  }
  if (!check(p, TK_SEMICOLON)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ';', 'in' or '{' in 'for' header");
    return NULL;
  }
  return parseForTriRest(p, node, init);
}

// while（04 §4）
static AstNode* parseWhileStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);
  AstNode* cond = parseExpr(p);
  if (cond == NULL) {
    return NULL;
  }
  AstNode* body = parseBlock(p);
  if (body == NULL) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_WHILE, line, col);
  node->as.whileStmt.cond = cond;
  node->as.whileStmt.body = body;
  return node;
}

// switch 值式/类型式（04 §5）：type 为普通标识符，switch 后 ID "type"+peek( 特判（任务 §6）
// switch 块内结构性错误：跳至配对 } 一并消费（泛 synchronize 会停在本块 } 误报 unmatched，T006 §2 先例）
static void skipSwitchClose(Parser* p) {
  int32_t depth = 1;  // switch 本体 { 已消费
  while (!check(p, TK_EOF)) {
    if (check(p, TK_LBRACE)) {
      depth++;
    } else if (check(p, TK_RBRACE)) {
      depth--;
      if (depth == 0) {
        advance(p);
        return;
      }
    }
    advance(p);
  }
}

static AstNode* parseSwitchStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // switch
  bool isType = check(p, TK_ID) && p->cur.len == 4 && memcmp(p->cur.start, "type", 4) == 0 && peekType(p) == TK_LPAREN;
  AstNode* subject;
  if (isType) {
    advance(p);  // type
    advance(p);  // (
    subject = parseExpr(p);
    if (subject == NULL) {
      return NULL;
    }
    if (!match(p, TK_RPAREN)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ')' after 'type('");
      return NULL;
    }
  } else {
    subject = parseExpr(p);
    if (subject == NULL) {
      return NULL;
    }
  }
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' after switch subject");
    return NULL;
  }
  AstNode** cases = NULL;
  int32_t count = 0, cap = 0;
  while (check(p, TK_CASE) || check(p, TK_DEFAULT)) {
    int32_t caseLine = p->cur.line, caseCol = p->cur.col;
    bool isDefault = check(p, TK_DEFAULT);
    advance(p);
    AstNode** values = NULL;
    int32_t valueCount = 0, valueCap = 0;
    const char* bindName = NULL;
    int32_t bindLen = 0;
    bool ok = true;
    if (!isDefault) {
      for (;;) {
        AstNode* value;
        if (isType) {
          if (!check(p, TK_ID)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected type name");
            ok = false;
            break;
          }
          value = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
          value->as.ident.name = p->cur.start;
          value->as.ident.nameLen = p->cur.len;
          advance(p);
        } else {
          value = parseExpr(p);
          if (value == NULL) {
            ok = false;
            break;
          }
        }
        MS_ARRAY_PUSH(values, valueCount, valueCap, value);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (ok && isType && match(p, TK_AS)) {  // case X as p：绑定进块作用域（04 §5.2）
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected identifier after 'as'");
          ok = false;
        } else {
          bindName = p->cur.start;
          bindLen = p->cur.len;
          advance(p);
        }
      }
    }
    if (ok && !match(p, TK_COLON)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' after case");
      ok = false;
    }
    AstNode* body = NULL;
    if (ok) {
      body = parseStatement(p);  // case 体为块或单语句
      if (body == NULL) {
        ok = false;
      }
    }
    if (!ok) {
      free(values);
      free(cases);
      skipSwitchClose(p);
      p->panic = false;  // 已恢复至语句边界（诊断已出）：免 synchronize 吞同行后续语句
      return NULL;
    }
    AstNode* caseNode = astNewNode(p->arena, AST_SWITCH_CASE, caseLine, caseCol);
    caseNode->as.switchCase.isDefault = isDefault;
    caseNode->as.switchCase.valueCount = valueCount;
    caseNode->as.switchCase.values = astNewNodeArray(p->arena, valueCount);
    if (valueCount > 0) {
      memcpy(caseNode->as.switchCase.values, values, sizeof(AstNode*) * (size_t)valueCount);
    }
    caseNode->as.switchCase.bindName = bindName;
    caseNode->as.switchCase.bindLen = bindLen;
    caseNode->as.switchCase.body = body;
    free(values);
    MS_ARRAY_PUSH(cases, count, cap, caseNode);
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'case' or '}' in switch body");
    free(cases);
    skipSwitchClose(p);
    p->panic = false;  // 同上：已消费至配对 }，边界干净
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_SWITCH, line, col);
  node->as.switchStmt.isType = isType;
  node->as.switchStmt.subject = subject;
  node->as.switchStmt.count = count;
  node->as.switchStmt.cases = astNewNodeArray(p->arena, count);
  if (count > 0) {
    memcpy(node->as.switchStmt.cases, cases, sizeof(AstNode*) * (size_t)count);
  }
  free(cases);
  return node;
}

// try/catch/finally（07 §1）：类型子句在前、裸绑定兜底最后
// 单 ID 子句一律按裸绑定解析（任务 §5.1 排序用例；绑定名恰为类名的复核归 T023 binder）
static AstNode* parseTryStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // try
  AstNode* tryBlock = parseBlock(p);
  if (tryBlock == NULL) {
    return NULL;
  }
  AstCatchClause* catches = NULL;
  int32_t catchCount = 0, catchCap = 0;
  bool seenBare = false;
  bool orderError = false;
  while (check(p, TK_CATCH)) {
    int32_t catchLine = p->cur.line, catchCol = p->cur.col, catchLen = p->cur.len;
    advance(p);
    if (seenBare && !orderError) {  // 裸绑定必须兜底最后：仅首条违序报错，后续子句静默并入
      diagPos(p, catchLine, catchCol, catchLen, MSE_SYN_CATCH_ORDER, "catch-all clause must be the last catch");
      orderError = true;
    }
    AstCatchClause clause = {0};
    if (check(p, TK_ID)) {
      const char* firstName = p->cur.start;
      int32_t firstLen = p->cur.len;
      int32_t nameLine = p->cur.line, nameCol = p->cur.col;
      advance(p);
      if (check(p, TK_COMMA)) {  // 类型子句：catch A, B, …
        AstNode** types = NULL;
        int32_t typeCount = 0, typeCap = 0;
        AstNode* firstType = astNewNode(p->arena, AST_IDENT, nameLine, nameCol);
        firstType->as.ident.name = firstName;
        firstType->as.ident.nameLen = firstLen;
        MS_ARRAY_PUSH(types, typeCount, typeCap, firstType);
        while (match(p, TK_COMMA)) {
          if (!check(p, TK_ID)) {
            // 就地恢复：缺类型名仅报此一条，已收集类型照常提交（不 panic、不级联）
            diagPos(p, p->cur.line, p->cur.col, p->cur.len, MSE_SYN_EXPECTED, "expected exception type name");
            // 残留分隔/名称就位恢复：跳至子句块边界（连续逗号等病态形态）
            while (match(p, TK_COMMA)) {
              if (match(p, TK_ID)) {
                // 丢弃该残段类型名（诊断已发）
              }
            }
            break;
          }
          AstNode* t = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
          t->as.ident.name = p->cur.start;
          t->as.ident.nameLen = p->cur.len;
          advance(p);
          MS_ARRAY_PUSH(types, typeCount, typeCap, t);
        }
        clause.typeCount = typeCount;
        clause.types = astNewNodeArray(p->arena, typeCount);
        if (typeCount > 0) {
          memcpy(clause.types, types, sizeof(AstNode*) * (size_t)typeCount);
        }
        free(types);
      } else {  // 裸绑定兜底：catch e
        clause.bindName = firstName;
        clause.bindLen = firstLen;
        seenBare = true;
      }
    }
    clause.block = parseBlock(p);
    if (clause.block == NULL) {
      // clause.types 已归 arena 所有（astNewNodeArray），不可 free；仅释放临时数组
      free(catches);
      return NULL;
    }
    MS_ARRAY_PUSH(catches, catchCount, catchCap, clause);
  }
  AstNode* finallyBlock = NULL;
  if (match(p, TK_FINALLY)) {
    finallyBlock = parseBlock(p);
    if (finallyBlock == NULL) {
      free(catches);
      return NULL;
    }
  }
  if (catchCount == 0 && finallyBlock == NULL) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'catch' or 'finally' after try block");
    free(catches);
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_TRY, line, col);
  node->as.tryStmt.tryBlock = tryBlock;
  node->as.tryStmt.catchCount = catchCount;
  node->as.tryStmt.catches = astArenaAlloc(p->arena, sizeof(AstCatchClause) * (size_t)catchCount);
  if (catchCount > 0) {
    memcpy(node->as.tryStmt.catches, catches, sizeof(AstCatchClause) * (size_t)catchCount);
  }
  free(catches);
  node->as.tryStmt.finallyBlock = finallyBlock;
  return node;
}

// defer（04 §7）：仅函数体内、操作数为调用（funcDepth 由 parseFuncBody 维护）
static AstNode* parseDeferStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // defer
  if (p->funcDepth == 0) {
    errTok(p, &p->cur, MSE_SYN_DEFER_TOPLEVEL, "'defer' is only allowed inside a function");
    return NULL;
  }
  AstNode* call = parseExpr(p);
  if (call == NULL) {
    return NULL;
  }
  if (call->kind != AST_CALL) {
    errPos(p, line, col, 5, MSE_SYN_DEFER_CALL, "'defer' requires a call expression");
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_DEFER, line, col);
  node->as.exprStmt.expr = call;
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// select（04 §8、08 §3）：接收赋值 / 丢弃接收 / 发送 / default，case 体为块或单语句
// select 非保留字（01 §2 共 37 关键字）：语句位 ID "select" 后随 { 才特判（type 先例，任务 §6）
// 块内结构性错误复用 skipSwitchClose（花括号配平跳过，T006 §2 先例）
static AstNode* parseSelectStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // select
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' after 'select'");
    return NULL;
  }
  AstNode** cases = NULL;
  int32_t count = 0, cap = 0;
  while (check(p, TK_CASE) || check(p, TK_DEFAULT)) {
    int32_t caseLine = p->cur.line, caseCol = p->cur.col;
    bool isDefault = check(p, TK_DEFAULT);
    advance(p);
    ms_u8 form = AST_SEL_DEFAULT;
    AstNode* target = NULL;
    AstNode* chan = NULL;
    AstNode* value = NULL;
    if (!isDefault) {
      AstNode* first = parseExpr(p);
      if (first == NULL) {
        free(cases);
        skipSwitchClose(p);
        p->panic = false;  // 已消费至配对 }：免 synchronize 吞同行后续语句
        return NULL;
      }
      if (first->kind == AST_RECV) {  // case <-ch:（parsePrefix13 于 <- 直接成 RECV）
        form = AST_SEL_RECV;
        chan = first;
      } else if (check(p, TK_LARROW)) {  // case ch <- v:
        form = AST_SEL_SEND;
        chan = first;
        advance(p);
        value = parseExpr(p);
        if (value == NULL) {
          free(cases);
          skipSwitchClose(p);
          p->panic = false;
          return NULL;
        }
      } else if (match(p, TK_EQ)) {  // case v = <-ch:
        form = AST_SEL_RECV;
        if (!isAssignTarget(first)) {
          // 就地恢复：仅报此一条，target 留空、子句照常完成（不 panic、不级联）
          diagPos(p, first->line, first->col, 1, MSE_SYN_BAD_TARGET, "invalid receive target");
        } else {
          target = first;
        }
        if (!check(p, TK_LARROW)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '<-' in select receive case");
          free(cases);
          skipSwitchClose(p);
          p->panic = false;
          return NULL;
        }
        chan = parseExpr(p);  // 以 <- 起始 → AST_RECV
        if (chan == NULL) {
          free(cases);
          skipSwitchClose(p);
          p->panic = false;
          return NULL;
        }
      } else {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '=' or '<-' in select case");
        free(cases);
        skipSwitchClose(p);
        p->panic = false;
        return NULL;
      }
    }
    if (!match(p, TK_COLON)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected ':' after select case");
      free(cases);
      skipSwitchClose(p);
      p->panic = false;
      return NULL;
    }
    AstNode* body = parseStatement(p);  // case 体为块或单语句（08 §3）
    if (body == NULL) {
      free(cases);
      skipSwitchClose(p);
      p->panic = false;
      return NULL;
    }
    AstNode* caseNode = astNewNode(p->arena, AST_SELECT_CASE, caseLine, caseCol);
    caseNode->as.selectCase.form = form;
    caseNode->as.selectCase.target = target;
    caseNode->as.selectCase.chan = chan;
    caseNode->as.selectCase.value = value;
    caseNode->as.selectCase.body = body;
    MS_ARRAY_PUSH(cases, count, cap, caseNode);
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'case' or '}' in select body");
    free(cases);
    return NULL;  // 此处 cur 非 }：panic 路径交由 synchronize
  }
  AstNode* node = astNewNode(p->arena, AST_SELECT, line, col);
  node->as.selectStmt.count = count;
  node->as.selectStmt.cases = astNewNodeArray(p->arena, count);
  if (count > 0) {
    memcpy(node->as.selectStmt.cases, cases, sizeof(AstNode*) * (size_t)count);
  }
  free(cases);
  return node;
}

// import 路径：普通/原始字符串字面量；插值与多段拒绝（09 §2 动态加载走 importlib）
static bool parseImportPath(Parser* p, const char** outStart, int32_t* outLen) {
  if (check(p, TK_RAW_STR)) {
    *outStart = p->cur.start + (p->cur.len >= 2 ? 1 : 0);
    *outLen = p->cur.len >= 2 ? p->cur.len - 2 : 0;
    advance(p);
    return true;
  }
  if (!check(p, TK_STRING)) {
    errTok(p, &p->cur, MSE_SYN_IMPORT_PATH, "import path must be a string literal");
    return false;
  }
  AstNode* lit = parseStringLit(p);
  if (lit == NULL) {
    return false;
  }
  if (lit->as.strLit.count != 1 || lit->as.strLit.segs[0].expr != NULL) {
    errPos(p, lit->line, lit->col, 1, MSE_SYN_IMPORT_PATH, "import path must be a plain string literal");
    return false;
  }
  *outStart = lit->as.strLit.segs[0].start;
  *outLen = lit->as.strLit.segs[0].len;
  return true;
}

// import 三式（04 §9、09 §2）：import "m" [as x] / from "m" import {…|*}
static AstNode* parseImportStmt(Parser* p) {
  int32_t line = p->cur.line, col = p->cur.col;
  bool from = check(p, TK_FROM);
  advance(p);  // import / from
  const char* path = NULL;
  int32_t pathLen = 0;
  if (!parseImportPath(p, &path, &pathLen)) {
    return NULL;
  }
  AstNode* node = astNewNode(p->arena, AST_IMPORT, line, col);
  node->as.importStmt.path = path;
  node->as.importStmt.pathLen = pathLen;
  if (from) {
    if (!match(p, TK_IMPORT)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'import' after module path");
      return NULL;
    }
    if (match(p, TK_STAR)) {
      node->as.importStmt.star = true;
    } else {
      if (!match(p, TK_LBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' or '*' in from-import");
        return NULL;
      }
      AstImportName* names = NULL;
      int32_t nameCount = 0, nameCap = 0;
      while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected imported name");
          free(names);
          return NULL;
        }
        AstImportName name = {p->cur.start, p->cur.len, NULL, 0};
        advance(p);
        if (match(p, TK_AS)) {
          if (!check(p, TK_ID)) {
            errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected alias after 'as'");
            free(names);
            return NULL;
          }
          name.asName = p->cur.start;
          name.asLen = p->cur.len;
          advance(p);
        }
        MS_ARRAY_PUSH(names, nameCount, nameCap, name);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
      if (!match(p, TK_RBRACE)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}' in from-import");
        free(names);
        return NULL;
      }
      node->as.importStmt.nameCount = nameCount;
      node->as.importStmt.names = astArenaAlloc(p->arena, sizeof(AstImportName) * (size_t)nameCount);
      if (nameCount > 0) {
        memcpy(node->as.importStmt.names, names, sizeof(AstImportName) * (size_t)nameCount);
      }
      free(names);
    }
  } else if (match(p, TK_AS)) {
    if (!check(p, TK_ID)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected alias after 'as'");
      return NULL;
    }
    node->as.importStmt.alias = p->cur.start;
    node->as.importStmt.aliasLen = p->cur.len;
    advance(p);
  }
  if (!endSimpleStmt(p)) {
    return NULL;
  }
  return node;
}

// 类体字段初始化 this 检查（06 §3）：表达式树递归（函数字面体属自身作用域，不检查）
static bool exprHasThis(const AstNode* n) {
  if (n == NULL) {
    return false;
  }
  switch (n->kind) {
    case AST_THIS:
      return true;
    case AST_UNARY:
    case AST_RECV:
    case AST_AWAIT:
    case AST_GO:
      return exprHasThis(n->as.unary.operand);
    case AST_BINARY:
    case AST_LOGIC:
      return exprHasThis(n->as.binary.lhs) || exprHasThis(n->as.binary.rhs);
    case AST_TERNARY:
      return exprHasThis(n->as.ternary.cond) || exprHasThis(n->as.ternary.thenExpr) ||
             exprHasThis(n->as.ternary.elseExpr);
    case AST_CALL:
      if (exprHasThis(n->as.call.callee)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.call.count; i++) {
        if (exprHasThis(n->as.call.args[i])) {
          return true;
        }
      }
      return false;
    case AST_INDEX:
      return exprHasThis(n->as.indexExpr.obj) || exprHasThis(n->as.indexExpr.index);
    case AST_MEMBER:
      return exprHasThis(n->as.memberExpr.obj);
    case AST_SLICE:
      return exprHasThis(n->as.slice.obj) || exprHasThis(n->as.slice.start) || exprHasThis(n->as.slice.end) ||
             exprHasThis(n->as.slice.step);
    case AST_LIST:
    case AST_TUPLE:
    case AST_SET:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        if (exprHasThis(n->as.list.items[i])) {
          return true;
        }
      }
      return false;
    case AST_MAP:
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        if (exprHasThis(n->as.mapLit.keys[i]) || exprHasThis(n->as.mapLit.values[i])) {
          return true;
        }
      }
      return false;
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      if (exprHasThis(n->as.compr.key) || exprHasThis(n->as.compr.value)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.compr.forCount; i++) {
        if (exprHasThis(n->as.compr.fors[i].iter)) {
          return true;
        }
      }
      for (int32_t i = 0; i < n->as.compr.condCount; i++) {
        if (exprHasThis(n->as.compr.conds[i])) {
          return true;
        }
      }
      return false;
    case AST_STRING:
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        if (exprHasThis(n->as.strLit.segs[i].expr)) {
          return true;
        }
      }
      return false;
    default:  // 字面量/IDENT/FUNC_EXPR 等叶子或独立作用域子树
      return false;
  }
}

// 类体 static 前缀（06 §4）：static 非保留字，后随 func/var/const 方可识别
static bool isStaticPrefix(Parser* p) {
  return check(p, TK_ID) && p->cur.len == 6 && memcmp(p->cur.start, "static", 6) == 0 &&
         (peekType(p) == TK_FUNC || peekType(p) == TK_VAR || peekType(p) == TK_CONST);
}

// class/trait 声明（06 §1 §2 §4）：继承清单 + 类体成员
// 类体内结构性错误：跳至配对 } 一并消费（synchronize 会停在本体 } 误报 unmatched，switch 先例）
static AstNode* parseClassDecl(Parser* p, bool isTrait) {
  int32_t line = p->cur.line, col = p->cur.col;
  advance(p);  // class / trait
  if (!check(p, TK_ID)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected class name");
    return NULL;
  }
  const char* name = p->cur.start;
  int32_t nameLen = p->cur.len;
  int32_t nameLine = p->cur.line, nameCol = p->cur.col;
  advance(p);
  if (scopeDeclare(p, name, nameLen)) {  // 类名登记；方法名不查重（05 §1 覆盖规则）
    errPos(p, nameLine, nameCol, nameLen, MSE_SYN_DUP_DECL, "duplicate declaration in the same scope");
    return NULL;
  }
  // fail 标签读到的变量一律先声明清零（goto 不可跨过未初始化声明）
  AstNode** bases = NULL;
  int32_t baseCount = 0, baseCap = 0;
  AstNode** members = NULL;
  int32_t memberCount = 0, memberCap = 0;
  bool inBody = false;
  if (match(p, TK_COLON)) {  // 继承清单：首基类，余 mixin（06 §2）
    do {
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected base class name");
        goto fail;
      }
      AstNode* base = astNewNode(p->arena, AST_IDENT, p->cur.line, p->cur.col);
      base->as.ident.name = p->cur.start;
      base->as.ident.nameLen = p->cur.len;
      advance(p);
      MS_ARRAY_PUSH(bases, baseCount, baseCap, base);
    } while (match(p, TK_COMMA));
  }
  if (!match(p, TK_LBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '{' in class body");
    goto fail;
  }
  inBody = true;  // 类体 { 已消费：fail 路径需配平跳过
  while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) {
    if (match(p, TK_SEMICOLON)) {
      continue;
    }
    bool hasStatic = false;
    if (isStaticPrefix(p)) {
      hasStatic = true;
      advance(p);
    }
    if (check(p, TK_VAR)) {  // 字段声明：var a, b = 1（每名一节点，06 §1）
      advance(p);
      for (;;) {
        if (!check(p, TK_ID)) {
          errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected field name");
          goto fail;
        }
        int32_t fieldLine = p->cur.line, fieldCol = p->cur.col;
        const char* fieldName = p->cur.start;
        int32_t fieldLen = p->cur.len;
        advance(p);
        AstNode* field = astNewNode(p->arena, AST_VAR_DECL, fieldLine, fieldCol);
        field->as.varDecl.name = fieldName;
        field->as.varDecl.nameLen = fieldLen;
        field->as.varDecl.flags = hasStatic ? AST_VAR_STATIC : 0;
        if (match(p, TK_EQ)) {
          field->as.varDecl.init = parseExpr(p);  // 单表达式：逗号分隔字段而非元组元素（06 §1）
          if (field->as.varDecl.init == NULL) {
            goto fail;
          }
          if (exprHasThis(field->as.varDecl.init)) {  // 06 §3
            errPos(p, fieldLine, fieldCol, fieldLen, MSE_SYN_CLASS_THIS, "'this' is not allowed in field initializer");
            goto fail;
          }
        }
        MS_ARRAY_PUSH(members, memberCount, memberCap, field);
        if (!match(p, TK_COMMA)) {
          break;
        }
      }
    } else if (check(p, TK_CONST)) {  // 类常量（06 §4）
      advance(p);
      if (!check(p, TK_ID)) {
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected constant name");
        goto fail;
      }
      int32_t fieldLine = p->cur.line, fieldCol = p->cur.col;
      const char* fieldName = p->cur.start;
      int32_t fieldLen = p->cur.len;
      advance(p);
      AstNode* field = astNewNode(p->arena, AST_VAR_DECL, fieldLine, fieldCol);
      field->as.varDecl.name = fieldName;
      field->as.varDecl.nameLen = fieldLen;
      field->as.varDecl.flags = AST_VAR_CONST | (hasStatic ? AST_VAR_STATIC : 0);
      if (match(p, TK_EQ)) {
        field->as.varDecl.init = parseExpr(p);  // 同上：const 初始化亦为单表达式
        if (field->as.varDecl.init == NULL) {
          goto fail;
        }
        if (exprHasThis(field->as.varDecl.init)) {  // 06 §3
          errPos(p, fieldLine, fieldCol, fieldLen, MSE_SYN_CLASS_THIS, "'this' is not allowed in field initializer");
          goto fail;
        }
      }
      MS_ARRAY_PUSH(members, memberCount, memberCap, field);
    } else if (check(p, TK_FUNC)) {  // 方法（含 dunder，06 §5）
      ms_u8 flags = hasStatic ? AST_FUNC_STATIC : 0;
      AstNode* method = parseFuncDeclStmt(p, flags, p->cur.line, p->cur.col);
      if (method == NULL) {
        goto fail;
      }
      if (method->kind != AST_FUNC_DECL) {  // 无名 func 表达式混入类体：成员契约仅 VAR_DECL/FUNC_DECL
        errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected method name in class body");
        goto fail;  // inBody：skipSwitchClose 配平消费 + panic=false，免 synchronize 误报
      }
      MS_ARRAY_PUSH(members, memberCount, memberCap, method);
    } else {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'var', 'const' or 'func' in class body");
      goto fail;
    }
  }
  if (!match(p, TK_RBRACE)) {
    errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected '}' in class body");
    goto fail;
  }
  AstNode* node = astNewNode(p->arena, AST_CLASS_DECL, line, col);
  node->as.classDecl.name = name;
  node->as.classDecl.nameLen = nameLen;
  node->as.classDecl.isTrait = isTrait;
  node->as.classDecl.baseCount = baseCount;
  node->as.classDecl.bases = astNewNodeArray(p->arena, baseCount);
  if (baseCount > 0) {
    memcpy(node->as.classDecl.bases, bases, sizeof(AstNode*) * (size_t)baseCount);
  }
  node->as.classDecl.memberCount = memberCount;
  node->as.classDecl.members = astNewNodeArray(p->arena, memberCount);
  if (memberCount > 0) {
    memcpy(node->as.classDecl.members, members, sizeof(AstNode*) * (size_t)memberCount);
  }
  free(bases);
  free(members);
  return node;
fail:
  free(bases);
  free(members);
  if (inBody) {
    skipSwitchClose(p);
    p->panic = false;                // 已消费至配对 }：免 synchronize 吞本体 } 误报 unmatched
  } else if (match(p, TK_LBRACE)) {  // 基类清单错误后紧随的类体 {：同样配平消费
    skipSwitchClose(p);
    p->panic = false;
  }
  return NULL;
}

static AstNode* parseStatement(Parser* p) {
  if (check(p, TK_LBRACE)) {
    return parseBlock(p);  // 块语句（自带边界，无需 ASI）
  }
  if (check(p, TK_VAR) || check(p, TK_CONST)) {
    ms_u8 flags = check(p, TK_CONST) ? AST_VAR_CONST : 0;
    AstNode* node = parseVarDecl(p, flags);
    if (node != NULL && !endSimpleStmt(p)) {
      return NULL;
    }
    return node;
  }
  if (check(p, TK_IF)) {
    return parseIfStmt(p);
  }
  if (check(p, TK_RETURN)) {
    return parseReturnStmt(p);
  }
  if (check(p, TK_FOR)) {
    return parseForStmt(p);
  }
  if (check(p, TK_WHILE)) {
    return parseWhileStmt(p);
  }
  if (check(p, TK_SWITCH)) {
    return parseSwitchStmt(p);
  }
  if (check(p, TK_TRY)) {
    return parseTryStmt(p);
  }
  if (check(p, TK_DEFER)) {
    return parseDeferStmt(p);
  }
  if (check(p, TK_IMPORT) || check(p, TK_FROM)) {
    return parseImportStmt(p);
  }
  if (check(p, TK_CLASS)) {
    return parseClassDecl(p, false);
  }
  if (check(p, TK_TRAIT)) {
    return parseClassDecl(p, true);
  }
  if (check(p, TK_ID) && p->cur.len == 6 && memcmp(p->cur.start, "select", 6) == 0 && peekType(p) == TK_LBRACE) {
    // select 非保留字（01 §2）：仅语句位 "select {" 特判，其余位置仍是普通标识符
    return parseSelectStmt(p);
  }
  if (check(p, TK_BREAK) || check(p, TK_CONTINUE)) {  // 合法性检查归 T009（04 §6）
    AstKind kind = check(p, TK_BREAK) ? AST_BREAK : AST_CONTINUE;
    int32_t line = p->cur.line, col = p->cur.col;
    advance(p);
    if (!endSimpleStmt(p)) {
      return NULL;
    }
    return astNewNode(p->arena, kind, line, col);
  }
  if (check(p, TK_THROW)) {
    return parseValueJumpStmt(p, AST_THROW);
  }
  if (check(p, TK_YIELD)) {
    return parseValueJumpStmt(p, AST_YIELD);
  }
  if (check(p, TK_FUNC)) {
    return parseFuncDeclStmt(p, 0, p->cur.line, p->cur.col);
  }
  if (check(p, TK_ASYNC)) {
    int32_t line = p->cur.line, col = p->cur.col;
    advance(p);
    if (!check(p, TK_FUNC)) {
      errTok(p, &p->cur, MSE_SYN_EXPECTED, "expected 'func' after 'async'");
      return NULL;
    }
    return parseFuncDeclStmt(p, AST_FUNC_ASYNC, line, col);
  }
  return parseSimpleStmt(p);
}

// ---------------- Parser API ----------------

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena, MsDiagSink* diags) {
  scannerInit(&p->scan, src, len, file, diags);
  p->arena = arena;
  p->diags = diags;
  p->scope = NULL;
  p->panic = false;
  p->noIn = false;
  p->loopHeader = false;
  p->funcDepth = 0;
  scannerNext(&p->scan, &p->cur);  // 首 token 直接拉取（不经 advance：避免读未初始化 prevLine）
  p->prevLine = 1;
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
