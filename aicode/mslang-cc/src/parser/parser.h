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
  MSE_SYN_UNEXPECTED = 1000,       // 意外 token（含缺表达式）
  MSE_SYN_EXPECTED = 1001,         // 缺少期望的语法成分（消息指明）
  MSE_SYN_SHORT_TOPLEVEL = 1002,   // := 出现在模块顶层（04 §1）
  MSE_SYN_IF_PAREN = 1003,         // if 条件加了括号（04 §3，Go 风格强制）
  MSE_SYN_DUP_DECL = 1004,         // 同作用域重复声明（04 §1）
  MSE_SYN_DESTRUCT_STARS = 1005,   // 解构目标 * 多于一个（04 §2）
  MSE_SYN_BAD_TARGET = 1006,       // 非法赋值目标 / 短声明目标
  MSE_SYN_CHAINED_CMP = 1007,      // 比较运算符不可链式（03 优先级表）
  MSE_SYN_STAR_NO_ASSIGN = 1008,   // * 目标只能出现在解构赋值
  MSE_SYN_GENEXPR_POS = 1009,      // 生成器表达式仅可作调用唯一实参或赋值右值（03 §1）
  MSE_SYN_SPREAD_NOT_LAST = 1010,  // * 展开实参必须是最后一个实参（03 §4）
  MSE_SYN_CATCH_ORDER = 1011,      // catch 裸绑定子句必须最后（07 §1）
  MSE_SYN_PARAM_ORDER = 1012,     // 参数序：普通 → 默认 → 至多一个 *rest（05 §1）
  MSE_SYN_DEFER_TOPLEVEL = 1013,  // defer 仅函数体内合法（04 §7）
  MSE_SYN_DEFER_CALL = 1014,      // defer 操作数必须是调用表达式（04 §7）
  MSE_SYN_IMPORT_PATH = 1015,     // import 路径必须是字符串字面量（09 §2）
  MSE_SYN_CLASS_THIS = 1016,      // 类体内非方法位置 this 非法（06 §3）
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
  bool noIn;           // for 头部解析中：in 不作二元运算符（for-in 判别，04 §4）
  int32_t funcDepth;   // 函数体嵌套深度（defer 合法性，04 §7）
} Parser;

void parserInit(Parser* p, const char* src, int32_t len, const char* file, AstArena* arena, MsDiagSink* diags);
AstNode* parserParseModule(Parser* p);  // 失败也返回部分树（诊断在 sink）

#endif  // INCLUDE_MS_PARSER_PARSER_H_
