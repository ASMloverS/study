// Scanner 主循环（T004）：标识符/数字/运算符/ASI；字符串/rune/插值归 T005
// 前端零分配：一律借用源缓冲指针；源须已 LF 归一化（读取层完成，01-lexical.md）
#ifndef INCLUDE_MS_SCANNER_SCANNER_H_
#define INCLUDE_MS_SCANNER_SCANNER_H_

#include <stdbool.h>
#include <stdint.h>

#include "scanner/token.h"

// 词法错误码（MSE0xxx 段，01-lexical.md；诊断 code 字段取枚举值）
typedef enum MseLexCode {
  MSE_INT_TRAILING = 1,          // 数字后紧跟标识符字符（123abc / 0x1Fg）
  MSE_FLOAT_EXPONENT = 2,        // 指数缺数字（1e / 1e+）
  MSE_INT_OVERFLOW = 3,          // 整数超 int64，升 BIGINT 占位（T016 精确解析）
  MSE_UNDERSCORE = 4,            // 下划线分组错位（1_ / 1__0）
  MSE_FLOAT_TRAILING_DOT = 5,    // 5. 缺小数位（. 后非数字且非成员访问）
  MSE_FLOAT_LEADING_DOT = 6,     // .5 缺整数部
  MSE_UNEXPECTED_CHAR = 7,       // 非法字符（含 " ' `；字符串/rune 归 T005）
  MSE_BLOCK_COMMENT = 8,         // /* */ 块注释（01 §5 非法）
  MSE_BASE_PREFIX = 9,           // 基前缀后缺数字（0x / 0o）
  MSE_DIGIT_BASE = 10,           // 数字超出进制（0o8 / 0b2）
  MSE_ESCAPE = 11,               // 非法/残缺转义（\q、\x4、\u12、\u{}）
  MSE_ESCAPE_VALUE = 12,         // \u{} 值超 0x10FFFF 或代理区段
  MSE_STRING_UNTERMINATED = 13,  // 普通串未闭合（裸换行/EOF）或插值域 EOF 未闭合
  MSE_RAW_UNTERMINATED = 14,     // 原始串到 EOF 未闭合
  MSE_RUNE_INVALID = 15,         // rune 空/多字符/未闭合
  MSE_LEX_DEPTH = 16,            // 字符串插值嵌套超 MS_LEX_MAX_DEPTH
} MseLexCode;

// 字符串/插值嵌套域（T005）：普通串内 ${}、插值内再嵌字符串（同规则）
typedef enum LexDomain {
  LEX_TOP,
  LEX_STR,
  LEX_INTERP,
} LexDomain;

#define MS_LEX_MAX_DEPTH 32

typedef struct LexFrame {
  LexDomain domain;
  int32_t brace;                         // INTERP：表达式内未闭合 { 计数（找本域闭合 }）
  int32_t openLine, openCol;             // 域开符号位置（诊断定位）
  int32_t partStart, partLine, partCol;  // STR：当前常量段起点（插值跨行后更新）
} LexFrame;

typedef struct Scanner {
  const char* src;  // 已 LF 归一化的 UTF-8 源（调用方持有）
  int32_t len, pos;
  int32_t line, col;   // 下一未读 rune 的位置（1-based，col 按 rune 计）
  int32_t parenDepth;  // ()[]{} 合计深度，>0 抑制 ASI（01 §7 细则 1）
  const char* file;
  MsDiagSink* diags;
  bool pendingAsi;                    // 上一 token 可结束语句，换行且 depth==0 时回补 ;
  int32_t docLine;                    // 最近一条 /// 文档注释行号（-1 无），供 ms doc（T042）
  LexFrame frames[MS_LEX_MAX_DEPTH];  // 字符串/插值嵌套域栈（T005）
  int32_t depth;
} Scanner;

// diags 由调用方持有（可为 NULL，仅免诊断场景）
void scannerInit(Scanner* s, const char* src, int32_t len, const char* file, MsDiagSink* diags);

// 产出下一 token（含 ASI 合成的 ;）；EOF 时写 TK_EOF 并返回 false
bool scannerNext(Scanner* s, MsToken* out);

// RUNE token 文本解码为码点（01 §3.4；扫描已校验，未知/残缺文本回退 0xFFFD）
uint32_t msRuneLiteralValue(const MsToken* tok);

#endif  // INCLUDE_MS_SCANNER_SCANNER_H_
