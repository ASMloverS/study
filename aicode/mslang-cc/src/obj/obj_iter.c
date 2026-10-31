// MsObjIter 实现（T017，22 §4）：构造族 + 统一推进 + 值→迭代器/收集公共面
// GC 纪律（D6）：本文件所有分配点的存活引用均经调用方 root（迭代器自身在栈/寄存器调用窗口）；
// 推进途中产出的中间值先压 VM 栈再进后续分配（callIterFn/ENUMERATE/ZIP 同 seqForm 纪律）
#include "obj/obj_iter.h"

#include <stdio.h>

#include "gc/gc.h"
#include "obj/obj_list.h"
#include "obj/obj_map.h"
#include "obj/obj_set.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"

// 统一零态分配：MsValue 字段先置 nil（markFn 纪律，同 msObjBoundNew）
static MsObjIter* iterAlloc(MsVM* vm, ms_u8 kind) {
  MsObjIter* it = (MsObjIter*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(MsObjIter));
  if (it == NULL) {
    return NULL;
  }
  it->kind = kind;
  it->src = msValueNil();
  it->src2 = msValueNil();
  it->fn = msValueNil();
  it->cur = 0;
  it->stop = 0;
  it->step = 0;
  it->i = -1;
  it->version = 0;
  it->flags = 0;
  return it;
}

void msObjIterMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjIter* it = (MsObjIter*)obj;
  gcMarkValue(vm, it->src);
  gcMarkValue(vm, it->src2);
  gcMarkValue(vm, it->fn);
}

MsObjIter* msObjIterRangeNew(MsVM* vm, ms_i64 start, ms_i64 stop, ms_i64 step) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_RANGE);
  if (it == NULL) {
    return NULL;
  }
  it->cur = start;
  it->stop = stop;
  it->step = step;
  return it;
}

MsObjIter* msObjIterMapNew(MsVM* vm, struct MsObjMap* m) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_MAP);
  if (it == NULL) {
    return NULL;
  }
  it->src = msValueObj(&m->head);  // m 由调用方 root
  return it;
}

MsObjIter* msObjIterSetNew(MsVM* vm, struct MsObjSet* st) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_SET);
  if (it == NULL) {
    return NULL;
  }
  it->src = msValueObj(&st->head);  // st 由调用方 root
  return it;
}

MsObjIter* msObjIterTupleNew(MsVM* vm, struct MsObjTuple* t) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_TUPLE);
  if (it == NULL) {
    return NULL;
  }
  it->src = msValueObj(&t->head);  // t 由调用方 root
  it->i = 0;
  return it;
}

MsObjIter* msObjIterReverseNew(MsVM* vm, MsValue seq) {
  // 仅 list/string/tuple；其余返回 NULL（调用方可预校验，错误翻译自负）
  if (seq.kind != MS_VAL_OBJ) {
    return NULL;
  }
  ms_u8 t = seq.as.obj->type;
  if (t != MS_OBJ_LIST && t != MS_OBJ_STRING && t != MS_OBJ_TUPLE) {
    return NULL;
  }
  MsObjIter* it = iterAlloc(vm, MS_ITER_REVERSE);  // seq 由调用方 root
  if (it == NULL) {
    return NULL;
  }
  if (t == MS_OBJ_LIST) {
    MsObjList* list = (MsObjList*)seq.as.obj;
    it->i = list->count - 1;
    it->version = list->version;
  } else if (t == MS_OBJ_STRING) {
    it->i = (ms_i64)((const MsObjString*)seq.as.obj)->charLen - 1;
  } else {
    it->i = (ms_i64)((const MsObjTuple*)seq.as.obj)->count - 1;
  }
  it->src = seq;
  return it;
}

MsObjIter* msObjIterEnumerateNew(MsVM* vm, MsValue srcIter, ms_i64 start) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_ENUMERATE);
  if (it == NULL) {
    return NULL;
  }
  it->src = srcIter;  // srcIter 由调用方 root
  it->cur = start;
  return it;
}

MsObjIter* msObjIterZipNew(MsVM* vm, MsValue a, MsValue b, bool strict) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_ZIP);
  if (it == NULL) {
    return NULL;
  }
  it->src = a;  // a/b 由调用方 root
  it->src2 = b;
  it->flags = (ms_u8)(strict ? 1 : 0);
  return it;
}

MsObjIter* msObjIterMapFnNew(MsVM* vm, MsValue srcIter, MsValue fn) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_MAPFN);
  if (it == NULL) {
    return NULL;
  }
  it->src = srcIter;  // srcIter/fn 由调用方 root
  it->fn = fn;
  return it;
}

MsObjIter* msObjIterFilterNew(MsVM* vm, MsValue srcIter, MsValue fn) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_FILTER);
  if (it == NULL) {
    return NULL;
  }
  it->src = srcIter;
  it->fn = fn;
  return it;
}

MsObjIter* msObjIterTakeNew(MsVM* vm, MsValue srcIter, ms_i64 n) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_TAKE);
  if (it == NULL) {
    return NULL;
  }
  it->src = srcIter;
  it->cur = n;
  return it;
}

MsObjIter* msObjIterDropNew(MsVM* vm, MsValue srcIter, ms_i64 n) {
  MsObjIter* it = iterAlloc(vm, MS_ITER_DROP);
  if (it == NULL) {
    return NULL;
  }
  it->src = srcIter;
  it->i = n;
  return it;
}

// 错误置槽（不置 errLine：vm.c FOR_ITER 经 RUNTIME_ERROR 补行号，同 bridgeErr 约定）
static void iterErr(MsVM* vm, const char* msg) {
  snprintf(vm->errMsg, sizeof(vm->errMsg), "%s", msg);
  vm->hasError = true;
}

bool msIterFromValue(MsVM* vm, MsValue v, MsValue* out) {
  // v 由调用方 root（OP_GET_ITER peek）：各构造分配窗口内源均存活
  if (v.kind != MS_VAL_OBJ) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: '%s' is not iterable", msValueTypeName(&v));
    return false;  // errMsg 已置不置 hasError：调用方翻译
  }
  switch (v.as.obj->type) {
    case MS_OBJ_ITERATOR:
      *out = v;  // 迭代器直通
      return true;
    case MS_OBJ_STRING: {
      MsObjStrIter* it = msObjStrIterNew(vm, (MsObjString*)v.as.obj, false);  // rune 模式
      if (it == NULL) {
        break;
      }
      *out = msValueObj(&it->head);
      return true;
    }
    case MS_OBJ_LIST: {
      MsObjListIter* it = msObjListIterNew(vm, (MsObjList*)v.as.obj);
      if (it == NULL) {
        break;
      }
      *out = msValueObj(&it->head);
      return true;
    }
    case MS_OBJ_MAP: {
      MsObjIter* it = msObjIterMapNew(vm, (MsObjMap*)v.as.obj);
      if (it == NULL) {
        break;
      }
      *out = msValueObj(&it->head);
      return true;
    }
    case MS_OBJ_SET: {
      MsObjIter* it = msObjIterSetNew(vm, (MsObjSet*)v.as.obj);
      if (it == NULL) {
        break;
      }
      *out = msValueObj(&it->head);
      return true;
    }
    case MS_OBJ_TUPLE: {
      MsObjIter* it = msObjIterTupleNew(vm, (MsObjTuple*)v.as.obj);
      if (it == NULL) {
        break;
      }
      *out = msValueObj(&it->head);
      return true;
    }
    default:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: '%s' is not iterable", msValueTypeName(&v));
      return false;
  }
  iterErr(vm, "RuntimeError: out of memory");
  return false;
}

// fn 参数种类校验（MAPFN/FILTER 共用）：脚本函数归 T018；非函数 → TypeError；失败置错误槽
static bool checkIterFn(MsVM* vm, const MsValue* fn) {
  if (fn->kind == MS_VAL_FUNCTION) {
    iterErr(vm, "RuntimeError: script function values execute in T018");
    return false;
  }
  if (fn->kind != MS_VAL_NATIVE) {
    iterErr(vm, "TypeError: expects a function argument");
    return false;
  }
  return true;
}

// 原生 fn 调用（MAPFN/FILTER 共用）：v 压栈 root、args 指向栈槽（OP_CALL peek 纪律）、
// 调毕弹出；fn 内部分配触发 GC 时 v 在栈存活；失败 false（错误槽已置）
static bool callIterFn(MsVM* vm, MsValue fn, MsValue v, MsValue* out) {
  if (!msVmPush(vm, v)) {
    return false;  // StackOverflow 槽已置
  }
  MsValue r = fn.as.native(vm, 1, &vm->stack[vm->stackTop - 1]);
  vm->stackTop--;  // 弹实参
  if (vm->hasError) {
    return false;
  }
  *out = r;  // 寄存器值：返回至调用方立即消费/上压，窗口内无分配
  return true;
}

bool msIterNext(MsVM* vm, MsValue it, MsValue* out) {
  MsObjIterShell* shell = (MsObjIterShell*)it.as.obj;
  switch (shell->kind) {
    case MS_ITER_STR:
      return msStrIterNext(vm, (MsObjStrIter*)shell, out);
    case MS_ITER_LIST: {
      bool modified = false;
      if (!msListIterNext((MsObjListIter*)shell, out, &modified)) {
        if (modified) {  // 22 §4：版本失配翻译（调用方协议）
          iterErr(vm, "IteratorError: list modified during iteration");
        }
        return false;
      }
      return true;
    }
    case MS_ITER_RANGE: {
      MsObjIter* p = (MsObjIter*)shell;
      ms_i64 cur = p->cur;
      ms_i64 step = p->step;
      if (!(step > 0 ? cur < p->stop : cur > p->stop)) {
        return false;  // 耗尽
      }
      *out = msValueInt(cur);
      // 推进（饱和到 stop 防 INT64 边界回绕，原 FOR_ITER inline 语义）
      if (step > 0 ? cur > INT64_MAX - step : cur < INT64_MIN - step) {
        p->cur = p->stop;
      } else {
        p->cur = cur + step;
      }
      return true;
    }
    case MS_ITER_MAP:
    case MS_ITER_SET: {  // 键游标：插入序 seq 链（10 §4 无版本号语义）
      MsObjIter* p = (MsObjIter*)shell;
      const MsObjMap* m = p->kind == MS_ITER_SET ? ((const MsObjSet*)p->src.as.obj)->map
                                                  : (const MsObjMap*)p->src.as.obj;
      ms_i64 slot = p->i < 0 ? msObjMapFirst(m) : msObjMapNext(m, p->i);
      if (slot < 0) {
        return false;  // 耗尽
      }
      p->i = slot;
      *out = *msObjMapKeyAt(m, slot);
      return true;
    }
    case MS_ITER_TUPLE: {
      MsObjIter* p = (MsObjIter*)shell;
      const MsObjTuple* t = (const MsObjTuple*)p->src.as.obj;
      if (p->i >= t->count) {
        return false;
      }
      *out = t->items[p->i++];
      return true;
    }
    case MS_ITER_REVERSE: {
      MsObjIter* p = (MsObjIter*)shell;
      if (p->i < 0) {
        return false;  // 越过首元素即耗尽
      }
      if (p->src.kind == MS_VAL_OBJ && p->src.as.obj->type == MS_OBJ_STRING) {
        ms_u32 cp = 0;
        // runeIndex 惰性建为分配点：src 经迭代器（调用方 root）标记存活
        if (!msObjStringRuneAt(vm, (MsObjString*)p->src.as.obj, p->i, &cp)) {
          return false;  // 防御（i 起始即 charLen-1 界内）
        }
        p->i--;
        *out = msValueInt((ms_i64)cp);
        return true;
      }
      if (p->src.kind == MS_VAL_OBJ && p->src.as.obj->type == MS_OBJ_LIST) {
        MsObjList* list = (MsObjList*)p->src.as.obj;
        if (p->version != list->version) {
          iterErr(vm, "IteratorError: list modified during iteration");
          return false;
        }
        if (!msObjListGet(list, p->i, out)) {
          return false;
        }
        p->i--;
        return true;
      }
      if (p->src.kind == MS_VAL_OBJ && p->src.as.obj->type == MS_OBJ_TUPLE) {
        const MsValue* item = msObjTupleGet((const MsObjTuple*)p->src.as.obj, p->i);
        if (item == NULL) {
          return false;  // 防御
        }
        *out = *item;
        p->i--;
        return true;
      }
      iterErr(vm, "RuntimeError: unknown iterator kind");
      return false;  // 构造器已拒其余类型：防御
    }
    case MS_ITER_ENUMERATE: {
      MsObjIter* p = (MsObjIter*)shell;
      MsValue v;
      if (!msIterNext(vm, p->src, &v)) {
        return false;  // 内层耗尽/错误直通
      }
      if (!msVmPush(vm, v)) {
        return false;  // StackOverflow 槽已置
      }
      MsObjTuple* t = msObjTupleNew(vm, 2);  // 分配点：v 已压栈 root
      if (t == NULL) {
        iterErr(vm, "RuntimeError: out of memory");
        vm->stackTop--;
        return false;
      }
      t->items[0] = msValueInt(p->cur++);
      t->items[1] = vm->stack[vm->stackTop - 1];  // 重读（值语义无碍，防御栈搬移）
      vm->stackTop--;
      *out = msValueObj(&t->head);
      return true;
    }
    case MS_ITER_ZIP: {
      MsObjIter* p = (MsObjIter*)shell;
      MsValue a;
      bool hasA = msIterNext(vm, p->src, &a);
      if (vm->hasError) {
        return false;
      }
      if (hasA && !msVmPush(vm, a)) {
        return false;  // StackOverflow 槽已置
      }
      MsValue b;
      bool hasB = msIterNext(vm, p->src2, &b);
      if (vm->hasError || !hasB) {
        if (hasA) {
          vm->stackTop--;
        }
        if (vm->hasError) {
          return false;  // 内层错误直通
        }
        if ((p->flags & 1) != 0) {  // strict：单侧先尽即失配
          iterErr(vm, "ValueError: zip() strict: iterables have unequal lengths");
          return false;
        }
        return false;  // 耗尽
      }
      if (!hasA) {
        if ((p->flags & 1) != 0) {
          iterErr(vm, "ValueError: zip() strict: iterables have unequal lengths");
          return false;
        }
        return false;  // 耗尽
      }
      if (!msVmPush(vm, b)) {
        vm->stackTop--;
        return false;
      }
      MsObjTuple* t = msObjTupleNew(vm, 2);  // 分配点：a/b 均已压栈 root
      if (t == NULL) {
        iterErr(vm, "RuntimeError: out of memory");
        vm->stackTop -= 2;
        return false;
      }
      t->items[0] = vm->stack[vm->stackTop - 2];
      t->items[1] = vm->stack[vm->stackTop - 1];
      vm->stackTop -= 2;
      *out = msValueObj(&t->head);
      return true;
    }
    case MS_ITER_MAPFN: {
      MsObjIter* p = (MsObjIter*)shell;
      MsValue v;
      if (!msIterNext(vm, p->src, &v)) {
        return false;
      }
      if (!checkIterFn(vm, &p->fn)) {
        return false;
      }
      return callIterFn(vm, p->fn, v, out);
    }
    case MS_ITER_FILTER: {
      MsObjIter* p = (MsObjIter*)shell;
      if (!checkIterFn(vm, &p->fn)) {
        return false;
      }
      for (;;) {
        MsValue v;
        if (!msIterNext(vm, p->src, &v)) {
          return false;  // 内层耗尽/错误直通
        }
        MsValue keep;
        if (!callIterFn(vm, p->fn, v, &keep)) {
          return false;
        }
        if (msValueIsTruthy(&keep)) {
          *out = v;  // keep 消费后窗口内无分配：v 寄存器值安全
          return true;
        }
      }
    }
    case MS_ITER_TAKE: {
      MsObjIter* p = (MsObjIter*)shell;
      if (p->cur <= 0) {
        return false;  // 额度耗尽
      }
      if (!msIterNext(vm, p->src, out)) {
        return false;  // 内层先尽
      }
      p->cur--;
      return true;
    }
    case MS_ITER_DROP: {
      MsObjIter* p = (MsObjIter*)shell;
      if ((p->flags & 2) == 0) {  // 首次推进：跳过 i 个
        p->flags |= 2;
        while (p->i > 0) {
          MsValue skip;
          if (!msIterNext(vm, p->src, &skip)) {
            return false;
          }
          p->i--;
        }
      }
      return msIterNext(vm, p->src, out);
    }
    default:
      iterErr(vm, "RuntimeError: unknown iterator kind");
      return false;
  }
}

bool msIterCollectList(MsVM* vm, MsValue it, MsValue* out) {
  // it 由调用方 root；元素逐个压栈（推进途中分配窗口内全部存活）
  int32_t startTop = vm->stackTop;
  for (;;) {
    MsValue item;
    if (!msIterNext(vm, it, &item)) {
      if (vm->hasError) {
        return false;  // 错误：stackTop 不恢复（同 seqForm 纪律）
      }
      break;  // 耗尽
    }
    if (!msVmPush(vm, item)) {
      return false;  // StackOverflow 槽已置；stackTop 不恢复（同上）
    }
  }
  // 收集契约（value.c）：消费栈顶 n 个源值、返回 list（寄存器安全）；OOM/溢出置槽返回 nil
  *out = msVmCollectStackToList(vm, vm->stackTop - startTop);
  return !vm->hasError;
}
