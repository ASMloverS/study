// 类对象族实现（T020）：构造 + GC 标记 + MRO 包含判定
// GC 纪律（D6）：构造器多次分配（类 + 5 子表 / 实例 + 字段表）间借 VM 栈临时 root；
// 载荷先全置 NULL——root 期间 GC 经 markFn 走到，children 标记逐指针判空容忍构造窗口
#include "obj/obj_class.h"

#include "gc/gc.h"
#include "obj/shape.h"

MsObjClass* msObjClassNew(MsVM* vm, MsObjString* name) {
  MsObjClass* c = (MsObjClass*)msObjAlloc(vm, MS_OBJ_CLASS, sizeof(MsObjClass));
  if (c == NULL) {
    return NULL;
  }
  c->name = name;
  c->bases = NULL;  // 载荷先全置 NULL：root 期间 GC 会经 markFn 走到（D6，同 msObjSetNew）
  c->mro = NULL;
  c->methods = NULL;
  c->statics = NULL;
  c->fieldNames = NULL;
  c->fieldDefaults = NULL;
  if (!msVmPush(vm, msValueObj(&c->head))) {  // 构造期临时 root
    return NULL;  // StackOverflow 槽已置；孤儿 c 交 GC 回收
  }
  c->bases = msObjListNew(vm);  // 后续分配可触发 GC：c 已在栈 root，子表逐个挂入即受护
  c->methods = msObjMapNew(vm);
  c->statics = msObjMapNew(vm);
  c->fieldNames = msObjListNew(vm);
  c->fieldDefaults = msObjMapNew(vm);
  vm->stackTop--;  // 弹临时 root（c 由调用方持有/转存）
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
  i->fields = NULL;  // 载荷先置 NULL：root 期间 GC 会经 markFn 走到（D6）
  if (!msVmPush(vm, msValueObj(&i->head))) {  // 同上：构造期临时 root
    return NULL;
  }
  i->fields = msObjMapNew(vm);  // 分配可触发 GC：i 已在栈 root
  vm->stackTop--;
  if (i->fields == NULL) {
    return NULL;  // 孤儿 i 交 GC 回收
  }
  return i;
}

void msObjClassMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjClass* c = (MsObjClass*)obj;
  if (c->name != NULL) {  // 构造窗口内可为 NULL：逐指针判空（D6 纪律）
    gcMarkValue(vm, msValueObj((MsObj*)c->name));
  }
  if (c->bases != NULL) {
    gcMarkValue(vm, msValueObj(&c->bases->head));
  }
  if (c->mro != NULL) {
    gcMarkValue(vm, msValueObj(&c->mro->head));
  }
  if (c->methods != NULL) {
    gcMarkValue(vm, msValueObj(&c->methods->head));
  }
  if (c->statics != NULL) {
    gcMarkValue(vm, msValueObj(&c->statics->head));
  }
  if (c->fieldNames != NULL) {
    gcMarkValue(vm, msValueObj(&c->fieldNames->head));
  }
  if (c->fieldDefaults != NULL) {
    gcMarkValue(vm, msValueObj(&c->fieldDefaults->head));
  }
}

void msObjInstanceMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjInstance* i = (MsObjInstance*)obj;
  if (i->klass != NULL) {
    gcMarkValue(vm, msValueObj(&i->klass->head));
  }
  if (i->fields != NULL) {  // 构造窗口内可为 NULL
    gcMarkValue(vm, msValueObj(&i->fields->head));
  }
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
