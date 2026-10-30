// Chunk 与常量池/行号表/跳转回填（T010，21 §2）：一个编译单元 = 一个 chunk
#ifndef INCLUDE_MS_COMPILER_CHUNK_H_
#define INCLUDE_MS_COMPILER_CHUNK_H_

#include <stdbool.h>
#include <stdint.h>

#include "common/common.h"
#include "compiler/opcodes.h"

struct MsVM;                // 缓存字段仅用指针（vm/vm.h；避免头环）
typedef struct MsObj MsObj; // T013：ConstEntry 字符串值缓存

// 常量池条目：T010 为字面量记录（文本借用源缓冲）；T013 起字符串运行时解码为对象
typedef enum ConstKind {
  CONST_NIL,
  CONST_BOOL,
  CONST_INT,
  CONST_FLOAT,
  CONST_RUNE,
  CONST_BIGINT,     // 借用源文本（n 后缀已剥离，LOAD_CONST 时精确解析，T016）
  CONST_STRING,     // 借用源文本段（转义原文，LOAD_CONST 时解码+intern）
  CONST_RAWSTRING,  // 原始串：内容不解码，反引号字面量
  CONST_INTERP,     // 插值段描述，Task 11 FORMAT_STRING 消费
  CONST_PROTO,      // 函数原型：持有独立 Chunk
} ConstKind;

typedef struct Chunk Chunk;

// 插值段（T013，Task 11 FORMAT_STRING 消费）：字面段编译期已解码，chunk 拥有
typedef struct InterpSeg {
  bool isExpr;         // true：表达式段（值运行时前置压栈）
  const char* start;   // false：字面段
  int32_t len;
} InterpSeg;

typedef struct ConstEntry {
  ConstKind kind;
  MsObj* strObj;           // T013：STRING/RAWSTRING 值缓存（intern 产物，永久代）
  struct MsVM* strVm;      // 缓存归属 VM（chunk 跨 VM 复跑按此失效）
  union {
    bool b;
    ms_i64 i;
    double f;
    ms_u32 cp;  // rune 码点
    struct {
      const char* start;
      int32_t len;
    } text;  // BIGINT / STRING / RAWSTRING 借用文本
    struct {
      InterpSeg* segs;
      int32_t count;
    } interp;
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
  const char* name;   // 函数名 / "main"（借用源文本，不保证 NUL 终止）
  int32_t nameLen;
};

// 跳转回填记录：编译期最小宽占位（opcode + u8）期间的 patch 表（任务书 §6）
typedef struct ChunkJump {
  int32_t instrOffset;  // 跳转 opcode 所在偏移
  int32_t target;       // 目标（编译期偏移空间的指令边界）
} ChunkJump;

void chunkInit(Chunk* c, const char* name, int32_t nameLen);
void chunkFree(Chunk* c);                           // 递归释放常量池内 proto chunk
int32_t chunkAddConst(Chunk* c, ConstEntry entry);  // 字面量按值去重；proto 恒追加
int32_t chunkAddConstInt(Chunk* c, ms_i64 v);
int32_t chunkAddConstFloat(Chunk* c, double v);
int32_t chunkAddConstBool(Chunk* c, bool v);
int32_t chunkAddConstRune(Chunk* c, ms_u32 cp);
int32_t chunkAddConstText(Chunk* c, ConstKind kind, const char* start, int32_t len);  // STRING/RAWSTRING/BIGINT
int32_t chunkAddConstProto(Chunk* c, Chunk* proto);
int32_t chunkAddConstInterp(Chunk* c, const InterpSeg* segs, int32_t count);  // INTERP：自持 segs 深拷贝（字面段字节同拷），恒追加
int32_t chunkEmitOp(Chunk* c, ms_u8 op, int32_t line);  // 指令起始：驱动行号 RLE；返回偏移
void chunkEmitByte(Chunk* c, ms_u8 b);                  // 操作数字节（不入行号表）
int32_t chunkLineAt(const Chunk* c, int32_t offset);    // RLE 查询；越界/无记录返回 -1
// 指令宽度（EXT 前缀链 + opcode + 布局操作数；跳转按物化前最小宽 2 字节计）
int32_t chunkInstrSize(const Chunk* c, int32_t off);
// peephole 占位（T010 §2）：NOP 压实 + JMP→JMP 链折叠，就地重映射 fixups 与行号偏移
void chunkPeephole(Chunk* c, ChunkJump* jumps, int32_t jumpCount);

// 指令元数据（定义于 chunk.c）：EXTENDED_ARG 布局为 OPL_U8，由解码器作前缀处理
extern const char* const msOpNames[OP__COUNT];
extern const MsOpndLayout msOpLayouts[OP__COUNT];

#endif  // INCLUDE_MS_COMPILER_CHUNK_H_
