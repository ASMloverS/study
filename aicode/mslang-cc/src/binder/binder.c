// Binder 主体（T009）：作用域栈、名称解析与标注、位置合法性、常量折叠（见 docs/tasks/T009）
#include "binder/binder.h"

#include <stdlib.h>
#include <string.h>

#define MS_BIND_MAX_FUNCS 64  // 解析路径上最多记录的函数边界数（嵌套超深时不再记录捕获）

typedef enum ScopeKind {
  SCOPE_MODULE,  // 模块层：声明一律 global
  SCOPE_FUNC,    // 函数参数域：函数边界（槽位分配器所在）
  SCOPE_BLOCK,   // 块/循环/子句域：声明借用所属函数的槽位分配器
  SCOPE_CLASS,   // 类体：不登记词法绑定，仅作遍历边界（成员为运行时属性）
  SCOPE_COMPR,   // 推导式/生成器表达式：隐式函数边界
} ScopeKind;

typedef struct Sym {
  const char* name;  // 借用源文本
  int32_t len;
  int32_t slot;   // 所属函数帧槽位；模块层 -1
  bool fresh;     // 每轮迭代新鲜绑定（04 §4）
  bool soft;      // 函数名：可被重复声明覆盖（05 §1 后定义覆盖先定义）
} Sym;

typedef struct Scope {
  ScopeKind kind;
  struct Scope* parent;
  Sym* syms;
  int32_t count, cap;
  int32_t nextSlot;     // FUNC/COMPR：槽位分配器（初值 = 参数数）
  struct Scope* owner;  // 最近函数边界 scope；MODULE = 自身
  AstNode* funcNode;    // FUNC/COMPR：对应 AST 节点（捕获记录定位）
  bool isAsync;         // FUNC：async（模块顶层视作 await 合法语境）
  int32_t loopDepth;    // 本作用域起算的循环嵌套（函数边界清零；子作用域继承）
} Scope;

struct Binder;  // 完整定义在 binder.h（栈上持有；top 为前向声明的 Scope*）

static void bindErr(Binder* b, const AstNode* n, int32_t len, uint32_t code, const char* msg) {
  if (b->diags == NULL) {
    return;
  }
  MsDiagnostic diag = {MS_DIAG_ERROR, code, b->file, n->line, n->col, len, msg};
  msDiagAdd(b->diags, &diag);
}

static Scope* pushScope(Binder* b, ScopeKind kind) {
  Scope* s = malloc(sizeof(Scope));
  MS_ASSERT(s != NULL);
  s->kind = kind;
  s->parent = b->top;
  s->syms = NULL;
  s->count = 0;
  s->cap = 0;
  s->funcNode = NULL;
  s->isAsync = false;
  s->loopDepth = b->top != NULL ? b->top->loopDepth : 0;
  if (kind == SCOPE_FUNC || kind == SCOPE_COMPR) {
    s->owner = s;
    s->nextSlot = 0;
    s->loopDepth = 0;  // 函数边界：循环深度重新起算
  } else if (b->top != NULL) {
    s->owner = b->top->owner;  // BLOCK/CLASS 借用外层函数分配器
  } else {
    s->owner = s;  // 模块根：自身即边界
  }
  b->top = s;
  return s;
}

static void popScope(Binder* b) {
  Scope* s = b->top;
  b->top = s->parent;
  free(s->syms);
  free(s);
}
// 同作用域查重并登记；非 soft 重复发 MSE_BIND_DUP_DECL 并返回 NULL（不登记）
// 命中 soft 符号（函数名，05 §1 覆盖合法）不报错：同槽复用
static Sym* declareChecked(Binder* b, const char* name, int32_t len, bool fresh, const AstNode* at,
                           bool soft) {
  Scope* s = b->top;
  for (int32_t i = 0; i < s->count; i++) {
    Sym* hit = &s->syms[i];
    if (hit->len == len && memcmp(hit->name, name, (size_t)len) == 0) {
      if (!soft && !hit->soft) {
        bindErr(b, at, len, MSE_BIND_DUP_DECL, "duplicate declaration in the same scope");
        return NULL;
      }
      hit->fresh = hit->fresh || fresh;  // 覆盖/复用：不新增条目，槽位不变
      return hit;
    }
  }
  Sym sym = {name, len, -1, fresh, soft};
  if (s->owner->kind == SCOPE_FUNC || s->owner->kind == SCOPE_COMPR) {
    sym.slot = s->owner->nextSlot++;  // 函数/推导式帧内分配槽位；模块层（含其块/循环域）→ global 无槽
  }
  MS_ARRAY_PUSH(s->syms, s->count, s->cap, sym);
  return &s->syms[s->count - 1];
}

static Sym* declareStrict(Binder* b, const char* name, int32_t len, bool fresh, const AstNode* at) {
  return declareChecked(b, name, len, fresh, at, false);
}

static Sym* declareSoft(Binder* b, const char* name, int32_t len, const AstNode* at) {
  return declareChecked(b, name, len, false, at, true);
}

typedef struct Resolved {
  Sym* sym;
  Scope* def;    // 定义所在 scope
  int32_t hops;  // 引用处与定义函数间的函数边界数（0 = 当前函数内）
  bool found;
} Resolved;

// 自内向外解析；途经函数边界计数（供 upvalue 深度与捕获记录）
static Resolved resolveName(Binder* b, const char* name, int32_t len) {
  Resolved r = {NULL, NULL, 0, false};
  int32_t chainLen = 0;
  for (Scope* s = b->top; s != NULL; s = s->parent) {
    if (s->kind == SCOPE_FUNC || s->kind == SCOPE_COMPR) {
      if (chainLen < MS_BIND_MAX_FUNCS) {
        chainLen++;
      }
    }
    for (int32_t i = s->count - 1; i >= 0; i--) {  // 最新声明优先（覆盖链）
      if (s->syms[i].len == len && memcmp(s->syms[i].name, name, (size_t)len) == 0) {
        r.sym = &s->syms[i];
        r.def = s;
        r.found = true;
        r.hops = chainLen > 0 ? chainLen - 1 : 0;  // 途经函数边界中最新一个即定义所在；未过边界 = 当前函数内
        return r;
      }
    }
  }
  return r;
}

// 声明类节点标注（VAR_DECL/FUNC_DECL/CLASS_DECL 的名字；须在 declare 成功后调用）
static void annotateDeclNode(AstNode* n, const Scope* scope, const Sym* sym, bool fresh) {
  if (scope->owner->kind == SCOPE_FUNC || scope->owner->kind == SCOPE_COMPR) {
    n->bindKind = AST_BIND_LOCAL;
    n->bindSlot = sym->slot;
  } else {
    n->bindKind = AST_BIND_GLOBAL;  // 模块层（含顶层块/循环域内声明）
  }
  if (fresh) {
    n->bindFlags |= AST_BIND_FRESH;
  }
}

static void recordCapture(Binder* b, const Resolved* r) {
  // 自内向外途经的每个函数边界（不含定义函数）都记录一次捕获（透传链，T018 消费）
  int32_t index = 0;
  for (Scope* s = b->top; s != NULL; s = s->parent) {
    if (s->kind != SCOPE_FUNC && s->kind != SCOPE_COMPR) {
      continue;
    }
    if (index >= r->hops) {
      break;
    }
    int32_t depth = r->hops - index;
    bool dup = false;
    for (int32_t i = 0; i < b->captureCount; i++) {
      const BinderCapture* c = &b->captures[i];
      if (c->func == s->funcNode && c->depth == depth && c->nameLen == r->sym->len &&
          memcmp(c->name, r->sym->name, (size_t)r->sym->len) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      BinderCapture cap = {s->funcNode, r->sym->name, r->sym->len, r->sym->slot, depth};
      MS_ARRAY_PUSH(b->captures, b->captureCount, b->captureCap, cap);
    }
    index++;
  }
}

// 标识符引用解析与标注（模块层/未解析 → global；跨函数边界 → upvalue）
static void bindIdent(Binder* b, AstNode* n) {
  Resolved r = resolveName(b, n->as.ident.name, n->as.ident.nameLen);
  if (!r.found || r.def->owner->kind == SCOPE_MODULE) {
    n->bindKind = AST_BIND_GLOBAL;  // 未解析或定义于模块层（含顶层块）→ 动态全局
    return;
  }
  if (r.hops == 0) {
    n->bindKind = AST_BIND_LOCAL;
    n->bindSlot = r.sym->slot;
    return;
  }
  n->bindKind = AST_BIND_UPVALUE;
  n->bindSlot = r.sym->slot;
  n->bindUpvalDepth = r.hops;
  recordCapture(b, &r);
}

void binderInit(Binder* b, AstArena* arena, const char* file, MsDiagSink* diags) {
  b->arena = arena;
  b->file = file;
  b->diags = diags;
  b->top = NULL;
  b->captures = NULL;
  b->captureCount = 0;
  b->captureCap = 0;
}

void binderDestroy(Binder* b) {
  free(b->captures);
  b->captures = NULL;
  b->captureCount = 0;
  b->captureCap = 0;
}

const BinderCapture* binderCaptures(const Binder* b, int32_t* outCount) {
  *outCount = b->captureCount;
  return b->captures;
}

static void bindStmt(Binder* b, AstNode* n);
static void bindExpr(Binder* b, AstNode* n);

// var/const/:= 声明：初值在声明前解析（RHS 不见新名），fresh 标记循环每轮新鲜绑定
static void bindVarDecl(Binder* b, AstNode* n, bool fresh) {
  if (n->as.varDecl.init != NULL) {
    bindExpr(b, n->as.varDecl.init);
  }
  Sym* sym = declareStrict(b, n->as.varDecl.name, n->as.varDecl.nameLen, fresh, n);
  if (sym != NULL) {
    annotateDeclNode(n, b->top, sym, fresh);
  }
}

static void bindBlock(Binder* b, AstNode* n) {
  pushScope(b, SCOPE_BLOCK);
  for (int32_t i = 0; i < n->as.list.count; i++) {
    bindStmt(b, n->as.list.items[i]);
  }
  popScope(b);
}

// for-in/推导式目标（IDENT 或 IDENT 元组）登记为 fresh 绑定
static void declareIterTargets(Binder* b, AstNode* target) {
  if (target == NULL) {
    return;
  }
  if (target->kind == AST_TUPLE) {
    for (int32_t i = 0; i < target->as.tuple.count; i++) {
      declareIterTargets(b, target->as.tuple.items[i]);
    }
    return;
  }
  Sym* sym = declareStrict(b, target->as.ident.name, target->as.ident.nameLen, true, target);
  if (sym != NULL) {
    annotateDeclNode(target, b->top, sym, true);
  }
}

// 函数声明/表达式公共绑定：默认值在外层域解析，参数按序占槽（i → slot i）
static void bindFuncCommon(Binder* b, AstNode* n, ms_u8 flags, AstParam* params, int32_t paramCount,
                           AstNode* body, AstNode* exprBody) {
  for (int32_t i = 0; i < paramCount; i++) {
    if (params[i].defaultExpr != NULL) {
      bindExpr(b, params[i].defaultExpr);  // 默认值求值于定义处作用域（05 §1）
    }
  }
  Scope* fn = pushScope(b, SCOPE_FUNC);
  fn->funcNode = n;
  fn->isAsync = (flags & AST_FUNC_ASYNC) != 0;
  for (int32_t i = 0; i < paramCount; i++) {
    declareStrict(b, params[i].name, params[i].nameLen, false, n);
  }
  if (body != NULL) {
    bindBlock(b, body);  // 体块为参数域子作用域（func f(a){a:=1} 合法遮蔽）
  } else if (exprBody != NULL) {
    bindExpr(b, exprBody);
  }
  popScope(b);
}

static void bindStmt(Binder* b, AstNode* n) {
  if (n == NULL) {
    return;
  }
  switch (n->kind) {
    case AST_MODULE: {  // 顶层语句序列（module 节点复用 list 载荷）
      pushScope(b, SCOPE_MODULE);  // 根作用域：owner 自身（pushScope 处理）
      for (int32_t i = 0; i < n->as.list.count; i++) {
        bindStmt(b, n->as.list.items[i]);
      }
      popScope(b);
      break;
    }
    case AST_BLOCK:
      bindBlock(b, n);
      break;
    case AST_VAR_DECL:
      bindVarDecl(b, n, false);  // 循环头的 fresh 变体由循环处理显式传入
      break;
    case AST_ASSIGN:
      bindExpr(b, n->as.assign.target);
      bindExpr(b, n->as.assign.value);
      break;
    case AST_DESTRUCTURE: {  // *rest 形态：全部目标在当前作用域登记（任务书 §2）
      bindExpr(b, n->as.destructure.value);
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        AstNode* t = n->as.destructure.targets[i];
        if (t == NULL) {
          continue;
        }
        Sym* sym = declareStrict(b, t->as.ident.name, t->as.ident.nameLen, false, t);
        if (sym != NULL) {
          annotateDeclNode(t, b->top, sym, false);
        }
      }
      break;
    }
    case AST_EXPR_STMT:
      bindExpr(b, n->as.exprStmt.expr);
      break;
    case AST_IF:
      bindExpr(b, n->as.ifStmt.cond);
      bindBlock(b, n->as.ifStmt.thenBlock);
      if (n->as.ifStmt.elseNode != NULL) {
        if (n->as.ifStmt.elseNode->kind == AST_BLOCK) {
          bindBlock(b, n->as.ifStmt.elseNode);
        } else {
          bindStmt(b, n->as.ifStmt.elseNode);  // else if 链
        }
      }
      break;
    case AST_FOR: {
      pushScope(b, SCOPE_BLOCK);  // 循环域：头部声明 + 体（04 §4 每轮新鲜）
      b->top->loopDepth++;
      if (n->as.forStmt.form == AST_FOR_TRI) {
        if (n->as.forStmt.init != NULL) {
          if (n->as.forStmt.init->kind == AST_VAR_DECL) {
            bindVarDecl(b, n->as.forStmt.init, true);
          } else {
            bindStmt(b, n->as.forStmt.init);
          }
        }
        bindExpr(b, n->as.forStmt.cond);
        bindExpr(b, n->as.forStmt.post);
      } else if (n->as.forStmt.form == AST_FOR_IN) {
        bindExpr(b, n->as.forStmt.iter);  // 迭代源先于目标登记（03 §2 外层作用域求值）
        declareIterTargets(b, n->as.forStmt.target);
      } else {
        bindExpr(b, n->as.forStmt.cond);
      }
      bindBlock(b, n->as.forStmt.body);
      popScope(b);
      break;
    }
    case AST_WHILE: {
      pushScope(b, SCOPE_BLOCK);
      b->top->loopDepth++;
      bindExpr(b, n->as.whileStmt.cond);
      bindBlock(b, n->as.whileStmt.body);
      popScope(b);
      break;
    }
    case AST_SWITCH: {
      bindExpr(b, n->as.switchStmt.subject);
      for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
        AstNode* c = n->as.switchStmt.cases[i];
        pushScope(b, SCOPE_BLOCK);  // case 体子作用域（as 绑定宿主）
        for (int32_t v = 0; v < c->as.switchCase.valueCount; v++) {
          bindExpr(b, c->as.switchCase.values[v]);
        }
        if (c->as.switchCase.bindName != NULL) {
          declareStrict(b, c->as.switchCase.bindName, c->as.switchCase.bindLen, false, c);
        }
        if (c->as.switchCase.body == NULL) {
          popScope(b);
          continue;  // 解析错误残留的部分 case（诊断已由 parser 发出）
        }
        if (c->as.switchCase.body->kind == AST_BLOCK) {
          bindBlock(b, c->as.switchCase.body);
        } else {
          bindStmt(b, c->as.switchCase.body);
        }
        popScope(b);
      }
      break;
    }
    case AST_RETURN:
    case AST_THROW:
    case AST_YIELD:
      bindExpr(b, n->as.exprStmt.expr);  // 载荷复用（T008）；位置检查 Task 4
      break;
    case AST_BREAK:
    case AST_CONTINUE:
      break;  // 位置检查 Task 4
    case AST_DEFER:
      bindExpr(b, n->as.exprStmt.expr);
      break;
    case AST_SEND:
      bindExpr(b, n->as.sendStmt.chan);
      bindExpr(b, n->as.sendStmt.value);
      break;
    case AST_SELECT: {
      for (int32_t i = 0; i < n->as.selectStmt.count; i++) {
        AstNode* c = n->as.selectStmt.cases[i];
        pushScope(b, SCOPE_BLOCK);  // 分支体子作用域
        if (c->as.selectCase.form == AST_SEL_RECV) {
          bindExpr(b, c->as.selectCase.target);  // 可 NULL（BAD_TARGET 已诊断）
          bindExpr(b, c->as.selectCase.chan);    // RECV：AST_RECV 源
        } else if (c->as.selectCase.form == AST_SEL_SEND) {
          bindExpr(b, c->as.selectCase.chan);
          bindExpr(b, c->as.selectCase.value);
        }
        if (c->as.selectCase.body == NULL) {
          popScope(b);
          continue;  // 解析错误残留的部分分支
        }
        if (c->as.selectCase.body->kind == AST_BLOCK) {
          bindBlock(b, c->as.selectCase.body);
        } else {
          bindStmt(b, c->as.selectCase.body);
        }
        popScope(b);
      }
      break;
    }
    case AST_IMPORT: {
      if (n->as.importStmt.alias != NULL) {
        declareStrict(b, n->as.importStmt.alias, n->as.importStmt.aliasLen, false, n);
      } else if (!n->as.importStmt.star && n->as.importStmt.names != NULL) {
        for (int32_t i = 0; i < n->as.importStmt.nameCount; i++) {
          const AstImportName* nm = &n->as.importStmt.names[i];
          const char* name = nm->asName != NULL ? nm->asName : nm->name;
          int32_t len = nm->asName != NULL ? nm->asLen : nm->nameLen;
          declareStrict(b, name, len, false, n);
        }
      }
      break;
    }
    case AST_FUNC_DECL: {
      Sym* sym = declareSoft(b, n->as.funcDecl.name, n->as.funcDecl.nameLen, n);
      if (sym != NULL) {
        annotateDeclNode(n, b->top, sym, false);
      }
      bindFuncCommon(b, n, n->as.funcDecl.flags, n->as.funcDecl.params, n->as.funcDecl.paramCount,
                     n->as.funcDecl.body, n->as.funcDecl.exprBody);
      break;
    }
    case AST_CLASS_DECL: {
      Sym* sym = declareStrict(b, n->as.classDecl.name, n->as.classDecl.nameLen, false, n);
      if (sym != NULL) {
        annotateDeclNode(n, b->top, sym, false);
      }
      for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
        bindExpr(b, n->as.classDecl.bases[i]);  // 基类在外层作用域解析
      }
      pushScope(b, SCOPE_CLASS);  // 透明边界：成员名不进入词法作用域（06 §1）
      for (int32_t i = 0; i < n->as.classDecl.memberCount; i++) {
        AstNode* m = n->as.classDecl.members[i];
        if (m->kind == AST_VAR_DECL) {
          bindExpr(b, m->as.varDecl.init);  // 字段初始化：仅解析初值，不登记成员名
        } else {
          bindFuncCommon(b, m, m->as.funcDecl.flags, m->as.funcDecl.params,
                         m->as.funcDecl.paramCount, m->as.funcDecl.body, m->as.funcDecl.exprBody);
        }
      }
      popScope(b);
      break;
    }
    case AST_TRY: {
      bindBlock(b, n->as.tryStmt.tryBlock);
      for (int32_t i = 0; i < n->as.tryStmt.catchCount; i++) {
        AstCatchClause* c = &n->as.tryStmt.catches[i];
        pushScope(b, SCOPE_BLOCK);  // catch 子句作用域（bindName 宿主）
        for (int32_t t = 0; t < c->typeCount; t++) {
          bindExpr(b, c->types[t]);
        }
        if (c->bindName != NULL) {
          declareStrict(b, c->bindName, c->bindLen, false, c->block);
        }
        if (c->block != NULL) {
          bindBlock(b, c->block);
        }
        popScope(b);
      }
      if (n->as.tryStmt.finallyBlock != NULL) {
        bindBlock(b, n->as.tryStmt.finallyBlock);
      }
      break;
    }
    default:
      break;
  }
}

// 推导式/生成器表达式（03 §2）：隐式函数作用域；逐 for 子句嵌套域，目标 fresh
static void bindCompr(Binder* b, AstNode* n) {
  Scope* fn = pushScope(b, SCOPE_COMPR);
  fn->funcNode = n;
  for (int32_t i = 0; i < n->as.compr.forCount; i++) {
    bindExpr(b, n->as.compr.fors[i].iter);  // 第 i 个迭代源在第 i 层子句域求值
    pushScope(b, SCOPE_BLOCK);
    declareIterTargets(b, n->as.compr.fors[i].target);
  }
  for (int32_t i = 0; i < n->as.compr.condCount; i++) {
    bindExpr(b, n->as.compr.conds[i]);
  }
  bindExpr(b, n->as.compr.key);
  bindExpr(b, n->as.compr.value);
  for (int32_t i = 0; i < n->as.compr.forCount; i++) {
    popScope(b);
  }
  popScope(b);
}

static void bindExpr(Binder* b, AstNode* n) {
  if (n == NULL) {
    return;
  }
  switch (n->kind) {
    case AST_IDENT:
      bindIdent(b, n);
      break;
    case AST_STRING:
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        if (n->as.strLit.segs[i].expr != NULL) {
          bindExpr(b, n->as.strLit.segs[i].expr);  // 插值段
        }
      }
      break;
    case AST_TUPLE:
    case AST_LIST:
    case AST_SET:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        bindExpr(b, n->as.list.items[i]);
      }
      break;
    case AST_MAP:
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        bindExpr(b, n->as.mapLit.keys[i]);
        bindExpr(b, n->as.mapLit.values[i]);
      }
      break;
    case AST_BINARY:
      bindExpr(b, n->as.binary.lhs);
      bindExpr(b, n->as.binary.rhs);
      break;  // 常量折叠 Task 5 接入
    case AST_LOGIC:
      bindExpr(b, n->as.binary.lhs);
      bindExpr(b, n->as.binary.rhs);
      break;
    case AST_UNARY:
    case AST_RECV:
    case AST_AWAIT:
    case AST_GO:
      bindExpr(b, n->as.unary.operand);  // 载荷复用（op==0）；await 位置检查 Task 4
      break;
    case AST_TERNARY:
      bindExpr(b, n->as.ternary.cond);
      bindExpr(b, n->as.ternary.thenExpr);
      bindExpr(b, n->as.ternary.elseExpr);
      break;
    case AST_CALL:
      bindExpr(b, n->as.call.callee);
      for (int32_t i = 0; i < n->as.call.count; i++) {
        bindExpr(b, n->as.call.args[i]);
      }
      break;
    case AST_INDEX:
      bindExpr(b, n->as.indexExpr.obj);
      bindExpr(b, n->as.indexExpr.index);
      break;
    case AST_MEMBER:
      bindExpr(b, n->as.memberExpr.obj);
      break;
    case AST_SLICE:
      bindExpr(b, n->as.slice.obj);
      bindExpr(b, n->as.slice.start);
      bindExpr(b, n->as.slice.end);
      bindExpr(b, n->as.slice.step);
      break;
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      bindCompr(b, n);
      break;
    case AST_FUNC_EXPR:
      bindFuncCommon(b, n, n->as.funcExpr.flags, n->as.funcExpr.params, n->as.funcExpr.paramCount,
                     n->as.funcExpr.body, n->as.funcExpr.exprBody);
      break;
    case AST_THIS:
    case AST_NIL:
    case AST_BOOL:
    case AST_INT:
    case AST_FLOAT:
    case AST_BIGINT:
    case AST_RUNE:
      break;
    default:
      break;
  }
}

bool binderBind(Binder* b, AstNode* module) {
  if (module == NULL || module->kind != AST_MODULE) {
    return true;
  }
  bindStmt(b, module);
  return b->diags == NULL || b->diags->count == 0;
}
