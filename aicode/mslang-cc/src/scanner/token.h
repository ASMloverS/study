// Scanner/Parser/Binder 共用前端基座：Token、关键字、诊断（T003）
// 结构一律借用指针（源缓冲由调用方持有），前端零分配；col/len 按 rune 计（1-based）
// 错误码段：01 词法 MSE0xxx、03/04 语法 MSE1xxx、02 类型/运行时 MSE2xxx，后续任务沿用
#ifndef INCLUDE_MS_SCANNER_TOKEN_H_
#define INCLUDE_MS_SCANNER_TOKEN_H_

#include <stddef.h>
#include <stdint.h>

// 37 保留字单列（01-lexical.md §2）+ 标识符/字面量 + 全部运算符标点（01 §4）+ EOF
// X-macro 单一事实源：枚举值与 token.c 名表共用（新增类型只改此清单）
#define MS_TOKEN_LIST                       \
  X(EOF)                                    \
  X(ID)                                     \
  X(INT)                                    \
  X(FLOAT)                                  \
  X(BIGINT)                                 \
  X(STRING)                                 \
  X(RUNE)                                   \
  X(STR_PART)                               \
  X(INTERP_BEGIN)                           \
  X(INTERP_END)                             \
  X(RAW_STR)                                \
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

// 类型名（枚举名去 TK_，如 "ID"/"STAR_STAR_EQ"）；非法值返回 "?"
const char* msTokenTypeName(MsTokenType type);

#endif
