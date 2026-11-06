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

// 函数 upvalue 表条目（T018）：isLocal = 直接捕获定义函数帧槽 index；否则经宿主闭包 upvalues[index] 透传
typedef struct UpvalEntry {
  const char* name;  // 借用源文本
  int32_t nameLen;
  int32_t index;
  bool isLocal;
} UpvalEntry;

// 循环子树直接捕获槽收集（CLOSE_UPVAL 发射判定，04 §4）
typedef struct SlotSet {
  int32_t* items;
  int32_t count, cap;
} SlotSet;

// tri 循环每轮新鲜槽重映射条目（T018 04 §4）：carry 槽 V → fresh 槽 W
typedef struct SlotAlias {
  int32_t from, to;
} SlotAlias;

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
  UpvalEntry* upvals;      // 本函数 upvalue 表（T018：proto 编译起始自 binder 附着表构建）
  int32_t upvalCount, upvalCap;
  SlotAlias* aliases;       // tri 循环 fresh 目标槽位重映射栈 V→W（嵌套捕获型循环分层，T018 04 §4）
  int32_t aliasCount, aliasCap;
} FuncState;

static void compErr(FuncState* fs, const AstNode* n, int32_t len, const char* msg) {
  MsDiagnostic diag = {MS_DIAG_ERROR, MSE_COMPILE_UNSUPPORTED, fs->file, n->line, n->col, len, msg};
  msDiagAdd(fs->diags, &diag);
}

// 无节点语境诊断（localOp 槽域校验）：行号由调用方给定，列/长度占位 1
static void compErrAt(FuncState* fs, int32_t line, const char* msg) {
  MsDiagnostic diag = {MS_DIAG_ERROR, MSE_COMPILE_UNSUPPORTED, fs->file, line, 1, 1, msg};
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

// ---- tri 循环 fresh 目标 alias 栈（T018 04 §4） ----

// 自栈顶（最内层循环）向下首个 from 命中即重映射（内层遮蔽外层；未命中原样返回）
static int32_t aliasRemap(const FuncState* fs, int32_t slot) {
  for (int32_t i = fs->aliasCount - 1; i >= 0; i--) {
    if (fs->aliases[i].from == slot) {
      return fs->aliases[i].to;
    }
  }
  return slot;
}

static void pushAlias(FuncState* fs, int32_t from, int32_t to) {
  SlotAlias a = {from, to};
  MS_ARRAY_PUSH(fs->aliases, fs->aliasCount, fs->aliasCap, a);
}

static void popAlias(FuncState* fs) { fs->aliasCount--; }

// 局部槽访问统一入口：槽位域校验（u8 操作数）+ localCount 跟踪 + 发射
// tri 循环 fresh 目标编译期重映射 V→W（cond/body/post 期间生效，T018 04 §4）
static void localOp(FuncState* fs, ms_u8 op, int32_t slot, int32_t line) {
  // alias 仅作用于 LOAD/STORE 语义：CLOSE_UPVAL 目标恒为已解析的 W 槽（W 非任何 alias from，重映射无操作）
  slot = aliasRemap(fs, slot);
  if (slot > 255) {
    compErrAt(fs, line, "too many local variables");
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

static void compileFunctionValue(FuncState* fs, const AstNode* n, bool isMethod);

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

// ---- upvalue 表（T018）：本函数按名查捕获索引；未命中 -1 ----

static int32_t findUpvalByName(const FuncState* fs, const char* name, int32_t len) {
  for (int32_t i = 0; i < fs->upvalCount; i++) {
    if (fs->upvals[i].nameLen == len && memcmp(fs->upvals[i].name, name, (size_t)len) == 0) {
      return i;
    }
  }
  return -1;
}

// upvalue 操作数发射（u8 域校验；超限诊断后占位维持发射面）
static void upvalOp(FuncState* fs, ms_u8 op, int32_t idx, int32_t line) {
  if (idx > 255) {
    compErrAt(fs, line, "too many upvalues");
    idx = 0;
  }
  emitOp(fs, op, line);
  emitByte(fs, (ms_u8)idx);
}

// ---- 循环子树捕获槽收集（T018 04 §4：CLOSE_UPVAL 仅对被闭包直接捕获的 fresh 目标发射） ----
// 以真实帧 F（或推导式伪帧，comprDepth 相对起算）为根收集子树内函数直接捕获该帧槽位的槽号。
// 判定与 compileFuncProto 的 isLocal 规则一致：realDepth==0 且 entry.depth-1==comprDepth
// （depth-1<comprDepth 为更内层伪帧目标，归推导式路径；realDepth>0 已隔真实函数边界，归透传）。
// 维护不变量：与 scanMaxLocalSlot 同——新增含子节点的 AstKind 必须同步本遍历（静默漏扫 → 漏发 CLOSE）。
static void slotSetAdd(SlotSet* s, int32_t slot) {
  for (int32_t i = 0; i < s->count; i++) {
    if (s->items[i] == slot) {
      return;
    }
  }
  MS_ARRAY_PUSH(s->items, s->count, s->cap, slot);
}

static bool slotSetHas(const SlotSet* s, int32_t slot) {
  for (int32_t i = 0; i < s->count; i++) {
    if (s->items[i] == slot) {
      return true;
    }
  }
  return false;
}

static void scanDirectCaptures(const AstNode* n, int32_t realDepth, int32_t comprDepth, SlotSet* out) {
  if (n == NULL) {
    return;
  }
  switch (n->kind) {
    case AST_MODULE:
    case AST_BLOCK:
    case AST_LIST:
    case AST_SET:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        scanDirectCaptures(n->as.list.items[i], realDepth, comprDepth, out);
      }
      break;
    case AST_VAR_DECL:
      scanDirectCaptures(n->as.varDecl.init, realDepth, comprDepth, out);
      break;
    case AST_ASSIGN:
      scanDirectCaptures(n->as.assign.target, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.assign.value, realDepth, comprDepth, out);
      break;
    case AST_DESTRUCTURE:
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        scanDirectCaptures(n->as.destructure.targets[i], realDepth, comprDepth, out);
      }
      scanDirectCaptures(n->as.destructure.value, realDepth, comprDepth, out);
      break;
    case AST_EXPR_STMT:
    case AST_RETURN:
    case AST_THROW:
    case AST_YIELD:
    case AST_DEFER:
      scanDirectCaptures(n->as.exprStmt.expr, realDepth, comprDepth, out);
      break;
    case AST_IF:
      scanDirectCaptures(n->as.ifStmt.cond, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.ifStmt.thenBlock, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.ifStmt.elseNode, realDepth, comprDepth, out);
      break;
    case AST_FOR:
      scanDirectCaptures(n->as.forStmt.init, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.forStmt.cond, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.forStmt.post, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.forStmt.target, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.forStmt.iter, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.forStmt.body, realDepth, comprDepth, out);
      break;
    case AST_WHILE:
      scanDirectCaptures(n->as.whileStmt.cond, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.whileStmt.body, realDepth, comprDepth, out);
      break;
    case AST_SWITCH:
      scanDirectCaptures(n->as.switchStmt.subject, realDepth, comprDepth, out);
      for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
        scanDirectCaptures(n->as.switchStmt.cases[i], realDepth, comprDepth, out);
      }
      break;
    case AST_SWITCH_CASE:
      for (int32_t i = 0; i < n->as.switchCase.valueCount; i++) {
        scanDirectCaptures(n->as.switchCase.values[i], realDepth, comprDepth, out);
      }
      scanDirectCaptures(n->as.switchCase.body, realDepth, comprDepth, out);
      break;
    case AST_SEND:
      scanDirectCaptures(n->as.sendStmt.chan, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.sendStmt.value, realDepth, comprDepth, out);
      break;
    case AST_SELECT:
      for (int32_t i = 0; i < n->as.selectStmt.count; i++) {
        scanDirectCaptures(n->as.selectStmt.cases[i], realDepth, comprDepth, out);
      }
      break;
    case AST_SELECT_CASE:
      scanDirectCaptures(n->as.selectCase.target, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.selectCase.chan, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.selectCase.value, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.selectCase.body, realDepth, comprDepth, out);
      break;
    case AST_FUNC_DECL:
    case AST_FUNC_EXPR: {
      const AstUpvalRef* upvals =
          n->kind == AST_FUNC_DECL ? n->as.funcDecl.upvals : n->as.funcExpr.upvals;
      int32_t upvalCount = n->kind == AST_FUNC_DECL ? n->as.funcDecl.upvalCount : n->as.funcExpr.upvalCount;
      const AstParam* params = n->kind == AST_FUNC_DECL ? n->as.funcDecl.params : n->as.funcExpr.params;
      int32_t paramCount = n->kind == AST_FUNC_DECL ? n->as.funcDecl.paramCount : n->as.funcExpr.paramCount;
      if (realDepth == 0) {  // 与根帧之间无真实函数边界：直接捕获根帧槽的条目在此判定
        for (int32_t i = 0; i < upvalCount; i++) {
          if (upvals[i].depth - 1 == comprDepth) {
            slotSetAdd(out, upvals[i].slot);
          }
        }
      }
      for (int32_t i = 0; i < paramCount; i++) {  // 默认值在外层域求值（05 §1）：不越函数边界
        scanDirectCaptures(params[i].defaultExpr, realDepth, comprDepth, out);
      }
      scanDirectCaptures(n->kind == AST_FUNC_DECL ? n->as.funcDecl.body : n->as.funcExpr.body, realDepth + 1,
                         comprDepth, out);
      scanDirectCaptures(n->kind == AST_FUNC_DECL ? n->as.funcDecl.exprBody : n->as.funcExpr.exprBody,
                         realDepth + 1, comprDepth, out);
      break;
    }
    case AST_CLASS_DECL:
      for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
        scanDirectCaptures(n->as.classDecl.bases[i], realDepth, comprDepth, out);
      }
      for (int32_t i = 0; i < n->as.classDecl.memberCount; i++) {
        scanDirectCaptures(n->as.classDecl.members[i], realDepth, comprDepth, out);
      }
      break;
    case AST_TRY:
      scanDirectCaptures(n->as.tryStmt.tryBlock, realDepth, comprDepth, out);
      for (int32_t i = 0; i < n->as.tryStmt.catchCount; i++) {
        const AstCatchClause* c = &n->as.tryStmt.catches[i];
        for (int32_t t = 0; t < c->typeCount; t++) {
          scanDirectCaptures(c->types[t], realDepth, comprDepth, out);
        }
        scanDirectCaptures(c->block, realDepth, comprDepth, out);
      }
      scanDirectCaptures(n->as.tryStmt.finallyBlock, realDepth, comprDepth, out);
      break;
    case AST_STRING:
      for (int32_t i = 0; i < n->as.strLit.count; i++) {
        scanDirectCaptures(n->as.strLit.segs[i].expr, realDepth, comprDepth, out);
      }
      break;
    case AST_TUPLE:
      for (int32_t i = 0; i < n->as.tuple.count; i++) {
        scanDirectCaptures(n->as.tuple.items[i], realDepth, comprDepth, out);
      }
      break;
    case AST_MAP:
      for (int32_t i = 0; i < n->as.mapLit.count; i++) {
        scanDirectCaptures(n->as.mapLit.keys[i], realDepth, comprDepth, out);
        scanDirectCaptures(n->as.mapLit.values[i], realDepth, comprDepth, out);
      }
      break;
    case AST_BINARY:
    case AST_LOGIC:
      scanDirectCaptures(n->as.binary.lhs, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.binary.rhs, realDepth, comprDepth, out);
      break;
    case AST_UNARY:
    case AST_RECV:
    case AST_GO:
    case AST_AWAIT:
      scanDirectCaptures(n->as.unary.operand, realDepth, comprDepth, out);
      break;
    case AST_TERNARY:
      scanDirectCaptures(n->as.ternary.cond, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.ternary.thenExpr, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.ternary.elseExpr, realDepth, comprDepth, out);
      break;
    case AST_CALL:
      scanDirectCaptures(n->as.call.callee, realDepth, comprDepth, out);
      for (int32_t i = 0; i < n->as.call.count; i++) {
        scanDirectCaptures(n->as.call.args[i], realDepth, comprDepth, out);
      }
      break;
    case AST_INDEX:
      scanDirectCaptures(n->as.indexExpr.obj, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.indexExpr.index, realDepth, comprDepth, out);
      break;
    case AST_MEMBER:
      scanDirectCaptures(n->as.memberExpr.obj, realDepth, comprDepth, out);
      break;
    case AST_SLICE:
      scanDirectCaptures(n->as.slice.obj, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.slice.start, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.slice.end, realDepth, comprDepth, out);
      scanDirectCaptures(n->as.slice.step, realDepth, comprDepth, out);
      break;
    case AST_COMPREHENSION:
    case AST_GEN_EXPR:  // 推导式为编译期伪帧边界（内联消解）：子树按 comprDepth+1 续扫
      scanDirectCaptures(n->as.compr.key, realDepth, comprDepth + 1, out);
      scanDirectCaptures(n->as.compr.value, realDepth, comprDepth + 1, out);
      for (int32_t i = 0; i < n->as.compr.forCount; i++) {
        scanDirectCaptures(n->as.compr.fors[i].target, realDepth, comprDepth + 1, out);
        scanDirectCaptures(n->as.compr.fors[i].iter, realDepth, comprDepth + 1, out);
      }
      for (int32_t i = 0; i < n->as.compr.condCount; i++) {
        scanDirectCaptures(n->as.compr.conds[i], realDepth, comprDepth + 1, out);
      }
      break;
    default:
      break;
  }
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
  int32_t idx = findUpvalByName(fs, n->as.ident.name, n->as.ident.nameLen);  // T018：宿主闭包装载
  if (idx < 0) {
    compErr(fs, n, n->as.ident.nameLen, "upvalue capture is not registered");
    emitOp(fs, OP_LOAD_NIL, n->line);  // 保持栈平衡的占位值
    return;
  }
  upvalOp(fs, OP_LOAD_UPVAL, idx, n->line);
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
  int32_t idx = findUpvalByName(fs, n->as.ident.name, n->as.ident.nameLen);  // T018：宿主闭包落盘
  if (idx < 0) {
    compErr(fs, n, n->as.ident.nameLen, "upvalue capture is not registered");
    emitOp(fs, OP_POP, n->line);  // 弹出待存值，维持栈平衡
    return;
  }
  upvalOp(fs, OP_STORE_UPVAL, idx, n->line);
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
    emitByte(fs, 0);  // flags：无展开（T018）
    emitOp(fs, OP_POP, n->line);  // append/add 返回 nil 弹净
    return;
  }
  compileExpr(fs, n->as.compr.fors[forIdx].iter);  // 迭代源在第 i 层子句域求值（03 §2）
  emitOp(fs, OP_GET_ITER, n->line);
  int32_t lTop = fs->chunk->codeLen;
  int32_t jExit = emitJump(fs, OP_FOR_ITER, n->line);
  // T018：本子句 fresh 目标被后续子树闭包捕获（04 §4）——产出后、落盘前闭合上一轮开放 upvalue
  //（捕获槽以本推导式伪帧为根收集，与 compileFuncProto 的 isLocal 判定同基准）
  SlotSet caps = {NULL, 0, 0};
  for (int32_t i = forIdx + 1; i < n->as.compr.forCount; i++) {
    scanDirectCaptures(n->as.compr.fors[i].iter, 0, 0, &caps);
  }
  for (int32_t i = 0; i < n->as.compr.condCount; i++) {
    scanDirectCaptures(n->as.compr.conds[i], 0, 0, &caps);
  }
  scanDirectCaptures(n->as.compr.key, 0, 0, &caps);
  scanDirectCaptures(n->as.compr.value, 0, 0, &caps);
  AstNode* ct = n->as.compr.fors[forIdx].target;
  if (ct != NULL) {
    if (ct->kind == AST_TUPLE) {
      for (int32_t i = 0; i < ct->as.tuple.count; i++) {
        AstNode* ti = ct->as.tuple.items[i];
        if (ti->kind == AST_IDENT && slotSetHas(&caps, ti->bindSlot)) {
          localOp(fs, OP_CLOSE_UPVAL, comprSlot(fs, ti->bindSlot), ti->line);  // 物化并闭合重映射槽
        }
      }
    } else if (ct->kind == AST_IDENT && slotSetHas(&caps, ct->bindSlot)) {
      localOp(fs, OP_CLOSE_UPVAL, comprSlot(fs, ct->bindSlot), ct->line);
    }
  }
  free(caps.items);
  storeComprTarget(fs, n->as.compr.fors[forIdx].target);
  compileComprBody(fs, n, acc, forIdx + 1, forIdx == n->as.compr.forCount - 1 ? lTop : lTopInner);
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  fs->jumps[jExit].target = fs->chunk->codeLen;
}

// AST_COMPREHENSION 内联脱糖（03 §1-§2；genexp 已于 parser 期脱糖为生成器调用，T019）
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
    case AST_CALL: {  // callee + args 依序 + CALL argc（末位 * 展开经 flags 位，T018）
      bool spreadLast = false;  // parser 限定 * 恒末位；编译器侧防御复核
      for (int32_t i = 0; i < n->as.call.count; i++) {
        if (n->as.call.spread[i]) {
          if (i != n->as.call.count - 1) {
            compErr(fs, n, 1, "spread argument must be the last argument");
          } else {
            spreadLast = true;
          }
        }
      }
      if (n->as.call.count > 255) {  // CALL/CALL_METHOD argc 为 u8 操作数
        compErr(fs, n, 1, "too many arguments in call");
        emitOp(fs, OP_LOAD_NIL, n->line);  // 占位维持发射面
        break;
      }
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
        emitByte(fs, (ms_u8)(spreadLast ? 1 : 0));  // flags：末位展开位
        break;
      }
      compileExpr(fs, n->as.call.callee);  // 任意可调用表达式（IDENT/INDEX/调用链等）求值入栈（T018 泛化）
      for (int32_t i = 0; i < n->as.call.count; i++) {
        compileExpr(fs, n->as.call.args[i]);
      }
      emitOp(fs, OP_CALL, n->line);
      emitByte(fs, (ms_u8)n->as.call.count);
      emitByte(fs, (ms_u8)(spreadLast ? 1 : 0));  // flags：末位展开位
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
      compileCompr(fs, n);
      break;
    case AST_GEN_EXPR:  // T019：parser 重写后不可达（防御）
      compErr(fs, n, 1, "generator expression was not desugared");
      emitOp(fs, OP_LOAD_NIL, n->line);
      break;
    case AST_FUNC_EXPR:  // 匿名函数表达式（05 §1）：物化函数值
      compileFunctionValue(fs, n, false);
      break;
    case AST_THIS: {  // T020：binder 已按隐藏声明标注（LOCAL=方法直层 / UPVALUE=嵌套捕获）
      if (n->bindKind == AST_BIND_LOCAL) {
        localOp(fs, OP_LOAD_LOCAL, n->bindSlot, n->line);
      } else if (n->bindKind == AST_BIND_UPVALUE) {
        int32_t idx = findUpvalByName(fs, "this", 4);
        if (idx < 0) {  // 防御（binder 捕获记录已保证存在）
          compErr(fs, n, 4, "upvalue capture is not registered");
          emitOp(fs, OP_LOAD_NIL, n->line);
          break;
        }
        upvalOp(fs, OP_LOAD_UPVAL, idx, n->line);
      } else {  // binder 已诊断（防御占位）
        compErr(fs, n, 4, "'this' is only allowed inside a method");
        emitOp(fs, OP_LOAD_NIL, n->line);
      }
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
// SET_INDEX 弹序（v obj idx → obj idx v）；MEMBER 先 obj 再 SWAP 对齐 SET_ATTR 弹尽（v obj → obj v，T020）
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
  if (t->kind == AST_MEMBER) {  // T020：解构目标 obj.name（值在栈顶：SWAP 对齐后 SET_ATTR 弹尽）
    compileExpr(fs, t->as.memberExpr.obj);
    emitOp(fs, OP_SWAP, t->line);
    int32_t k = nameConst(fs, t->as.memberExpr.name, t->as.memberExpr.nameLen);
    emitConstOp(fs, OP_SET_ATTR, k, t->line);
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
  if (target->kind == AST_MEMBER) {  // T020：obj.name = v（复合同 DUP→GET→OP→SET）
    int32_t k = nameConst(fs, target->as.memberExpr.name, target->as.memberExpr.nameLen);
    compileExpr(fs, target->as.memberExpr.obj);
    if (n->as.assign.op != AST_OP_NONE) {
      emitOp(fs, OP_DUP, n->line);  // obj 双占：GET_ATTR 消费副本，SET_ATTR 消费原件
      emitConstOp(fs, OP_GET_ATTR, k, n->line);
      emitByte(fs, 0);  // icSlot（T022）
      compileExpr(fs, n->as.assign.value);
      emitOp(fs, binaryOpCode(n->as.assign.op), n->line);
    } else {
      compileExpr(fs, n->as.assign.value);
    }
    emitConstOp(fs, OP_SET_ATTR, k, n->line);
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

// 直层 yield 扫描（T019，05 §4 隐式生成器判定）：体内任一直层 yield 即生成器；
// 不进入嵌套函数体（其 yield 属该函数自身判定）；表达式位语法上不可含 yield（语句/表达式分离）
// 维护不变量：compileStmt 新增可含 yield 的语句形态时须同步本遍历（漏扫 → 隐式生成器漏判）
static bool bodyHasDirectYield(const AstNode* n) {
  if (n == NULL) {
    return false;
  }
  switch (n->kind) {
    case AST_YIELD:
      return true;
    case AST_MODULE:
    case AST_BLOCK:
      for (int32_t i = 0; i < n->as.list.count; i++) {
        if (bodyHasDirectYield(n->as.list.items[i])) {
          return true;
        }
      }
      return false;
    case AST_FUNC_DECL:
    case AST_FUNC_EXPR:
      return false;
    case AST_VAR_DECL:
      return bodyHasDirectYield(n->as.varDecl.init);
    case AST_ASSIGN:
      return bodyHasDirectYield(n->as.assign.target) || bodyHasDirectYield(n->as.assign.value);
    case AST_DESTRUCTURE:
      if (bodyHasDirectYield(n->as.destructure.value)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.destructure.count; i++) {
        if (bodyHasDirectYield(n->as.destructure.targets[i])) {
          return true;
        }
      }
      return false;
    case AST_EXPR_STMT:
    case AST_RETURN:
    case AST_THROW:
    case AST_DEFER:
      return bodyHasDirectYield(n->as.exprStmt.expr);
    case AST_IF:
      return bodyHasDirectYield(n->as.ifStmt.cond) || bodyHasDirectYield(n->as.ifStmt.thenBlock) ||
             bodyHasDirectYield(n->as.ifStmt.elseNode);
    case AST_FOR:
      return bodyHasDirectYield(n->as.forStmt.init) || bodyHasDirectYield(n->as.forStmt.cond) ||
             bodyHasDirectYield(n->as.forStmt.post) || bodyHasDirectYield(n->as.forStmt.target) ||
             bodyHasDirectYield(n->as.forStmt.iter) || bodyHasDirectYield(n->as.forStmt.body);
    case AST_WHILE:
      return bodyHasDirectYield(n->as.whileStmt.cond) || bodyHasDirectYield(n->as.whileStmt.body);
    case AST_SWITCH:
      if (bodyHasDirectYield(n->as.switchStmt.subject)) {
        return true;
      }
      for (int32_t i = 0; i < n->as.switchStmt.count; i++) {
        if (bodyHasDirectYield(n->as.switchStmt.cases[i])) {
          return true;
        }
      }
      return false;
    case AST_SWITCH_CASE:
      for (int32_t i = 0; i < n->as.switchCase.valueCount; i++) {
        if (bodyHasDirectYield(n->as.switchCase.values[i])) {
          return true;
        }
      }
      return bodyHasDirectYield(n->as.switchCase.body);
    default:  // 表达式/叶子/其余语句形态（yield 为语句，表达式位不可达）
      return false;
  }
}

// 通用 AST 遍历：收集 BIND_LOCAL bindSlot 最大值（含推导式目标/嵌套函数——安全超估），
// 供推导式临时槽基线（nextTemp 起点高于全部真实局部槽，杜绝冲突）
// 维护不变量：后续新增含子节点的 AstKind 必须同步加入本遍历（静默低估 → 函数帧临时/局部槽冲突）
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

// 函数原型：体编译为独立 chunk（RETURN/局部槽位来自 Binder 标注），挂入常量池；
// FUNC_DECL/FUNC_EXPR 载荷同名字段按 kind 取用（exprBody 为 -> 表达式体，05 §1）
static void compileFuncProto(FuncState* fs, const AstNode* n, bool isMethod, Chunk** protoOut) {
  bool isExpr = n->kind == AST_FUNC_EXPR;
  const AstParam* params = isExpr ? n->as.funcExpr.params : n->as.funcDecl.params;
  int32_t paramCount = isExpr ? n->as.funcExpr.paramCount : n->as.funcDecl.paramCount;
  AstNode* body = isExpr ? n->as.funcExpr.body : n->as.funcDecl.body;
  AstNode* exprBody = isExpr ? n->as.funcExpr.exprBody : n->as.funcDecl.exprBody;
  Chunk* proto = malloc(sizeof(Chunk));
  MS_ASSERT(proto != NULL);
  chunkInit(proto, isExpr ? NULL : n->as.funcDecl.name, isExpr ? 0 : n->as.funcDecl.nameLen);
  proto->paramCount = paramCount + (isMethod ? 1 : 0);  // T020：方法 slot 0 = this（参数顺延 1..n）
  if (proto->paramCount > 0) {
    proto->params = malloc(sizeof(MsParamName) * (size_t)proto->paramCount);
    MS_ASSERT(proto->params != NULL);
  }
  if (isMethod) {
    proto->params[0].name = "this";  // 隐藏接收者（binder 已占 slot 0；T020）
    proto->params[0].len = 4;
  }
  for (int32_t i = 0; i < paramCount; i++) {  // 形态防御（parser 已拦）+ 名字表（借用源文本，rest 名不含 *）
    proto->params[i + (isMethod ? 1 : 0)].name = params[i].name;
    proto->params[i + (isMethod ? 1 : 0)].len = params[i].nameLen;
    if (params[i].rest && i != paramCount - 1) {
      compErr(fs, n, 1, "rest parameter must be the last parameter");
    }
    if (params[i].defaultExpr != NULL) {
      proto->defaultParamCount++;
    } else if (proto->defaultParamCount > 0 && !params[i].rest) {
      compErr(fs, n, 1, "parameter with default must be followed by parameters with defaults");
    }
    if (params[i].rest) {
      proto->hasRest = true;
    }
  }
  if (proto->defaultParamCount > 127) {  // flags 低 7 位容量（u8 与 rest 位共享，T018）
    compErr(fs, n, 1, "too many default parameters");
  }
  ms_u8 fnFlags = isExpr ? n->as.funcExpr.flags : n->as.funcDecl.flags;
  if ((fnFlags & AST_FUNC_GENERATOR) != 0 || bodyHasDirectYield(body)) {  // T019：显式 func* 或隐式直层 yield
    proto->flags |= MS_CHUNK_GENERATOR;
  }
  if (isMethod) {  // T020：方法帧（VM 依此走 this 装配/GET_SUPER 语境）
    proto->flags |= MS_CHUNK_METHOD;
  }
  // 临时槽基线：高于全部真实局部槽（预扫描安全超估）且不低于参数槽数
  int32_t scan = -1;
  scanMaxLocalSlot(body, &scan);
  scanMaxLocalSlot(exprBody, &scan);
  int32_t base = proto->paramCount;
  if (scan + 1 > base) {
    base = scan + 1;
  }
  FuncState inner = {proto, NULL, 0, 0, NULL, 0, 0, fs->diags, fs->file, 0, base, NULL, 0, 0, NULL, 0, 0, NULL, 0, 0};
  // T018：宿主捕获表（binder 附着清单 → 本函数 upvalue 表，先于体编译）：
  // isLocal = 直接捕获本帧槽（depth-1 ≤ 定义处推导式深度；伪帧槽经 comprResolveSlot 重映射，
  // tri fresh 目标经 alias 重映射到每轮新鲜槽 W）；间接条目按名取宿主表（binder 透传链保证存在）
  const AstUpvalRef* upvals = isExpr ? n->as.funcExpr.upvals : n->as.funcDecl.upvals;
  int32_t upvalCount = isExpr ? n->as.funcExpr.upvalCount : n->as.funcDecl.upvalCount;
  for (int32_t i = 0; i < upvalCount; i++) {
    const AstUpvalRef* c = &upvals[i];
    bool isLocal = c->depth - 1 <= fs->comprDepth;
    int32_t index = 0;
    if (isLocal) {
      // 重映射顺序：先 compr（推导式伪帧槽 → 定义帧真实槽）再 alias 栈（真实 carry 槽 V → 每轮新鲜槽 W）；
      // alias 只作用于真实帧槽，颠倒顺序会拿伪帧槽号误比 alias from
      index = fs->comprDepth > 0 ? comprResolveSlot(fs, c->slot, c->depth - 1) : c->slot;
      index = aliasRemap(fs, index);
      if (index < 0) {
        compErr(fs, n, 1, "comprehension variable used before declaration");
        index = 0;
      }
    } else {
      index = findUpvalByName(fs, c->name, c->nameLen);
      if (index < 0) {
        compErr(fs, n, c->nameLen, "broken upvalue capture chain");
        index = 0;
      }
    }
    UpvalEntry e = {c->name, c->nameLen, index, isLocal};
    MS_ARRAY_PUSH(inner.upvals, inner.upvalCount, inner.upvalCap, e);
  }
  if (inner.upvalCount > 255) {  // LOAD/STORE_UPVAL 与描述表 index 均为 u8
    compErr(fs, n, 1, "too many upvalues");
  }
  proto->nUpvalues = inner.upvalCount;
  if (inner.upvalCount > 0) {
    proto->upvalDescs = malloc(sizeof(MsUpvalDesc) * (size_t)inner.upvalCount);
    MS_ASSERT(proto->upvalDescs != NULL);
    for (int32_t i = 0; i < inner.upvalCount; i++) {
      proto->upvalDescs[i].isLocal = inner.upvals[i].isLocal ? 1 : 0;
      proto->upvalDescs[i].index = (ms_u8)inner.upvals[i].index;
    }
  }
  if (body != NULL) {
    for (int32_t i = 0; i < body->as.list.count; i++) {
      compileStmt(&inner, body->as.list.items[i]);
    }
  } else {  // 表达式体（05 §1）：值编译 + 隐式 RETURN
    compileExpr(&inner, exprBody);
    emitOp(&inner, OP_RETURN, n->line);
  }
  // epilogue：仅当体末语句非 return 时补隐式 nil 返回（语句级判定——路径不敏感的字节级判定会在
  // 条件 return 形态下漏补，false 路径落出 codeLen 越界执行；末尾 return 必退出故无落空路径）
  int32_t last = body != NULL ? body->as.list.count - 1 : -1;
  bool tailReturns = exprBody != NULL || (last >= 0 && body->as.list.items[last]->kind == AST_RETURN);
  if (!tailReturns) {
    int32_t line = last >= 0 ? body->as.list.items[last]->line : n->line;
    emitOp(&inner, OP_LOAD_NIL, line);
    emitOp(&inner, OP_RETURN, line);
  }
  finishChunk(&inner);
  // localCount 含参数槽（未引用参数亦占位；方法含 this）与推导式临时槽：T018 帧入参区依赖
  proto->localCount = inner.localCount > proto->paramCount ? inner.localCount : proto->paramCount;
  free(inner.jumps);
  free(inner.loops);
  free(inner.comprStack);
  free(inner.upvals);
  free(inner.aliases);
  *protoOut = proto;
}

// 函数值物化（声明/表达式共用，21 §3.3/§4）：proto 编译入池 → 默认参依序压栈 → MAKE_FUNCTION/CLOSURE
// 默认参定义处求值（05 §1：默认值绑定于定义作用域，Python 语义）；
// 捕获函数（proto->nUpvalues > 0）与类方法（T020）走 CLOSURE（无 LOAD_CONST/MAKE_FUNCTION，T018）
static void compileFunctionValue(FuncState* fs, const AstNode* n, bool isMethod) {
  bool isExpr = n->kind == AST_FUNC_EXPR;
  const AstParam* params = isExpr ? n->as.funcExpr.params : n->as.funcDecl.params;
  int32_t paramCount = isExpr ? n->as.funcExpr.paramCount : n->as.funcDecl.paramCount;
  Chunk* proto = NULL;
  compileFuncProto(fs, n, isMethod, &proto);
  int32_t k = chunkAddConstProto(fs->chunk, proto);
  bool hasCaptures = proto->nUpvalues > 0;
  bool plain = !hasCaptures && !isMethod;
  if (plain) {
    emitConstOp(fs, OP_LOAD_CONST, k, n->line);
  }
  for (int32_t i = 0; i < paramCount; i++) {  // 栈形 [proto, d1..dn]：默认值居顶，fn 于 proto 槽就地物化
    if (params[i].defaultExpr != NULL) {
      compileExpr(fs, params[i].defaultExpr);
    }
  }
  if (plain) {
    emitOp(fs, OP_MAKE_FUNCTION, n->line);
    emitByte(fs, (ms_u8)(proto->defaultParamCount | (proto->hasRest ? 0x80 : 0)));
  } else {  // 栈形 [d1..dn, CLOSURE]：fn 依 proto 元数据就地物化后闭包覆盖栈顶（T018）
    // 捕获函数与类方法（T020：帧 closure 恒持——GET_SUPER 经 fn->owner）
    emitConstOp(fs, OP_CLOSURE, k, n->line);
  }
}

static void compileFuncDecl(FuncState* fs, AstNode* n) {
  compileFunctionValue(fs, n, false);
  finishDeclStore(fs, n);  // 声明落盘（global：DEF+STORE；local：STORE_LOCAL）
}

// 类声明编译（T020，21 §3.4/06 §1-§4）：bases 压栈 → NEW_CLASS → 逐成员 DEFINE_METHOD →
// INHERIT_INIT（MRO）→ 类名落盘。成员求值序 = 声明序（字段默认/static 初值类创建期求值一次）
static void compileClassDecl(FuncState* fs, AstNode* n) {
  if (n->as.classDecl.baseCount > 255) {  // NEW_CLASS baseCount 为 u8 操作数
    compErr(fs, n, n->as.classDecl.nameLen, "too many base classes");
    return;
  }
  for (int32_t i = 0; i < n->as.classDecl.baseCount; i++) {
    compileExpr(fs, n->as.classDecl.bases[i]);
  }
  int32_t k = nameConst(fs, n->as.classDecl.name, n->as.classDecl.nameLen);
  emitConstOp(fs, OP_NEW_CLASS, k, n->line);
  emitByte(fs, (ms_u8)n->as.classDecl.baseCount);
  for (int32_t i = 0; i < n->as.classDecl.memberCount; i++) {
    AstNode* m = n->as.classDecl.members[i];
    if (m->kind == AST_FUNC_DECL) {
      bool isStatic = (m->as.funcDecl.flags & AST_FUNC_STATIC) != 0;
      compileFunctionValue(fs, m, !isStatic);
      int32_t mk = nameConst(fs, m->as.funcDecl.name, m->as.funcDecl.nameLen);
      ms_u8 flags = isStatic ? MS_DEFM_STATIC : 0u;
      int32_t nl = m->as.funcDecl.nameLen;
      if (nl > 4 && m->as.funcDecl.name[0] == '_' && m->as.funcDecl.name[1] == '_' &&
          m->as.funcDecl.name[nl - 2] == '_' && m->as.funcDecl.name[nl - 1] == '_') {
        flags |= MS_DEFM_DUNDER;  // __x__ 形态标记（T021 消费）
      }
      emitConstOp(fs, OP_DEFINE_METHOD, mk, m->line);
      emitByte(fs, flags);
    } else {  // AST_VAR_DECL 字段/类属性
      bool isStatic = (m->as.varDecl.flags & (AST_VAR_STATIC | AST_VAR_CONST)) != 0;
      if (m->as.varDecl.init != NULL) {
        compileExpr(fs, m->as.varDecl.init);
      } else {
        emitOp(fs, OP_LOAD_NIL, m->line);
      }
      int32_t mk = nameConst(fs, m->as.varDecl.name, m->as.varDecl.nameLen);
      ms_u8 flags = isStatic ? MS_DEFM_STATIC
                             : (ms_u8)(MS_DEFM_FIELD | (m->as.varDecl.init != NULL ? MS_DEFM_FIELD_DEF : 0));
      emitConstOp(fs, OP_DEFINE_METHOD, mk, m->line);
      emitByte(fs, flags);
    }
  }
  emitOp(fs, OP_INHERIT_INIT, n->line);
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

// T018：for-in fresh 目标被捕获（04 §4）——FOR_ITER 产出后、落盘前闭合上一轮开放 upvalue
static void emitIterTargetClose(FuncState* fs, AstNode* t, const SlotSet* caps) {
  if (t == NULL) {
    return;
  }
  if (t->kind == AST_TUPLE) {
    for (int32_t i = 0; i < t->as.tuple.count; i++) {
      emitIterTargetClose(fs, t->as.tuple.items[i], caps);
    }
    return;
  }
  if (t->kind == AST_IDENT && t->bindKind == AST_BIND_LOCAL && (t->bindFlags & AST_BIND_FRESH) != 0 &&
      slotSetHas(caps, t->bindSlot)) {
    localOp(fs, OP_CLOSE_UPVAL, t->bindSlot, t->line);
  }
}

static void compileFor(FuncState* fs, AstNode* n) {
  SlotSet caps = {NULL, 0, 0};
  scanDirectCaptures(n, 0, 0, &caps);  // T018：循环子树闭包直接捕获的本帧槽（CLOSE 判定，04 §4）
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
      emitIterTargetClose(fs, target, &caps);  // 捕获的 fresh 目标每轮闭合（快照）
      storeIterTarget(fs, target);  // IDENT 直存 / TUPLE 解构递归（多变量迭代 04 §4）
    }
    compileStmt(fs, n->as.forStmt.body);
    int32_t jBack = emitJump(fs, OP_JMP, n->line);
    fs->jumps[jBack].target = lTop;
    fs->jumps[jExit].target = fs->chunk->codeLen;
    endLoop(fs);
    free(caps.items);
    return;
  }
  // T018 tri fresh 捕获（04 §4）：隐藏 carry 槽 V 供循环机构，每轮新鲜槽 W 供闭包捕获；
  // cond/body/post 内 V 引用经 alias 栈重映射到 W（localOp，嵌套循环按栈分层），携带拷贝绕开重映射
  AstNode* init = n->as.forStmt.init;
  bool freshCarry = n->as.forStmt.form == AST_FOR_TRI && init != NULL && init->kind == AST_VAR_DECL &&
                    init->bindKind == AST_BIND_LOCAL && (init->bindFlags & AST_BIND_FRESH) != 0 &&
                    slotSetHas(&caps, init->bindSlot);
  int32_t carry = -1;
  int32_t fresh = -1;
  beginLoop(fs, false);
  if (freshCarry) {
    carry = init->bindSlot;
    fresh = fs->nextTemp++;
    pushAlias(fs, carry, fresh);
    compileStmt(fs, init);  // 初值声明经 alias 落 W
    popAlias(fs);
    localOp(fs, OP_LOAD_LOCAL, fresh, n->line);
    localOp(fs, OP_STORE_LOCAL, carry, n->line);  // 播种：V = W
  } else if (init != NULL) {
    compileStmt(fs, init);  // TRI：简单语句（可省略）
  }
  int32_t lTop = fs->chunk->codeLen;
  if (freshCarry) {
    localOp(fs, OP_LOAD_LOCAL, carry, n->line);
    localOp(fs, OP_STORE_LOCAL, fresh, n->line);  // W_k = carry（Go 1.22：每轮新鲜变量自携带拷贝）
  }
  if (n->as.forStmt.form != AST_FOR_TRI) {
    currentLoop(fs)->contTarget = lTop;  // COND/INF：continue 回条件/体首
  }
  if (freshCarry) {
    pushAlias(fs, carry, fresh);  // cond/body/post 编译期：V 引用统一到 W（嵌套：内层压栈遮蔽不破坏外层）
  }
  int32_t jExit = -1;
  if (n->as.forStmt.cond != NULL) {
    compileExpr(fs, n->as.forStmt.cond);
    jExit = emitJump(fs, OP_JMP_IF_FALSE, n->line);
  }
  compileStmt(fs, n->as.forStmt.body);
  if (freshCarry) {
    currentLoop(fs)->contTarget = fs->chunk->codeLen;  // continue 落点：先闭合再执行 post
    localOp(fs, OP_CLOSE_UPVAL, fresh, n->line);  // 体末闭合（快照体终值；post 写 W 前定格，Go 1.22）
  } else if (n->as.forStmt.form == AST_FOR_TRI) {
    currentLoop(fs)->contTarget = fs->chunk->codeLen;  // continue 先执行 post
  }
  if (n->as.forStmt.post != NULL) {
    compileStmt(fs, n->as.forStmt.post);
  }
  if (freshCarry) {
    popAlias(fs);  // carry 拷贝不经 alias（LOAD W → STORE V 原槽）
    localOp(fs, OP_LOAD_LOCAL, fresh, n->line);
    localOp(fs, OP_STORE_LOCAL, carry, n->line);  // carry 更新：V = W
  }
  int32_t jBack = emitJump(fs, OP_JMP, n->line);
  fs->jumps[jBack].target = lTop;
  if (jExit >= 0) {
    fs->jumps[jExit].target = fs->chunk->codeLen;
  }
  endLoop(fs);
  free(caps.items);
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
    case AST_CLASS_DECL:  // T020：类声明（bases/成员/方法体编译）
      compileClassDecl(fs, n);
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
    case AST_YIELD:  // 生成器挂起（05 §4）：值可省（缺省 nil）
      if (n->as.exprStmt.expr != NULL) {
        compileExpr(fs, n->as.exprStmt.expr);
      } else {
        emitOp(fs, OP_LOAD_NIL, n->line);
      }
      emitOp(fs, OP_YIELD, n->line);
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
  FuncState fs = {out, NULL, 0, 0, NULL, 0, 0, diags, file, 0, scan + 1, NULL, 0, 0, NULL, 0, 0, NULL, 0, 0};
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
  free(fs.aliases);
  for (int32_t i = 0; i < diags->count; i++) {
    if (diags->items[i].level == MS_DIAG_ERROR) {
      return false;
    }
  }
  return true;
}
