// 指令集全集定义（T010，21 §3）：opcode 1 字节 + 0/1/2 字节操作数
// EXTENDED_ARG <u8> 前置扩展支持 16/24/32 位常量索引与跳转目标（21 §1）
// 全集一次定义、生成/执行按任务分组逐步启用：未启用指令 VM 统一 unimplemented 陷阱（任务书 §6）
#ifndef INCLUDE_MS_COMPILER_OPCODES_H_
#define INCLUDE_MS_COMPILER_OPCODES_H_

#include "common/common.h"

typedef enum MsOpcode {
  OP_NOP = 0,
  OP_EXTENDED_ARG,  // <u8>：前置扩展（高 8 位，可链 16/24/32 位）
  // 栈操作（21 §3.1 MOVE 经栈）
  OP_POP,
  OP_DUP,
  OP_DUP2,
  OP_ROT,  // 三元旋转 a b c → b c a
  OP_SWAP,
  // 装载/存储（21 §3.1）
  OP_LOAD_NIL,
  OP_LOAD_TRUE,
  OP_LOAD_FALSE,
  OP_LOAD_CONST,      // <const k>
  OP_LOAD_LOCAL,      // <u8 slot>
  OP_STORE_LOCAL,     // <u8 slot>
  OP_LOAD_GLOBAL,     // <const name>
  OP_STORE_GLOBAL,    // <const name>
  OP_DEF_GLOBAL,      // <const name>
  OP_LOAD_UPVAL,      // <u8 idx>
  OP_STORE_UPVAL,     // <u8 idx>
  OP_LOAD_GLOBAL_IC,  // <const name> <u8 icSlot>（T022）
  // 运算（21 §3.2）
  OP_BINARY_ADD,
  OP_BINARY_SUB,
  OP_BINARY_MUL,
  OP_BINARY_DIV,
  OP_BINARY_MOD,
  OP_BINARY_POW,
  OP_BINARY_AND,  // 位与（逻辑短路走 JMP 族，无 opcode）
  OP_BINARY_OR,
  OP_BINARY_XOR,
  OP_BINARY_SHL,
  OP_BINARY_SHR,
  OP_UNARY_NEG,
  OP_UNARY_NOT,
  OP_UNARY_INVERT,
  OP_COMPARE_EQ,
  OP_COMPARE_NE,
  OP_COMPARE_LT,
  OP_COMPARE_LE,
  OP_COMPARE_GT,
  OP_COMPARE_GE,
  OP_IS,  // 同一性（03 §5，不可重载）
  OP_IN,  // 成员包含（03 §5，走 __contains__）
  OP_GET_INDEX,
  OP_SET_INDEX,
  OP_BUILD_SLICE,  // <u8 argc=3>
  // 函数与调用（21 §3.3）
  OP_MAKE_FUNCTION,  // <const proto> <u8 flags>（默认参数个数/rest 标志在 flags）
  OP_CLOSURE,        // <const proto>（捕获描述表在 proto）
  OP_CALL,           // <u8 argc>
  OP_CALL_METHOD,    // <const name> <u8 icSlot> <u8 argc>
  OP_RETURN,
  // 类（21 §3.4）
  OP_NEW_CLASS,      // <const name> <const base>（base = 255 表示无基类）
  OP_DEFINE_METHOD,  // <const name> <u8 flags(method/static/dunder)>
  OP_INHERIT_INIT,   // 构建 MRO/方法表
  OP_GET_ATTR,       // <const name> <u8 icSlot>
  OP_SET_ATTR,       // <const name>
  // 控制流（21 §3.5）：目标为 chunk 内绝对偏移
  OP_JMP,
  OP_JMP_IF_FALSE,       // 条件出栈
  OP_JMP_IF_TRUE,        // 条件出栈
  OP_JMP_IF_FALSE_OR_POP,  // 短路：假跳（保留值）/真弹出
  OP_JMP_IF_TRUE_OR_POP,   // 短路：真跳（保留值）/假弹出
  OP_POP_JUMP_IF_NIL,
  OP_SWITCH_TABLE,        // 变长：<u8 n> <t default> + n×(<const key>, <t>)
  OP_TYPE_SWITCH_TABLE,   // 变长：类常量键 → 目标
  OP_CHECK_TYPE,          // <const class>（case as 绑定校验）
  // 迭代与生成器（21 §3.6）
  OP_GET_ITER,
  OP_FOR_ITER,  // <t exit>（迭代协议 + nil 耗尽跳出）
  OP_YIELD,
  OP_RESUME,
  // 异常 / defer（21 §3.7）
  OP_PUSH_TRY,    // <t catch> <t finally>
  OP_POP_TRY,
  OP_THROW,       // 抛出栈顶值
  OP_MATCH_CATCH,  // <const class>
  OP_PUSH_DEFER,   // <const fn>
  OP_RUN_DEFERS,   // <u8 n>
  OP_RERAISE,      // 裸 throw 重抛
  // 并发（21 §3.8）
  OP_MAKE_CHAN,  // <u8 cap>
  OP_SEND,
  OP_RECV,
  OP_CLOSE_CHAN,
  OP_SELECT_SETUP,  // <u8 n>
  OP_SELECT_ARM,    // <u8 i>
  OP_SELECT_GO,     // 变长：可选 <t default>
  OP_SELECT_COMMIT,
  OP_SPAWN,  // go 语句：新 G
  OP_AWAIT,  // 挂起直至 Future 完成
  // 杂项（21 §3.9）
  OP_BUILD_LIST,     // <u8 n>
  OP_BUILD_MAP,      // <u8 n*2>
  OP_BUILD_TUPLE,    // <u8 n>
  OP_BUILD_SET,      // <u8 n>
  OP_UNPACK_SEQ,     // <u8 n>
  OP_UNPACK_STAR,    // <u8 slot>
  OP_FORMAT_STRING,  // <const k-interp>
  OP_CHECK_SAFEPOINT,  // 回边预算：抢占 + GC safepoint（21 §6）
  OP_ASSERT,           // 变长：可选 <const msg>
  OP__COUNT
} MsOpcode;

// 操作数布局（定长类；变长指令为 OPL_SPECIAL，T010 不发射）
// OPL_CONST 操作数 1 字节常量池索引（EXT 可扩）；OPL_JUMP 操作数 1 字节绝对偏移低 8 位（EXT 扩展）
typedef enum MsOpndLayout {
  OPL_NONE = 0,
  OPL_U8,
  OPL_CONST,
  OPL_JUMP,
  OPL_U8_U8,
  OPL_CONST_U8,
  OPL_CONST_CONST,
  OPL_JUMP_JUMP,
  OPL_CONST_U8_U8,
  OPL_SPECIAL,
} MsOpndLayout;

#endif  // INCLUDE_MS_COMPILER_OPCODES_H_
