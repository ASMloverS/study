// Chunk 实现（T010）：发射、常量池去重、行号 RLE；指令元数据表全集（21 §3）
#include "compiler/chunk.h"

#include <string.h>

// 名字与布局均按 MsOpcode 枚举序一一对应；新增指令须同步三处
const char* const msOpNames[OP__COUNT] = {
    "NOP",
    "EXTENDED_ARG",
    "POP",
    "DUP",
    "DUP2",
    "ROT",
    "SWAP",
    "LOAD_NIL",
    "LOAD_TRUE",
    "LOAD_FALSE",
    "LOAD_CONST",
    "LOAD_LOCAL",
    "STORE_LOCAL",
    "LOAD_GLOBAL",
    "STORE_GLOBAL",
    "DEF_GLOBAL",
    "LOAD_UPVAL",
    "STORE_UPVAL",
    "LOAD_GLOBAL_IC",
    "BINARY_ADD",
    "BINARY_SUB",
    "BINARY_MUL",
    "BINARY_DIV",
    "BINARY_MOD",
    "BINARY_POW",
    "BINARY_AND",
    "BINARY_OR",
    "BINARY_XOR",
    "BINARY_SHL",
    "BINARY_SHR",
    "UNARY_NEG",
    "UNARY_NOT",
    "UNARY_INVERT",
    "COMPARE_EQ",
    "COMPARE_NE",
    "COMPARE_LT",
    "COMPARE_LE",
    "COMPARE_GT",
    "COMPARE_GE",
    "IS",
    "IN",
    "GET_INDEX",
    "SET_INDEX",
    "BUILD_SLICE",
    "MAKE_FUNCTION",
    "CLOSURE",
    "CALL",
    "CALL_METHOD",
    "RETURN",
    "NEW_CLASS",
    "DEFINE_METHOD",
    "INHERIT_INIT",
    "GET_ATTR",
    "SET_ATTR",
    "JMP",
    "JMP_IF_FALSE",
    "JMP_IF_TRUE",
    "JMP_IF_FALSE_OR_POP",
    "JMP_IF_TRUE_OR_POP",
    "POP_JUMP_IF_NIL",
    "SWITCH_TABLE",
    "TYPE_SWITCH_TABLE",
    "CHECK_TYPE",
    "GET_ITER",
    "FOR_ITER",
    "YIELD",
    "RESUME",
    "PUSH_TRY",
    "POP_TRY",
    "THROW",
    "MATCH_CATCH",
    "PUSH_DEFER",
    "RUN_DEFERS",
    "RERAISE",
    "MAKE_CHAN",
    "SEND",
    "RECV",
    "CLOSE_CHAN",
    "SELECT_SETUP",
    "SELECT_ARM",
    "SELECT_GO",
    "SELECT_COMMIT",
    "SPAWN",
    "AWAIT",
    "BUILD_LIST",
    "BUILD_MAP",
    "BUILD_TUPLE",
    "BUILD_SET",
    "UNPACK_SEQ",
    "UNPACK_STAR",
    "FORMAT_STRING",
    "CHECK_SAFEPOINT",
    "ASSERT",
};

const MsOpndLayout msOpLayouts[OP__COUNT] = {
    OPL_NONE,            // NOP
    OPL_U8,              // EXTENDED_ARG
    OPL_NONE,            // POP
    OPL_NONE,            // DUP
    OPL_NONE,            // DUP2
    OPL_NONE,            // ROT
    OPL_NONE,            // SWAP
    OPL_NONE,            // LOAD_NIL
    OPL_NONE,            // LOAD_TRUE
    OPL_NONE,            // LOAD_FALSE
    OPL_CONST,           // LOAD_CONST
    OPL_U8,              // LOAD_LOCAL
    OPL_U8,              // STORE_LOCAL
    OPL_CONST,           // LOAD_GLOBAL
    OPL_CONST,           // STORE_GLOBAL
    OPL_CONST,           // DEF_GLOBAL
    OPL_U8,              // LOAD_UPVAL
    OPL_U8,              // STORE_UPVAL
    OPL_CONST_U8,        // LOAD_GLOBAL_IC
    OPL_NONE,            // BINARY_ADD
    OPL_NONE,            // BINARY_SUB
    OPL_NONE,            // BINARY_MUL
    OPL_NONE,            // BINARY_DIV
    OPL_NONE,            // BINARY_MOD
    OPL_NONE,            // BINARY_POW
    OPL_NONE,            // BINARY_AND
    OPL_NONE,            // BINARY_OR
    OPL_NONE,            // BINARY_XOR
    OPL_NONE,            // BINARY_SHL
    OPL_NONE,            // BINARY_SHR
    OPL_NONE,            // UNARY_NEG
    OPL_NONE,            // UNARY_NOT
    OPL_NONE,            // UNARY_INVERT
    OPL_NONE,            // COMPARE_EQ
    OPL_NONE,            // COMPARE_NE
    OPL_NONE,            // COMPARE_LT
    OPL_NONE,            // COMPARE_LE
    OPL_NONE,            // COMPARE_GT
    OPL_NONE,            // COMPARE_GE
    OPL_NONE,            // IS
    OPL_NONE,            // IN
    OPL_NONE,            // GET_INDEX
    OPL_NONE,            // SET_INDEX
    OPL_U8,              // BUILD_SLICE
    OPL_CONST_U8,        // MAKE_FUNCTION
    OPL_CONST,           // CLOSURE
    OPL_U8,              // CALL
    OPL_CONST_U8_U8,     // CALL_METHOD
    OPL_NONE,            // RETURN
    OPL_CONST_CONST,     // NEW_CLASS
    OPL_CONST_U8,        // DEFINE_METHOD
    OPL_NONE,            // INHERIT_INIT
    OPL_CONST_U8,        // GET_ATTR
    OPL_CONST,           // SET_ATTR
    OPL_JUMP,            // JMP
    OPL_JUMP,            // JMP_IF_FALSE
    OPL_JUMP,            // JMP_IF_TRUE
    OPL_JUMP,            // JMP_IF_FALSE_OR_POP
    OPL_JUMP,            // JMP_IF_TRUE_OR_POP
    OPL_JUMP,            // POP_JUMP_IF_NIL
    OPL_SPECIAL,         // SWITCH_TABLE
    OPL_SPECIAL,         // TYPE_SWITCH_TABLE
    OPL_CONST,           // CHECK_TYPE
    OPL_NONE,            // GET_ITER
    OPL_JUMP,            // FOR_ITER
    OPL_NONE,            // YIELD
    OPL_NONE,            // RESUME
    OPL_JUMP_JUMP,       // PUSH_TRY
    OPL_NONE,            // POP_TRY
    OPL_NONE,            // THROW
    OPL_CONST,           // MATCH_CATCH
    OPL_CONST,           // PUSH_DEFER
    OPL_U8,              // RUN_DEFERS
    OPL_NONE,            // RERAISE
    OPL_U8,              // MAKE_CHAN
    OPL_NONE,            // SEND
    OPL_NONE,            // RECV
    OPL_NONE,            // CLOSE_CHAN
    OPL_U8,              // SELECT_SETUP
    OPL_U8,              // SELECT_ARM
    OPL_SPECIAL,         // SELECT_GO
    OPL_NONE,            // SELECT_COMMIT
    OPL_NONE,            // SPAWN
    OPL_NONE,            // AWAIT
    OPL_U8,              // BUILD_LIST
    OPL_U8,              // BUILD_MAP
    OPL_U8,              // BUILD_TUPLE
    OPL_U8,              // BUILD_SET
    OPL_U8,              // UNPACK_SEQ
    OPL_U8,              // UNPACK_STAR
    OPL_CONST,           // FORMAT_STRING
    OPL_NONE,            // CHECK_SAFEPOINT
    OPL_SPECIAL,         // ASSERT
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
