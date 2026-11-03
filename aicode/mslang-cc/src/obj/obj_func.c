// 函数对象族数据面（T018，22 §2）：FAM 单块分配；markChildren 遍历默认参/upvalue 引用
#include "obj/obj_func.h"

#include <stdio.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_list.h"
#include "obj/obj_string.h"
#include "vm/vm.h"

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

// 内省失败置槽（OOM/栈溢出已由分配/压栈路径置位，此处仅 OOM 翻译）
static void attrErr(MsVM* vm) {
  snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
  vm->hasError = true;
}

// f.__name__ / f.__params__（05 §6）：__name__ 返回 intern 永久串（匿名函数为空串）；
// __params__ 返回参数名 list（含默认参名、rest 名不含 *）。miss 返回 false 不置槽（调用方续查
// 方法表）；OOM 置错误槽后返回 false。f 须 FUNCTION/CLOSURE（调用方已验）
bool msFuncAttrGet(MsVM* vm, const MsValue* f, const char* name, int32_t nameLen, MsValue* out) {
  const Chunk* chunk = msCallableChunk(f);
  if (nameLen == 8 && memcmp(name, "__name__", 8) == 0) {
    MsObjString* s = msInternBytes(vm, chunk->nameLen > 0 ? chunk->name : "", chunk->nameLen);
    if (s == NULL) {
      attrErr(vm);
      return false;
    }
    *out = msValueObj(&s->head);  // intern 产物属永久代：无回收窗口
    return true;
  }
  if (nameLen == 10 && memcmp(name, "__params__", 10) == 0) {
    MsObjList* list = msObjListNew(vm);
    if (list == NULL) {
      attrErr(vm);
      return false;
    }
    if (!msVmPush(vm, msValueObj(&list->head))) {  // 构建期 root（D6 纪律）
      return false;  // StackOverflow 槽已置
    }
    for (int32_t i = 0; i < chunk->paramCount; i++) {
      MsObjString* s = msInternBytes(vm, chunk->params[i].name, chunk->params[i].len);
      if (s == NULL || !msObjListPush(vm, list, msValueObj(&s->head))) {
        vm->stackTop--;  // 弹 list（错误路径恢复栈）
        attrErr(vm);
        return false;
      }
    }
    vm->stackTop--;
    *out = msValueObj(&list->head);
    return true;
  }
  return false;
}
