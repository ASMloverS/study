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
  AST_FOR,
  AST_WHILE,
  AST_SWITCH,
  AST_SWITCH_CASE,
  AST_BREAK,
  AST_CONTINUE,
  AST_RETURN,  // as.exprStmt 复用（value）
  AST_THROW,
  AST_DEFER,
  AST_SEND,
  AST_SELECT,
  AST_SELECT_CASE,
  AST_IMPORT,
  AST_FUNC_DECL,
  AST_CLASS_DECL,
  // 表达式
  AST_IDENT,   // as.ident
  AST_NIL,     // 无载荷
  AST_BOOL,    // as.boolLit
  AST_INT,     // as.intLit
  AST_FLOAT,   // as.floatLit
  AST_BIGINT,  // as.bigintLit（借用源文本，显式 n 后缀已剥离）
  AST_STRING,  // as.strLit（普通串段列表 / 原始串单段）
  AST_RUNE,    // as.runeLit
  AST_TUPLE,   // 逗号列表（元组字面量/元组赋值目标）：as.tuple
  AST_BINARY,  // as.binary（逻辑短路 AST_LOGIC 复用 binary 载荷）
  AST_INDEX,   // as.indexExpr（后缀 [e]，赋值目标用）
  AST_MEMBER,  // as.memberExpr（后缀 .name）
  // 表达式扩展：T007 已实现（AST_RECV/AST_AWAIT/AST_GO 为 T008+ 预留）
  AST_UNARY,
  AST_LOGIC,
  AST_TERNARY,
  AST_CALL,
  AST_SLICE,
  AST_LIST,  // as.list 复用
  AST_MAP,
  AST_SET,  // as.list 复用
  AST_COMPREHENSION,
  AST_GEN_EXPR,
  AST_FUNC_EXPR,
  AST_RECV,
  AST_AWAIT,
  AST_GO,
} AstKind;

// 二元运算符：复合赋值展开与 AST_BINARY 共用；token → op 映射见 parser.c
typedef enum AstBinOp {
  AST_OP_NONE = 0,
  AST_OP_ADD,
  AST_OP_SUB,
  AST_OP_MUL,
  AST_OP_DIV,
  AST_OP_MOD,
  AST_OP_POW,
  AST_OP_BIT_AND,
  AST_OP_BIT_OR,
  AST_OP_BIT_XOR,
  AST_OP_SHL,
  AST_OP_SHR,
  AST_OP_EQ,
  AST_OP_NE,
  AST_OP_LT,
  AST_OP_LE,
  AST_OP_GT,
  AST_OP_GE,
  AST_OP_IS,  // 同一性（03 §5，不可重载）
  AST_OP_IN,  // 成员包含（03 §5，走 __contains__）
  AST_OP_AND,
  AST_OP_OR,  // 短路逻辑（T007 AST_LOGIC 载荷）
} AstBinOp;

// var 声明形态标志（as.varDecl.flags 位域）
enum {
  AST_VAR_SHORT = 1,  // x := e
  AST_VAR_CONST = 2,  // const x = e
};

typedef struct AstNode AstNode;

// 一元运算符（03 优先级 12）
typedef enum AstUnaryOp {
  AST_UNOP_NEG = 1,  // -
  AST_UNOP_NOT,      // ! / not
  AST_UNOP_INV,      // ~
} AstUnaryOp;

// 推导式/生成器 for 子句：target 为 AST_IDENT 或 AST_TUPLE（多变量）
typedef struct AstComprFor {
  AstNode* target;
  AstNode* iter;
} AstComprFor;

// 函数参数（T007 仅填 name；defaultExpr/rest 形态 T008 复用）
typedef struct AstParam {
  const char* name;
  int32_t nameLen;
  AstNode* defaultExpr;
  bool rest;
} AstParam;

// 推导式子类型（as.compr.compKind）
enum {
  AST_COMP_LIST = 1,
  AST_COMP_MAP = 2,
  AST_COMP_SET = 3,
};

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
    struct {
      AstNode** items;
      int32_t count;
    } list;  // MODULE/BLOCK 语句序列
    struct {
      const char* name;
      int32_t nameLen;
      ms_u8 flags;
      AstNode* init;
    } varDecl;
    struct {
      AstNode* target;
      AstNode* value;
      AstBinOp op;
    } assign;  // op==NONE 普通赋值
    struct {
      AstNode** targets;
      int32_t count;
      int32_t restIndex;
      AstNode* value;
    } destructure;
    struct {
      AstNode* expr;
    } exprStmt;
    struct {
      AstNode* cond;
      AstNode* thenBlock;
      AstNode* elseNode;
    } ifStmt;
    struct {
      const char* name;
      int32_t nameLen;
    } ident;
    struct {
      bool b;
    } boolLit;
    struct {
      ms_i64 v;
    } intLit;
    struct {
      double v;
    } floatLit;
    struct {
      const char* start;
      int32_t len;
    } bigintLit;
    struct {
      bool raw;
      AstStrSeg* segs;
      int32_t count;
    } strLit;
    struct {
      uint32_t cp;
    } runeLit;
    struct {
      AstNode** items;
      int32_t count;
    } tuple;
    struct {
      AstNode* lhs;
      AstNode* rhs;
      AstBinOp op;
    } binary;
    struct {
      AstNode* obj;
      AstNode* index;
    } indexExpr;
    struct {
      AstNode* obj;
      const char* name;
      int32_t nameLen;
    } memberExpr;
    struct {
      AstNode* operand;
      AstUnaryOp op;
    } unary;
    struct {
      AstNode* cond;
      AstNode* thenExpr;
      AstNode* elseExpr;
    } ternary;  // AST_TERNARY（右结合，03 §7）
    struct {
      AstNode* callee;
      AstNode** args;
      bool* spread;  // 与 args 平行：位置 i 为 * 展开实参
      int32_t count;
    } call;
    struct {
      AstNode* obj;
      AstNode* start;  // 三者均可 NULL（端点/步进省略）
      AstNode* end;
      AstNode* step;
    } slice;
    struct {
      AstNode** keys;
      AstNode** values;
      int32_t count;
    } mapLit;  // AST_MAP
    struct {
      ms_u8 compKind;  // AST_COMP_*；AST_GEN_EXPR 恒 0
      AstNode* key;    // map 推导式键；其余 NULL
      AstNode* value;  // list/set 元素或 map 值
      AstComprFor* fors;
      int32_t forCount;
      AstNode** conds;
      int32_t condCount;
    } compr;  // AST_COMPREHENSION / AST_GEN_EXPR 共用
    struct {
      AstParam* params;
      int32_t paramCount;
      AstNode* body;  // AST_BLOCK
    } funcExpr;
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
