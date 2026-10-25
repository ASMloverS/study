// MsObjTuple 数据面（T015，22 §4）：FAM 单块分配；markChildren 遍历元素
#include "obj/obj_tuple.h"

#include "gc/gc.h"

MsObjTuple* msObjTupleNew(MsVM* vm, int32_t count) {
  if (count < 0 || (size_t)count > (SIZE_MAX - sizeof(MsObjTuple)) / sizeof(MsValue)) {
    return NULL;
  }
  MsObjTuple* t = (MsObjTuple*)msObjAlloc(vm, MS_OBJ_TUPLE, sizeof(MsObjTuple) + (size_t)count * sizeof(MsValue));
  if (t == NULL) {
    return NULL;
  }
  t->hash = 0;
  t->hashValid = false;
  t->count = count;
  for (int32_t i = 0; i < count; i++) {
    t->items[i] = msValueNil();  // 分配点后即达 GC 安全态（调用方覆写前可见全 nil）
  }
  return t;
}

const MsValue* msObjTupleGet(const MsObjTuple* t, ms_i64 i) {
  if (i < 0) {
    i += t->count;
  }
  if (i < 0 || i >= t->count) {
    return NULL;
  }
  return &t->items[i];
}

void msObjTupleMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjTuple* t = (MsObjTuple*)obj;
  for (int32_t i = 0; i < t->count; i++) {
    gcMarkValue(vm, t->items[i]);
  }
}
