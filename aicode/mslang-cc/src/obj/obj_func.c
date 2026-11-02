// 函数对象族数据面（T018，22 §2）：FAM 单块分配；markChildren 遍历默认参/upvalue 引用
#include "obj/obj_func.h"

#include "gc/gc.h"

MsObjFunction* msObjFunctionNew(MsVM* vm, const Chunk* chunk, int32_t defaultCount, const MsValue* defaults) {
  if (defaultCount < 0 || (size_t)defaultCount > (SIZE_MAX - sizeof(MsObjFunction)) / sizeof(MsValue)) {
    return NULL;
  }
  MsObjFunction* fn = (MsObjFunction*)msObjAlloc(vm, MS_OBJ_FUNCTION,
                                                 sizeof(MsObjFunction) + sizeof(MsValue) * (size_t)defaultCount);
  if (fn == NULL) {
    return NULL;
  }
  fn->chunk = chunk;
  fn->defaultCount = defaultCount;
  for (int32_t i = 0; i < defaultCount; i++) {
    fn->defaults[i] = defaults[i];  // 分配点后逐槽拷贝（源值由调用方 root；Task 3 实参协议）
  }
  return fn;
}

MsObjClosure* msObjClosureNew(MsVM* vm, MsObjFunction* fn) {
  int32_t n = fn->chunk->nUpvalues;
  if (n < 0 || (size_t)n > (SIZE_MAX - sizeof(MsObjClosure)) / sizeof(MsObjUpvalue*)) {
    return NULL;
  }
  MsObjClosure* c = (MsObjClosure*)msObjAlloc(vm, MS_OBJ_CLOSURE,
                                              sizeof(MsObjClosure) + sizeof(MsObjUpvalue*) * (size_t)n);
  if (c == NULL) {
    return NULL;
  }
  c->fn = fn;
  for (int32_t i = 0; i < n; i++) {
    c->upvalues[i] = NULL;  // 零初始化即 GC 安全（CLOSURE 填充归 Task 4）
  }
  return c;
}

void msObjFunctionMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjFunction* fn = (MsObjFunction*)obj;
  for (int32_t i = 0; i < fn->defaultCount; i++) {
    gcMarkValue(vm, fn->defaults[i]);
  }
}

void msObjClosureMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjClosure* c = (MsObjClosure*)obj;
  gcMarkValue(vm, msValueObj(&c->fn->head));
  for (int32_t i = 0; i < c->fn->chunk->nUpvalues; i++) {
    if (c->upvalues[i] != NULL) {
      gcMarkValue(vm, msValueObj(&c->upvalues[i]->head));
    }
  }
}

void msObjUpvalueMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjUpvalue* u = (MsObjUpvalue*)obj;
  if (u->location != NULL) {  // 开放：所指栈槽值；闭合：&closed（同一表达式覆盖）
    gcMarkValue(vm, *u->location);
  }
}
