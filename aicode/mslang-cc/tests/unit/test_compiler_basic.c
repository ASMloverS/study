// T010：Compiler/Chunk 单测（docs/tasks/T010 §5.1）
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binder/binder.h"
#include "compiler/chunk.h"
#include "parser/parser.h"
#include "test_harness.h"

// ---------- Chunk 基础（常量去重 / 行号 RLE） ----------

TEST(constDedupByValue) {
  Chunk c;
  chunkInit(&c, "main");
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
  chunkInit(&c, "main");
  Chunk* p1 = malloc(sizeof(Chunk));
  Chunk* p2 = malloc(sizeof(Chunk));
  chunkInit(p1, "f");
  chunkInit(p2, "f");
  int32_t i1 = chunkAddConstProto(&c, p1);
  int32_t i2 = chunkAddConstProto(&c, p2);
  EXPECT_EQ(ctx, 0, i1);
  EXPECT_EQ(ctx, 1, i2);
  EXPECT_EQ(ctx, 2, c.constsLen);
  chunkFree(&c);  // 递归释放 p1/p2
}

TEST(lineTableRle) {
  Chunk c;
  chunkInit(&c, "main");
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

int main(int argc, char** argv) {
  TestCtx ctx = {0};
  int only = argc > 1 ? atoi(argv[1]) : -1;
  typedef void (*TestFn)(TestCtx*);
  const TestFn tests[] = {
      constDedupByValue,
      protoNeverDeduped,
      lineTableRle,
      opcodeTableComplete,
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
