// 反汇编（T010，21 §4 样式 / 27 §8）：常量池 + 指令流（偏移与操作数注释）+ 行号表
// 输出格式即 conformance `.bc` 快照基线：确定性、逐 chunk 段落化、偏移各自从 0 起
#include <string.h>

#include "compiler/compiler.h"

// 常量值渲染（与 ast_dump 的 %.17g 浮点形态一致）
static void printConstValue(FILE* out, const ConstEntry* e) {
  switch (e->kind) {
    case CONST_NIL:
      fputs("nil", out);
      break;
    case CONST_BOOL:
      fputs(e->as.b ? "true" : "false", out);
      break;
    case CONST_INT:
      fprintf(out, "%lld", (long long)e->as.i);
      break;
    case CONST_FLOAT:
      fprintf(out, "%.17g", e->as.f);
      break;
    case CONST_RUNE:
      fprintf(out, "U+%04X", e->as.cp);
      break;
    case CONST_BIGINT:
      fprintf(out, "%.*sn", e->as.text.len, e->as.text.start);
      break;
    case CONST_STRING:
    case CONST_RAWSTRING:  // 反引号原始串渲染同 STRING（解码值等价）
      putc('"', out);
      for (int32_t i = 0; i < e->as.text.len; i++) {
        ms_u8 ch = (ms_u8)e->as.text.start[i];
        if (ch == '"' || ch == '\\') {
          fprintf(out, "\\%c", ch);
        } else if (ch == '\n') {
          fputs("\\n", out);
        } else if (ch == '\t') {
          fputs("\\t", out);
        } else if (ch == '\r') {
          fputs("\\r", out);
        } else if (ch < 0x20 || ch > 0x7E) {
          fprintf(out, "\\x%02X", ch);
        } else {
          putc(ch, out);
        }
      }
      putc('"', out);
      break;
    case CONST_INTERP:  // Task 11 FORMAT_STRING 消费
      fprintf(out, "<interp %d>", e->as.interp.count);
      break;
    case CONST_PROTO:
      fprintf(out, "<func %.*s>", e->as.proto->nameLen, e->as.proto->name);
      break;
  }
}

static void printConstKind(FILE* out, const ConstEntry* e) {
  switch (e->kind) {
    case CONST_NIL:
      fputs("nil", out);
      break;
    case CONST_BOOL:
      fputs("bool", out);
      break;
    case CONST_INT:
      fputs("int", out);
      break;
    case CONST_FLOAT:
      fputs("float", out);
      break;
    case CONST_RUNE:
      fputs("rune", out);
      break;
    case CONST_BIGINT:
      fputs("bigint", out);
      break;
    case CONST_STRING:
    case CONST_RAWSTRING:
      fputs("string", out);
      break;
    case CONST_INTERP:
      fputs("interp", out);
      break;
    case CONST_PROTO:
      fputs("proto", out);
      break;
  }
}

// EXT 链 + 布局操作数（u8/const/jump 及成对组合），返回指令总长；opOut 为真实 opcode
static int32_t decodeAt(const Chunk* c, int32_t off, ms_u8* opOut, int32_t operands[3], int32_t* operandCount) {
  int32_t acc = 0;
  int32_t p = off + 1;
  while (c->code[p - 1] == OP_EXTENDED_ARG) {
    acc = (acc << 8) | c->code[p];
    p += 2;
  }
  ms_u8 op = c->code[p - 1];
  int32_t n = 0;
  int32_t bytes[3] = {0, 0, 0};
  switch (msOpLayouts[op]) {
    case OPL_NONE:
    case OPL_SPECIAL:
      break;
    case OPL_U8:
    case OPL_CONST:
    case OPL_JUMP:
      bytes[0] = 1;
      break;
    case OPL_U8_U8:
    case OPL_CONST_U8:
    case OPL_CONST_CONST:
    case OPL_JUMP_JUMP:
      bytes[0] = 1;
      bytes[1] = 1;
      break;
    case OPL_CONST_U8_U8:
      bytes[0] = 1;
      bytes[1] = 1;
      bytes[2] = 1;
      break;
  }
  for (int32_t i = 0; i < 3 && bytes[i] != 0; i++) {
    operands[n++] = (acc << 8) | c->code[p + i];
  }
  *operandCount = n;
  *opOut = op;
  return (p - off) + (bytes[0] + bytes[1] + bytes[2]);
}

static void disasmChunk(const Chunk* c, FILE* out, bool isModule) {
  if (isModule) {
    fprintf(out, "; %.*s\n", c->nameLen, c->name);
  } else {
    fprintf(out, "; func %.*s\n", c->nameLen, c->name);
  }
  fprintf(out, "consts %d\n", c->constsLen);
  for (int32_t i = 0; i < c->constsLen; i++) {
    fprintf(out, "  %d ", i);
    printConstKind(out, &c->consts[i]);
    putc(' ', out);
    printConstValue(out, &c->consts[i]);
    putc('\n', out);
  }
  fprintf(out, "code %d\n", c->codeLen);
  int32_t off = 0;
  while (off < c->codeLen) {
    ms_u8 op = 0;
    int32_t operands[3] = {0, 0, 0};
    int32_t operandCount = 0;
    int32_t size = decodeAt(c, off, &op, operands, &operandCount);
    if (operandCount == 0) {
      fprintf(out, "%04d %s\n", off, msOpNames[op]);
      off += size;
      continue;
    }
    fprintf(out, "%04d %-16s", off, msOpNames[op]);
    for (int32_t i = 0; i < operandCount; i++) {
      fprintf(out, " %d", operands[i]);
    }
    // 操作数注释：常量 → 值；跳转（首操作数为 JUMP 类布局）→ 绝对目标
    MsOpndLayout layout = msOpLayouts[op];
    if (layout == OPL_CONST || layout == OPL_CONST_U8 || layout == OPL_CONST_CONST || layout == OPL_CONST_U8_U8) {
      fprintf(out, " ; ");
      printConstValue(out, &c->consts[operands[0]]);
    } else if (layout == OPL_JUMP || layout == OPL_JUMP_JUMP) {
      fprintf(out, " ; -> %04d", operands[0]);
    }
    putc('\n', out);
    off += size;
  }
  fprintf(out, "upvalues %d\n", c->nUpvalues);
  fprintf(out, "icslots %d\n", (int)c->nIcSlots);
  fprintf(out, "lines %d\n", c->linesLen);
  for (int32_t i = 0; i < c->linesLen; i++) {
    fprintf(out, "  %04d %d\n", c->lines[i].offset, c->lines[i].line);
  }
  for (int32_t i = 0; i < c->constsLen; i++) {
    if (c->consts[i].kind == CONST_PROTO) {
      disasmChunk(c->consts[i].as.proto, out, false);
    }
  }
}

void compilerDisasm(const Chunk* c, FILE* out) { disasmChunk(c, out, true); }
