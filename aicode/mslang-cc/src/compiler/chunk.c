// Chunk 实现（T010）：发射、常量池去重、行号 RLE；指令元数据表全集（21 §3）
#include "compiler/chunk.h"

#include <string.h>

// 名字与布局均按 MsOpcode 枚举序一一对应；新增指令须同步三处
const char* const msOpNames[OP__COUNT] = {
    "NOP",         "EXTENDED_ARG",     "POP",         "DUP",           "DUP2",
    "ROT",         "SWAP",             "LOAD_NIL",    "LOAD_TRUE",     "LOAD_FALSE",
    "LOAD_CONST",  "LOAD_LOCAL",       "STORE_LOCAL", "LOAD_GLOBAL",   "STORE_GLOBAL",
    "DEF_GLOBAL",  "LOAD_UPVAL",       "STORE_UPVAL", "LOAD_GLOBAL_IC", "BINARY_ADD",
    "BINARY_SUB",  "BINARY_MUL",       "BINARY_DIV",  "BINARY_MOD",    "BINARY_POW",
    "BINARY_AND",  "BINARY_OR",        "BINARY_XOR",  "BINARY_SHL",    "BINARY_SHR",
    "UNARY_NEG",   "UNARY_NOT",        "UNARY_INVERT", "COMPARE_EQ",   "COMPARE_NE",
    "COMPARE_LT",  "COMPARE_LE",       "COMPARE_GT",  "COMPARE_GE",    "IS",
    "IN",          "GET_INDEX",        "SET_INDEX",   "BUILD_SLICE",   "MAKE_FUNCTION",
    "CLOSURE",     "CALL",             "CALL_METHOD", "RETURN",        "NEW_CLASS",
    "DEFINE_METHOD", "INHERIT_INIT",   "GET_ATTR",    "SET_ATTR",      "JMP",
    "JMP_IF_FALSE", "JMP_IF_TRUE",     "JMP_IF_FALSE_OR_POP", "JMP_IF_TRUE_OR_POP", "POP_JUMP_IF_NIL",
    "SWITCH_TABLE", "TYPE_SWITCH_TABLE", "CHECK_TYPE", "GET_ITER",     "FOR_ITER",
    "YIELD",       "RESUME",           "PUSH_TRY",    "POP_TRY",       "THROW",
    "MATCH_CATCH", "PUSH_DEFER",       "RUN_DEFERS",  "RERAISE",       "MAKE_CHAN",
    "SEND",        "RECV",             "CLOSE_CHAN",  "SELECT_SETUP",  "SELECT_ARM",
    "SELECT_GO",   "SELECT_COMMIT",    "SPAWN",       "AWAIT",         "BUILD_LIST",
    "BUILD_MAP",   "BUILD_TUPLE",      "BUILD_SET",   "UNPACK_SEQ",    "UNPACK_STAR",
    "FORMAT_STRING", "CHECK_SAFEPOINT", "ASSERT",
};

const MsOpndLayout msOpLayouts[OP__COUNT] = {
    OPL_NONE,     OPL_U8,          OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_CONST,    OPL_U8,          OPL_U8,       OPL_CONST,         OPL_CONST,
    OPL_CONST,    OPL_U8,          OPL_U8,       OPL_CONST_U8,      OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_NONE,     OPL_NONE,        OPL_NONE,     OPL_U8,            OPL_CONST_U8,
    OPL_CONST,    OPL_U8,          OPL_CONST_U8_U8, OPL_NONE,      OPL_CONST_CONST,
    OPL_CONST_U8, OPL_NONE,        OPL_CONST_U8, OPL_CONST,        OPL_JUMP,
    OPL_JUMP,     OPL_JUMP,        OPL_JUMP,     OPL_JUMP,          OPL_JUMP,
    OPL_JUMP,     OPL_SPECIAL,     OPL_SPECIAL,  OPL_CONST,         OPL_NONE,
    OPL_JUMP,     OPL_NONE,        OPL_NONE,     OPL_JUMP_JUMP,     OPL_NONE,
    OPL_NONE,     OPL_CONST,       OPL_CONST,    OPL_U8,            OPL_NONE,
    OPL_U8,       OPL_NONE,        OPL_NONE,     OPL_NONE,          OPL_U8,
    OPL_U8,       OPL_SPECIAL,     OPL_NONE,     OPL_NONE,          OPL_NONE,
    OPL_U8,       OPL_U8,          OPL_U8,       OPL_U8,            OPL_U8,
    OPL_U8,       OPL_CONST,       OPL_NONE,     OPL_SPECIAL,
};

void chunkInit(Chunk* c, const char* name) {
  c->code = NULL;
  c->codeLen = c->codeCap = 0;
  c->consts = NULL;
  c->constsLen = c->constsCap = 0;
  c->lines = NULL;
  c->linesLen = c->linesCap = 0;
  c->nUpvalues = 0;
  c->nIcSlots = 0;
  c->name = name;
}

void chunkFree(Chunk* c) {
  for (int32_t i = 0; i < c->constsLen; i++) {
    if (c->consts[i].kind == CONST_PROTO) {
      chunkFree(c->consts[i].as.proto);
      free(c->consts[i].as.proto);
    }
  }
  free(c->code);
  free(c->consts);
  free(c->lines);
  c->code = NULL;
  c->consts = NULL;
  c->lines = NULL;
  c->codeLen = c->codeCap = 0;
  c->constsLen = c->constsCap = 0;
  c->linesLen = c->linesCap = 0;
}

// 字面量按值去重（float 按位比较，NaN 仅同比特模式合并）；proto 恒追加
static bool constEquals(const ConstEntry* a, const ConstEntry* b) {
  if (a->kind != b->kind) {
    return false;
  }
  switch (a->kind) {
    case CONST_NIL:
      return true;
    case CONST_BOOL:
      return a->as.b == b->as.b;
    case CONST_INT:
      return a->as.i == b->as.i;
    case CONST_FLOAT:
      return memcmp(&a->as.f, &b->as.f, sizeof(double)) == 0;
    case CONST_RUNE:
      return a->as.cp == b->as.cp;
    case CONST_BIGINT:
    case CONST_STRING:
      return a->as.text.len == b->as.text.len &&
             memcmp(a->as.text.start, b->as.text.start, (size_t)a->as.text.len) == 0;
    case CONST_PROTO:
      return false;
  }
  return false;
}

int32_t chunkAddConst(Chunk* c, ConstEntry entry) {
  for (int32_t i = 0; i < c->constsLen; i++) {
    if (constEquals(&c->consts[i], &entry)) {
      return i;
    }
  }
  MS_ARRAY_PUSH(c->consts, c->constsLen, c->constsCap, entry);
  return c->constsLen - 1;
}

int32_t chunkAddConstInt(Chunk* c, ms_i64 v) {
  ConstEntry e = {CONST_INT, {0}};
  e.as.i = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstFloat(Chunk* c, double v) {
  ConstEntry e = {CONST_FLOAT, {0}};
  e.as.f = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstBool(Chunk* c, bool v) {
  ConstEntry e = {CONST_BOOL, {0}};
  e.as.b = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstRune(Chunk* c, ms_u32 cp) {
  ConstEntry e = {CONST_RUNE, {0}};
  e.as.cp = cp;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstText(Chunk* c, ConstKind kind, const char* start, int32_t len) {
  ConstEntry e = {kind, {0}};
  e.as.text.start = start;
  e.as.text.len = len;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstProto(Chunk* c, Chunk* proto) {
  ConstEntry e = {CONST_PROTO, {0}};
  e.as.proto = proto;
  MS_ARRAY_PUSH(c->consts, c->constsLen, c->constsCap, e);
  return c->constsLen - 1;
}

int32_t chunkEmitOp(Chunk* c, ms_u8 op, int32_t line) {
  int32_t offset = c->codeLen;
  chunkEmitByte(c, op);
  // RLE：源行随发射序单调，仅行变化处落 run
  if (c->linesLen == 0 || c->lines[c->linesLen - 1].line != line) {
    SourceRun run = {offset, line};
    MS_ARRAY_PUSH(c->lines, c->linesLen, c->linesCap, run);
  }
  return offset;
}

void chunkEmitByte(Chunk* c, ms_u8 b) { MS_ARRAY_PUSH(c->code, c->codeLen, c->codeCap, b); }

int32_t chunkLineAt(const Chunk* c, int32_t offset) {
  int32_t lo = 0;
  int32_t hi = c->linesLen - 1;
  int32_t found = -1;
  while (lo <= hi) {
    int32_t mid = lo + (hi - lo) / 2;
    if (c->lines[mid].offset <= offset) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return found >= 0 ? c->lines[found].line : -1;
}

void chunkPeephole(Chunk* c, ChunkJump* jumps, int32_t jumpCount) {
  // 占位（T010 计划 Task 6）：NOP 压实与 JMP→JMP 链折叠（21 §2 peephole）
  (void)c;
  (void)jumps;
  (void)jumpCount;
}
