// 调用装配实现（T018，20 §7/21 §6）：帧切换参数装配 + upvalue 开放链维护
#include "vm/call.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "obj/obj_func.h"
#include "obj/obj_list.h"

#define MS_VM_MAX_FRAMES 2048  // 帧深上限（过渡；可增长栈归 T028）
#define MS_VM_MAX_CALL_DEPTH 32  // C 层重入上限（Debug MSVC 实测 ~18KB/层、1MB 原生栈约 56 层溢出，留余量；T028 评估）

static void callErr(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

bool msVmSetupCall(MsVM* vm, int32_t argc) {
  MsValue callee = vm->stack[vm->stackTop - argc - 1];
  const MsObjFunction* fn = msCallableFn(&callee);
  const Chunk* proto = fn->chunk;
  if (vm->frameCount >= MS_VM_MAX_FRAMES) {
    callErr(vm, "StackOverflowError: frame limit exceeded (%d)", MS_VM_MAX_FRAMES);
    return false;
  }
  int32_t nFixed = proto->paramCount - (proto->hasRest ? 1 : 0);
  int32_t nReq = nFixed - fn->defaultCount;  // 默认参个数以对象为准（defaults FAM 即 defaultCount 尺寸）
  const char* fname = proto->nameLen > 0 ? proto->name : "<anonymous>";
  int32_t fnameLen = proto->nameLen > 0 ? proto->nameLen : (int32_t)strlen(fname);
  if (argc < nReq) {
    callErr(vm, "ArgumentError: %.*s() expects at least %d argument(s), got %d", (int)fnameLen, fname, nReq, argc);
    return false;
  }
  if (argc > nFixed && !proto->hasRest) {
    callErr(vm, "ArgumentError: %.*s() expects at most %d argument(s), got %d", (int)fnameLen, fname, nFixed, argc);
    return false;
  }
  int32_t base = vm->stackTop - argc;
  for (int32_t p = argc; p < nFixed; p++) {  // 默认参填充（callee 在 base-1 槽 root，fn 读取安全）
    if (!msVmPush(vm, fn->defaults[p - nReq])) {
      return false;  // 栈溢出槽已置
    }
  }
  if (proto->hasRest) {  // 余量收 list（BUILD_LIST 同款：list 先落栈 root 再拷贝）
    int32_t n = vm->stackTop - (base + nFixed);
    MsObjList* rest = msObjListNew(vm);
    if (rest == NULL) {
      callErr(vm, "RuntimeError: out of memory");
      return false;
    }
    if (!msVmPush(vm, msValueObj(&rest->head))) {
      return false;
    }
    if (!msObjListReserve(vm, rest, n)) {
      callErr(vm, "RuntimeError: out of memory");
      return false;
    }
    if (n > 0) {
      memcpy(rest->items, &vm->stack[base + nFixed], sizeof(MsValue) * (size_t)n);
      rest->count = n;
    }
    vm->stack[base + nFixed] = vm->stack[vm->stackTop - 1];  // rest 下移覆盖首余量槽
    vm->stackTop = base + nFixed + 1;
  }
  while (vm->stackTop < base + proto->localCount) {  // nil 区（帧局部槽上界，GC root）
    if (!msVmPush(vm, msValueNil())) {
      return false;
    }
  }
  MsObjClosure* closure = callee.as.obj->type == MS_OBJ_CLOSURE ? (MsObjClosure*)callee.as.obj : NULL;
  MsFrame frame = {proto, 0, base, closure};
  MS_ARRAY_PUSH(vm->frames, vm->frameCount, vm->frameCap, frame);
  return true;
}

bool msVmFlattenSpread(MsVM* vm, int32_t* argc) {
  MsValue sv = vm->stack[vm->stackTop - 1];
  if (sv.kind != MS_VAL_OBJ || sv.as.obj->type != MS_OBJ_LIST) {
    callErr(vm, "TypeError: spread argument must be a list");
    return false;
  }
  const MsObjList* list = (const MsObjList*)sv.as.obj;
  vm->stackTop--;  // 弹 list（C 局部持有；弹后至逐元素压栈间无分配窗口，无回收风险）
  for (int32_t i = 0; i < list->count; i++) {
    if (!msVmPush(vm, list->items[i])) {
      return false;  // 栈溢出槽已置
    }
  }
  *argc = *argc - 1 + list->count;
  return true;
}

MsObjUpvalue* msVmCaptureUpvalue(MsVM* vm, MsValue* slot) {  // 开放链按地址降序插入/复用（Task 4 接入 CLOSURE）
  MsObjUpvalue* prev = NULL;
  MsObjUpvalue* u = vm->openUpvalues;
  while (u != NULL && u->location > slot) {
    prev = u;
    u = u->next;
  }
  if (u != NULL && u->location == slot) {
    return u;  // 已有开放捕获复用
  }
  MsObjUpvalue* n = (MsObjUpvalue*)msObjAlloc(vm, MS_OBJ_UPVALUE, sizeof(MsObjUpvalue));
  if (n == NULL) {
    return NULL;
  }
  n->next = u;
  n->location = slot;
  n->closed = msValueNil();
  if (prev == NULL) {
    vm->openUpvalues = n;
  } else {
    prev->next = n;
  }
  return n;
}

void msVmCloseUpvalues(MsVM* vm, MsValue* fromSlot) {  // 闭合 location >= fromSlot 的开放 upvalue（值拷贝入对象）
  while (vm->openUpvalues != NULL && vm->openUpvalues->location >= fromSlot) {
    MsObjUpvalue* u = vm->openUpvalues;
    u->closed = *u->location;
    u->location = &u->closed;
    vm->openUpvalues = u->next;
    u->next = NULL;
  }
}

bool msVmCallValue(MsVM* vm, const MsValue* callee, const MsValue* argv, int32_t argc, MsValue* out) {
  if (callee->kind == MS_VAL_NATIVE) {
    return msVmCallNative(vm, callee, argv, argc, out);
  }
  if (!msIsCallableObj(callee)) {
    callErr(vm, "TypeError: '%s' is not callable", msValueTypeName(callee));
    return false;
  }
  if (vm->callDepth >= MS_VM_MAX_CALL_DEPTH) {
    callErr(vm, "StackOverflowError: call nesting limit exceeded (%d)", MS_VM_MAX_CALL_DEPTH);
    return false;
  }
  if (!msVmPush(vm, *callee)) {
    return false;  // 栈溢出槽已置
  }
  for (int32_t i = 0; i < argc; i++) {
    if (!msVmPush(vm, argv[i])) {
      return false;  // 栈溢出槽已置（错误路径不恢复 stackTop：整场 run 经 unwind 废弃）
    }
  }
  if (!msVmSetupCall(vm, argc)) {
    return false;  // 错误槽已置
  }
  vm->callDepth++;
  msVmRunFrames(vm);  // 嵌套执行：入口帧 RETURN 后收口（结果在栈顶，被调帧 upvalue 已按其基闭合）
  vm->callDepth--;
  if (vm->hasError) {
    return false;
  }
  *out = vm->stack[--vm->stackTop];
  return true;
}
