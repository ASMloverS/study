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
  AST_YIELD,  // as.exprStmt 复用（value，05 §4 生成器）
  AST_DEFER,
  AST_SEND,
  AST_SELECT,
  AST_SELECT_CASE,
  AST_IMPORT,
  AST_FUNC_DECL,
  AST_CLASS_DECL,
  AST_TRY,  // try/catch/finally：as.tryStmt
  // 表达式
  AST_IDENT,   // as.ident
  AST_THIS,    // this 表达式（06 §3）：无载荷
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
  AST_VAR_SHORT = 1,   // x := e
  AST_VAR_CONST = 2,   // const x = e
  AST_VAR_STATIC = 4,  // 类体 static var/const（06 §4）
};

// 函数声明/表达式形态标志（funcDecl/funcExpr.flags 位域）
enum {
  AST_FUNC_GENERATOR = 1,  // func*（05 §4）
  AST_FUNC_ASYNC = 2,      // async func（05 §8）
  AST_FUNC_STATIC = 4,     // 类体 static func（06 §4，Task 10 用）
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

// for 形态（forStmt.form，04 §4）
enum {
  AST_FOR_TRI = 1,   // for init; cond; post（段均可省略）
  AST_FOR_IN = 2,    // for x in xs / for k, v in xs
  AST_FOR_COND = 3,  // for cond
  AST_FOR_INF = 4,   // for {}
};

// select 分支形态（selectCase.form，04 §8）
enum {
  AST_SEL_RECV = 1,     // case v = <-ch: / case <-ch:
  AST_SEL_SEND = 2,     // case ch <- v:
  AST_SEL_DEFAULT = 3,  // default:
};

// 字符串段：expr != NULL 为插值段，否则为字面段（借用源文本，转义原文保留）
typedef struct AstStrSeg {
  AstNode* expr;
  const char* start;
  int32_t len;
} AstStrSeg;

// catch 子句（tryStmt.catches 元素，07 §1）：types 空 = 裸绑定兜底
typedef struct AstCatchClause {
  AstNode** types;  // AST_IDENT 类型清单
  int32_t typeCount;
  const char* bindName;  // 可选绑定名
  int32_t bindLen;
  AstNode* block;  // AST_BLOCK
} AstCatchClause;

// from-import 具名项（importStmt.names 元素，09 §2）
typedef struct AstImportName {
  const char* name;
  int32_t nameLen;
  const char* asName;  // b as c 本地名；NULL = name
  int32_t asLen;
} AstImportName;

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
      AstNode* chan;
      AstNode* value;
    } sendStmt;  // AST_SEND：ch <- v
    struct {
      ms_u8 form;       // AST_FOR_*
      AstNode* init;    // TRI：简单语句（可 NULL）
      AstNode* cond;    // TRI/COND
      AstNode* post;    // TRI（i++/i-- 糖已展开为复合赋值）
      AstNode* target;  // IN：IDENT 或 TUPLE(IDENT)；可 NULL（BAD_TARGET 已诊断，T009 需容忍）
      AstNode* iter;    // IN
      AstNode* body;    // AST_BLOCK
    } forStmt;
    struct {
      AstNode* cond;
      AstNode* body;
    } whileStmt;
    struct {
      bool isType;       // 值式 false / 类型式 true（04 §5）
      AstNode* subject;  // 值式：判别表达式；类型式：type(v) 的 v
      AstNode** cases;   // AST_SWITCH_CASE 列表
      int32_t count;
    } switchStmt;
    struct {
      bool isDefault;
      AstNode** values;  // 值式：判别值；类型式：类型名 AST_IDENT（04 §5.2）
      int32_t valueCount;
      const char* bindName;  // 类型式 as 绑定；NULL 无
      int32_t bindLen;
      AstNode* body;  // 块或单语句（08 §3 同则）
    } switchCase;
    struct {
      AstNode* tryBlock;
      AstCatchClause* catches;
      int32_t catchCount;
      AstNode* finallyBlock;  // 可 NULL
    } tryStmt;
    struct {
      AstNode** cases;  // AST_SELECT_CASE 列表
      int32_t count;
    } selectStmt;
    struct {
      ms_u8 form;       // AST_SEL_*
      AstNode* target;  // RECV：赋值目标（NULL = 丢弃接收）
      AstNode* chan;    // RECV：AST_RECV 源；SEND：目标 channel
      AstNode* value;   // SEND：发送值
      AstNode* body;    // 块或单语句（08 §3）
    } selectCase;
    struct {
      const char* path;  // 模块路径（字面量原文，引号/反引号已剥离）
      int32_t pathLen;
      const char* alias;  // import "m" as x；无别名 NULL
      int32_t aliasLen;
      AstImportName* names;  // from-import 具名清单；非 from 形式 NULL
      int32_t nameCount;
      bool star;  // from "m" import *
    } importStmt;
    struct {
      const char* name;
      int32_t nameLen;
      bool isTrait;     // trait 声明（06 §2）
      AstNode** bases;  // AST_IDENT 清单（首基类，余 mixin）
      int32_t baseCount;
      AstNode** members;  // AST_VAR_DECL / AST_FUNC_DECL
      int32_t memberCount;
    } classDecl;
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
      ms_u8 flags;  // AST_FUNC_*
      AstParam* params;
      int32_t paramCount;
      AstNode* body;      // AST_BLOCK；表达式体 NULL
      AstNode* exprBody;  // -> expr（05 §1）
    } funcExpr;
    struct {
      const char* name;
      int32_t nameLen;
      ms_u8 flags;  // AST_FUNC_*
      AstParam* params;
      int32_t paramCount;
      AstNode* body;      // AST_BLOCK；表达式体 NULL
      AstNode* exprBody;  // -> expr
    } funcDecl;           // AST_FUNC_DECL
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
