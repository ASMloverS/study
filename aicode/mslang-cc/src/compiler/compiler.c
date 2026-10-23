// Compiler 实现（T010）：基础路径字节码发射；跳转 patch/松弛物化见 finishChunk（任务书 §6）
#include "compiler/compiler.h"

#include <string.h>

// 编译单元状态：chunk + 跳转 fixup 表（emitJump 记录，finishChunk 统一回填/重定位）+ 循环上下文
typedef struct LoopCtx {
  int32_t* breaks;  // break 跳转索引表
  int32_t breakCount, breakCap;
  int32_t* conts;  // continue 跳转索引表
  int32_t contCount, contCap;
  int32_t contTarget;  // continue 目标（tri：post 前；其余：条件/迭代头）
  bool isIter;         // for-in：break 需先弹迭代器
} LoopCtx;

typedef struct FuncState {
  Chunk* chunk;
  ChunkJump* jumps;
  int32_t jumpCount, jumpCap;
  LoopCtx* loops;
  int32_t loopCount, loopCap;
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

// ---- 循环上下文（break/continue 跳转收集，循环收尾统一回填） ----

static void beginLoop(FuncState* fs, bool isIter) {
  LoopCtx lc = {NULL, 0, 0, NULL, 0, 0, -1, isIter};
  MS_ARRAY_PUSH(fs->loops, fs->loopCount, fs->loopCap, lc);
}

static LoopCtx* currentLoop(FuncState* fs) { return &fs->loops[fs->loopCount - 1]; }

static void endLoop(FuncState* fs) {
  LoopCtx* lc = currentLoop(fs);
  int32_t end = fs->chunk->codeLen;
  for (int32_t i = 0; i < lc->breakCount; i++) {
    fs->jumps[lc->breaks[i]].target = end;
  }
  MS_ASSERT(lc->contTarget >= 0);
  for (int32_t i = 0; i < lc->contCount; i++) {
    fs->jumps[lc->conts[i]].target = lc->contTarget;
  }
  free(lc->breaks);
  free(lc->conts);
  fs->loopCount--;
}

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
      for (int32_t s = widths[j]; s >= 1; s--) {  // EXT 载荷 = 高位字节（t>>8 起）
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

// 无插值字符串 → 常量（空串 0 段亦然）；插值/多段 → 下方 else 分支 CONST_INTERP + FORMAT_STRING
// T013：反引号原始串走 CONST_RAWSTRING（内容不解码，LOAD_CONST 直拷 intern）
static bool tryStringConst(FuncState* fs, const AstNode* n, int32_t* idxOut) {
  const AstStrSeg* segs = n->as.strLit.segs;
  if (n->as.strLit.count == 0) {  // ""：空串
    *idxOut = chunkAddConstText(fs->chunk, CONST_STRING, "", 0);
    return true;
  }
  if (n->as.strLit.count == 1 && segs[0].expr == NULL) {
    ConstKind k = n->as.strLit.raw ? CONST_RAWSTRING : CONST_STRING;
    *idxOut = chunkAddConstText(fs->chunk, k, segs[0].start, segs[0].len);
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
      } else {  // 插值：字面段编译期解码入 CONST_INTERP；表达式段依次压栈后 FORMAT_STRING（21 §3.9）
        int32_t count = n->as.strLit.count;
        InterpSeg* segs = malloc(sizeof(InterpSeg) * (size_t)count);
        MS_ASSERT(segs != NULL);
        for (int32_t i = 0; i < count; i++) {
          const AstStrSeg* seg = &n->as.strLit.segs[i];
          if (seg->expr != NULL) {
            segs[i] = (InterpSeg){true, NULL, 0};
          } else {
            char* decoded = malloc((size_t)seg->len + 1);  // 解码只会收缩；+1 供 NUL 终止契约
            MS_ASSERT(decoded != NULL);
            int32_t dlen = msTextDecodeEscapes(seg->start, seg->len, decoded, seg->len + 1);
            segs[i] = (InterpSeg){false, decoded, dlen};
          }
        }
        int32_t k2 = chunkAddConstInterp(fs->chunk, segs, count);
        for (int32_t i = 0; i < count; i++) {
          if (segs[i].isExpr) {
            compileExpr(fs, n->as.strLit.segs[i].expr);
          } else {
            free((void*)segs[i].start);  // const 已拷贝 own 副本
          }
        }
        free(segs);
        emitConstOp(fs, OP_FORMAT_STRING, k2, n->line);
      }
      break;
    }
    case AST_LIST: {
      for (int32_t i = 0; i < n->as.list.count; i++) {
        compileExpr(fs, n->as.list.items[i]);
      }
      emitOp(fs, OP_BUILD_LIST, n->line);
      emitByte(fs, (ms_u8)n->as.list.count);
      break;
    }
    case AST_INDEX: {  // D6：obj, index → GET_INDEX（slice 形态经 BUILD_SLICE 归 Task 10）
      compileExpr(fs, n->as.indexExpr.obj);
      compileExpr(fs, n->as.indexExpr.index);
      emitOp(fs, OP_GET_INDEX, n->line);
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
      if (n->as.call.callee->kind == AST_MEMBER) {  // s.m(args) → CALL_METHOD（21 §3.3）
        const AstNode* m = n->as.call.callee;
        compileExpr(fs, m->as.memberExpr.obj);
        for (int32_t i = 0; i < n->as.call.count; i++) {
          compileExpr(fs, n->as.call.args[i]);
        }
        int32_t k = nameConst(fs, m->as.memberExpr.name, m->as.memberExpr.nameLen);
        emitConstOp(fs, OP_CALL_METHOD, k, n->line);
        emitByte(fs, 0);  // icSlot（T022 内联缓存）
        emitByte(fs, (ms_u8)n->as.call.count);
        break;
      }
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
    case AST_MEMBER: {  // 方法取值：GET_ATTR 物化 bound（类属性 T020）
      compileExpr(fs, n->as.memberExpr.obj);
      int32_t k = nameConst(fs, n->as.memberExpr.name, n->as.memberExpr.nameLen);
      emitConstOp(fs, OP_GET_ATTR, k, n->line);
      emitByte(fs, 0);  // icSlot（T022）
      break;
    }
    case AST_SLICE: {  // 切片对象不独立暴露：BUILD_SLICE 后立即 GET_INDEX（03 §3）
      compileExpr(fs, n->as.slice.obj);
      const AstNode* parts[3] = {n->as.slice.start, n->as.slice.end, n->as.slice.step};
      for (int i = 0; i < 3; i++) {
        if (parts[i] != NULL) {
          compileExpr(fs, parts[i]);
        } else {
          emitOp(fs, OP_LOAD_NIL, n->line);
        }
      }
      emitOp(fs, OP_BUILD_SLICE, n->line);
      emitByte(fs, 3);
      emitOp(fs, OP_GET_INDEX, n->line);
      break;
    }
    default:
      compErr(fs, n, 1, "expression form is not supported yet");
      emitOp(fs, OP_LOAD_NIL, n->line);
      break;
  }
}

// break/continue：仅循环体内合法（binder 已拦）；跳转收集到当前循环上下文
static void emitBreak(FuncState* fs, const AstNode* n) {
  if (fs->loopCount == 0) {  // 防御（MSE_BIND_LOOP_ESCAPE 已发）
    compErr(fs, n, 1, "break outside loop");
    return;
  }
  if (currentLoop(fs)->isIter) {
    emitOp(fs, OP_POP, n->line);  // 弹迭代器，保持栈平衡
  }
  int32_t j = emitJump(fs, OP_JMP, n->line);
  LoopCtx* lc = currentLoop(fs);
  MS_ARRAY_PUSH(lc->breaks, lc->breakCount, lc->breakCap, j);
}

static void emitContinue(FuncState* fs, const AstNode* n) {
  if (fs->loopCount == 0) {
    compErr(fs, n, 1, "continue outside loop");
    return;
  }
  int32_t j = emitJump(fs, OP_JMP, n->line);
  LoopCtx* lc = currentLoop(fs);
  MS_ARRAY_PUSH(lc->conts, lc->contCount, lc->contCap, j);
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
  if (target->kind == AST_INDEX && n->as.assign.op == AST_OP_NONE) {
    // obj[idx] = v（string 只读在运行时报；03 §4 求值序 obj→idx→v；复合赋值 index 目标维持不支持）
    compileExpr(fs, target->as.indexExpr.obj);
    compileExpr(fs, target->as.indexExpr.index);
    compileExpr(fs, n->as.assign.value);
    emitOp(fs, OP_SET_INDEX, n->line);
    return;
  }
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
  FuncState inner = {proto, NULL, 0, 0, NULL, 0, 0, fs->diags, fs->file};
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
  free(inner.loops);
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

// ---- 控制流（T011，04 §3-§5） ----

static void compileIf(FuncState* fs, AstNode* n) {
  compileExpr(fs, n->as.ifStmt.cond);
  int32_t jElse = emitJump(fs, OP_JMP_IF_FALSE, n->line);
  compileStmt(fs, n->as.ifStmt.thenBlock);
  if (n->as.ifStmt.elseNode != NULL) {
    int32_t jEnd = emitJump(fs, OP_JMP, n->line);
    patchJump(fs, jElse);
    compileStmt(fs, n->as.ifStmt.elseNode);  // BLOCK 或 else-if 链（AST_IF）
    patchJump(fs, jEnd);
  } else {
    patchJump(fs, jElse);
  }
}

static void compileFor(FuncState* fs, AstNode* n) {
  if (n->as.forStmt.form == AST_FOR_IN) {
    beginLoop(fs, true);
    compileExpr(fs, n->as.forStmt.iter);
    emitOp(fs, OP_GET_ITER, n->line);
    int32_t lTop = fs->chunk->codeLen;
    currentLoop(fs)->contTarget = lTop;
    int32_t jExit = emitJump(fs, OP_FOR_ITER, n->line);
    AstNode* target = n->as.forStmt.target;
    if (target == NULL) {
      emitOp(fs, OP_POP, n->line);  // 无目标（BAD_TARGET 残留）：丢弃
    } else if (target->kind == AST_IDENT && target->bindKind == AST_BIND_LOCAL) {
      emitOp(fs, OP_STORE_LOCAL, target->line);
      emitByte(fs, (ms_u8)target->bindSlot);
    } else if (target->kind == AST_IDENT) {  // 模块层循环变量：声明式落盘（fresh 绑定 04 §4）
      int32_t k = nameConst(fs, target->as.ident.name, target->as.ident.nameLen);
      emitConstOp(fs, OP_DEF_GLOBAL, k, target->line);
      emitConstOp(fs, OP_STORE_GLOBAL, k, target->line);
    } else {
      compErr(fs, target, 1, "multi-variable iteration lands in T017");
      emitOp(fs, OP_POP, n->line);
    }
    compileStmt(fs, n->as.forStmt.body);
    int32_t jBack = emitJump(fs, OP_JMP, n->line);
    fs->jumps[jBack].target = lTop;
    fs->jumps[jExit].target = fs->chunk->codeLen;
    endLoop(fs);
    return;
  }
  beginLoop(fs, false);
  if (n->as.forStmt.init != NULL) {
    compileStmt(fs, n->as.forStmt.init);  // TRI：简单语句（可省略）
  }
  int32_t lTop = fs->chunk->codeLen;
  if (n->as.forStmt.form != AST_FOR_TRI) {
    currentLoop(fs)->contTarget = lTop;  // COND/INF：continue 回条件/体首
  }
  int32_t jExit = -1;
  if (n->as.forStmt.cond != NULL) {
    compileExpr(fs, n->as.forStmt.cond);
    jExit = emitJump(fs, OP_JMP_IF_FALSE, n->line);
  }
  compileStmt(fs, n->as.forStmt.body);
  if (n->as.forStmt.form == AST_FOR_TRI) {
    currentLoop(fs)->contTarget = fs->chunk->codeLen;  // continue 先执行 post
  }
  if (n->as.forStmt.post != NULL) {
    compileStmt(fs, n->as.forStmt.post);
  }
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  if (jExit >= 0) {
    fs->jumps[jExit].target = fs->chunk->codeLen;
  }
  endLoop(fs);
}

static void compileWhile(FuncState* fs, AstNode* n) {
  beginLoop(fs, false);
  int32_t lTop = fs->chunk->codeLen;
  currentLoop(fs)->contTarget = lTop;
  compileExpr(fs, n->as.whileStmt.cond);
  int32_t jExit = emitJump(fs, OP_JMP_IF_FALSE, n->line);
  compileStmt(fs, n->as.whileStmt.body);
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  fs->jumps[jExit].target = fs->chunk->codeLen;
  endLoop(fs);
}

// 类型式 as 绑定落盘（判别值已在栈顶；binder 标注见 binder.c AST_SWITCH）
static void storeCaseBinding(FuncState* fs, const AstNode* c) {
  if (c->bindKind == AST_BIND_LOCAL) {
    emitOp(fs, OP_STORE_LOCAL, c->line);
    emitByte(fs, (ms_u8)c->bindSlot);
  } else {
    int32_t k = nameConst(fs, c->as.switchCase.bindName, c->as.switchCase.bindLen);
    emitConstOp(fs, OP_DEF_GLOBAL, k, c->line);
    emitConstOp(fs, OP_STORE_GLOBAL, k, c->line);
  }
}

// switch 值/类型式统一比较链（04 §5；SWITCH_TABLE 跳转表优化后置）
// 判别值驻栈，逐 case 逐值 DUP+测试+假跳；命中落体，default 兜底，无 fallthrough
static void compileSwitch(FuncState* fs, AstNode* n) {
  compileExpr(fs, n->as.switchStmt.subject);
  int32_t* endJumps = NULL;
  int32_t endCount = 0, endCap = 0;
  int32_t* pendingTests = NULL;  // 各 case 末测试假跳（体后统一补至下一 case/兜底）
  int32_t pendingCount = 0, pendingCap = 0;
  AstNode* defaultCase = NULL;
  for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
    AstNode* c = n->as.switchStmt.cases[i];
    if (c->as.switchCase.isDefault) {
      defaultCase = c;
      continue;
    }
    int32_t lastTest = -1;  // 前一测试假跳：补至下一测试/case 兜底处
    for (int32_t v = 0; v < c->as.switchCase.valueCount; v++) {
      if (lastTest >= 0) {
        patchJump(fs, lastTest);
      }
      AstNode* vn = c->as.switchCase.values[v];
      emitOp(fs, OP_DUP, vn->line);
      if (n->as.switchStmt.isType) {
        if (vn->kind != AST_IDENT) {
          compErr(fs, vn, 1, "type case expects a type name");
          emitOp(fs, OP_POP, vn->line);  // 弹 DUP 保持平衡
        } else {
          int32_t k = nameConst(fs, vn->as.ident.name, vn->as.ident.nameLen);
          emitConstOp(fs, OP_CHECK_TYPE, k, vn->line);
        }
      } else {
        compileExpr(fs, vn);  // case 值任意表达式，== 语义（04 §5.1）
        emitOp(fs, OP_COMPARE_EQ, vn->line);
      }
      lastTest = emitJump(fs, OP_JMP_IF_FALSE, vn->line);
    }
    if (lastTest >= 0) {
      MS_ARRAY_PUSH(pendingTests, pendingCount, pendingCap, lastTest);
    }
    if (c->as.switchCase.bindName != NULL) {  // 命中：判别值出栈入绑定
      storeCaseBinding(fs, c);
    } else {
      emitOp(fs, OP_POP, c->line);
    }
    compileStmt(fs, c->as.switchCase.body);  // 可为 NULL（解析残留；诊断已发）
    int32_t je = emitJump(fs, OP_JMP, c->line);
    MS_ARRAY_PUSH(endJumps, endCount, endCap, je);
    for (int32_t t = 0; t < pendingCount; t++) {
      patchJump(fs, pendingTests[t]);  // 末测试假跳落至下一 case/兜底
    }
    pendingCount = 0;
  }
  if (defaultCase != NULL) {  // 全测试未命中：default 体（源内任意位置合法）
    if (defaultCase->as.switchCase.bindName != NULL) {
      storeCaseBinding(fs, defaultCase);
    } else {
      emitOp(fs, OP_POP, defaultCase->line);
    }
    compileStmt(fs, defaultCase->as.switchCase.body);
  } else {
    emitOp(fs, OP_POP, n->line);  // 仅弹判别值
  }
  for (int32_t e = 0; e < endCount; e++) {
    patchJump(fs, endJumps[e]);
  }
  free(endJumps);
  free(pendingTests);
}

static void compileStmt(FuncState* fs, AstNode* n) {
  if (n == NULL) {  // 解析残留部分节点（诊断已发）：跳过
    return;
  }
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
    case AST_IF:
      compileIf(fs, n);
      break;
    case AST_FOR:
      compileFor(fs, n);
      break;
    case AST_WHILE:
      compileWhile(fs, n);
      break;
    case AST_SWITCH:
      compileSwitch(fs, n);
      break;
    case AST_BREAK:
      emitBreak(fs, n);
      break;
    case AST_CONTINUE:
      emitContinue(fs, n);
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
  FuncState fs = {out, NULL, 0, 0, NULL, 0, 0, diags, file};
  for (int32_t i = 0; i < module->as.list.count; i++) {
    compileStmt(&fs, module->as.list.items[i]);
  }
  int32_t lastLine = module->as.list.count > 0 ? module->as.list.items[module->as.list.count - 1]->line : 1;
  emitOp(&fs, OP_LOAD_NIL, lastLine);  // 模块恒以 nil 返回收尾（21 §4 main 形态）
  emitOp(&fs, OP_RETURN, lastLine);
  finishChunk(&fs);
  free(fs.jumps);
  free(fs.loops);
  for (int32_t i = 0; i < diags->count; i++) {
    if (diags->items[i].level == MS_DIAG_ERROR) {
      return false;
    }
  }
  return true;
}
