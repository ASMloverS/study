// 类运行时操作实现（T020，06 §2/§4）：MRO 线性化 + 实例/类/super 属性查找
// GC 纪律：msClassBuildMro 两次列表分配——seq/mro 建后即压 VM 栈 root，成功弹出并挂入
// klass->mro，失败弹出（GC 回收）；klass 由调用方 root（INHERIT_INIT 栈上类值）。
// 查找族零分配：mro/表均经指针只读游走
#include "vm/class_ops.h"

#include <stdio.h>

#include "obj/obj_string.h"
#include "obj/shape.h"

// ---- MRO 线性化（06 §2）----

// 错误槽置 OOM 返回 false（errLine 由调用方按指令偏移补写）
static bool failOom(MsVM* vm) {
  snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
  vm->hasError = true;
  return false;
}

// 类名安全取用（C 直构可无名）：NULL 名以 "?" 呈现
static const char* klassName(const MsObjClass* klass, int* len) {
  if (klass->name == NULL) {
    *len = 1;
    return "?";
  }
  *len = (int)klass->name->byteLen;
  return klass->name->bytes;
}

// 指针同一性包含（mro 元素为类值）
static bool mroContains(const MsObjList* mro, const MsObjClass* c) {
  for (int32_t i = 0; i < mro->count; i++) {
    if (mro->items[i].as.obj == (const MsObj*)c) {
      return true;
    }
  }
  return false;
}

// 保序子序列判定：sub 各元素按序出现于 seq（指针同一；C3 冲突校验用）
static bool mroIsSubsequence(const MsObjList* sub, const MsObjList* seq) {
  int32_t j = 0;
  for (int32_t i = 0; i < sub->count; i++) {
    const MsObj* t = sub->items[i].as.obj;
    while (j < seq->count && seq->items[j].as.obj != t) {
      j++;
    }
    if (j == seq->count) {
      return false;
    }
    j++;
  }
  return true;
}

bool msClassBuildMro(MsVM* vm, MsObjClass* klass) {
  if (klass->mro != NULL) {
    return true;  // 幂等防御（编译器保证 INHERIT_INIT 单次）
  }
  int nlen;
  const char* nptr = klassName(klass, &nlen);
  int32_t nBases = klass->bases->count;
  if (nBases == 0) {  // 无基类：mro = [self]
    MsObjList* mro = msObjListNew(vm);  // klass 已 root：分配窗口安全
    if (mro == NULL) {
      return failOom(vm);
    }
    klass->mro = mro;  // 先挂入再填（挂入即随 klass 可达；push 非 GC 点）
    if (!msObjListPush(vm, mro, msValueObj(&klass->head))) {
      return failOom(vm);  // 半成品 mro 随错误 unwind 废弃
    }
    return true;
  }
  for (int32_t i = 0; i < nBases; i++) {  // 类型校验 + 重复基类校验
    if (!msIsClass(&klass->bases->items[i])) {
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: base class %d of '%.*s' is not a class", (int)i,
               nlen, nptr);
      vm->hasError = true;
      return false;
    }
    for (int32_t j = 0; j < i; j++) {
      if (klass->bases->items[j].as.obj == klass->bases->items[i].as.obj) {
        snprintf(vm->errMsg, sizeof(vm->errMsg), "SyntaxError: duplicate base class in '%.*s'", nlen, nptr);
        vm->hasError = true;
        return false;
      }
    }
  }
  MsObjClass* base = (MsObjClass*)klass->bases->items[0].as.obj;  // 首位基类
  int32_t roots = 0;  // 本函数自压 root 数（失败路径按数恢复）
  MsObjList* seq = msObjListNew(vm);  // klass 已 root
  if (seq == NULL) {
    return failOom(vm);
  }
  if (!msVmPush(vm, msValueObj(&seq->head))) {
    return false;  // StackOverflow 槽已置；孤儿 seq 交 GC 回收
  }
  roots = 1;
  bool ok = msObjListPush(vm, seq, msValueObj(&klass->head));  // [本类]
  for (int32_t i = nBases - 1; ok && i >= 1; i--) {  // mixin 逆序（末位 mixin 先入）
    ok = msObjListPush(vm, seq, klass->bases->items[i]);
  }
  if (ok) {  // 基类 MRO（NULL 视同 [base]：msClassMroContains 同口径防御）
    if (base->mro != NULL) {
      for (int32_t i = 0; ok && i < base->mro->count; i++) {
        ok = msObjListPush(vm, seq, base->mro->items[i]);
      }
    } else {
      ok = msObjListPush(vm, seq, klass->bases->items[0]);
    }
  }
  MsObjList* mro = NULL;
  if (ok) {
    mro = msObjListNew(vm);  // seq 已在栈 root
    ok = mro != NULL;
    if (ok && !msVmPush(vm, msValueObj(&mro->head))) {
      vm->stackTop -= roots;
      return false;  // StackOverflow 槽已置；seq/mro 交 GC 回收
    }
    roots = 2;
  }
  if (ok) {
    for (int32_t i = 0; ok && i < seq->count; i++) {  // 去重保首
      if (!mroContains(mro, (const MsObjClass*)seq->items[i].as.obj)) {
        ok = msObjListPush(vm, mro, seq->items[i]);
      }
    }
  }
  if (ok) {  // C3 冲突：各 base mro 须为结果保序子序列（NULL 已视同 [base]，平凡成立）
    for (int32_t i = 0; ok && i < nBases; i++) {
      MsObjClass* b = (MsObjClass*)klass->bases->items[i].as.obj;
      if (b->mro != NULL && !mroIsSubsequence(b->mro, mro)) {
        vm->stackTop -= roots;
        snprintf(vm->errMsg, sizeof(vm->errMsg),
                 "SyntaxError: cannot create a consistent MRO for class '%.*s'", nlen, nptr);
        vm->hasError = true;
        return false;
      }
    }
  }
  if (!ok) {
    vm->stackTop -= roots;
    return failOom(vm);
  }
  vm->stackTop -= roots;  // 弹双 root（mro 挂入即随 klass 可达；seq 交 GC）
  klass->mro = mro;
  return true;
}

// ---- 属性查找（06 §4：字段 → MRO methods → MRO statics；零分配） ----

static bool mapGet(const MsObjMap* m, const MsObjString* name, MsValue* out) {
  MsValue key = msValueObj((MsObj*)name);
  return msObjMapGet(m, &key, out) == MS_MAP_OK;
}

// MRO 单级命中：先 methods 后 statics（方法遮蔽同名类属性）
static bool mroLevelGet(const MsObjClass* c, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (mapGet(c->methods, name, out)) {
    *isMethod = true;
    return true;
  }
  if (mapGet(c->statics, name, out)) {
    *isMethod = false;
    return true;
  }
  return false;
}

bool msInstanceFindAttr(const MsObjInstance* inst, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (msShapeFieldGet(inst, name, out)) {
    *isMethod = false;
    return true;
  }
  if (inst->klass == NULL) {
    return false;  // C 直构实例无类（防御）
  }
  return msClassFindAttr(inst->klass, name, out, isMethod);
}

bool msClassFindAttr(const MsObjClass* klass, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (klass == NULL || klass->mro == NULL) {
    return false;  // 防御：INHERIT_INIT 前不可达（编译器保证）
  }
  for (int32_t i = 0; i < klass->mro->count; i++) {
    MsObjClass* c = (MsObjClass*)klass->mro->items[i].as.obj;
    if (mroLevelGet(c, name, out, isMethod)) {
      return true;
    }
  }
  return false;
}

bool msClassFindAttrAfter(const MsObjClass* start, const MsObjString* name, MsValue* out, bool* isMethod) {
  if (start == NULL || start->mro == NULL) {
    return false;
  }
  int32_t idx = -1;
  for (int32_t i = 0; i < start->mro->count; i++) {
    if (start->mro->items[i].as.obj == (const MsObj*)start) {
      idx = i;
      break;
    }
  }
  for (int32_t i = idx + 1; i < start->mro->count; i++) {
    MsObjClass* c = (MsObjClass*)start->mro->items[i].as.obj;
    if (mroLevelGet(c, name, out, isMethod)) {
      return true;
    }
  }
  return false;
}
