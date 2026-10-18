// Binder（T009）：作用域栈与名称解析（local/global/upvalue 分类）、位置合法性、常量折叠
// 遍历产出"标注 AST"：AST_IDENT 引用与 VAR_DECL/FUNC_DECL/CLASS_DECL 声明节点带 bind* 标注，
// 参数按声明序约定槽位 i → slot i，函数体局部变量从 paramCount 起连续编号（T010 直接消费）
#ifndef INCLUDE_MS_BINDER_BINDER_H_
#define INCLUDE_MS_BINDER_BINDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "parser/ast.h"
#include "scanner/scanner.h"

// 语义错误码（MSE1xxx 段续 1017 起；04 §6、05 §8、03 §8）
typedef enum MseBindCode {
  MSE_BIND_TOP_RETURN = 1017,       // 模块顶层 return
  MSE_BIND_LOOP_ESCAPE = 1018,      // 循环外 break/continue
  MSE_BIND_AWAIT_POS = 1019,        // await 仅 async 函数体/模块顶层
  MSE_BIND_DUP_DECL = 1020,         // 同作用域重复声明（参数/导入/解构等 parser 未覆盖处）
  MSE_BIND_YIELD_MISPLACED = 1021,  // async 函数内 yield / 模块顶层 yield
} MseBindCode;

// upvalue 捕获需求记录（T009 仅记录；open/closed 链 T018 消费）
typedef struct BinderCapture {
  AstNode* func;     // 发生捕获的函数节点（FUNC_DECL/FUNC_EXPR/COMPREHENSION/GEN_EXPR）
  const char* name;  // 捕获名（借用源文本）
  int32_t nameLen;
  int32_t slot;   // 定义函数内的槽位
  int32_t depth;  // func 与定义函数间的函数边界数（1 = 直接内层）
} BinderCapture;

// 栈上按值持有（作用域链等内部结构仅 .c 可见，遍历期 malloc/free）
struct Scope;  // 内部作用域结构前向声明（定义在 binder.c）

typedef struct Binder {
  AstArena* arena;
  const char* file;
  MsDiagSink* diags;
  struct Scope* top;  // 当前作用域链顶（opaque）
  BinderCapture* captures;
  int32_t captureCount, captureCap;
} Binder;

void binderInit(Binder* b, AstArena* arena, const char* file, MsDiagSink* diags);
void binderDestroy(Binder* b);
// 标注整棵模块树（错误仍尽力继续）；返回 false = sink 中存在 ERROR 级诊断
bool binderBind(Binder* b, AstNode* module);
const BinderCapture* binderCaptures(const Binder* b, int32_t* outCount);

#endif  // INCLUDE_MS_BINDER_BINDER_H_
