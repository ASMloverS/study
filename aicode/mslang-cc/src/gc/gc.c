// 过渡版整堆 STW 标记清除（T012，23-gc.md §1 过渡形态）
#include "gc/gc.h"

#include <errno.h>
#include <stdlib.h>

#include "common/common.h"

#define MS_GC_HEAP_MAX_DEFAULT (8u * 1024u * 1024u)  // 任务书 T012 §2：默认 8MB

static MarkChildrenFn g_markFns[MS_OBJ_COUNT];  // 未注册 = 无 children

void gcSetMarkFn(MsObjType type, MarkChildrenFn fn) {
  MS_ASSERT((unsigned)type < (unsigned)MS_OBJ_COUNT);  // 无符号单侧比较：规避 GCC -Wtype-limits 恒真告警
  g_markFns[type] = fn;
}

void gcHeapInit(GcHeap* heap) {
  heap->objects = NULL;
  heap->bytes = 0;
  heap->count = 0;
  heap->threshold = MS_GC_HEAP_MAX_DEFAULT;
  const char* env = getenv("MS_GC_HEAP_MAX");  // 先行尊重（T012 §2）：十进制字节数
  if (env != NULL && env[0] != '-') {  // 负数/溢出/尾随垃圾/0 一律回落默认
    char* end = NULL;
    errno = 0;
    unsigned long long v = strtoull(env, &end, 10);
    if (errno == 0 && end != env && *end == '\0' && v > 0) {
      heap->threshold = (size_t)v;
    }
  }
  heap->gray = NULL;
  heap->grayCount = heap->grayCap = 0;
  heap->collecting = false;
}

void gcHeapFree(GcHeap* heap) {
  MsObj* obj = heap->objects;
  while (obj != NULL) {
    MsObj* next = obj->next;
    free(obj);
    obj = next;
  }
  free(heap->gray);
  heap->objects = NULL;
  heap->bytes = 0;
  heap->count = 0;
  heap->gray = NULL;
  heap->grayCount = heap->grayCap = 0;
  heap->collecting = false;
}

size_t gcHeapBytes(const MsVM* vm) { return vm->heap->bytes; }

int32_t gcHeapObjCount(const MsVM* vm) { return vm->heap->count; }

// ---- 标记清除（root = 栈 + 全局表；模块帧值栈即整个栈，无 native 帧）----

void gcMarkValue(MsVM* vm, MsValue v) {
  if (v.kind != MS_VAL_OBJ) {
    return;  // NIL/BOOL/I64/F64 直存；过渡 kind 借用非堆存储
  }
  GcHeap* heap = vm->heap;
  if (v.as.obj->flags & MS_OBJ_MARK) {
    return;
  }
  v.as.obj->flags |= MS_OBJ_MARK;
  MS_ARRAY_PUSH(heap->gray, heap->grayCount, heap->grayCap, v.as.obj);
}

static void markRoots(MsVM* vm) {
  for (int32_t i = 0; i < vm->stackTop; i++) {
    gcMarkValue(vm, vm->stack[i]);
  }
  for (int32_t i = 0; i < vm->globalCount; i++) {
    gcMarkValue(vm, vm->globals[i].value);
  }
}

void gcCollectAll(MsVM* vm) {
  GcHeap* heap = vm->heap;
  MS_ASSERT(!heap->collecting);  // markFn 内再分配触发嵌套回收会在 drain/sweep 中途破坏链表（T013+ 扩展点纪律）
  heap->collecting = true;
  markRoots(vm);
  while (heap->grayCount > 0) {  // 迭代 worklist（灰队列方向；防爆栈）
    MsObj* obj = heap->gray[--heap->grayCount];
    MarkChildrenFn fn = g_markFns[obj->type];
    if (fn != NULL) {
      fn(vm, obj);
    }
  }
  MsObj** link = &heap->objects;
  while (*link != NULL) {  // sweep：摘白释放，黑/永久保留
    MsObj* obj = *link;
    if (obj->flags & (MS_OBJ_MARK | MS_OBJ_PERMANENT)) {
      obj->flags &= (ms_u8)~MS_OBJ_MARK;
      link = &obj->next;
      continue;
    }
    *link = obj->next;
    heap->bytes -= obj->size;
    heap->count--;
    free(obj);
  }
  heap->collecting = false;
}

void msVmAllocGuard(MsVM* vm) {
  // 固定阈值无退避：活集贴近阈值时每次分配都触发整堆回收（平方退化）；heapGoal 增长策略归 T026/T027
  if (vm->heap->bytes >= vm->heap->threshold) {
    gcCollectAll(vm);
  }
}
