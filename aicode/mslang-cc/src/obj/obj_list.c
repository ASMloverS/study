// 最小 MsObjList 数据面（T013，22 §4；D6）：倍增数组 + 负索引读取 + children 标记导出
// GC 纪律：push/reserve 的 realloc 不是 GC 触发点（不走 msObjAlloc，无 guard）；
// 向 list 放入新分配对象的调用方须保证值已可达（栈/全局）——BUILD_LIST 源值先落栈，
// split/join 内部使用时元素先落栈或逐个 push 前保持源串存活
#include "obj/obj_list.h"

#include <stdlib.h>

#include "gc/gc.h"

MsObjList* msObjListNew(MsVM* vm) {
  MsObjList* list = (MsObjList*)msObjAlloc(vm, MS_OBJ_LIST, sizeof(MsObjList));
  if (list == NULL) {
    return NULL;
  }
  list->items = NULL;
  list->count = 0;
  list->cap = 0;
  return list;
}

bool msObjListPush(MsVM* vm, MsObjList* list, MsValue v) {
  (void)vm;  // realloc 非 GC 分配点（文件头纪律注记）
  if (list->count == list->cap) {
    int32_t cap = list->cap == 0 ? 8 : list->cap * 2;
    MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
    if (items == NULL) {
      return false;  // 原块不动：列表保持不变
    }
    list->items = items;
    list->cap = cap;
  }
  list->items[list->count++] = v;
  return true;
}

bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n) {
  (void)vm;
  if (n <= list->cap) {
    return true;  // 容量已足（n 为 BUILD_LIST u8 计数，恒 ≥ 0）
  }
  int32_t cap = n < 8 ? 8 : n;
  MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
  if (items == NULL) {
    return false;
  }
  list->items = items;
  list->cap = cap;
  return true;
}

bool msObjListGet(const MsObjList* list, ms_i64 i, MsValue* out) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  *out = list->items[i];
  return true;
}

void msObjListMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjList* list = (MsObjList*)obj;
  for (int32_t i = 0; i < list->count; i++) {
    gcMarkValue(vm, list->items[i]);
  }
}

void msObjListFreeData(MsObj* obj) {
  free(((MsObjList*)obj)->items);  // items 可为 NULL（free(NULL) 无害）
}
