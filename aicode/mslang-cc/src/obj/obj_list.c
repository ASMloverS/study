// MsObjList 数据面（T014，22 §4）：倍增数组 + 结构版本号 + 核心 mutator
// GC 纪律：push/insert 的 realloc 非 GC 分配点（不走 msObjAlloc，无 guard）；
// 向 list 放入新分配对象的调用方须保证值已可达（栈/全局）——BUILD_LIST 源值先落栈，
// split/join 内部使用时元素先落栈或逐个 push 前保持源串存活
#include "obj/obj_list.h"

#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_string.h"

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

// ---- 切片/拼接/重复（msObjListNew 为 GC 触发点：源由调用方 root） ----

static ms_i64 sliceCount(ms_i64 start, ms_i64 stop, ms_i64 step) {
  ms_i64 span = step > 0 ? stop - start : start - stop;
  ms_i64 inc = step > 0 ? step : -step;
  return (span + inc - 1) / inc;  // 规范化后 span >= 0：上取整计元素数
}

MsObjList* msObjListSlice(MsVM* vm, const MsObjList* list, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                          bool hasC, ms_i64 c, bool* errStepZero) {
  *errStepZero = false;
  ms_i64 start = 0;
  ms_i64 stop = 0;
  ms_i64 step = 1;
  if (!msSliceRange(list->count, hasA, a, hasB, b, hasC, c, &start, &stop, &step)) {
    *errStepZero = true;  // 与 string 同一规范切片算法（T013 抽出复用点）
    return NULL;
  }
  ms_i64 n = sliceCount(start, stop, step);
  if (n > INT32_MAX) {
    return NULL;
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：源 list 由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (n > 0 && !msObjListReserve(vm, r, (int32_t)n)) {
    return NULL;  // r 挂堆但不可达，交 GC 回收
  }
  for (ms_i64 k = 0; k < n; k++) {
    r->items[k] = list->items[start + k * step];
  }
  r->count = (int32_t)n;
  return r;
}

MsObjList* msObjListConcat(MsVM* vm, const MsObjList* a, const MsObjList* b) {
  if ((ms_i64)a->count + b->count > INT32_MAX) {
    return NULL;
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：a/b 由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (!msObjListReserve(vm, r, a->count + b->count)) {
    return NULL;
  }
  if (a->count > 0) {
    memcpy(r->items, a->items, sizeof(MsValue) * (size_t)a->count);
  }
  if (b->count > 0) {
    memcpy(r->items + a->count, b->items, sizeof(MsValue) * (size_t)b->count);
  }
  r->count = a->count + b->count;
  return r;
}

MsObjList* msObjListRepeat(MsVM* vm, const MsObjList* a, ms_i64 n) {
  if (n < 0) {
    n = 0;  // 负数重复为空（Python 语义，同 string repeat）
  }
  MsObjList* r = msObjListNew(vm);  // 分配点：源由调用方 root
  if (r == NULL) {
    return NULL;
  }
  if (a->count == 0 || n == 0) {
    return r;
  }
  if (n > (ms_i64)(INT32_MAX / a->count)) {  // 除法式防乘法溢出（同 obj_string.c repeat）
    return NULL;
  }
  ms_i64 total = (ms_i64)a->count * n;
  if (!msObjListReserve(vm, r, (int32_t)total)) {
    return NULL;
  }
  for (ms_i64 k = 0; k < total; k++) {
    r->items[k] = a->items[k % a->count];
  }
  r->count = (int32_t)total;
  return r;
}

// ---- 原地稳定归并排序（temp 缓冲 ping-pong；比较失败原序保持） ----

static MsListSortResult mergeHalves(MsValue* dst, const MsValue* src, ms_i64 lo, ms_i64 mid,
                                    ms_i64 hi) {
  ms_i64 i = lo;
  ms_i64 j = mid;
  ms_i64 k = lo;
  while (i < mid && j < hi) {
    int32_t cmp = 0;
    if (!msValueOrder(&src[i], &src[j], &cmp)) {
      return MS_LIST_SORT_TYPE;
    }
    dst[k++] = cmp <= 0 ? src[i++] : src[j++];  // <= 保稳定（相等取左半）
  }
  if (i < mid) {
    memcpy(&dst[k], &src[i], sizeof(MsValue) * (size_t)(mid - i));
  } else if (j < hi) {
    memcpy(&dst[k], &src[j], sizeof(MsValue) * (size_t)(hi - j));
  }
  return MS_LIST_SORT_OK;
}

MsListSortResult msObjListSort(MsObjList* list) {
  int32_t n = list->count;
  if (n < 2) {
    return MS_LIST_SORT_OK;  // 无结构变化：不 bump
  }
  MsValue* tmp = malloc(sizeof(MsValue) * (size_t)n);  // 非 GC 分配点
  if (tmp == NULL) {
    return MS_LIST_SORT_OOM;
  }
  MsValue* src = list->items;
  MsValue* dst = tmp;
  for (ms_i64 width = 1; width < n; width *= 2) {
    for (ms_i64 lo = 0; lo < n; lo += 2 * width) {
      ms_i64 mid = lo + width < n ? lo + width : n;
      ms_i64 hi = lo + 2 * width < n ? lo + 2 * width : n;
      if (mid == hi) {
        memcpy(&dst[lo], &src[lo], sizeof(MsValue) * (size_t)(hi - lo));  // 尾段直通
        continue;
      }
      MsListSortResult r = mergeHalves(dst, src, lo, mid, hi);
      if (r != MS_LIST_SORT_OK) {
        free(tmp);
        return r;  // 奇数趟（dst=tmp）items 原序未动；偶数趟（dst=items）前段可能已部分归并——
      }             // 元素多重集不变（内置比较子族内同质恒通过）；快照恢复归 T021 cmp 协议
    }
    MsValue* swap = src;
    src = dst;
    dst = swap;
  }
  if (src != list->items) {
    memcpy(list->items, src, sizeof(MsValue) * (size_t)n);
  }
  free(tmp);
  list->version++;
  return MS_LIST_SORT_OK;
}

// ---- list 迭代器（22 §4：游标 + 结构版本快照） ----

MsObjListIter* msObjListIterNew(MsVM* vm, MsObjList* list) {
  MsObjListIter* it = (MsObjListIter*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(MsObjListIter));
  if (it == NULL) {
    return NULL;
  }
  it->kind = MS_ITER_LIST;
  it->list = list;  // 分配期间源由调用方 root（GET_ITER peek）
  it->pos = 0;
  it->version = list->version;
  return it;
}

bool msListIterNext(MsObjListIter* it, MsValue* out, bool* errModified) {
  *errModified = false;
  if (it->version != it->list->version) {
    *errModified = true;  // 22 §4：修改即失效，继续 next 抛 IteratorError（调用方翻译）
    return false;
  }
  if (it->pos >= it->list->count) {
    return false;  // 耗尽
  }
  *out = it->list->items[it->pos++];
  return true;
}
