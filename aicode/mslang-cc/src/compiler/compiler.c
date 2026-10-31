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

// 推导式内联编译帧（T017 Task 4）：binder COMPR 伪帧槽位 → 真实帧槽位映射（按需物化）
typedef struct ComprFrame {
  int32_t* map;
  int32_t mapLen, mapCap;
} ComprFrame;

typedef struct FuncState {
  Chunk* chunk;
  ChunkJump* jumps;
  int32_t jumpCount, jumpCap;
  LoopCtx* loops;
  int32_t loopCount, loopCap;
  MsDiagSink* diags;
  const char* file;
  int32_t localCount;  // 本帧最大槽位号 + 1（收尾写 chunk->localCount）
  int32_t nextTemp;    // 下一临时槽（推导式累加器/目标）
  ComprFrame* comprStack;
  int32_t comprDepth, comprCap;
} FuncState;

static void compErr(FuncState* fs, const AstNode* n, int32_t len, const char* msg) {
  if (n == NULL) {  // 无节点语境（localOp 槽域校验）：占位行号入 sink 维持统一诊断面
    MsDiagnostic diag = {MS_DIAG_ERROR, MSE_COMPILE_UNSUPPORTED, fs->file, 0, 0, 1, msg};
    msDiagAdd(fs->diags, &diag);
    return;
  }
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

// 局部槽访问统一入口：槽位域校验（u8 操作数）+ localCount 跟踪 + 发射
static void localOp(FuncState* fs, ms_u8 op, int32_t slot, int32_t line) {
  if (slot > 255) {
    compErr(fs, NULL, 1, "too many local variables");
    slot = 0;  // 占位维持发射面（诊断已发，编译整体中止）
  }
  if (slot + 1 > fs->localCount) {
    fs->localCount = slot + 1;
  }
  emitOp(fs, op, line);
  emitByte(fs, (ms_u8)slot);
}

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

// 推导式帧栈内解析槽位（T017 Task 4）：BIND_LOCAL → 内层伪帧 map；
// BIND_UPVALUE(depth) → depth 层外伪帧 map，或 depth == comprDepth 时直通真实外层函数槽
// 返回 -1 = 不可解析（槽位未声明 / 跨真实函数边界，upvalue 归 T018）
static int32_t comprResolveSlot(const FuncState* fs, int32_t slot, int32_t hops) {
  if (fs->comprDepth == 0) {
    return slot;  // 帧局部直通（非推导式语境）
  }
  const ComprFrame* inner = &fs->comprStack[fs->comprDepth - 1];
  if (hops < 0) {  // BIND_LOCAL：推导式内声明（目标）只可能在内层伪帧
    return slot < inner->mapLen ? inner->map[slot] : -1;
  }
  if (hops == fs->comprDepth) {
    return slot;  // 定义于真实外层函数：bindSlot 即本帧槽
  }
  if (hops > fs->comprDepth) {
    return -1;  // 跨真实函数边界：upvalue 捕获归 T018
  }
  const ComprFrame* def = &fs->comprStack[fs->comprDepth - 1 - hops];
  return slot < def->mapLen ? def->map[slot] : -1;
}

static void loadIdent(FuncState* fs, const AstNode* n) {
  if (n->bindKind == AST_BIND_LOCAL) {
    int32_t slot = comprResolveSlot(fs, n->bindSlot, -1);
    if (slot < 0) {
      compErr(fs, n, n->as.ident.nameLen, "comprehension variable used before declaration");
      emitOp(fs, OP_LOAD_NIL, n->line);  // 保持栈平衡的占位值
      return;
    }
    localOp(fs, OP_LOAD_LOCAL, slot, n->line);
    return;
  }
  if (n->bindKind == AST_BIND_GLOBAL) {
    int32_t k = nameConst(fs, n->as.ident.name, n->as.ident.nameLen);
    emitConstOp(fs, OP_LOAD_GLOBAL, k, n->line);
    return;
  }
  if (fs->comprDepth > 0 && n->bindUpvalDepth <= fs->comprDepth) {  // 推导式捕获：同帧消解
    int32_t slot = comprResolveSlot(fs, n->bindSlot, n->bindUpvalDepth);
    if (slot >= 0) {
      localOp(fs, OP_LOAD_LOCAL, slot, n->line);
      return;
    }
  }
  compErr(fs, n, n->as.ident.nameLen, "upvalue capture is not supported yet");
  emitOp(fs, OP_LOAD_NIL, n->line);  // 保持栈平衡的占位值
}

static void storeIdent(FuncState* fs, const AstNode* n) {
  if (n->bindKind == AST_BIND_LOCAL) {
    int32_t slot = comprResolveSlot(fs, n->bindSlot, -1);
    if (slot < 0) {  // 防御：赋值语句不出现于推导式表达式内（语句/表达式分离）
      compErr(fs, n, n->as.ident.nameLen, "comprehension variable used before declaration");
      emitOp(fs, OP_POP, n->line);  // 弹出待存值，维持栈平衡
      return;
    }
    localOp(fs, OP_STORE_LOCAL, slot, n->line);
    return;
  }
  if (n->bindKind == AST_BIND_GLOBAL) {
    int32_t k = nameConst(fs, n->as.ident.name, n->as.ident.nameLen);
    emitConstOp(fs, OP_STORE_GLOBAL, k, n->line);
    return;
  }
  compErr(fs, n, n->as.ident.nameLen, "upvalue store is not supported yet");
  emitOp(fs, OP_POP, n->line);  // 弹出待存值，维持栈平衡
}

// ---- 推导式 / 生成器表达式（03 §1-§2：内联脱糖 + 帧槽重映射） ----

static void comprPush(FuncState* fs) {
  if (fs->comprDepth == fs->comprCap) {
    fs->comprCap = fs->comprCap == 0 ? 4 : fs->comprCap * 2;
    fs->comprStack = realloc(fs->comprStack, sizeof(ComprFrame) * (size_t)fs->comprCap);
    MS_ASSERT(fs->comprStack != NULL);
  }
  ComprFrame* f = &fs->comprStack[fs->comprDepth++];
  f->map = NULL;
  f->mapLen = f->mapCap = 0;
}

static void comprPop(FuncState* fs) {
  ComprFrame* f = &fs->comprStack[--fs->comprDepth];
  free(f->map);
  f->map = NULL;
}

// binder COMPR 伪帧槽 → 真实帧槽（按需物化；映射表随声明顺序增长）
static int32_t comprSlot(FuncState* fs, int32_t slot) {
  ComprFrame* f = &fs->comprStack[fs->comprDepth - 1];
  while (f->mapLen <= slot) {  // 稀疏防御：中间槽顺序占位（binder 槽位致密，正常不触发）
    int32_t tmp = fs->nextTemp++;
    MS_ARRAY_PUSH(f->map, f->mapLen, f->mapCap, tmp);
  }
  return f->map[slot];
}

// 推导式目标落盘（remap 版 storeIterTarget）：IDENT 经 comprSlot；TUPLE → UNPACK_SEQ 逆序递归
static void storeComprTarget(FuncState* fs, AstNode* t) {
  if (t->kind == AST_TUPLE) {
    if (t->as.tuple.count > 255) {  // UNPACK_SEQ u8 计数上限
      compErr(fs, t, 1, "too many iteration targets");
      return;
    }
    emitOp(fs, OP_UNPACK_SEQ, t->line);
    emitByte(fs, (ms_u8)t->as.tuple.count);
    for (int32_t i = t->as.tuple.count - 1; i >= 0; i--) {  // 逆序消费：UNPACK 后栈顶为末元素
      storeComprTarget(fs, t->as.tuple.items[i]);
    }
    return;
  }
  if (t->kind != AST_IDENT) {  // BAD_TARGET 残留防御（诊断已发，编译整体中止）
    compErr(fs, t, 1, "iteration target form is not supported yet");
    emitOp(fs, OP_POP, t->line);
    return;
  }
  localOp(fs, OP_STORE_LOCAL, comprSlot(fs, t->bindSlot), t->line);
}

// 脱糖主体：嵌套 GET_ITER/FOR_ITER 循环 + 条件 + 元素追加（03 §2；无 LoopCtx——不可 break/continue）
// 累加器驻帧槽 acc；list/set 经 CALL_METHOD append/add，map 经 SET_INDEX（弹三无产出，栈平衡）
static void compileComprBody(FuncState* fs, const AstNode* n, int32_t acc, int32_t forIdx, int32_t lTopInner) {
  if (forIdx == n->as.compr.forCount) {
    for (int32_t i = 0; i < n->as.compr.condCount; i++) {  // 多 if 合取：假 → 内层循环头
      compileExpr(fs, n->as.compr.conds[i]);
      int32_t j = emitJump(fs, OP_JMP_IF_FALSE, n->as.compr.conds[i]->line);
      fs->jumps[j].target = lTopInner;
    }
    if (n->as.compr.compKind == AST_COMP_MAP) {
      localOp(fs, OP_LOAD_LOCAL, acc, n->line);
      compileExpr(fs, n->as.compr.key);
      compileExpr(fs, n->as.compr.value);
      emitOp(fs, OP_SET_INDEX, n->line);
      return;
    }
    localOp(fs, OP_LOAD_LOCAL, acc, n->line);
    compileExpr(fs, n->as.compr.value);
    const char* mname = n->as.compr.compKind == AST_COMP_SET ? "add" : "append";
    int32_t k = nameConst(fs, mname, (int32_t)strlen(mname));
    emitConstOp(fs, OP_CALL_METHOD, k, n->line);
    emitByte(fs, 0);  // icSlot（T022 内联缓存）
    emitByte(fs, 1);  // argc = 1（接收者之外仅元素）
    emitOp(fs, OP_POP, n->line);  // append/add 返回 nil 弹净
    return;
  }
  compileExpr(fs, n->as.compr.fors[forIdx].iter);  // 迭代源在第 i 层子句域求值（03 §2）
  emitOp(fs, OP_GET_ITER, n->line);
  int32_t lTop = fs->chunk->codeLen;
  int32_t jExit = emitJump(fs, OP_FOR_ITER, n->line);
  storeComprTarget(fs, n->as.compr.fors[forIdx].target);
  compileComprBody(fs, n, acc, forIdx + 1, forIdx == n->as.compr.forCount - 1 ? lTop : lTopInner);
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  fs->jumps[jExit].target = fs->chunk->codeLen;
}

// AST_COMPREHENSION / AST_GEN_EXPR 共用（03 §1-§2）：genexp = 物化 list + GET_ITER（T019 替换为惰性管道）
// 累加器与目标全部落帧临时槽（VM 入帧保留 nil 区即 GC root，不与表达式栈冲突）
static void compileCompr(FuncState* fs, const AstNode* n) {
  comprPush(fs);
  int32_t acc = fs->nextTemp++;  // 累加器临时槽（帧局部区 root）
  ms_u8 build = n->as.compr.compKind == AST_COMP_MAP ? OP_BUILD_MAP
                : n->as.compr.compKind == AST_COMP_SET ? OP_BUILD_SET
                                                       : OP_BUILD_LIST;
  emitOp(fs, build, n->line);
  emitByte(fs, 0);
  localOp(fs, OP_STORE_LOCAL, acc, n->line);
  compileComprBody(fs, n, acc, 0, 0);
  comprPop(fs);
  localOp(fs, OP_LOAD_LOCAL, acc, n->line);
  if (n->kind == AST_GEN_EXPR) {
    emitOp(fs, OP_GET_ITER, n->line);
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
    case AST_MAP: {  // k1 v1 … kn vn 依序压栈（21 §3.9 BUILD_MAP 操作数 = 2×对数）
      int32_t count = n->as.mapLit.count;
      if (count > 127) {  // 2*count 截 u8：127 为上限（128×2 溢出）
        compErr(fs, n, 1, "map literal has too many entries");
        return;
      }
      for (int32_t i = 0; i < count; i++) {
        compileExpr(fs, n->as.mapLit.keys[i]);
        compileExpr(fs, n->as.mapLit.values[i]);
      }
      emitOp(fs, OP_BUILD_MAP, n->line);
      emitByte(fs, (ms_u8)(count * 2));
      break;
    }
    case AST_SET: {
      int32_t count = n->as.list.count;
      if (count > 255) {  // BUILD_SET u8 计数上限
        compErr(fs, n, 1, "set literal has too many elements");
        return;
      }
      for (int32_t i = 0; i < count; i++) {
        compileExpr(fs, n->as.list.items[i]);
      }
      emitOp(fs, OP_BUILD_SET, n->line);
      emitByte(fs, (ms_u8)count);
      break;
    }
    case AST_TUPLE: {
      int32_t count = n->as.tuple.count;
      if (count > 255) {  // BUILD_TUPLE u8 计数上限
        compErr(fs, n, 1, "tuple literal has too many elements");
        return;
      }
      for (int32_t i = 0; i < count; i++) {
        compileExpr(fs, n->as.tuple.items[i]);
      }
      emitOp(fs, OP_BUILD_TUPLE, n->line);
      emitByte(fs, (ms_u8)count);
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
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      compileCompr(fs, n);
      break;
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
    localOp(fs, OP_STORE_LOCAL, decl->bindSlot, decl->line);
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

// 声明式落盘（DESTRUCTURE 目标 = binder 登记的新绑定）：global DEF+STORE；local STORE
// （同 finishDeclStore 语义，载荷取 as.ident）
static void storeDeclIdent(FuncState* fs, const AstNode* t) {
  if (t->bindKind == AST_BIND_LOCAL) {
    localOp(fs, OP_STORE_LOCAL, t->bindSlot, t->line);
  } else {
    int32_t k = nameConst(fs, t->as.ident.name, t->as.ident.nameLen);
    emitConstOp(fs, OP_DEF_GLOBAL, k, t->line);
    emitConstOp(fs, OP_STORE_GLOBAL, k, t->line);
  }
}

// 解构落盘：值在栈顶（逆序消费）——IDENT 按声明/引用语义直存；INDEX 先 obj/idx 再 ROT 对齐
// SET_INDEX 弹序（v obj idx → obj idx v）；MEMBER 目标与单目标赋值同样归 T020
static void storeUnpackTarget(FuncState* fs, AstNode* t, bool declare) {
  if (t->kind == AST_IDENT) {
    if (declare) {
      storeDeclIdent(fs, t);
    } else {
      storeIdent(fs, t);
    }
    return;
  }
  if (t->kind == AST_INDEX) {
    compileExpr(fs, t->as.indexExpr.obj);
    compileExpr(fs, t->as.indexExpr.index);
    emitOp(fs, OP_ROT, t->line);
    emitOp(fs, OP_SET_INDEX, t->line);
    return;
  }
  compErr(fs, t, 1, "assignment target form is not supported yet");
  emitOp(fs, OP_POP, t->line);
}

// 元组目标赋值（04 §2，无星号）：右值多表达式逐个压栈（临时栈序列，T015 统一走 BUILD_TUPLE）；
// 单表达式 UNPACK_SEQ 展开；目标数可少于右值元素（多余丢弃）
static void compileTupleAssign(FuncState* fs, AstNode* n, AstNode* target) {
  int32_t count = target->as.tuple.count;
  const AstNode* value = n->as.assign.value;
  if (count > 255) {
    compErr(fs, n, 1, "too many unpack targets");
    return;
  }
  if (value->kind == AST_TUPLE) {
    int32_t m = value->as.tuple.count;
    for (int32_t i = 0; i < m; i++) {
      compileExpr(fs, value->as.tuple.items[i]);
    }
    if (m != count) {  // 字面计数失配：编译期诊断（发射面保持平衡）
      compErr(fs, n, 1, "unpack target and value counts differ");
      for (int32_t i = 0; i < m; i++) {
        emitOp(fs, OP_POP, n->line);
      }
      return;
    }
  } else {
    compileExpr(fs, value);
    emitOp(fs, OP_UNPACK_SEQ, n->line);
    emitByte(fs, (ms_u8)count);
  }
  for (int32_t i = count - 1; i >= 0; i--) {  // 目标为既有绑定的赋值：引用式落盘
    storeUnpackTarget(fs, target->as.tuple.items[i], false);
  }
}

// 含 *rest 的解构赋值（04 §2）：右值物化为 list（多表达式经 BUILD_LIST）后 UNPACK_STAR 展平
static void compileDestructure(FuncState* fs, AstNode* n) {
  int32_t count = n->as.destructure.count;
  int32_t rest = n->as.destructure.restIndex;
  const AstNode* value = n->as.destructure.value;
  if (count > 255) {
    compErr(fs, n, 1, "too many unpack targets");
    return;
  }
  if (value->kind == AST_TUPLE) {
    if (value->as.tuple.count > 255) {  // BUILD_LIST u8 计数上限（review：防截断错位）
      compErr(fs, n, 1, "too many values to unpack");
      return;
    }
    for (int32_t i = 0; i < value->as.tuple.count; i++) {
      compileExpr(fs, value->as.tuple.items[i]);
    }
    emitOp(fs, OP_BUILD_LIST, n->line);
    emitByte(fs, (ms_u8)value->as.tuple.count);
  } else {
    compileExpr(fs, value);
  }
  emitOp(fs, OP_UNPACK_STAR, n->line);
  emitByte(fs, (ms_u8)rest);
  emitByte(fs, (ms_u8)(count - rest - 1));
  for (int32_t i = count - 1; i >= 0; i--) {  // 目标为 binder 登记的新绑定：声明式落盘
    storeUnpackTarget(fs, n->as.destructure.targets[i], true);
  }
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
  if (target->kind == AST_TUPLE && n->as.assign.op == AST_OP_NONE) {  // 元组目标解构（04 §2）
    compileTupleAssign(fs, n, target);
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

// 通用 AST 遍历：收集 BIND_LOCAL bindSlot 最大值（含推导式目标/嵌套函数——安全超估），
// 供推导式临时槽基线（nextTemp 起点高于全部真实局部槽，杜绝冲突）
static void scanMaxLocalSlot(const AstNode* n, int32_t* maxSlot) {
  if (n == NULL) {
    return;
  }
  if (n->bindKind == AST_BIND_LOCAL && n->bindSlot > *maxSlot) {
    *maxSlot = n->bindSlot;
  }
  switch (n->kind) {
    case AST_MODULE:
    case AST_BLOCK:
    case AST_LIST:
    case AST_SET:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        scanMaxLocalSlot(n->as.list.items[i], maxSlot);
      }
      break;
    case AST_VAR_DECL:
      scanMaxLocalSlot(n->as.varDecl.init, maxSlot);
      break;
    case AST_ASSIGN:
      scanMaxLocalSlot(n->as.assign.target, maxSlot);
      scanMaxLocalSlot(n->as.assign.value, maxSlot);
      break;
    case AST_DESTRUCTURE:
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        scanMaxLocalSlot(n->as.destructure.targets[i], maxSlot);
      }
      scanMaxLocalSlot(n->as.destructure.value, maxSlot);
      break;
    case AST_EXPR_STMT:
    case AST_RETURN:
    case AST_THROW:
    case AST_YIELD:
    case AST_DEFER:
      scanMaxLocalSlot(n->as.exprStmt.expr, maxSlot);
      break;
    case AST_IF:
      scanMaxLocalSlot(n->as.ifStmt.cond, maxSlot);
      scanMaxLocalSlot(n->as.ifStmt.thenBlock, maxSlot);
      scanMaxLocalSlot(n->as.ifStmt.elseNode, maxSlot);
      break;
    case AST_FOR:
      scanMaxLocalSlot(n->as.forStmt.init, maxSlot);
      scanMaxLocalSlot(n->as.forStmt.cond, maxSlot);
      scanMaxLocalSlot(n->as.forStmt.post, maxSlot);
      scanMaxLocalSlot(n->as.forStmt.target, maxSlot);
      scanMaxLocalSlot(n->as.forStmt.iter, maxSlot);
      scanMaxLocalSlot(n->as.forStmt.body, maxSlot);
      break;
    case AST_WHILE:
      scanMaxLocalSlot(n->as.whileStmt.cond, maxSlot);
      scanMaxLocalSlot(n->as.whileStmt.body, maxSlot);
      break;
    case AST_SWITCH:
      scanMaxLocalSlot(n->as.switchStmt.subject, maxSlot);
      for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
        scanMaxLocalSlot(n->as.switchStmt.cases[i], maxSlot);
      }
      break;
    case AST_SWITCH_CASE:
      for (int32_t i = 0; i < n->as.switchCase.valueCount; i++) {
        scanMaxLocalSlot(n->as.switchCase.values[i], maxSlot);
      }
      scanMaxLocalSlot(n->as.switchCase.body, maxSlot);
      break;
    case AST_SEND:
      scanMaxLocalSlot(n->as.sendStmt.chan, maxSlot);
      scanMaxLocalSlot(n->as.sendStmt.value, maxSlot);
      break;
    case AST_SELECT:
      for (int32_t i = 0; i < n->as.selectStmt.count; i++) {
        scanMaxLocalSlot(n->as.selectStmt.cases[i], maxSlot);
      }
      break;
    case AST_SELECT_CASE:
      scanMaxLocalSlot(n->as.selectCase.target, maxSlot);
      scanMaxLocalSlot(n->as.selectCase.chan, maxSlot);
      scanMaxLocalSlot(n->as.selectCase.value, maxSlot);
      scanMaxLocalSlot(n->as.selectCase.body, maxSlot);
      break;
    case AST_FUNC_DECL:
      for (int32_t i = 0; i < n->as.funcDecl.paramCount; i++) {
        scanMaxLocalSlot(n->as.funcDecl.params[i].defaultExpr, maxSlot);
      }
      scanMaxLocalSlot(n->as.funcDecl.body, maxSlot);
      scanMaxLocalSlot(n->as.funcDecl.exprBody, maxSlot);
      break;
    case AST_CLASS_DECL:
      for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
        scanMaxLocalSlot(n->as.classDecl.bases[i], maxSlot);
      }
      for (int32_t i = 0; i < n->as.classDecl.memberCount; i++) {
        scanMaxLocalSlot(n->as.classDecl.members[i], maxSlot);
      }
      break;
    case AST_TRY:
      scanMaxLocalSlot(n->as.tryStmt.tryBlock, maxSlot);
      for (int32_t i = 0; i < n->as.tryStmt.catchCount; i++) {
        const AstCatchClause* c = &n->as.tryStmt.catches[i];
        for (int32_t t = 0; t < c->typeCount; t++) {
          scanMaxLocalSlot(c->types[t], maxSlot);
        }
        scanMaxLocalSlot(c->block, maxSlot);
      }
      scanMaxLocalSlot(n->as.tryStmt.finallyBlock, maxSlot);
      break;
    case AST_STRING:
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        scanMaxLocalSlot(n->as.strLit.segs[i].expr, maxSlot);
      }
      break;
    case AST_TUPLE:
      for (int32_t i = 0; i < n->as.tuple.count; i++) {
        scanMaxLocalSlot(n->as.tuple.items[i], maxSlot);
      }
      break;
    case AST_MAP:
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        scanMaxLocalSlot(n->as.mapLit.keys[i], maxSlot);
        scanMaxLocalSlot(n->as.mapLit.values[i], maxSlot);
      }
      break;
    case AST_BINARY:
    case AST_LOGIC:
      scanMaxLocalSlot(n->as.binary.lhs, maxSlot);
      scanMaxLocalSlot(n->as.binary.rhs, maxSlot);
      break;
    case AST_UNARY:
    case AST_RECV:
    case AST_GO:
    case AST_AWAIT:
      scanMaxLocalSlot(n->as.unary.operand, maxSlot);
      break;
    case AST_TERNARY:
      scanMaxLocalSlot(n->as.ternary.cond, maxSlot);
      scanMaxLocalSlot(n->as.ternary.thenExpr, maxSlot);
      scanMaxLocalSlot(n->as.ternary.elseExpr, maxSlot);
      break;
    case AST_CALL:
      scanMaxLocalSlot(n->as.call.callee, maxSlot);
      for (int32_t i = 0; i < n->as.call.count; i++) {
        scanMaxLocalSlot(n->as.call.args[i], maxSlot);
      }
      break;
    case AST_INDEX:
      scanMaxLocalSlot(n->as.indexExpr.obj, maxSlot);
      scanMaxLocalSlot(n->as.indexExpr.index, maxSlot);
      break;
    case AST_MEMBER:
      scanMaxLocalSlot(n->as.memberExpr.obj, maxSlot);
      break;
    case AST_SLICE:
      scanMaxLocalSlot(n->as.slice.obj, maxSlot);
      scanMaxLocalSlot(n->as.slice.start, maxSlot);
      scanMaxLocalSlot(n->as.slice.end, maxSlot);
      scanMaxLocalSlot(n->as.slice.step, maxSlot);
      break;
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:
      scanMaxLocalSlot(n->as.compr.key, maxSlot);
      scanMaxLocalSlot(n->as.compr.value, maxSlot);
      for (int32_t i = 0; i < n->as.compr.forCount; i++) {
        scanMaxLocalSlot(n->as.compr.fors[i].target, maxSlot);
        scanMaxLocalSlot(n->as.compr.fors[i].iter, maxSlot);
      }
      for (int32_t i = 0; i < n->as.compr.condCount; i++) {
        scanMaxLocalSlot(n->as.compr.conds[i], maxSlot);
      }
      break;
    case AST_FUNC_EXPR:
      for (int32_t i = 0; i < n->as.funcExpr.paramCount; i++) {
        scanMaxLocalSlot(n->as.funcExpr.params[i].defaultExpr, maxSlot);
      }
      scanMaxLocalSlot(n->as.funcExpr.body, maxSlot);
      scanMaxLocalSlot(n->as.funcExpr.exprBody, maxSlot);
      break;
    default:
      break;
  }
}

// 函数原型：体编译为独立 chunk（RETURN/局部槽位来自 Binder 标注），挂入常量池
static void compileFuncProto(FuncState* fs, AstNode* n, Chunk** protoOut) {
  Chunk* proto = malloc(sizeof(Chunk));
  MS_ASSERT(proto != NULL);
  chunkInit(proto, n->as.funcDecl.name, n->as.funcDecl.nameLen);
  // 临时槽基线：高于全部真实局部槽（预扫描安全超估）且不低于参数槽数
  AstNode* body = n->as.funcDecl.body;
  int32_t scan = -1;
  scanMaxLocalSlot(body, &scan);
  int32_t base = n->as.funcDecl.paramCount;
  if (scan + 1 > base) {
    base = scan + 1;
  }
  FuncState inner = {proto, NULL, 0, 0, NULL, 0, 0, fs->diags, fs->file, 0, base, NULL, 0, 0};
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
  // localCount 含参数槽（未引用参数亦占位）与推导式临时槽：T018 帧入参区依赖
  proto->localCount = inner.localCount > n->as.funcDecl.paramCount ? inner.localCount : n->as.funcDecl.paramCount;
  free(inner.jumps);
  free(inner.loops);
  free(inner.comprStack);
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

// for-in 目标落盘（binder 已按 fresh 声明标注，同单变量路径 04 §4）：IDENT 直存；
// TUPLE 先 UNPACK_SEQ 再逆序递归（栈顶为末元素；嵌套目标先于更早平铺目标消费）
static void storeIterTarget(FuncState* fs, AstNode* t) {
  if (t->kind == AST_TUPLE) {
    if (t->as.tuple.count > 255) {  // UNPACK_SEQ u8 计数上限
      compErr(fs, t, 1, "too many iteration targets");
      return;
    }
    emitOp(fs, OP_UNPACK_SEQ, t->line);
    emitByte(fs, (ms_u8)t->as.tuple.count);
    for (int32_t i = t->as.tuple.count - 1; i >= 0; i--) {  // 逆序消费：UNPACK 后栈顶为末元素
      storeIterTarget(fs, t->as.tuple.items[i]);
    }
    return;
  }
  if (t->kind != AST_IDENT) {  // BAD_TARGET 残留防御（诊断已发，编译整体中止）
    compErr(fs, t, 1, "iteration target form is not supported yet");
    emitOp(fs, OP_POP, t->line);
    return;
  }
  if (t->bindKind == AST_BIND_LOCAL) {
    localOp(fs, OP_STORE_LOCAL, t->bindSlot, t->line);
    return;
  }
  int32_t k = nameConst(fs, t->as.ident.name, t->as.ident.nameLen);
  emitConstOp(fs, OP_DEF_GLOBAL, k, t->line);  // 模块层循环变量：声明式落盘（fresh 04 §4）
  emitConstOp(fs, OP_STORE_GLOBAL, k, t->line);
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
    } else {
      storeIterTarget(fs, target);  // IDENT 直存 / TUPLE 解构递归（多变量迭代 04 §4）
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
    localOp(fs, OP_STORE_LOCAL, c->bindSlot, c->line);
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
    case AST_DESTRUCTURE:
      compileDestructure(fs, n);
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
  // 临时槽基线：模块层无自有 local，预扫描（含推导式目标/嵌套函数，安全超估）后从其上起步
  int32_t scan = -1;
  scanMaxLocalSlot(module, &scan);
  FuncState fs = {out, NULL, 0, 0, NULL, 0, 0, diags, file, 0, scan + 1, NULL, 0, 0};
  for (int32_t i = 0; i < module->as.list.count; i++) {
    compileStmt(&fs, module->as.list.items[i]);
  }
  int32_t lastLine = module->as.list.count > 0 ? module->as.list.items[module->as.list.count - 1]->line : 1;
  emitOp(&fs, OP_LOAD_NIL, lastLine);  // 模块恒以 nil 返回收尾（21 §4 main 形态）
  emitOp(&fs, OP_RETURN, lastLine);
  finishChunk(&fs);
  out->localCount = fs.localCount;
  free(fs.jumps);
  free(fs.loops);
  free(fs.comprStack);
  for (int32_t i = 0; i < diags->count; i++) {
    if (diags->items[i].level == MS_DIAG_ERROR) {
      return false;
    }
  }
  return true;
}
