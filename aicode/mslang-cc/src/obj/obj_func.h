// 函数对象族（T018，22 §2 类型表）：function/closure/upvalue；闭包运行时链归 Task 4
#ifndef INCLUDE_MS_OBJ_OBJ_FUNC_H_
#define INCLUDE_MS_OBJ_OBJ_FUNC_H_

#include <stdbool.h>
#include <stdint.h>

#include "compiler/chunk.h"
#include "obj/obj.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjFunction {
  MsObj head;
  const Chunk* chunk;  // 原型（参数元数据/upvalue 描述符在 chunk，后续任务填充）
  int32_t defaultCount;
  MsValue defaults[];  // FAM：默认参值（定义处求值拷贝；Task 3 消费）
} MsObjFunction;

typedef struct MsObjUpvalue {
  MsObj head;
  struct MsObjUpvalue* next;  // VM 开放链（按栈槽地址降序；Task 4 消费）
  MsValue* location;          // 开放：VM 栈槽；闭合：&closed
  MsValue closed;
} MsObjUpvalue;

typedef struct MsObjClosure {
  MsObj head;
  MsObjFunction* fn;
  MsObjUpvalue* upvalues[];  // FAM：fn->chunk->nUpvalues 项（CLOSURE 填充；Task 4）
} MsObjClosure;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 创建函数对象（defaults 为 defaultCount 项源数组；count 0 时可 NULL）；失败 NULL
MsObjFunction* msObjFunctionNew(MsVM* vm, const Chunk* chunk, int32_t defaultCount, const MsValue* defaults);
// 创建闭包对象（upvalues 数组按 fn->chunk->nUpvalues 零初始化）；失败 NULL
MsObjClosure* msObjClosureNew(MsVM* vm, MsObjFunction* fn);

// children 标记（gcSetMarkFn 注册转发）
void msObjFunctionMarkChildren(MsVM* vm, MsObj* obj);
void msObjClosureMarkChildren(MsVM* vm, MsObj* obj);
void msObjUpvalueMarkChildren(MsVM* vm, MsObj* obj);

// 内省属性读取（05 §6，T018 Task 5）：f 须 FUNCTION/CLOSURE；__name__ intern 永久串（匿名空串）、
// __params__ 参数名 list（含默认参名、rest 名不含 *）；miss 返回 false 不置槽（调用方续查方法表），
// OOM 置错误槽返回 false
bool msFuncAttrGet(MsVM* vm, const MsValue* f, const char* name, int32_t nameLen, MsValue* out);

static inline bool msIsCallableObj(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && (v->as.obj->type == MS_OBJ_FUNCTION || v->as.obj->type == MS_OBJ_CLOSURE);
}

static inline const Chunk* msCallableChunk(const MsValue* v) {
  const MsObj* o = v->as.obj;
  return o->type == MS_OBJ_CLOSURE ? ((const MsObjClosure*)o)->fn->chunk : ((const MsObjFunction*)o)->chunk;
}

static inline MsObjFunction* msCallableFn(const MsValue* v) {
  MsObj* o = v->as.obj;
  return o->type == MS_OBJ_CLOSURE ? ((MsObjClosure*)o)->fn : (MsObjFunction*)o;
}

#endif  // INCLUDE_MS_OBJ_OBJ_FUNC_H_
