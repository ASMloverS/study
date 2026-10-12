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
  Scanner scan;      // 内嵌：逐 token 拉取（20-architecture §1）
  MsToken cur;       // 当前 lookahead（单 token，不可回退）
  int32_t prevLine;  // cur 前一 token 的行号（块内无 ASI 分号时的换行终止判定）
  AstArena* arena;
  MsDiagSink* diags;   // 可为 NULL（免诊断场景）
  ParserScope* scope;  // 声明查重作用域链（完整解析在 T009 Binder）
  bool panic;          // 恢复中：屏蔽级联诊断直至同步点
} Parser;

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena, MsDiagSink* diags);
AstNode* parserParseModule(Parser* p);  // 失败也返回部分树（诊断在 sink）

#endif  // INCLUDE_MS_PARSER_PARSER_H_
