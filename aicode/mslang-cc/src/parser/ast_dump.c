// AST 规范化文本输出（T009）：节点 + 关键域 + Binder 标注；实现随任务补全
#include <stdio.h>

#include "parser/ast.h"

void astDumpModule(const AstNode* module, FILE* out) {
  (void)module;
  fprintf(out, "(module)\n");
}
