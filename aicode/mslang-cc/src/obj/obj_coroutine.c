// 协程对象实现（T019）：分配/children 标记/独立数据释放
#include "obj/obj_coroutine.h"

#include <stdlib.h>

#include "gc/gc.h"
#include "obj/obj_func.h"

MsObjCoroutine* msObjCoroutineNew(MsVM* vm, const Chunk* chunk, MsObjClosure* closure) {
  MsObjCoroutine* co = (MsObjCoroutine*)msObjAlloc(vm, MS_OBJ_COROUTINE, sizeof(MsObjCoroutine));
  if (co == NULL) {
    return NULL;
  }
  co->chunk = chunk;
  co->closure = closure;
  co->stack = NULL;
  co->stackTop = co->stackCap = 0;
  co->ip = 0;
  co->state = MS_CORO_START;
  co->links = NULL;
  co->linkCount = co->linkCap = 0;
  return co;
}

void msObjCoroutineMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjCoroutine* co = (MsObjCoroutine*)obj;
  if (co->closure != NULL) {
    gcMarkValue(vm, msValueObj(&co->closure->head));
  }
  gcMarkValueRange(vm, co->stack, co->stackTop);  // 挂起帧槽（T028 G 栈扫描复用同一面）
  for (int32_t i = 0; i < co->linkCount; i++) {
    gcMarkValue(vm, msValueObj(&co->links[i].upv->head));
  }
}

void msObjCoroutineFreeData(MsObj* obj) {
  MsObjCoroutine* co = (MsObjCoroutine*)obj;
  free(co->stack);
  free(co->links);
}
