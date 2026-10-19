// Compiler 实现（T010）：基础路径字节码发射；跳转 patch/松弛物化见 finishChunk（任务书 §6）
#include "compiler/compiler.h"

#include <string.h>

// 编译单元状态：chunk + 跳转 fixup 表（emitJump 记录，finishChunk 统一回填/重定位）
typedef struct FuncState {
  Chunk* chunk;
  ChunkJump* jumps;
  int32_t jumpCount, jumpCap;
  MsDiagSink* diags;
  const char* file;
} FuncState;

static void compErr(FuncState* fs, const AstNode* n, int32_t len, const char* msg) {
  MsDiagnostic diag = {MS_DIAG_ERROR, MSE_COMPILE_UNSUPPORTED, fs->file, n->line, n->col, len, msg};
  msDiagAdd(fs->diags, &diag);
}

// ---- 发射助手 ----

static void emitOp(FuncState* fs, ms_u8 op, int32_t line) { chunkEmitOp(fs->chunk, op, line); }

static void emitByte(FuncState* fs, ms_u8 b) { chunkEmitByte(fs->chunk, b); }

// 常量索引操作数：发射时索引已知，按宽前置 EXTENDED_ARG 链（21 §1，高 → 低）
static void emitConstOp(FuncState* fs, ms_u8 op, int32_t idx, int32_t line) {
  if (idx > 0xFFFFFF) {
    emitOp(fs, OP_EXTENDED_ARG, line);
    emitByte(fs, (ms_u8)(idx >> 24));
  }
  if (idx > 0xFFFF) {
    emitOp(fs, OP_EXTENDED_ARG, line);
    emitByte(fs, (ms_u8)(idx >> 16));
  }
  if (idx > 0xFF) {
    emitOp(fs, OP_EXTENDED_ARG, line);
    emitByte(fs, (ms_u8)(idx >> 8));
  }
  emitOp(fs, op, line);
  emitByte(fs, (ms_u8)idx);
}

// 跳转发射：最小宽占位（opcode + u8），目标经 fixup 表二次回填（任务书 §6）
static int32_t emitJump(FuncState* fs, ms_u8 op, int32_t line) {
  int32_t at = chunkEmitOp(fs->chunk, op, line);
  chunkEmitByte(fs->chunk, 0);
  ChunkJump jump = {at, 0};
  MS_ARRAY_PUSH(fs->jumps, fs->jumpCount, fs->jumpCap, jump);
  return fs->jumpCount - 1;
}

static void patchJump(FuncState* fs, int32_t jumpIdx) { fs->jumps[jumpIdx].target = fs->chunk->codeLen; }

// 目标所需 EXT 前缀数（8 位操作数之上逐级 +1）
static int32_t jumpWidthFor(int32_t target) {
  if (target < 0x100) {
    return 0;
  }
  if (target < 0x10000) {
    return 1;
  }
  if (target < 0x1000000) {
    return 2;
  }
  return 3;
}

// 收束编译单元：peephole → 宽度松弛不动点 → 物化（EXT 前缀回填 + 行号表重建）
// EXTENDED_ARG 长度变化触发的重定位在 patch 阶段统一处理（任务书 §6）
static void finishChunk(FuncState* fs) {
  Chunk* c = fs->chunk;
  chunkPeephole(c, fs->jumps, fs->jumpCount);  // T010 计划 Task 6 接入真身
  if (fs->jumpCount == 0) {
    return;
  }
  MS_ASSERT(c->codeLen > 0);
  int32_t* widths = calloc((size_t)fs->jumpCount, sizeof(int32_t));
  int32_t* posMap = malloc(sizeof(int32_t) * (size_t)(c->codeLen + 1));
  MS_ASSERT(widths != NULL && posMap != NULL);
  for (;;) {
    // 按当前宽度走边界，填 old → new 偏移映射（fixups 按 instrOffset 有序）
    int32_t off = 0;
    int32_t newOff = 0;
    int32_t j = 0;
    while (off < c->codeLen) {
      posMap[off] = newOff;
      if (j < fs->jumpCount && fs->jumps[j].instrOffset == off) {
        newOff += 2 + 2 * widths[j];  // EXT 前缀每级 2 字节（opcode + 高位字节）
        off += 2;
        j++;
      } else {
        int32_t size = chunkInstrSize(c, off);
        newOff += size;
        off += size;
      }
    }
    posMap[c->codeLen] = newOff;
    bool changed = false;
    for (int32_t i = 0; i < fs->jumpCount; i++) {
      int32_t need = jumpWidthFor(posMap[fs->jumps[i].target]);
      if (need != widths[i]) {
        widths[i] = need;
        changed = true;
      }
    }
    if (!changed) {
      break;
    }
  }
  // 物化：跳转写 EXT 链（高 → 低）+ opcode + 低字节；行号表按映射重建 RLE
  int32_t newLen = posMap[c->codeLen];
  ms_u8* newCode = malloc((size_t)newLen);
  MS_ASSERT(newCode != NULL);
  SourceRun* newLines = NULL;
  int32_t newLinesLen = 0, newLinesCap = 0;
  int32_t lastLine = -1;
  int32_t off = 0;
  int32_t newOff = 0;
  int32_t j = 0;
  while (off < c->codeLen) {
    int32_t line = chunkLineAt(c, off);
    if (line >= 0 && line != lastLine) {
      SourceRun run = {newOff, line};
      MS_ARRAY_PUSH(newLines, newLinesLen, newLinesCap, run);
      lastLine = line;
    }
    if (j < fs->jumpCount && fs->jumps[j].instrOffset == off) {
      int32_t t = posMap[fs->jumps[j].target];
      for (int32_t s = widths[j] - 1; s >= 0; s--) {
        newCode[newOff++] = OP_EXTENDED_ARG;
        newCode[newOff++] = (ms_u8)((t >> (8 * s)) & 0xFF);
      }
      newCode[newOff++] = c->code[off];
      newCode[newOff++] = (ms_u8)(t & 0xFF);
      off += 2;
      j++;
    } else {
      int32_t size = chunkInstrSize(c, off);
      memcpy(newCode + newOff, c->code + off, (size_t)size);
      newOff += size;
      off += size;
    }
  }
  MS_ASSERT(newOff == newLen);
  free(c->code);
  c->code = newCode;
  c->codeLen = newLen;
  c->codeCap = newLen;
  free(c->lines);
  c->lines = newLines;
  c->linesLen = newLinesLen;
  c->linesCap = newLinesCap;
  free(widths);
  free(posMap);
}

static int32_t nameConst(FuncState* fs, const char* name, int32_t len) {
  return chunkAddConstText(fs->chunk, CONST_STRING, name, len);
}

// ---- 表达式 ----

static void compileExpr(FuncState* fs, const AstNode* n);

static void loadIdent(FuncState* fs, const AstNode* n) {
  if (n->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_LOAD_LOCAL, n->line);
    emitByte(fs, (ms_u8)n->bindSlot);
  } else if (n->bindKind == AST_BIND_GLOBAL) {
    int32_t k = nameConst(fs, n->as.ident.name, n->as.ident.nameLen);
    emitConstOp(fs, OP_LOAD_GLOBAL, k, n->line);
  } else {
    compErr(fs, n, n->as.ident.nameLen, "upvalue capture is not supported yet");
    emitOp(fs, OP_LOAD_NIL, n->line);  // 保持栈平衡的占位值
  }
}

static void storeIdent(FuncState* fs, const AstNode* n) {
  if (n->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_STORE_LOCAL, n->line);
    emitByte(fs, (ms_u8)n->bindSlot);
  } else if (n->bindKind == AST_BIND_GLOBAL) {
    int32_t k = nameConst(fs, n->as.ident.name, n->as.ident.nameLen);
    emitConstOp(fs, OP_STORE_GLOBAL, k, n->line);
  } else {
    compErr(fs, n, n->as.ident.nameLen, "upvalue store is not supported yet");
    emitOp(fs, OP_POP, n->line);  // 弹出待存值，维持栈平衡
  }
}

static ms_u8 binaryOpCode(AstBinOp op) {
  switch (op) {
    case AST_OP_ADD:
      return OP_BINARY_ADD;
    case AST_OP_SUB:
      return OP_BINARY_SUB;
    case AST_OP_MUL:
      return OP_BINARY_MUL;
    case AST_OP_DIV:
      return OP_BINARY_DIV;
    case AST_OP_MOD:
      return OP_BINARY_MOD;
    case AST_OP_POW:
      return OP_BINARY_POW;
    case AST_OP_BIT_AND:
      return OP_BINARY_AND;
    case AST_OP_BIT_OR:
      return OP_BINARY_OR;
    case AST_OP_BIT_XOR:
      return OP_BINARY_XOR;
    case AST_OP_SHL:
      return OP_BINARY_SHL;
    case AST_OP_SHR:
      return OP_BINARY_SHR;
    case AST_OP_EQ:
      return OP_COMPARE_EQ;
    case AST_OP_NE:
      return OP_COMPARE_NE;
    case AST_OP_LT:
      return OP_COMPARE_LT;
    case AST_OP_LE:
      return OP_COMPARE_LE;
    case AST_OP_GT:
      return OP_COMPARE_GT;
    case AST_OP_GE:
      return OP_COMPARE_GE;
    case AST_OP_IS:
      return OP_IS;
    case AST_OP_IN:
      return OP_IN;
    default:
      return OP_NOP;  // AND/OR 走 AST_LOGIC 短路，不可达
  }
}

// 单段无插值字符串 → 常量；插值/多段 → FORMAT_STRING 未启用
static bool tryStringConst(FuncState* fs, const AstNode* n, int32_t* idxOut) {
  const AstStrSeg* segs = n->as.strLit.segs;
  if (n->as.strLit.count == 1 && segs[0].expr == NULL) {
    *idxOut = chunkAddConstText(fs->chunk, CONST_STRING, segs[0].start, segs[0].len);
    return true;
  }
  return false;
}

static void compileExpr(FuncState* fs, const AstNode* n) {
  switch (n->kind) {
    case AST_NIL:
      emitOp(fs, OP_LOAD_NIL, n->line);
      break;
    case AST_BOOL:
      emitOp(fs, n->as.boolLit.b ? OP_LOAD_TRUE : OP_LOAD_FALSE, n->line);
      break;
    case AST_INT: {
      int32_t k = chunkAddConstInt(fs->chunk, n->as.intLit.v);
      emitConstOp(fs, OP_LOAD_CONST, k, n->line);
      break;
    }
    case AST_FLOAT: {
      int32_t k = chunkAddConstFloat(fs->chunk, n->as.floatLit.v);
      emitConstOp(fs, OP_LOAD_CONST, k, n->line);
      break;
    }
    case AST_RUNE: {
      int32_t k = chunkAddConstRune(fs->chunk, n->as.runeLit.cp);
      emitConstOp(fs, OP_LOAD_CONST, k, n->line);
      break;
    }
    case AST_BIGINT: {
      int32_t k = chunkAddConstText(fs->chunk, CONST_BIGINT, n->as.bigintLit.start, n->as.bigintLit.len);
      emitConstOp(fs, OP_LOAD_CONST, k, n->line);
      break;
    }
    case AST_STRING: {
      int32_t k = 0;
      if (tryStringConst(fs, n, &k)) {
        emitConstOp(fs, OP_LOAD_CONST, k, n->line);
      } else {
        compErr(fs, n, 1, "string interpolation is not supported yet");
        emitOp(fs, OP_LOAD_NIL, n->line);
      }
      break;
    }
    case AST_IDENT:
      loadIdent(fs, n);
      break;
    case AST_UNARY:
      compileExpr(fs, n->as.unary.operand);
      switch (n->as.unary.op) {
        case AST_UNOP_NEG:
          emitOp(fs, OP_UNARY_NEG, n->line);
          break;
        case AST_UNOP_NOT:
          emitOp(fs, OP_UNARY_NOT, n->line);
          break;
        case AST_UNOP_INV:
          emitOp(fs, OP_UNARY_INVERT, n->line);
          break;
      }
      break;
    case AST_BINARY:
      compileExpr(fs, n->as.binary.lhs);
      compileExpr(fs, n->as.binary.rhs);
      emitOp(fs, binaryOpCode(n->as.binary.op), n->line);
      break;
    case AST_LOGIC: {  // 短路：lhs 保留值跳 / 弹出继续（21 §3.5）
      compileExpr(fs, n->as.binary.lhs);
      ms_u8 op = n->as.binary.op == AST_OP_AND ? OP_JMP_IF_FALSE_OR_POP : OP_JMP_IF_TRUE_OR_POP;
      int32_t j = emitJump(fs, op, n->line);
      compileExpr(fs, n->as.binary.rhs);
      patchJump(fs, j);
      break;
    }
    case AST_TERNARY: {
      compileExpr(fs, n->as.ternary.cond);
      int32_t jElse = emitJump(fs, OP_JMP_IF_FALSE, n->line);
      compileExpr(fs, n->as.ternary.thenExpr);
      int32_t jEnd = emitJump(fs, OP_JMP, n->line);
      patchJump(fs, jElse);
      compileExpr(fs, n->as.ternary.elseExpr);
      patchJump(fs, jEnd);
      break;
    }
    case AST_CALL: {  // 桩路径（T018 完整化）：callee + args 依序 + CALL argc
      if (n->as.call.callee->kind != AST_IDENT) {
        compErr(fs, n->as.call.callee, 1, "callable expression form is not supported yet");
        emitOp(fs, OP_LOAD_NIL, n->line);
      } else {
        compileExpr(fs, n->as.call.callee);
      }
      for (int32_t i = 0; i < n->as.call.count; i++) {
        compileExpr(fs, n->as.call.args[i]);
      }
      emitOp(fs, OP_CALL, n->line);
      emitByte(fs, (ms_u8)n->as.call.count);
      break;
    }
    default:
      compErr(fs, n, 1, "expression form is not supported yet");
      emitOp(fs, OP_LOAD_NIL, n->line);
      break;
  }
}

// ---- 语句 ----

static void compileStmt(FuncState* fs, AstNode* n);

// 值已在栈上：按声明处绑定类别落盘（global：DEF+STORE；local：STORE_LOCAL）
static void finishDeclStore(FuncState* fs, const AstNode* decl) {
  if (decl->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_STORE_LOCAL, decl->line);
    emitByte(fs, (ms_u8)decl->bindSlot);
  } else {
    int32_t k = nameConst(fs, decl->as.varDecl.name, decl->as.varDecl.nameLen);
    emitConstOp(fs, OP_DEF_GLOBAL, k, decl->line);
    emitConstOp(fs, OP_STORE_GLOBAL, k, decl->line);
  }
}

static void compileVarDecl(FuncState* fs, AstNode* n) {
  if (n->as.varDecl.init != NULL) {
    compileExpr(fs, n->as.varDecl.init);
  } else {
    emitOp(fs, OP_LOAD_NIL, n->line);  // 04 §1：var 无初值默认 nil
  }
  finishDeclStore(fs, n);
}

static void compileAssign(FuncState* fs, AstNode* n) {
  AstNode* target = n->as.assign.target;
  if (target->kind != AST_IDENT) {
    compileExpr(fs, n->as.assign.value);  // 先求值保持副作用序
    compErr(fs, target, 1, "assignment target form is not supported yet");
    emitOp(fs, OP_POP, n->line);
    return;
  }
  if (n->as.assign.op != AST_OP_NONE) {  // 复合赋值展开：load op store
    loadIdent(fs, target);
    compileExpr(fs, n->as.assign.value);
    emitOp(fs, binaryOpCode(n->as.assign.op), n->line);
  } else {
    compileExpr(fs, n->as.assign.value);
  }
  storeIdent(fs, target);
}

// 函数原型：体编译为独立 chunk（RETURN/局部槽位来自 Binder 标注），挂入常量池
static void compileFuncProto(FuncState* fs, AstNode* n, Chunk** protoOut) {
  Chunk* proto = malloc(sizeof(Chunk));
  MS_ASSERT(proto != NULL);
  chunkInit(proto, n->as.funcDecl.name, n->as.funcDecl.nameLen);
  FuncState inner = {proto, NULL, 0, 0, fs->diags, fs->file};
  AstNode* body = n->as.funcDecl.body;
  for (int32_t i = 0; i < body->as.list.count; i++) {
    compileStmt(&inner, body->as.list.items[i]);
  }
  // epilogue：末指令已为 RETURN 则不补（21 §4 func add 形态）
  if (inner.chunk->codeLen == 0 || inner.chunk->code[inner.chunk->codeLen - 1] != OP_RETURN) {
    int32_t line = body->as.list.count > 0 ? body->as.list.items[body->as.list.count - 1]->line : n->line;
    emitOp(&inner, OP_LOAD_NIL, line);
    emitOp(&inner, OP_RETURN, line);
  }
  finishChunk(&inner);
  free(inner.jumps);
  *protoOut = proto;
}

static void compileFuncDecl(FuncState* fs, AstNode* n) {
  Chunk* proto = NULL;
  compileFuncProto(fs, n, &proto);
  int32_t k = chunkAddConstProto(fs->chunk, proto);
  emitConstOp(fs, OP_LOAD_CONST, k, n->line);
  emitOp(fs, OP_MAKE_FUNCTION, n->line);
  emitByte(fs, 0);  // flags：默认参数/rest 形态 T018 填充
  finishDeclStore(fs, n);
}

static void compileStmt(FuncState* fs, AstNode* n) {
  switch (n->kind) {
    case AST_EXPR_STMT:
      compileExpr(fs, n->as.exprStmt.expr);
      emitOp(fs, OP_POP, n->line);
      break;
    case AST_VAR_DECL:
      compileVarDecl(fs, n);
      break;
    case AST_ASSIGN:
      compileAssign(fs, n);
      break;
    case AST_BLOCK:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        compileStmt(fs, n->as.list.items[i]);
      }
      break;
    case AST_FUNC_DECL:
      compileFuncDecl(fs, n);
      break;
    case AST_RETURN:
      if (n->as.exprStmt.expr != NULL) {
        compileExpr(fs, n->as.exprStmt.expr);
      } else {
        emitOp(fs, OP_LOAD_NIL, n->line);
      }
      emitOp(fs, OP_RETURN, n->line);
      break;
    default:
      compErr(fs, n, 1, "statement form is not supported yet");
      break;
  }
}

bool compilerCompile(AstNode* module, Chunk* out, const char* file, MsDiagSink* diags) {
  chunkInit(out, "main", 4);
  FuncState fs = {out, NULL, 0, 0, diags, file};
  for (int32_t i = 0; i < module->as.list.count; i++) {
    compileStmt(&fs, module->as.list.items[i]);
  }
  int32_t lastLine = module->as.list.count > 0 ? module->as.list.items[module->as.list.count - 1]->line : 1;
  emitOp(&fs, OP_LOAD_NIL, lastLine);  // 模块恒以 nil 返回收尾（21 §4 main 形态）
  emitOp(&fs, OP_RETURN, lastLine);
  finishChunk(&fs);
  free(fs.jumps);
  for (int32_t i = 0; i < diags->count; i++) {
    if (diags->items[i].level == MS_DIAG_ERROR) {
      return false;
    }
  }
  return true;
}
