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

TEST(unsupportedStmtReportsDiag) {  // T011 起 if 等控制流已支持；class 编译归 T020
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, !compileSrc("class A {}", &c, &diags));
  EXPECT_EQ(ctx, 1, diags.count);
  EXPECT_EQ(ctx, MSE_COMPILE_UNSUPPORTED, (int)diags.items[0].code);
  chunkFree(&c);
}

TEST(interpStringCompilesToFormatString) {  // Task 11：占位报错移除；CONST_INTERP 深拷 + FORMAT_STRING
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var s = \"a\\t${b}c\"", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, CONST_INTERP, c.consts[0].kind);  // interp 常量先入池
  const ConstEntry* e = &c.consts[0];
  EXPECT_EQ(ctx, 3, e->as.interp.count);
  EXPECT(ctx, !e->as.interp.segs[0].isExpr);  // 字面段编译期解码（\t → 0x09）且自持副本
  EXPECT_EQ(ctx, 2, e->as.interp.segs[0].len);
  EXPECT(ctx, e->as.interp.segs[0].start[0] == 'a' && e->as.interp.segs[0].start[1] == '\t');
  EXPECT(ctx, e->as.interp.segs[1].isExpr);
  EXPECT(ctx, !e->as.interp.segs[2].isExpr && e->as.interp.segs[2].len == 1 &&
              e->as.interp.segs[2].start[0] == 'c');
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 6, decodeAll(&c, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[0]);  // 表达式值先行压栈
  EXPECT_EQ(ctx, OP_FORMAT_STRING, ops[1]);
  EXPECT_EQ(ctx, 0, operands[1]);  // 指向 interp 常量
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[2]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[3]);
  chunkFree(&c);  // 字面段字节随常量池释放（深拷自持）
}

// ---------- 短路 / 三元 / 跳转回填与 EXTENDED_ARG ----------

TEST(shortCircuitAnd) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func f(a, b) { return a && b }", &c, &diags));
  const Chunk* proto = c.consts[0].as.proto;
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 4, decodeAll(proto, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[0]);
  EXPECT_EQ(ctx, OP_JMP_IF_FALSE_OR_POP, ops[1]);
  EXPECT_EQ(ctx, 6, operands[1]);  // 跳到 RETURN（rhs 后）
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[2]);
  EXPECT_EQ(ctx, OP_RETURN, ops[3]);
  EXPECT_EQ(ctx, 7, proto->codeLen);
  chunkFree(&c);
}

TEST(shortCircuitOr) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func f(a, b) { return a || b }", &c, &diags));
  const Chunk* proto = c.consts[0].as.proto;
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 4, decodeAll(proto, ops, operands, 8));
  EXPECT_EQ(ctx, OP_JMP_IF_TRUE_OR_POP, ops[1]);
  chunkFree(&c);
}

TEST(ternaryJumpShape) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func f(a, b, c) { return a ? b : c }", &c, &diags));
  const Chunk* proto = c.consts[0].as.proto;
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 6, decodeAll(proto, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[0]);
  EXPECT_EQ(ctx, OP_JMP_IF_FALSE, ops[1]);
  EXPECT_EQ(ctx, 8, operands[1]);  // Lelse = else 分支
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[2]);
  EXPECT_EQ(ctx, OP_JMP, ops[3]);
  EXPECT_EQ(ctx, 10, operands[3]);  // Lend = 末尾
  EXPECT_EQ(ctx, OP_LOAD_LOCAL, ops[4]);
  EXPECT_EQ(ctx, OP_RETURN, ops[5]);
  EXPECT_EQ(ctx, 11, proto->codeLen);
  chunkFree(&c);
}

// 生成 n 个 var 声明（常量 0..n-1 互异），供大常量池/大跳距用例
static void genDecls(char* buf, size_t cap, int32_t n, const char* tail) {
  size_t pos = 0;
  for (int32_t i = 0; i < n; i++) {
    int written = snprintf(buf + pos, cap - pos, "var v%d = %d\n", i, i);
    MS_ASSERT(written > 0 && (size_t)written < cap - pos);
    pos += (size_t)written;
  }
  snprintf(buf + pos, cap - pos, "%s", tail);
}

TEST(extArgLargeConstIndex) {
  char src[16 * 1024];
  genDecls(src, sizeof(src), 300, "");
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc(src, &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 600, c.constsLen);  // 300 int + 300 名字
  // 常量索引 256 → EXTENDED_ARG 0x01 前缀 + LOAD_CONST 0x00
  bool found = false;
  for (int32_t i = 0; i + 3 < c.codeLen; i++) {
    if (c.code[i] == OP_EXTENDED_ARG && c.code[i + 1] == 0x01 && c.code[i + 2] == OP_LOAD_CONST &&
        c.code[i + 3] == 0x00) {
      found = true;
      break;
    }
  }
  EXPECT(ctx, found);
  // 解码器解析 EXT 链后操作数 == 256
  ms_u8 ops[2048];
  int32_t operands[2048];
  int32_t n = decodeAll(&c, ops, operands, 2048);
  EXPECT(ctx, n > 900);
  bool resolved = false;
  for (int32_t i = 0; i < n; i++) {
    if (ops[i] == OP_LOAD_CONST && operands[i] == 256) {
      resolved = true;
      break;
    }
  }
  EXPECT(ctx, resolved);
  chunkFree(&c);
}

TEST(extArgLargeJumpDistance) {
  char src[16 * 1024];
  genDecls(src, sizeof(src), 300, "var r = v0 ? v1 : v299\n");
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc(src, &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  ms_u8 ops[4096];
  int32_t operands[4096];
  int32_t n = decodeAll(&c, ops, operands, 4096);
  // 尾部三元的两个跳转目标均 > 255（EXTENDED_ARG 物化）
  bool farFalse = false;
  bool farEnd = false;
  for (int32_t i = n - 8; i < n; i++) {
    if (i < 0) {
      continue;
    }
    if (ops[i] == OP_JMP_IF_FALSE && operands[i] >= 256) {
      farFalse = true;
    }
    if (ops[i] == OP_JMP && operands[i] >= 256) {
      farEnd = true;
    }
  }
  EXPECT(ctx, farFalse);
  EXPECT(ctx, farEnd);
  chunkFree(&c);
}

// ---------- 调用桩（21 §4 示例形态） ----------

TEST(callStubPrint) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("print(1)", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  ms_u8 ops[8];
  int32_t operands[8];
  EXPECT_EQ(ctx, 6, decodeAll(&c, ops, operands, 8));
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[0]);  // "print"
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[1]);   // 1
  EXPECT_EQ(ctx, OP_CALL, ops[2]);
  EXPECT_EQ(ctx, 1, operands[2]);
  EXPECT_EQ(ctx, OP_POP, ops[3]);  // 表达式语句丢弃返回值
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[4]);
  EXPECT_EQ(ctx, OP_RETURN, ops[5]);
  chunkFree(&c);
}

TEST(callStubNested) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func add(a, b) { return a + b }\nprint(add(1, 2))", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  EXPECT_EQ(ctx, 5, c.constsLen);  // {proto add, "add", "print", int 1, int 2}
  ms_u8 ops[16];
  int32_t operands[16];
  int32_t n = decodeAll(&c, ops, operands, 16);
  // func 声明：LOAD_CONST proto / MAKE_FUNCTION / DEF_GLOBAL / STORE_GLOBAL
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[0]);
  EXPECT_EQ(ctx, OP_MAKE_FUNCTION, ops[1]);
  EXPECT_EQ(ctx, OP_DEF_GLOBAL, ops[2]);
  EXPECT_EQ(ctx, OP_STORE_GLOBAL, ops[3]);
  // print(add(1, 2))：LOAD_GLOBAL print / LOAD_GLOBAL add / 1 / 2 / CALL 2 / CALL 1 / POP
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[4]);
  EXPECT_EQ(ctx, OP_LOAD_GLOBAL, ops[5]);
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[6]);
  EXPECT_EQ(ctx, OP_LOAD_CONST, ops[7]);
  EXPECT_EQ(ctx, OP_CALL, ops[8]);
  EXPECT_EQ(ctx, 2, operands[8]);
  EXPECT_EQ(ctx, OP_CALL, ops[9]);
  EXPECT_EQ(ctx, 1, operands[9]);
  EXPECT_EQ(ctx, OP_POP, ops[10]);
  EXPECT_EQ(ctx, OP_LOAD_NIL, ops[11]);
  EXPECT_EQ(ctx, OP_RETURN, ops[12]);
  EXPECT_EQ(ctx, 13, n);
  chunkFree(&c);
}

// ---------- peephole（NOP 压实 + JMP 链折叠） ----------

TEST(peepholeNopCompaction) {
  Chunk c;
  chunkInit(&c, "main", 4);
  chunkEmitOp(&c, OP_NOP, 1);        // 0
  chunkEmitOp(&c, OP_NOP, 1);        // 1
  chunkEmitOp(&c, OP_LOAD_TRUE, 2);  // 2
  chunkEmitOp(&c, OP_JMP, 2);        // 3 → target 0（首 NOP）
  chunkEmitByte(&c, 0);
  ChunkJump jumps[1] = {{3, 0}};
  chunkPeephole(&c, jumps, 1);
  EXPECT_EQ(ctx, 3, c.codeLen);  // NOP×2 压实
  EXPECT_EQ(ctx, OP_LOAD_TRUE, c.code[0]);
  EXPECT_EQ(ctx, OP_JMP, c.code[1]);
  EXPECT_EQ(ctx, 0, c.code[2]);  // 目标重定向到 LOAD_TRUE 新偏移
  EXPECT_EQ(ctx, 1, jumps[0].instrOffset);
  EXPECT_EQ(ctx, 0, jumps[0].target);
  chunkFree(&c);
}

TEST(peepholeJumpChainFold) {
  Chunk c;
  chunkInit(&c, "main", 4);
  chunkEmitOp(&c, OP_LOAD_NIL, 1);  // 0
  chunkEmitOp(&c, OP_JMP, 1);       // 1 → 4
  chunkEmitByte(&c, 4);
  chunkEmitOp(&c, OP_LOAD_TRUE, 1);  // 3
  chunkEmitOp(&c, OP_JMP, 1);        // 4 → 6（中转跳转）
  chunkEmitByte(&c, 6);
  chunkEmitOp(&c, OP_RETURN, 1);  // 6
  ChunkJump jumps[2] = {{1, 4}, {4, 6}};
  chunkPeephole(&c, jumps, 2);
  EXPECT_EQ(ctx, 6, jumps[0].target);  // 直达最终目标
  EXPECT_EQ(ctx, 6, jumps[1].target);
  chunkFree(&c);
}

TEST(peepholeMutualJumpsTerminate) {
  Chunk c;
  chunkInit(&c, "main", 4);
  chunkEmitOp(&c, OP_JMP, 1);  // 0 → 2
  chunkEmitByte(&c, 2);
  chunkEmitOp(&c, OP_JMP, 1);  // 2 → 0（互相成环）
  chunkEmitByte(&c, 0);
  chunkEmitOp(&c, OP_RETURN, 1);  // 4
  ChunkJump jumps[2] = {{0, 2}, {2, 0}};
  chunkPeephole(&c, jumps, 2);  // 不死循环即通过
  EXPECT_EQ(ctx, 5, c.codeLen);
  chunkFree(&c);
}

static bool isJumpOp(ms_u8 op) {
  return op == OP_JMP || op == OP_JMP_IF_FALSE || op == OP_JMP_IF_TRUE || op == OP_JMP_IF_FALSE_OR_POP ||
         op == OP_JMP_IF_TRUE_OR_POP || op == OP_POP_JUMP_IF_NIL || op == OP_FOR_ITER;
}

TEST(extArgJumpTargetsOnBoundaries) {  // T011 回归：EXT 高位字节物化错位修复
  char src[16 * 1024];
  genDecls(src, sizeof(src), 300, "var r = v0 ? v1 : v299\n");
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc(src, &c, &diags));
  // 指令边界集合 + 每条跳转目标必须落在边界上（错误载荷会指向操作数字节中间）
  bool* boundary = calloc((size_t)c.codeLen + 1, 1);
  int32_t off = 0;
  while (off < c.codeLen) {
    boundary[off] = true;
    ms_u8 op = 0;
    int32_t operand = -1;
    off += decodeInstr(&c, off, &op, &operand);
  }
  off = 0;
  while (off < c.codeLen) {
    ms_u8 op = 0;
    int32_t operand = -1;
    decodeInstr(&c, off, &op, &operand);
    if (isJumpOp(op)) {
      EXPECT(ctx, operand < c.codeLen && boundary[operand]);
    }
    ms_u8 dummyOp = 0;
    int32_t dummyOperand = -1;
    off += decodeInstr(&c, off, &dummyOp, &dummyOperand);
  }
  free(boundary);
  chunkFree(&c);
}

TEST(nestedTernaryFold) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var r = a ? (b ? 1 : 2) : 3", &c, &diags));
  EXPECT_EQ(ctx, 0, diags.count);
  int32_t off = 0;
  bool noJumpToJump = true;
  while (off < c.codeLen) {
    ms_u8 op = 0;
    int32_t operand = -1;
    int32_t size = decodeInstr(&c, off, &op, &operand);
    if (isJumpOp(op)) {
      ms_u8 targetOp = 0;
      int32_t targetOperand = -1;
      decodeInstr(&c, operand, &targetOp, &targetOperand);
      if (targetOp == OP_JMP) {
        noJumpToJump = false;  // 折叠后不应存在跳转→JMP
      }
    }
    off += size;
  }
  EXPECT_EQ(ctx, c.codeLen, off);  // 解码走齐 = 指令边界对齐
  EXPECT(ctx, noJumpToJump);
  chunkFree(&c);
}

// ---------- 反汇编 ----------

// chunk 树 → 文本（临时文件回读，避开平台 fmemopen 差异）
static char* disasmToBuffer(const Chunk* c) {
  FILE* f = tmpfile();
  if (f == NULL) {
    return NULL;
  }
  compilerDisasm(c, f);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = malloc((size_t)len + 1);
  size_t got = fread(buf, 1, (size_t)len, f);
  buf[got] = '\0';
  fclose(f);
  return buf;
}

static bool containsLine(const char* text, const char* line) { return strstr(text, line) != NULL; }

// 与 disasm 相同的 %-16s 填充构造指令行 needle（rest 形如 " 0 ; 1"）
static bool hasInstrLine(const char* text, const char* name, const char* rest) {
  char needle[96];
  snprintf(needle, sizeof(needle), "%-16s%s", name, rest);
  return strstr(text, needle) != NULL;
}

TEST(disasmBasicFormat) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var a = 1 + x", &c, &diags));
  char* text = disasmToBuffer(&c);
  EXPECT(ctx, text != NULL);
  EXPECT(ctx, containsLine(text, "; main"));
  EXPECT(ctx, containsLine(text, "consts 3"));
  EXPECT(ctx, containsLine(text, "0 int 1"));
  EXPECT(ctx, containsLine(text, "1 string \"x\""));
  EXPECT(ctx, containsLine(text, "code "));
  EXPECT(ctx, hasInstrLine(text, "LOAD_CONST", " 0 ; 1"));
  EXPECT(ctx, hasInstrLine(text, "LOAD_GLOBAL", " 1 ; \"x\""));
  EXPECT(ctx, containsLine(text, "BINARY_ADD"));
  EXPECT(ctx, hasInstrLine(text, "DEF_GLOBAL", " 2 ; \"a\""));
  EXPECT(ctx, containsLine(text, "upvalues 0"));
  EXPECT(ctx, containsLine(text, "icslots 0"));
  EXPECT(ctx, containsLine(text, "lines "));
  free(text);
  chunkFree(&c);
}

TEST(disasmFuncProtoSections) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("func add(a, b) { return a + b }\nprint(add(1, 2))", &c, &diags));
  char* text = disasmToBuffer(&c);
  EXPECT(ctx, text != NULL);
  EXPECT(ctx, containsLine(text, "; func add"));
  EXPECT(ctx, hasInstrLine(text, "LOAD_LOCAL", " 0"));
  EXPECT(ctx, hasInstrLine(text, "LOAD_LOCAL", " 1"));
  EXPECT(ctx, hasInstrLine(text, "MAKE_FUNCTION", " 0"));
  EXPECT(ctx, hasInstrLine(text, "CALL", " 2"));
  EXPECT(ctx, hasInstrLine(text, "CALL", " 1"));
  free(text);
  chunkFree(&c);
}

TEST(disasmJumpAnnotation) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var r = a ? b : c", &c, &diags));
  char* text = disasmToBuffer(&c);
  EXPECT(ctx, text != NULL);
  EXPECT(ctx, hasInstrLine(text, "JMP_IF_FALSE", " 8 ; -> 0008"));
  EXPECT(ctx, hasInstrLine(text, "JMP", " 10 ; -> 0010"));
  free(text);
  chunkFree(&c);
}

TEST(disasmDeterministicAndEscaped) {
  MsDiagSink diags = {0};
  Chunk c;
  EXPECT(ctx, compileSrc("var s = \"a\\nb\"\nvar r = x ? y : z", &c, &diags));
  char* a = disasmToBuffer(&c);
  char* b2 = disasmToBuffer(&c);
  EXPECT(ctx, a != NULL && b2 != NULL);
  EXPECT_STREQ(ctx, a, b2);
  EXPECT(ctx, containsLine(a, "string \"a\\\\nb\""));  // 源内反斜杠转义渲染
  free(a);
  free(b2);
  chunkFree(&c);
}

TEST(compileIsDeterministic) {
  char src[16 * 1024];
  genDecls(src, sizeof(src), 120, "var r = a ? (b ? 1 : 2) : 3\n");
  Chunk a;
  Chunk b2;
  MsDiagSink diags1 = {0};
  MsDiagSink diags2 = {0};
  EXPECT(ctx, compileSrc(src, &a, &diags1));
  EXPECT(ctx, compileSrc(src, &b2, &diags2));
  EXPECT_EQ(ctx, a.codeLen, b2.codeLen);
  EXPECT(ctx, memcmp(a.code, b2.code, (size_t)a.codeLen) == 0);
  EXPECT_EQ(ctx, a.constsLen, b2.constsLen);
  EXPECT_EQ(ctx, a.linesLen, b2.linesLen);
  EXPECT(ctx, memcmp(a.lines, b2.lines, (size_t)a.linesLen * sizeof(SourceRun)) == 0);
  chunkFree(&a);
  chunkFree(&b2);
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
      interpStringCompilesToFormatString,
      shortCircuitAnd,
      shortCircuitOr,
      ternaryJumpShape,
      extArgLargeConstIndex,
      extArgLargeJumpDistance,
      extArgJumpTargetsOnBoundaries,
      callStubPrint,
      callStubNested,
      peepholeNopCompaction,
      peepholeJumpChainFold,
      peepholeMutualJumpsTerminate,
      nestedTernaryFold,
      disasmBasicFormat,
      disasmFuncProtoSections,
      disasmJumpAnnotation,
      disasmDeterministicAndEscaped,
      compileIsDeterministic,
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
