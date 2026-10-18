// Chunk 与常量池/行号表/跳转回填（T010，21 §2）：一个编译单元 = 一个 chunk
#ifndef INCLUDE_MS_COMPILER_CHUNK_H_
#define INCLUDE_MS_COMPILER_CHUNK_H_

#include <stdbool.h>
#include <stdint.h>

#include "common/common.h"
#include "compiler/opcodes.h"

// 常量池条目：T010 为字面量记录（文本借用源缓冲）；T012 起替换为完整 MsValue
typedef enum ConstKind {
  CONST_NIL,
  CONST_BOOL,
  CONST_INT,
  CONST_FLOAT,
  CONST_RUNE,
  CONST_BIGINT,  // 借用源文本（n 后缀已剥离，精确解析归 T016）
  CONST_STRING,  // 借用源文本段（转义原文，解码归 T013）
  CONST_PROTO,   // 函数原型：持有独立 Chunk
} ConstKind;

typedef struct Chunk Chunk;

typedef struct ConstEntry {
  ConstKind kind;
  union {
    bool b;
    ms_i64 i;
    double f;
    ms_u32 cp;  // rune 码点
    struct {
      const char* start;
      int32_t len;
    } text;  // BIGINT / STRING 借用文本
    Chunk* proto;
  } as;
} ConstEntry;

// 行号表 RLE（21 §2）：仅源行变化处记录（指令起始偏移 → 源行）
typedef struct SourceRun {
  int32_t offset;
  int32_t line;
} SourceRun;

struct Chunk {
  ms_u8* code;
  int32_t codeLen, codeCap;
  ConstEntry* consts;
  int32_t constsLen, constsCap;
  SourceRun* lines;
  int32_t linesLen, linesCap;
  int32_t nUpvalues;  // 闭包捕获描述（T018 填充）
  uint16_t nIcSlots;  // 内联缓存槽（T022 分配）
  const char* name;   // 函数名 / "main"（借用）
};

// 跳转回填记录：编译期最小宽占位（opcode + u8）期间的 patch 表（任务书 §6）
typedef struct ChunkJump {
  int32_t instrOffset;  // 跳转 opcode 所在偏移
  int32_t target;       // 目标（编译期偏移空间的指令边界）
};

void chunkInit(Chunk* c, const char* name);
void chunkFree(Chunk* c);  // 递归释放常量池内 proto chunk
int32_t chunkAddConst(Chunk* c, ConstEntry entry);  // 字面量按值去重；proto 恒追加
int32_t chunkAddConstInt(Chunk* c, ms_i64 v);
int32_t chunkAddConstFloat(Chunk* c, double v);
int32_t chunkAddConstBool(Chunk* c, bool v);
int32_t chunkAddConstRune(Chunk* c, ms_u32 cp);
int32_t chunkAddConstText(Chunk* c, ConstKind kind, const char* start, int32_t len);  // STRING/BIGINT
int32_t chunkAddConstProto(Chunk* c, Chunk* proto);
int32_t chunkEmitOp(Chunk* c, ms_u8 op, int32_t line);  // 指令起始：驱动行号 RLE；返回偏移
void chunkEmitByte(Chunk* c, ms_u8 b);                   // 操作数字节（不入行号表）
int32_t chunkLineAt(const Chunk* c, int32_t offset);     // RLE 查询；越界/无记录返回 -1
// peephole 占位（T010 §2）：NOP 压实 + JMP→JMP 链折叠，就地重映射 fixups 与行号偏移
void chunkPeephole(Chunk* c, ChunkJump* jumps, int32_t jumpCount);

// 指令元数据（定义于 chunk.c）：EXTENDED_ARG 布局为 OPL_U8，由解码器作前缀处理
extern const char* const msOpNames[OP__COUNT];
extern const MsOpndLayout msOpLayouts[OP__COUNT];

#endif  // INCLUDE_MS_COMPILER_CHUNK_H_
