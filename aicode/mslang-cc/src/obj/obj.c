// 全堆链表分配器（T012，22 §2）：malloc + 头初始化 + 头插链表 + 统计累加
#include "obj/obj.h"

#include <stdlib.h>

#include "gc/gc.h"

MsObj* msObjAlloc(MsVM* vm, MsObjType type, size_t size) {
  msVmAllocGuard(vm);
  MsObj* obj = malloc(size);
  if (obj == NULL) {
    return NULL;  // v1 简化：分配失败上抛 NULL，OOM 策略归 T026
  }
  obj->type = (ms_u8)type;
  obj->flags = 0;
  obj->size = size;
  GcHeap* heap = vm->heap;
  obj->next = heap->objects;
  heap->objects = obj;
  heap->bytes += size;
  heap->count++;
  return obj;
}

MsObjNative* msObjNativeNew(MsVM* vm, MsNativeFn fn, const char* name, int32_t nameLen) {
  MsObjNative* n = (MsObjNative*)msObjAlloc(vm, MS_OBJ_NATIVE, sizeof(MsObjNative));
  if (n == NULL) {
    return NULL;
  }
  n->fn = fn;
  n->name = name;
  n->nameLen = nameLen;
  return n;
}

MsObjBoundMethod* msObjBoundNew(MsVM* vm, MsValue receiver, MsObjNative* method) {
  MsObjBoundMethod* b = (MsObjBoundMethod*)msObjAlloc(vm, MS_OBJ_BOUND_METHOD, sizeof(MsObjBoundMethod));
  if (b == NULL) {
    return NULL;
  }
  b->receiver = msValueNil();  // MsValue 字段先置 nil（markFn 纪律，同 test_gc_basic allocNode）
  b->method = method;
  b->receiver = receiver;
  return b;
}
