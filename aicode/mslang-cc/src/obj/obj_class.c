// 类对象族实现（T020）：构造 + GC 标记 + MRO 包含判定
#include "obj/obj_class.h"

#include "gc/gc.h"
#include "obj/shape.h"

MsObjClass* msObjClassNew(MsVM* vm, MsObjString* name) {
  MsObjClass* c = (MsObjClass*)msObjAlloc(vm, MS_OBJ_CLASS, sizeof(MsObjClass));
  if (c == NULL) {
    return NULL;
  }
  c->name = name;
  c->mro = NULL;
  c->bases = msObjListNew(vm);         // 分配期 name 无堆依赖（intern 永久）
  c->methods = msObjMapNew(vm);
  c->statics = msObjMapNew(vm);
  c->fieldNames = msObjListNew(vm);
  c->fieldDefaults = msObjMapNew(vm);
  if (c->bases == NULL || c->methods == NULL || c->statics == NULL || c->fieldNames == NULL ||
      c->fieldDefaults == NULL) {
    return NULL;  // 失败子对象由 GC 回收（c 已挂堆链）
  }
  return c;
}

MsObjInstance* msObjInstanceNew(MsVM* vm, MsObjClass* klass) {
  MsObjInstance* i = (MsObjInstance*)msObjAlloc(vm, MS_OBJ_INSTANCE, sizeof(MsObjInstance));
  if (i == NULL) {
    return NULL;
  }
  i->klass = klass;
  i->fields = msObjMapNew(vm);  // klass 无堆依赖；fields 失败由 GC 回收
  if (i->fields == NULL) {
    return NULL;
  }
  return i;
}

void msObjClassMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjClass* c = (MsObjClass*)obj;
  gcMarkValue(vm, msValueObj((MsObj*)c->name));
  gcMarkValue(vm, msValueObj(&c->bases->head));
  if (c->mro != NULL) {
    gcMarkValue(vm, msValueObj(&c->mro->head));
  }
  gcMarkValue(vm, msValueObj(&c->methods->head));
  gcMarkValue(vm, msValueObj(&c->statics->head));
  gcMarkValue(vm, msValueObj(&c->fieldNames->head));
  gcMarkValue(vm, msValueObj(&c->fieldDefaults->head));
}

void msObjInstanceMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjInstance* i = (MsObjInstance*)obj;
  if (i->klass != NULL) {
    gcMarkValue(vm, msValueObj(&i->klass->head));
  }
  gcMarkValue(vm, msValueObj(&i->fields->head));
}

bool msClassMroContains(const MsObjClass* x, const MsObjClass* target) {
  if (x->mro == NULL) {
    return x == target;
  }
  for (int32_t i = 0; i < x->mro->count; i++) {
    if (x->mro->items[i].as.obj == (const MsObj*)target) {
      return true;
    }
  }
  return false;
}
