// MsObjSet 数据面（T015，22 §4）：map 特例——键即元素、值恒占位 true；增删查委托内层 map
// GC 纪律（D6）：构造器两次分配（set + 内层 map）间借 VM 栈临时 root；
// 快照/拷贝/运算族结果创建后即落栈 root 至填充完毕；源集合由调用方 root（方法 peek/操作数栈）。
// Add 家族在 map 建立后无 GC 触发点（put 再哈希为纯 malloc 路径，见 obj_map.c 文件头）
#include "obj/obj_set.h"

#include <stdlib.h>

#include "gc/gc.h"
#include "obj/obj_list.h"

MsObjSet* msObjSetNew(MsVM* vm) {
  MsObjSet* st = (MsObjSet*)msObjAlloc(vm, MS_OBJ_SET, sizeof(MsObjSet));
  if (st == NULL) {
    return NULL;
  }
  st->map = NULL;  // 构造窗口内 GC 可扫到此 set：markChildren 须容忍 map==NULL
  if (!msVmPush(vm, msValueObj(&st->head))) {  // D6：临时 root（栈溢出槽已置）
    return NULL;  // 孤儿 set 交 GC 回收
  }
  MsObjMap* map = msObjMapNew(vm);  // 第二 GC 触发点：set 已在栈 root
  vm->stackTop--;  // 弹临时 root（此后无分配窗口）
  if (map == NULL) {
    return NULL;  // 孤儿 set 交 GC 回收
  }
  st->map = map;
  return st;
}

MsMapOp msObjSetAdd(MsVM* vm, MsObjSet* st, const MsValue* v) {
  MsValue ph = msValueBool(true);  // 值占位（22 §4）：同键再 Add 改写同槽亦无害
  return msObjMapPut(vm, st->map, v, &ph);
}

bool msObjSetHas(const MsObjSet* st, const MsValue* v) {
  return msObjMapGet(st->map, v, NULL) == MS_MAP_OK;  // 不可 hash → false
}

MsMapOp msObjSetRemove(MsObjSet* st, const MsValue* v) {
  return msObjMapRemove(st->map, v, NULL);
}

void msObjSetClear(MsObjSet* st) {
  msObjMapClear(st->map);
}

int32_t msObjSetCount(const MsObjSet* st) {
  return st->map->liveCount;
}

// 插入序快照（22 §4）：list 直填路径（模式同 list_methods.c lstCopy）
MsObjList* msObjSetToList(MsVM* vm, const MsObjSet* st) {
  MsObjList* list = msObjListNew(vm);  // GC 触发点：源 set 由调用方 root
  if (list == NULL) {
    return NULL;
  }
  if (!msVmPush(vm, msValueObj(&list->head))) {  // D6：结果落栈 root
    return NULL;  // 孤儿 list 交 GC 回收
  }
  int32_t count = 0;
  for (ms_i64 cur = msObjMapFirst(st->map); cur >= 0; cur = msObjMapNext(st->map, cur)) {
    count++;
  }
  if (!msObjListReserve(vm, list, count)) {  // 纯 malloc 路径（非 GC 触发点）
    vm->stackTop--;  // 弹 root；孤儿 list 交 GC 回收
    return NULL;
  }
  for (ms_i64 cur = msObjMapFirst(st->map); cur >= 0; cur = msObjMapNext(st->map, cur)) {
    list->items[list->count++] = *msObjMapKeyAt(st->map, cur);  // 直填无分配
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return list;
}

// 填充失败仅 OOM（UNHASHABLE 不可达：源键入表即证）→ 弹 root 返回 NULL（头契约）
MsObjSet* msObjSetCopy(MsVM* vm, const MsObjSet* st) {
  MsObjSet* r = msObjSetNew(vm);
  if (r == NULL || !msVmPush(vm, msValueObj(&r->head))) {  // D6：结果 root
    return NULL;  // 孤儿 r 交 GC 回收
  }
  for (ms_i64 cur = msObjMapFirst(st->map); cur >= 0; cur = msObjMapNext(st->map, cur)) {
    if (msObjSetAdd(vm, r, msObjMapKeyAt(st->map, cur)) != MS_MAP_OK) {  // OOM（UNHASHABLE 不可达：源键入表即证）
      vm->stackTop--;  // 弹 root；孤儿 r 交 GC 回收
      return NULL;
    }
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return r;
}

// 并（02 §5.4 `|`）：copy(a) 后并入 b 全量——重复键改写占位槽无害，左序在前
MsObjSet* msObjSetUnion(MsVM* vm, const MsObjSet* a, const MsObjSet* b) {
  MsObjSet* r = msObjSetCopy(vm, a);
  if (r == NULL || !msVmPush(vm, msValueObj(&r->head))) {  // D6：结果 root
    return NULL;  // 孤儿 r 交 GC 回收
  }
  for (ms_i64 cur = msObjMapFirst(b->map); cur >= 0; cur = msObjMapNext(b->map, cur)) {
    if (msObjSetAdd(vm, r, msObjMapKeyAt(b->map, cur)) != MS_MAP_OK) {  // OOM（UNHASHABLE 不可达：源键入表即证）
      vm->stackTop--;  // 弹 root；孤儿 r 交 GC 回收
      return NULL;
    }
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return r;
}

// 交（02 §5.4 `&`）：留 a 中亦在 b 的键——a 序保持
MsObjSet* msObjSetIntersect(MsVM* vm, const MsObjSet* a, const MsObjSet* b) {
  MsObjSet* r = msObjSetNew(vm);
  if (r == NULL || !msVmPush(vm, msValueObj(&r->head))) {  // D6：结果 root
    return NULL;  // 孤儿 r 交 GC 回收
  }
  for (ms_i64 cur = msObjMapFirst(a->map); cur >= 0; cur = msObjMapNext(a->map, cur)) {
    const MsValue* k = msObjMapKeyAt(a->map, cur);
    if (msObjSetHas(b, k)) {
      if (msObjSetAdd(vm, r, k) != MS_MAP_OK) {  // OOM（UNHASHABLE 不可达：源键入表即证）
        vm->stackTop--;  // 弹 root；孤儿 r 交 GC 回收
        return NULL;
      }
    }
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return r;
}

// 差（02 §5.4 `-`）：留 a 中不在 b 的键——a 序保持
MsObjSet* msObjSetDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b) {
  MsObjSet* r = msObjSetNew(vm);
  if (r == NULL || !msVmPush(vm, msValueObj(&r->head))) {  // D6：结果 root
    return NULL;  // 孤儿 r 交 GC 回收
  }
  for (ms_i64 cur = msObjMapFirst(a->map); cur >= 0; cur = msObjMapNext(a->map, cur)) {
    const MsValue* k = msObjMapKeyAt(a->map, cur);
    if (!msObjSetHas(b, k)) {
      if (msObjSetAdd(vm, r, k) != MS_MAP_OK) {  // OOM（UNHASHABLE 不可达：源键入表即证）
        vm->stackTop--;  // 弹 root；孤儿 r 交 GC 回收
        return NULL;
      }
    }
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return r;
}

// 对称差（02 §5.4 `^`）：diff(a,b) 后并入 b-only——a-only 序在前、b-only 在后
MsObjSet* msObjSetSymDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b) {
  MsObjSet* r = msObjSetDiff(vm, a, b);
  if (r == NULL || !msVmPush(vm, msValueObj(&r->head))) {  // D6：结果 root
    return NULL;  // 孤儿 r 交 GC 回收
  }
  for (ms_i64 cur = msObjMapFirst(b->map); cur >= 0; cur = msObjMapNext(b->map, cur)) {
    const MsValue* k = msObjMapKeyAt(b->map, cur);
    if (!msObjSetHas(a, k)) {
      if (msObjSetAdd(vm, r, k) != MS_MAP_OK) {  // OOM（UNHASHABLE 不可达：源键入表即证）
        vm->stackTop--;  // 弹 root；孤儿 r 交 GC 回收
        return NULL;
      }
    }
  }
  vm->stackTop--;  // 弹 root（此后无分配）
  return r;
}

// 构造窗口内 map 可为 NULL（set 已入栈、内层未建）：gcMarkValue 不受 NULL 须守卫
void msObjSetMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjSet* st = (MsObjSet*)obj;
  if (st->map != NULL) {
    gcMarkValue(vm, msValueObj(&st->map->head));  // 内层 map 为独立堆对象，GC 自行释放
  }
}
