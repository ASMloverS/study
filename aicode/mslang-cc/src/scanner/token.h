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
