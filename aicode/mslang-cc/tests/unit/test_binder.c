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

TEST(closureCapturesUpvalue) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func counter() {\nvar n = 0\nreturn func() { n += 1; return n }\n}",
                         &arena, &diags, &binder);
  AstNode* inner = stmt0(mod)->as.funcDecl.body->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_FUNC_EXPR, inner->kind);
  AstNode* nRef = inner->as.funcExpr.body->as.list.items[0]->as.assign.target;
  EXPECT_EQ(ctx, AST_BIND_UPVALUE, nRef->bindKind);
  EXPECT_EQ(ctx, 0, nRef->bindSlot);        // n 在 counter 内 slot 0
  EXPECT_EQ(ctx, 1, nRef->bindUpvalDepth);  // 直接内层
  int32_t count = 0;
  const BinderCapture* caps = binderCaptures(&binder, &count);
  EXPECT_EQ(ctx, 1, count);
  if (count == 1) {
    EXPECT_EQ(ctx, (long long)inner, (long long)caps[0].func);
    EXPECT_EQ(ctx, 0, caps[0].slot);
    EXPECT_EQ(ctx, 1, caps[0].depth);
  }
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(upvalueChainDepthTwo) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func a() {\nvar n = 0\nfunc b() {\nfunc c() { n }\nc()\n}\nb()\n}\n}",
                         &arena, &diags, &binder);
  AstNode* fa = stmt0(mod);
  AstNode* fb = fa->as.funcDecl.body->as.list.items[1];
  AstNode* fc = fb->as.funcDecl.body->as.list.items[0];
  AstNode* nRef = fc->as.funcDecl.body->as.list.items[0]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_UPVALUE, nRef->bindKind);
  EXPECT_EQ(ctx, 0, nRef->bindSlot);
  EXPECT_EQ(ctx, 2, nRef->bindUpvalDepth);
  int32_t count = 0;
  const BinderCapture* caps = binderCaptures(&binder, &count);
  EXPECT_EQ(ctx, 2, count);  // b 透传 + c 引用
  bool hasB = false, hasC = false;
  for (int32_t i = 0; i < count; i++) {
    if (caps[i].func == (AstNode*)fb && caps[i].depth == 1) {
      hasB = true;
    }
    if (caps[i].func == (AstNode*)fc && caps[i].depth == 2) {
      hasC = true;
    }
  }
  EXPECT(ctx, hasB);
  EXPECT(ctx, hasC);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(comprehensionCapturesUpvalue) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f(xs) {\nvar k = 2\nreturn [x * k for x in xs]\n}", &arena, &diags,
                         &binder);
  AstNode* comp = stmt0(mod)->as.funcDecl.body->as.list.items[1]->as.exprStmt.expr;
  AstNode* k = comp->as.compr.value->as.binary.rhs;
  EXPECT_EQ(ctx, AST_BIND_UPVALUE, k->bindKind);  // 推导式隐式函数捕获外层局部
  EXPECT_EQ(ctx, 1, k->bindUpvalDepth);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(localRefNotUpvalue) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("func f() {\nvar x = 1\nreturn func() { var x = 2; return x }\n}", &arena,
                         &diags, &binder);
  AstNode* inner = stmt0(mod)->as.funcDecl.body->as.list.items[1]->as.exprStmt.expr;
  AstNode* x = inner->as.funcExpr.body->as.list.items[1]->as.exprStmt.expr;
  EXPECT_EQ(ctx, AST_BIND_LOCAL, x->bindKind);  // 内层自有 x → 局部非 upvalue
  EXPECT_EQ(ctx, 0, x->bindSlot);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(topLevelReturnRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("return 1", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_TOP_RETURN, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(breakOutsideLoopRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func f() { break }", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_LOOP_ESCAPE, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(continueOutsideLoopRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func f() { continue }", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_LOOP_ESCAPE, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(breakInFuncInsideLoopRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("while true { func g() { break } }", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);  // 函数边界重置循环深度
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(breakInsideLoopAccepted) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func f(xs) {\nfor x in xs { break }\nwhile true { continue }\n}", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(awaitInNormalFuncRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func f() { await g() }", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_AWAIT_POS, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(awaitInAsyncFuncAndTopLevelAccepted) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("async func f() { await g() }\nvar r = await h()", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 0, diags.count);  // 模块顶层 await 合法（00 §"顶层即入口"）
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(yieldInAsyncRejected) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("async func f() { yield 1 }", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, (long long)MSE_BIND_YIELD_MISPLACED, (long long)diags.items[0].code);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(yieldInGeneratorAccepted) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  bindSrc("func* fib() {\nvar a = 0\nfor { yield a }\n}", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 0, diags.count);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldArithPrecedence) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var x = 1 + 2 * 3", &arena, &diags, &binder);
  EXPECT_EQ(ctx, AST_INT, stmt0(mod)->as.varDecl.init->kind);
  EXPECT_EQ(ctx, 7, (int)stmt0(mod)->as.varDecl.init->as.intLit.v);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldLeftAssocAndTruncDiv) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var a = 10 - 3 - 2\nvar b = -7 / 2\nvar c = -7 % 2", &arena, &diags,
                         &binder);
  EXPECT_EQ(ctx, 5, (int)stmt0(mod)->as.varDecl.init->as.intLit.v);
  EXPECT_EQ(ctx, -3, (int)mod->as.list.items[1]->as.varDecl.init->as.intLit.v);  // 截断除（02 §3.5）
  EXPECT_EQ(ctx, -1, (int)mod->as.list.items[2]->as.varDecl.init->as.intLit.v);   // 符号随被除数
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldDivByZeroNotFolded) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var x = 1 / 0", &arena, &diags, &binder);
  EXPECT_EQ(ctx, AST_BINARY, stmt0(mod)->as.varDecl.init->kind);  // 运行时 ArithmeticError
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldOverflowAddUpgradesBigint) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var x = 9223372036854775807 + 1", &arena, &diags, &binder);
  AstNode* lit = stmt0(mod)->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BIGINT, lit->kind);
  EXPECT_EQ(ctx, 19, lit->as.bigintLit.len);
  EXPECT_EQ(ctx, 0, memcmp("9223372036854775808", lit->as.bigintLit.start, 19));
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldOverflowNegativeAndMul) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var x = -9223372036854775807 - 2\nvar y = 9223372036854775807 * 2", &arena,
                         &diags, &binder);
  AstNode* x = stmt0(mod)->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BIGINT, x->kind);
  EXPECT_EQ(ctx, 20, x->as.bigintLit.len);
  EXPECT_EQ(ctx, 0, memcmp("-9223372036854775809", x->as.bigintLit.start, 20));
  AstNode* y = mod->as.list.items[1]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BIGINT, y->kind);
  EXPECT_EQ(ctx, 20, y->as.bigintLit.len);
  EXPECT_EQ(ctx, 0, memcmp("18446744073709551614", y->as.bigintLit.start, 20));
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldPow) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var a = 2 ** 10\nvar b = 2 ** 64", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 1024, (int)stmt0(mod)->as.varDecl.init->as.intLit.v);
  AstNode* bigPow = mod->as.list.items[1]->as.varDecl.init;
  EXPECT_EQ(ctx, AST_BIGINT, bigPow->kind);
  EXPECT_EQ(ctx, 20, bigPow->as.bigintLit.len);
  EXPECT_EQ(ctx, 0, memcmp("18446744073709551616", bigPow->as.bigintLit.start, 20));
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldFloatAndCompareAndLogic) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc(
      "var f = 1.5 + 2.25\nvar g = 7.0 / 2.0\nvar b = 1 < 2\nvar c = 2 == 3\nvar l = true && false\nvar n = !true",
      &arena, &diags, &binder);
  EXPECT_F64_EQ(ctx, 3.75, stmt0(mod)->as.varDecl.init->as.floatLit.v);
  EXPECT_F64_EQ(ctx, 3.5, mod->as.list.items[1]->as.varDecl.init->as.floatLit.v);
  EXPECT_EQ(ctx, true, mod->as.list.items[2]->as.varDecl.init->as.boolLit.b);
  EXPECT_EQ(ctx, false, mod->as.list.items[3]->as.varDecl.init->as.boolLit.b);
  EXPECT_EQ(ctx, false, mod->as.list.items[4]->as.varDecl.init->as.boolLit.b);
  EXPECT_EQ(ctx, false, mod->as.list.items[5]->as.varDecl.init->as.boolLit.b);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldBitwiseChain) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var m = (1 << 4) | 3\nvar s = 19 >> 2", &arena, &diags, &binder);
  EXPECT_EQ(ctx, 19, (int)stmt0(mod)->as.varDecl.init->as.intLit.v);
  EXPECT_EQ(ctx, 4, (int)mod->as.list.items[1]->as.varDecl.init->as.intLit.v);
  binderDestroy(&binder);
  astArenaDestroy(&arena);
}

TEST(foldStringNotFolded) {
  MsDiagSink diags = {0};
  AstArena arena;
  astArenaInit(&arena);
  Binder binder;
  AstNode* mod = bindSrc("var s = \"a\" + \"b\"", &arena, &diags, &binder);
  EXPECT_EQ(ctx, AST_BINARY, stmt0(mod)->as.varDecl.init->kind);  // 字符串拼接留给运行时/插值优化
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
      comprehensionScopesTargets,    closureCapturesUpvalue,
      upvalueChainDepthTwo,          comprehensionCapturesUpvalue,
      localRefNotUpvalue,            topLevelReturnRejected,
      breakOutsideLoopRejected,      continueOutsideLoopRejected,
      breakInFuncInsideLoopRejected, breakInsideLoopAccepted,
      awaitInNormalFuncRejected,     awaitInAsyncFuncAndTopLevelAccepted,
      yieldInAsyncRejected,          yieldInGeneratorAccepted,
      foldArithPrecedence,           foldLeftAssocAndTruncDiv,
      foldDivByZeroNotFolded,        foldOverflowAddUpgradesBigint,
      foldOverflowNegativeAndMul,    foldPow,
      foldFloatAndCompareAndLogic,   foldBitwiseChain,
      foldStringNotFolded,
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
