// T010：Compiler/Chunk 单测（docs/tasks/T010 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "compiler/compiler.h"
#include "parser/parser.h"
#include "test_harness.h"

// ---------- Chunk 基础（常量去重 / 行号 RLE） ----------

TEST(constDedupByValue) {
  Chunk c;
  chunkInit(&c, "main", 4);
  EXPECT_EQ(ctx, 0, chunkAddConstInt(&c, 1));
  EXPECT_EQ(ctx, 1, chunkAddConstFloat(&c, 2.5));
  EXPECT_EQ(ctx, 0, chunkAddConstInt(&c, 1));      // int 去重
  EXPECT_EQ(ctx, 1, chunkAddConstFloat(&c, 2.5));  // float 去重
  EXPECT_EQ(ctx, 2, chunkAddConstText(&c, CONST_STRING, "hi", 2));
  EXPECT_EQ(ctx, 2, chunkAddConstText(&c, CONST_STRING, "hi", 2));  // string 去重
  EXPECT_EQ(ctx, 3, chunkAddConstText(&c, CONST_BIGINT, "hi", 2));  // BIGINT 与 STRING 互不合并
  EXPECT_EQ(ctx, 4, chunkAddConstBool(&c, true));
  EXPECT_EQ(ctx, 5, c.constsLen);
  EXPECT_EQ(ctx, CONST_STRING, c.consts[2].kind);
  EXPECT_EQ(ctx, 2, c.consts[2].as.text.len);
  chunkFree(&c);
}

TEST(protoNeverDeduped) {
  Chunk c;
  chunkInit(&c, "main", 4);
  Chunk* p1 = malloc(sizeof(Chunk));
  Chunk* p2 = malloc(sizeof(Chunk));
  chunkInit(p1, "f", 1);
  chunkInit(p2, "f", 1);
  int32_t i1 = chunkAddConstProto(&c, p1);
  int32_t i2 = chunkAddConstProto(&c, p2);
  EXPECT_EQ(ctx, 0, i1);
  EXPECT_EQ(ctx, 1, i2);
  EXPECT_EQ(ctx, 2, c.constsLen);
  chunkFree(&c);  // 递归释放 p1/p2
}

TEST(lineTableRle) {
  Chunk c;
  chunkInit(&c, "main", 4);
  chunkEmitOp(&c, OP_LOAD_CONST, 1);  // offset 0（含 1 字节操作数）
  chunkEmitByte(&c, 0);
  chunkEmitOp(&c, OP_LOAD_NIL, 2);  // offset 2，行变 → 新 run
  chunkEmitOp(&c, OP_RETURN, 2);    // 同行：不新增 run
  EXPECT_EQ(ctx, 2, c.linesLen);
  EXPECT_EQ(ctx, 0, c.lines[0].offset);
  EXPECT_EQ(ctx, 1, c.lines[0].line);
  EXPECT_EQ(ctx, 2, c.lines[1].offset);
  EXPECT_EQ(ctx, 2, c.lines[1].line);
  EXPECT_EQ(ctx, 1, chunkLineAt(&c, 0));
  EXPECT_EQ(ctx, 1, chunkLineAt(&c, 1));
  EXPECT_EQ(ctx, 2, chunkLineAt(&c, 2));
  EXPECT_EQ(ctx, 2, chunkLineAt(&c, 99));  // RLE 前向覆盖
  chunkFree(&c);
}

TEST(opcodeTableComplete) {
  EXPECT_EQ(ctx, OP__COUNT, (int)sizeof(msOpNames) / sizeof(msOpNames[0]));
  EXPECT_EQ(ctx, 0, strcmp(msOpNames[OP_NOP], "NOP"));
  EXPECT_EQ(ctx, 0, strcmp(msOpNames[OP_EXTENDED_ARG], "EXTENDED_ARG"));
  EXPECT_EQ(ctx, 0, strcmp(msOpNames[OP_LOAD_CONST], "LOAD_CONST"));
  EXPECT_EQ(ctx, 0, strcmp(msOpNames[OP_JMP], "JMP"));
  EXPECT_EQ(ctx, 0, strcmp(msOpNames[OP_ASSERT], "ASSERT"));
  EXPECT_EQ(ctx, OPL_CONST, msOpLayouts[OP_LOAD_CONST]);
  EXPECT_EQ(ctx, OPL_JUMP, msOpLayouts[OP_JMP_IF_FALSE_OR_POP]);
  EXPECT_EQ(ctx, OPL_NONE, msOpLayouts[OP_BINARY_ADD]);
  EXPECT_EQ(ctx, OPL_U8, msOpLayouts[OP_CALL]);
  EXPECT_EQ(ctx, OPL_SPECIAL, msOpLayouts[OP_SWITCH_TABLE]);
}

// ---------- 编译前端助手：scan → parse → bind → compile ----------

static bool compileSrc(const char* src, Chunk* chunk, MsDiagSink* diags) {
  AstArena arena;
  astArenaInit(&arena);
  Parser p;
  parserInit(&p, src, (int32_t)strlen(src), "t.ms", &arena, diags);
  AstNode* mod = parserParseModule(&p);
  bool ok = false;
  if (mod != NULL) {
    Binder b;
    binderInit(&b, &arena, "t.ms", diags);
    binderBind(&b, mod);
    binderDestroy(&b);
    ok = compilerCompile(mod, chunk, "t.ms", diags);
  }
  astArenaDestroy(&arena);
  return ok;
}

// 解码一条指令（EXT 链累积）：返回指令总长；操作数取首操作数（无则 -1）
static int32_t decodeInstr(const Chunk* c, int32_t off, ms_u8* opOut, int32_t* operandOut) {
  int32_t acc = 0;
  int32_t p = off;
  ms_u8 op = c->code[p++];
  while (op == OP_EXTENDED_ARG) {
    acc = (acc << 8) | c->code[p++];
    op = c->code[p++];
  }
  int32_t nBytes = 0;
  switch (msOpLayouts[op]) {
    case OPL_NONE:
    case OPL_SPECIAL:
      nBytes = 0;
      break;
    case OPL_U8:
    case OPL_CONST:
    case OPL_JUMP:
      nBytes = 1;
      break;
    case OPL_U8_U8:
    case OPL_CONST_U8:
    case OPL_CONST_CONST:
    case OPL_JUMP_JUMP:
      nBytes = 2;
      break;
    case OPL_CONST_U8_U8:
      nBytes = 3;
      break;
  }
  *opOut = op;
  *operandOut = nBytes >= 1 ? (int32_t)(((ms_u32)acc << 8) | c->code[p]) : -1;
  return (p - off) + nBytes;
}

static int32_t decodeAll(const Chunk* c, ms_u8* ops, int32_t* operands, int32_t max) {
  int32_t count = 0;
  int32_t off = 0;
  while (off < c->codeLen && count < max) {
    off += decodeInstr(c, off, &ops[count], &operands[count]);
    count++;
  }
  return count;
}

// ---------- Compiler 基础路径 ----------

TEST(constLoadSingle) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var a = 3.5", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 2, c.constsLen);  // {float 3.5, "a"}
  EXPECT_EQ(ctx, CONST_FLOAT, c.consts[0].kind);
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 5, decodeAll(&c, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[0]);
  EXPECT_EQ(ctx, 0, operands[0]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[1]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[2]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[3]);
  EXPECT_EQ(ctx, OP_RETURN, ops[4]);
  chunkFree(&c);
}

TEST(binaryOnGlobals) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var z = x + y", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 7, decodeAll(&c, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[0]);  // x
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[1]);  // y
  EXPECT_EQ(ctx, OP_BINARY_ADD, ops[2]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[3]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[4]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[5]);
  EXPECT_EQ(ctx, OP_RETURN, ops[6]);
  chunkFree(&c);
}

TEST(globalReadWrite) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var x = 1\nx = x + 1", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 2, c.constsLen);  // {int 1, "x"}
  ms_u8 ops[12];
  int32_t operands[12];
  EXPECT_EQ(ctx, 9, decodeAll(&c, ops, operands, 12));
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[0]);
  EXPECT_EQ(ctx, 0, operands[0]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[1]);
  EXPECT_EQ(ctx, 1, operands[1]);  // "x"
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[2]);
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[3]);
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[4]);  // 1 去重复用
  EXPECT_EQ(ctx, 0, operands[4]);
  EXPECT_EQ(ctx, OP_BINARY_ADD, ops[5]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[6]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[7]);
  EXPECT_EQ(ctx, OP_RETURN, ops[8]);
  chunkFree(&c);
}

TEST(compoundAssignExpands) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var x = 1\nx += 2", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 3, c.constsLen);  // {int 1, "x", int 2}
  ms_u8 ops[12];
  int32_t operands[12];
  EXPECT_EQ(ctx, 9, decodeAll(&c, ops, operands, 12));
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[3]);
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[4]);
  EXPECT_EQ(ctx, 2, operands[4]);
  EXPECT_EQ(ctx, OP_BINARY_ADD, ops[5]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[6]);
  chunkFree(&c);
}

TEST(noInitVarDefaultsNil) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var x", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 1, c.constsLen);  // 仅 "x"
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 5, decodeAll(&c, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[0]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[1]);
  chunkFree(&c);
}

TEST(localReadWriteInFunc) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func f() { var x = 1\n x = x + 1 }", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, CONST_PROTO, c.consts[0].kind);
  const Chunk* proto = c.consts[0].as.proto;
  EXPECT(ctx, proto->nameLen == 1 && strncmp(proto->name, "f", 1) == 0);
  EXPECT_EQ(ctx, 1, proto->constsLen);  // {int 1}
  EXPECT_EQ(ctx, CONST_INT, proto->consts[0].kind);
  ms_u8 ops[12];
  int32_t operands[12];
  EXPECT_EQ(ctx, 8, decodeAll(proto, ops, operands, 12));
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[0]);
  EXPECT_EQ(ctx, OP_STORE_LOCAL, ops[1]);
  EXPECT_EQ(ctx, 0, operands[1]);
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[2]);
  EXPECT_EQ(ctx, 0, operands[2]);
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[3]);
  EXPECT_EQ(ctx, OP_BINARY_ADD, ops[4]);
  EXPECT_EQ(ctx, OP_STORE_LOCAL, ops[5]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[6]);  // epilogue
  EXPECT_EQ(ctx, OP_RETURN, ops[7]);
  chunkFree(&c);
}

TEST(funcProtoWithReturnHasNoEpilogue) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func add(a, b) { return a + b }", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, CONST_PROTO, c.consts[0].kind);
  const Chunk* proto = c.consts[0].as.proto;
  EXPECT(ctx, proto->nameLen == 3 && strncmp(proto->name, "add", 3) == 0);
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 4, decodeAll(proto, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[0]);
  EXPECT_EQ(ctx, 0, operands[0]);  // a → slot 0
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[1]);
  EXPECT_EQ(ctx, 1, operands[1]);  // b → slot 1
  EXPECT_EQ(ctx, OP_BINARY_ADD, ops[2]);
  EXPECT_EQ(ctx, OP_RETURN, ops[3]);  // 末指令 RETURN：不补 epilogue
  ms_u8 mods[8];
  int32_t modOperands[8];
  EXPECT_EQ(ctx, 6, decodeAll(&c, mods, modOperands, 8));
  EXPECT_EQ(ctx, OP_LOAD_CONST, mods[0]);  // proto
  EXPECT_EQ(ctx, OP_MAKE_FUNCTION, mods[1]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, mods[2]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, mods[3]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, mods[4]);
  EXPECT_EQ(ctx, OP_RETURN, mods[5]);
  chunkFree(&c);
}

TEST(emptyFuncBodyGetsEpilogue) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func f() {}", &c, &diags));
  EXPECT_EQ(ctx, CONST_PROTO, c.consts[0].kind);
  const Chunk* proto = c.consts[0].as.proto;
  ms_u8 ops[4];
  int32_t operands[4];
  EXPECT_EQ(ctx, 2, decodeAll(proto, ops, operands, 4));
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[0]);
  EXPECT_EQ(ctx, OP_RETURN, ops[1]);
  chunkFree(&c);
}

TEST(unsupportedStmtReportsDiag) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, !compileSrc("if true { var x = 1 }", &c, &diags));
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_COMPILE_UNSUPPORTED, (int)diags.items[0].code);
  chunkFree(&c);
}

TEST(interpStringUnsupported) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, !compileSrc("var s = \"a${b}c\"", &c, &diags));
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_COMPILE_UNSUPPORTED, (int)diags.items[0].code);
  chunkFree(&c);
}

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      constDedupByValue,
      protoNeverDeduped,
      lineTableRle,
      opcodeTableComplete,
      constLoadSingle,
      binaryOnGlobals,
      globalReadWrite,
      compoundAssignExpands,
      noInitVarDefaultsNil,
      localReadWriteInFunc,
      funcProtoWithReturnHasNoEpilogue,
      emptyFuncBodyGetsEpilogue,
      unsupportedStmtReportsDiag,
      interpStringUnsupported,
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
