// 协程对象（T019，22 §2 / 21 §3.6 / 24 §3）：生成器轻量执行态——挂起帧子集
// （chunk + ip + 栈快照 + upvalue 重联表）；T028 扩展为完整 G 执行态（复用本结构与栈扫描面）
#ifndef INCLUDE_MS_OBJ_OBJ_COROUTINE_H_
#define INCLUDE_MS_OBJ_OBJ_COROUTINE_H_

#include <stdbool.h>
#include <stdint.h>

#include "compiler/chunk.h"
#include "obj/obj.h"

struct MsObjClosure;  // obj_func.h（按指针避免头环）

// 协程状态（05 §4 生成器生命周期）：RUNNING 期间自然 RETURN 即耗尽（YIELD 恒先置 SUSPENDED）
typedef enum MsCoroState {
  MS_CORO_START = 1,
  MS_CORO_RUNNING,
  MS_CORO_SUSPENDED,
  MS_CORO_DONE,
} MsCoroState;

// YIELD 时被关闭 upvalue 的重联记录（T019 别名语义）：恢复时 closed 值回拷帧槽并重新开放
typedef struct MsUpvalLink {
  int32_t slotOff;             // 相对帧 base 的槽位偏移
  struct MsObjUpvalue* upv;    // 挂起期间 location=&closed（自包含，无悬垂）
} MsUpvalLink;

typedef struct MsObjCoroutine {
  MsObj head;
  const Chunk* chunk;          // 生成器函数原型
  struct MsObjClosure* closure;  // 生成器自身闭包（LOAD/STORE_UPVAL 宿主；raw fn 帧 NULL）
  MsValue* stack;              // 挂起帧槽快照（[0, stackTop)）
  int32_t stackTop;            // 快照槽数
  int32_t stackCap;
  int32_t ip;                  // 恢复点（START 恒 0）
  ms_u8 state;                 // MsCoroState
  MsUpvalLink* links;          // upvalue 重联表
  int32_t linkCount, linkCap;
} MsObjCoroutine;

MsObjCoroutine* msObjCoroutineNew(MsVM* vm, const Chunk* chunk, struct MsObjClosure* closure);
void msObjCoroutineMarkChildren(MsVM* vm, MsObj* obj);  // gcSetMarkFn 注册转发
void msObjCoroutineFreeData(MsObj* obj);                // gcSetFreeFn 注册转发

static inline bool msIsCoroutine(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_COROUTINE;
}
#endif  // INCLUDE_MS_OBJ_OBJ_COROUTINE_H_
