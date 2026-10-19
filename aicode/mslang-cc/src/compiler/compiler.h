// Compiler（T010）：标注 AST → 栈式字节码（21 §2/§3）
// 本任务启用基础路径：字面量/变量/运算/比较/短路/三元/函数桩；其余构造发 MSE_COMPILE_UNSUPPORTED 尽力继续
#ifndef INCLUDE_MS_COMPILER_COMPILER_H_
#define INCLUDE_MS_COMPILER_COMPILER_H_

#include <stdio.h>

#include "compiler/chunk.h"
#include "parser/ast.h"
#include "scanner/scanner.h"

// 编译错误码（MSE1xxx 段续 binder 1021 起）
typedef enum MseCompileCode {
  MSE_COMPILE_UNSUPPORTED = 1022,  // 构造未启用（按任务分组逐步放开）
} MseCompileCode;

// 编译模块：标注 AST → 主 chunk（函数体递归为常量池 CONST_PROTO）
// 尽力编译（与前端风格一致）；返回 false = sink 存在 ERROR 级诊断
bool compilerCompile(AstNode* module, Chunk* out, const char* file, MsDiagSink* diags);

// 反汇编（21 §4 样式）：chunk 树 → out；实现见 disasm.c
void compilerDisasm(const Chunk* c, FILE* out);

#endif  // INCLUDE_MS_COMPILER_COMPILER_H_
