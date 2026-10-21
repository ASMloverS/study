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
