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
    OPL_NONE,         // NOP
    OPL_U8,           // EXTENDED_ARG
    OPL_NONE,         // POP
    OPL_NONE,         // DUP
    OPL_NONE,         // DUP2
    OPL_NONE,         // ROT
    OPL_NONE,         // SWAP
    OPL_NONE,         // LOAD_NIL
    OPL_NONE,         // LOAD_TRUE
    OPL_NONE,         // LOAD_FALSE
    OPL_CONST,        // LOAD_CONST
    OPL_U8,           // LOAD_LOCAL
    OPL_U8,           // STORE_LOCAL
    OPL_CONST,        // LOAD_GLOBAL
    OPL_CONST,        // STORE_GLOBAL
    OPL_CONST,        // DEF_GLOBAL
    OPL_U8,           // LOAD_UPVAL
    OPL_U8,           // STORE_UPVAL
    OPL_CONST_U8,     // LOAD_GLOBAL_IC
    OPL_NONE,         // BINARY_ADD
    OPL_NONE,         // BINARY_SUB
    OPL_NONE,         // BINARY_MUL
    OPL_NONE,         // BINARY_DIV
    OPL_NONE,         // BINARY_MOD
    OPL_NONE,         // BINARY_POW
    OPL_NONE,         // BINARY_AND
    OPL_NONE,         // BINARY_OR
    OPL_NONE,         // BINARY_XOR
    OPL_NONE,         // BINARY_SHL
    OPL_NONE,         // BINARY_SHR
    OPL_NONE,         // UNARY_NEG
    OPL_NONE,         // UNARY_NOT
    OPL_NONE,         // UNARY_INVERT
    OPL_NONE,         // COMPARE_EQ
    OPL_NONE,         // COMPARE_NE
    OPL_NONE,         // COMPARE_LT
    OPL_NONE,         // COMPARE_LE
    OPL_NONE,         // COMPARE_GT
    OPL_NONE,         // COMPARE_GE
    OPL_NONE,         // IS
    OPL_NONE,         // IN
    OPL_NONE,         // GET_INDEX
    OPL_NONE,         // SET_INDEX
    OPL_U8,           // BUILD_SLICE
    OPL_U8,           // MAKE_FUNCTION
    OPL_CONST,        // CLOSURE
    OPL_U8,           // CALL
    OPL_CONST_U8_U8,  // CALL_METHOD
    OPL_NONE,         // RETURN
    OPL_CONST_CONST,  // NEW_CLASS
    OPL_CONST_U8,     // DEFINE_METHOD
    OPL_NONE,         // INHERIT_INIT
    OPL_CONST_U8,     // GET_ATTR
    OPL_CONST,        // SET_ATTR
    OPL_JUMP,         // JMP
    OPL_JUMP,         // JMP_IF_FALSE
    OPL_JUMP,         // JMP_IF_TRUE
    OPL_JUMP,         // JMP_IF_FALSE_OR_POP
    OPL_JUMP,         // JMP_IF_TRUE_OR_POP
    OPL_JUMP,         // POP_JUMP_IF_NIL
    OPL_SPECIAL,      // SWITCH_TABLE
    OPL_SPECIAL,      // TYPE_SWITCH_TABLE
    OPL_CONST,        // CHECK_TYPE
    OPL_NONE,         // GET_ITER
    OPL_JUMP,         // FOR_ITER
    OPL_NONE,         // YIELD
    OPL_NONE,         // RESUME
    OPL_JUMP_JUMP,    // PUSH_TRY
    OPL_NONE,         // POP_TRY
    OPL_NONE,         // THROW
    OPL_CONST,        // MATCH_CATCH
    OPL_CONST,        // PUSH_DEFER
    OPL_U8,           // RUN_DEFERS
    OPL_NONE,         // RERAISE
    OPL_U8,           // MAKE_CHAN
    OPL_NONE,         // SEND
    OPL_NONE,         // RECV
    OPL_NONE,         // CLOSE_CHAN
    OPL_U8,           // SELECT_SETUP
    OPL_U8,           // SELECT_ARM
    OPL_SPECIAL,      // SELECT_GO
    OPL_NONE,         // SELECT_COMMIT
    OPL_NONE,         // SPAWN
    OPL_NONE,         // AWAIT
    OPL_U8,           // BUILD_LIST
    OPL_U8,           // BUILD_MAP
    OPL_U8,           // BUILD_TUPLE
    OPL_U8,           // BUILD_SET
    OPL_U8,           // UNPACK_SEQ
    OPL_U8_U8,        // UNPACK_STAR
    OPL_CONST,        // FORMAT_STRING
    OPL_NONE,         // CHECK_SAFEPOINT
    OPL_SPECIAL,      // ASSERT
};

void chunkInit(Chunk* c, const char* name, int32_t nameLen) {
  c->code = NULL;
  c->codeLen = c->codeCap = 0;
  c->consts = NULL;
  c->constsLen = c->constsCap = 0;
  c->lines = NULL;
  c->linesLen = c->linesCap = 0;
  c->nUpvalues = 0;
  c->nIcSlots = 0;
  c->name = name;
  c->nameLen = nameLen;
}

void chunkFree(Chunk* c) {
  for (int32_t i = 0; i < c->constsLen; i++) {
    if (c->consts[i].kind == CONST_PROTO) {
      chunkFree(c->consts[i].as.proto);
      free(c->consts[i].as.proto);
    } else if (c->consts[i].kind == CONST_INTERP) {
      InterpSeg* segs = c->consts[i].as.interp.segs;
      for (int32_t s = 0; s < c->consts[i].as.interp.count; s++) {
        if (!segs[s].isExpr) {
          free((void*)segs[s].start);  // 字面段字节深拷贝自持（chunkAddConstInterp）
        }
      }
      free(segs);
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
    case CONST_RAWSTRING:
      return a->as.text.len == b->as.text.len &&
             memcmp(a->as.text.start, b->as.text.start, (size_t)a->as.text.len) == 0;
    case CONST_INTERP:
    case CONST_PROTO:
      return false;  // 恒追加（interp 原样不去重，proto 各自独立）
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
  ConstEntry e = {0};
  e.kind = CONST_INT;
  e.as.i = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstFloat(Chunk* c, double v) {
  ConstEntry e = {0};
  e.kind = CONST_FLOAT;
  e.as.f = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstBool(Chunk* c, bool v) {
  ConstEntry e = {0};
  e.kind = CONST_BOOL;
  e.as.b = v;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstRune(Chunk* c, ms_u32 cp) {
  ConstEntry e = {0};
  e.kind = CONST_RUNE;
  e.as.cp = cp;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstText(Chunk* c, ConstKind kind, const char* start, int32_t len) {
  ConstEntry e = {0};
  e.kind = kind;
  e.as.text.start = start;
  e.as.text.len = len;
  return chunkAddConst(c, e);
}

int32_t chunkAddConstProto(Chunk* c, Chunk* proto) {
  ConstEntry e = {0};
  e.kind = CONST_PROTO;
  e.as.proto = proto;
  MS_ARRAY_PUSH(c->consts, c->constsLen, c->constsCap, e);
  return c->constsLen - 1;
}

int32_t chunkAddConstInterp(Chunk* c, const InterpSeg* segs, int32_t count) {
  InterpSeg* copy = NULL;
  if (count > 0) {
    copy = malloc(sizeof(InterpSeg) * (size_t)count);
    MS_ASSERT(copy != NULL);
    for (int32_t i = 0; i < count; i++) {  // 深拷贝：字面段字节自持（chunk 拥有；调用方缓冲可释放）
      copy[i] = segs[i];
      if (!segs[i].isExpr) {
        char* bytes = malloc((size_t)segs[i].len + 1);  // +1：NUL 终止契约（len=0 防 malloc(0)）
        MS_ASSERT(bytes != NULL);
        memcpy(bytes, segs[i].start, (size_t)segs[i].len);
        bytes[segs[i].len] = '\0';
        copy[i].start = bytes;
      }
    }
  }
  ConstEntry e = {0};
  e.kind = CONST_INTERP;
  e.as.interp.segs = copy;
  e.as.interp.count = count;
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

static int32_t operandBytesOf(MsOpndLayout layout) {
  switch (layout) {
    case OPL_NONE:
    case OPL_SPECIAL:
      return 0;
    case OPL_U8:
    case OPL_CONST:
    case OPL_JUMP:
      return 1;
    case OPL_U8_U8:
    case OPL_CONST_U8:
    case OPL_CONST_CONST:
    case OPL_JUMP_JUMP:
      return 2;
    case OPL_CONST_U8_U8:
      return 3;
  }
  return 0;
}

int32_t chunkInstrSize(const Chunk* c, int32_t off) {
  int32_t p = off + 1;
  while (c->code[p - 1] == OP_EXTENDED_ARG) {
    p += 2;
  }
  return p - off + operandBytesOf(msOpLayouts[c->code[p - 1]]);
}

// ---- peephole（T010 §2）：NOP 压实 + JMP→JMP 链折叠 ----

// fixups 按 instrOffset 有序发射，但压实后仍用线性查（规模小、调用频度低）
static int32_t findJumpAt(const ChunkJump* jumps, int32_t n, int32_t offset) {
  for (int32_t i = 0; i < n; i++) {
    if (jumps[i].instrOffset == offset) {
      return i;
    }
  }
  return -1;
}

// 目标解析：跳过 NOP 链；落点为无条件 JMP 时直达其目标（步数上限防互相成环死循环）
static int32_t resolveTarget(const Chunk* c, const ChunkJump* jumps, int32_t n, int32_t target) {
  int32_t steps = 0;
  for (;;) {
    while (target < c->codeLen && c->code[target] == OP_NOP) {
      target += chunkInstrSize(c, target);
    }
    int32_t j = findJumpAt(jumps, n, target);
    if (j < 0 || c->code[target] != OP_JMP || jumps[j].target == target || steps++ > n) {
      return target;
    }
    target = jumps[j].target;
  }
}

void chunkPeephole(Chunk* c, ChunkJump* jumps, int32_t jumpCount) {
  if (c->codeLen == 0) {
    return;
  }
  // 1) 折叠重定向至不动点（先解析后写回，避免链中段丢失）
  bool changed = true;
  while (changed) {
    changed = false;
    for (int32_t i = 0; i < jumpCount; i++) {
      int32_t t = resolveTarget(c, jumps, jumpCount, jumps[i].target);
      if (t != jumps[i].target) {
        jumps[i].target = t;
        changed = true;
      }
    }
  }
  // 2) NOP 压实：目标已被重定向越过 NOP，剩余 NOP 均无引用
  bool anyNop = false;
  for (int32_t off = 0; off < c->codeLen;) {
    if (c->code[off] == OP_NOP) {
      anyNop = true;
    }
    off += chunkInstrSize(c, off);
  }
  if (!anyNop) {
    return;
  }
  int32_t* posMap = malloc(sizeof(int32_t) * (size_t)(c->codeLen + 1));
  ms_u8* newCode = malloc((size_t)c->codeLen);
  MS_ASSERT(posMap != NULL && newCode != NULL);
  int32_t oldOff = 0;
  int32_t newOff = 0;
  while (oldOff < c->codeLen) {
    posMap[oldOff] = newOff;
    int32_t size = chunkInstrSize(c, oldOff);
    if (c->code[oldOff] != OP_NOP) {
      memcpy(newCode + newOff, c->code + oldOff, (size_t)size);
      newOff += size;
    }
    oldOff += size;
  }
  posMap[c->codeLen] = newOff;
  free(c->code);
  c->code = newCode;
  c->codeLen = newOff;
  c->codeCap = newOff;
  for (int32_t i = 0; i < jumpCount; i++) {
    jumps[i].instrOffset = posMap[jumps[i].instrOffset];
    jumps[i].target = posMap[jumps[i].target];
  }
  // 行号偏移重映射；被删 NOP 上的 run 并入同偏移后继（去重保序）
  int32_t write = 0;
  for (int32_t i = 0; i < c->linesLen; i++) {
    int32_t mapped = posMap[c->lines[i].offset];
    if (write > 0 && c->lines[write - 1].offset == mapped) {
      c->lines[write - 1].line = c->lines[i].line;
      continue;
    }
    c->lines[write].offset = mapped;
    c->lines[write].line = c->lines[i].line;
    write++;
  }
  c->linesLen = write;
  free(posMap);
}
