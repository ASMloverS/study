// MsObjList 数据面（T014，22 §4）：倍增数组 + 结构版本号 + 核心 mutator
// GC 纪律：push/insert 的 realloc 非 GC 分配点（不走 msObjAlloc，无 guard）；
// 向 list 放入新分配对象的调用方须保证值已可达（栈/全局）——BUILD_LIST 源值先落栈，
// split/join 内部使用时元素先落栈或逐个 push 前保持源串存活
#include "obj/obj_list.h"

#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"

MsObjList* msObjListNew(MsVM* vm) {
  MsObjList* list = (MsObjList*)msObjAlloc(vm, MS_OBJ_LIST, sizeof(MsObjList));
  if (list == NULL) {
    return NULL;
  }
  list->items = NULL;
  list->count = 0;
  list->cap = 0;
  list->version = 0;
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
  list->version++;  // 结构性修改（22 §4）
  return true;
}

bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n) {
  (void)vm;
  if (n <= list->cap) {
    return true;  // 容量已足（n 为 BUILD_LIST u8 计数，恒 >= 0）
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

bool msObjListSet(MsObjList* list, ms_i64 i, MsValue v) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  list->items[i] = v;  // 原位替换非结构性：不 bump（22 §4 仅结构修改失效迭代器）
  return true;
}

bool msObjListInsert(MsObjList* list, ms_i64 i, MsValue v) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0) {
    i = 0;  // 越界钳位（Python insert 语义）
  }
  if (i > list->count) {
    i = list->count;
  }
  if (list->count == list->cap) {
    int32_t cap = list->cap == 0 ? 8 : list->cap * 2;
    MsValue* items = realloc(list->items, sizeof(MsValue) * (size_t)cap);
    if (items == NULL) {
      return false;  // 原块不动：列表保持不变
    }
    list->items = items;
    list->cap = cap;
  }
  memmove(&list->items[i + 1], &list->items[i], sizeof(MsValue) * (size_t)(list->count - (int32_t)i));
  list->items[i] = v;
  list->count++;
  list->version++;
  return true;
}

bool msObjListRemoveAt(MsObjList* list, ms_i64 i, MsValue* out) {
  if (i < 0) {
    i += list->count;
  }
  if (i < 0 || i >= list->count) {
    return false;
  }
  int32_t at = (int32_t)i;
  *out = list->items[at];
  memmove(&list->items[at], &list->items[at + 1], sizeof(MsValue) * (size_t)(list->count - at - 1));
  list->count--;
  list->version++;
  return true;
}

void msObjListClear(MsObjList* list) {
  list->count = 0;  // 容量保留（复用免再分配）
  list->version++;
}

void msObjListReverse(MsObjList* list) {
  int32_t i = 0;
  int32_t j = list->count - 1;
  while (i < j) {
    MsValue t = list->items[i];
    list->items[i] = list->items[j];
    list->items[j] = t;
    i++;
    j--;
  }
  list->version++;
}

int32_t msObjListIndexOf(const MsObjList* list, const MsValue* v) {
  for (int32_t i = 0; i < list->count; i++) {
    if (msValueEqual(&list->items[i], v)) {
      return i;
    }
  }
  return -1;
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
