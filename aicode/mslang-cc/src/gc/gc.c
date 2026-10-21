// 过渡版整堆 STW 标记清除（T012，23-gc.md §1 过渡形态）
#include "gc/gc.h"

#include <stdlib.h>

#include "common/common.h"

#define MS_GC_HEAP_MAX_DEFAULT (8u * 1024u * 1024u)  // 任务书 T012 §2：默认 8MB

static MarkChildrenFn g_markFns[MS_OBJ_COUNT];  // 未注册 = 无 children

void gcSetMarkFn(MsObjType type, MarkChildrenFn fn) { g_markFns[type] = fn; }

void gcHeapInit(GcHeap* heap) {
  heap->objects = NULL;
  heap->bytes = 0;
  heap->count = 0;
  heap->threshold = MS_GC_HEAP_MAX_DEFAULT;
  const char* env = getenv("MS_GC_HEAP_MAX");  // 先行尊重（T012 §2）：十进制字节数
  if (env != NULL) {
    char* end = NULL;
    unsigned long long v = strtoull(env, &end, 10);
    if (end != env && *end == '\0' && v > 0) {
      heap->threshold = (size_t)v;
    }
  }
  heap->gray = NULL;
  heap->grayCount = heap->grayCap = 0;
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
}

size_t gcHeapBytes(const MsVM* vm) { return vm->heap->bytes; }

int32_t gcHeapObjCount(const MsVM* vm) { return vm->heap->count; }
