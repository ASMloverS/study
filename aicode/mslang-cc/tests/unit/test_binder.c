// T009：Binder 单测（docs/tasks/T009 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "parser/parser.h"
#include "test_harness.h"

// 解析 + 绑定一步到位；返回模块节点
static AstNode* bindSrc(const char* src, AstArena* arena, MsDiagSink* diags, Binder* binder) {
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", arena, diags);
  AstNode* mod = parserParseModule(&p);
  binderInit(binder, arena, "t.ms", diags);
  binderBind(binder, mod);
  return mod;
}

// 首语句快捷取用
static AstNode* stmt0(AstNode* mod) { return mod->as.list.items[0]; }

TEST(localSlotClassification) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() { var x = 1; return x }", &arena, &diags, &binder);
  AstNode* f = stmt0(mod);
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, f->bindKind);  // 模块层函数名 → global
  AstNode* var = f->as.funcDecl.body->as.list.items[0];
  EXPECT_EQ(ctx, AST_BIND_LOCAL, var->bindKind);
  EXPECT_EQ(ctx, 0, var->bindSlot);
  AstNode* x = f->as.funcDecl.body->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_IDENT, x->kind);
  EXPECT_EQ(ctx, AST_BIND_LOCAL, x->bindKind);
  EXPECT_EQ(ctx, 0, x->bindSlot);
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(paramsTakeLeadingSlots) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func add(a, b) { var s = a + b; return s }", &arena, &diags, &binder);
  AstNode* body = stmt0(mod)->as.funcDecl.body;
  AstNode* sum = body->as.list.items[0]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, sum->as.binary.lhs->bindKind);
  EXPECT_EQ(ctx, 0, sum->as.binary.lhs->bindSlot);  // a → slot 0（参数按序约定）
  EXPECT_EQ(ctx, AST_BIND_LOCAL, sum->as.binary.rhs->bindKind);
  EXPECT_EQ(ctx, 1, sum->as.binary.rhs->bindSlot);  // b → slot 1
  EXPECT_EQ(ctx, AST_BIND_LOCAL, body->as.list.items[0]->bindKind);
  EXPECT_EQ(ctx, 2, body->as.list.items[0]->bindSlot);  // s → slot 2
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(innerShadowGetsNewSlot) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() { var x = 1; { var x = 2; x }; x }", &arena, &diags, &binder);
  AstNode* body = stmt0(mod)->as.funcDecl.body;
  AstNode* inner = body->as.list.items[1]->as.list.items[0];  // 内层 var x
  EXPECT_EQ(ctx, AST_BIND_LOCAL, inner->bindKind);
  EXPECT_EQ(ctx, 1, inner->bindSlot);                    // 遮蔽 → 新槽位
  EXPECT_EQ(ctx, 0, body->as.list.items[0]->bindSlot);   // 外层 x 不被改写
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(moduleDeclAndRefsAreGlobal) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var g = 1\nfunc f() { return g }\nf", &arena, &diags, &binder);
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, stmt0(mod)->bindKind);
  AstNode* g = stmt0(mod)->as.varDecl.init;
  EXPECT_EQ(ctx, AST_INT, g->kind);
  AstNode* ref = mod->as.list.items[1]->as.funcDecl.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, ref->bindKind);  // 函数内引用模块层名 → global
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(unresolvedNameIsGlobal) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() { return h }", &arena, &diags, &binder);
  AstNode* h = stmt0(mod)->as.funcDecl.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, h->bindKind);  // 未解析 → 动态全局查找
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(dupParamsReportBindError) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func f(a, a) {}", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_DUP_DECL, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(destructureTargetsDeclare) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f(xs) { a, b, *r = xs; return r }", &arena, &diags, &binder);
  AstNode* body = stmt0(mod)->as.funcDecl.body;
  AstNode* d = body->as.list.items[0];
  EXPECT_EQ(ctx, AST_DESTRUCTURE, d->kind);
  AstNode* r = d->as.destructure.targets[2];
  EXPECT_EQ(ctx, AST_IDENT, r->kind);
  EXPECT_EQ(ctx, AST_BIND_LOCAL, r->bindKind);
  EXPECT_EQ(ctx, 3, r->bindSlot);  // xs=0, a=1, b=2, r=3（声明序）
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(forInTargetIsFreshAtModuleLevel) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("for x in xs { x }", &arena, &diags, &binder);
  AstNode* loop = stmt0(mod);
  AstNode* x = loop->as.forStmt.target;
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, x->bindKind);  // 模块层（含循环域）→ global 无槽
  EXPECT_EQ(ctx, (long long)AST_BIND_FRESH, (long long)(x->bindFlags & AST_BIND_FRESH));
  AstNode* bodyX = loop->as.forStmt.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, bodyX->bindKind);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(forInFreshInsideFunc) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f(xs) { for x in xs { x } }", &arena, &diags, &binder);
  AstNode* loop = stmt0(mod)->as.funcDecl.body->as.list.items[0];
  AstNode* x = loop->as.forStmt.target;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, x->bindKind);
  EXPECT_EQ(ctx, 1, x->bindSlot);  // xs=0, x=1
  EXPECT_EQ(ctx, (long long)AST_BIND_FRESH, (long long)(x->bindFlags & AST_BIND_FRESH));
  AstNode* bodyX = loop->as.forStmt.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, bodyX->bindKind);
  EXPECT_EQ(ctx, 1, bodyX->bindSlot);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(forTriInitIsFresh) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() { for i := 0; i < 10; i += 1 { i } }", &arena, &diags, &binder);
  AstNode* loop = stmt0(mod)->as.funcDecl.body->as.list.items[0];
  AstNode* init = loop->as.forStmt.init;
  EXPECT_EQ(ctx, AST_VAR_DECL, init->kind);
  EXPECT_EQ(ctx, AST_BIND_LOCAL, init->bindKind);
  EXPECT_EQ(ctx, 0, init->bindSlot);
  EXPECT_EQ(ctx, (long long)AST_BIND_FRESH, (long long)(init->bindFlags & AST_BIND_FRESH));
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(catchAndSwitchAsBind) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f(v) {\ntry { g() } catch e { e }\nswitch type(v) { case int as s: { s } default: { nil } }\n}",
                         &arena, &diags, &binder);
  AstNode* body = stmt0(mod)->as.funcDecl.body;
  AstNode* tryNode = body->as.list.items[0];
  AstNode* e = tryNode->as.tryStmt.catches[0].block->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, e->bindKind);  // catch e 绑定于子句作用域
  AstNode* sw = body->as.list.items[1];
  AstNode* s = sw->as.switchStmt.cases[0]->as.switchCase.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, s->bindKind);  // case … as s 同理
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(importAliasDeclaresAndDups) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("var x = 1\nimport \"m\" as x", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_DUP_DECL, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(funcRedeclareLegalSameSlot) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() { return 1 }\nfunc f() { return 2 }\nf", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 0, diags.count);  // 05 §1 后定义覆盖
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, stmt0(mod)->bindKind);
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, mod->as.list.items[1]->bindKind);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(classMembersTransparentToLexicalLookup) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("class Counter {\nvar total = 0\nfunc get() -> total\n}", &arena, &diags,
                         &binder);
  AstNode* cls = stmt0(mod);
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, cls->bindKind);
  AstNode* get = cls->as.classDecl.members[1];
  AstNode* total = get->as.funcDecl.exprBody;
  EXPECT_EQ(ctx, AST_BIND_GLOBAL, total->bindKind);  // 类体成员名不进入词法作用域
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(comprehensionScopesTargets) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f(xs) { return [x + 1 for x in xs if x > 0] }", &arena, &diags,
                         &binder);
  AstNode* comp = stmt0(mod)->as.funcDecl.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_COMPREHENSION, comp->kind);
  AstNode* xInValue = comp->as.compr.value->as.binary.lhs;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, xInValue->bindKind);  // 迭代变量在推导式作用域内
  AstNode* xInCond = comp->as.compr.conds[0]->as.binary.lhs;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, xInCond->bindKind);
  EXPECT_EQ(ctx, xInValue->bindSlot, xInCond->bindSlot);
  EXPECT_EQ(ctx, (long long)AST_BIND_FRESH,
            (long long)(comp->as.compr.fors[0].target->bindFlags & AST_BIND_FRESH));
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      localSlotClassification,       paramsTakeLeadingSlots,
      innerShadowGetsNewSlot,        moduleDeclAndRefsAreGlobal,
      unresolvedNameIsGlobal,        dupParamsReportBindError,
      destructureTargetsDeclare,     forInTargetIsFreshAtModuleLevel,
      forInFreshInsideFunc,          forTriInitIsFresh,
      catchAndSwitchAsBind,          importAliasDeclaresAndDups,
      funcRedeclareLegalSameSlot,    classMembersTransparentToLexicalLookup,
      comprehensionScopesTargets,
  };
  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  for (int i = 0; i < total; i++) {
    if (only < 0 || only == i) {
      tests[i](&ctx);
    }
  }
  if (ctx.failures == 0) {
    printf("all tests passed\n");
  }
  return ctx.failures == 0 ? 0 : 1;
}
