// 生成器执行面实现（T019，05 §4 / 21 §3.6）：入口挂起 + 恢复驱动 + upvalue 重联
// 别名语义：YIELD 关闭帧内开放 upvalue（值拷入 closed），恢复时 closed 回拷槽位并重新开放——
// 挂起期间经闭包的写入（closed）与恢复后的帧槽保持同一变量（CPython cells 等效）
#include "vm/generator.h"

#include <stdio.h>
#include <string.h>

#include "obj/obj_coroutine.h"
#include "obj/obj_func.h"
#include "vm/call.h"

void msVmGeneratorSuspendEntry(MsVM* vm) {
  MsFrame* gen = &vm->frames[vm->frameCount - 1];
  int32_t base = gen->base;
  // 协程分配点：帧槽（实参/默认值/rest/nil 区）与闭包（被调槽）均在栈 root
  MsObjCoroutine* co = msObjCoroutineNew(vm, gen->chunk, gen->closure);
  if (co == NULL) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
    vm->hasError = true;
    return;
  }
  int32_t count = vm->stackTop - base;
  co->stack = malloc(sizeof(MsValue) * (size_t)count);
  MS_ASSERT(co->stack != NULL);
  memcpy(co->stack, &vm->stack[base], sizeof(MsValue) * (size_t)count);
  co->stackTop = co->stackCap = count;
  co->ip = 0;
  co->state = MS_CORO_START;
  vm->frameCount--;                       // 弹未执行帧
  vm->stack[base - 1] = msValueObj(&co->head);  // 协程即调用结果（被调槽改写）
  vm->stackTop = base;                    // 结果保持活跃栈顶（OP_RETURN 同款）
}

// 恢复期 upvalue 重联：closed 值回拷帧槽（挂起期间闭包写入同步）+ location 重指 + 按地址降序插回开放链
static void relinkUpvalues(MsVM* vm, MsObjCoroutine* co, int32_t base) {
  for (int32_t i = 0; i < co->linkCount; i++) {
    MsObjUpvalue* u = co->links[i].upv;
    MsValue* slot = &vm->stack[base + co->links[i].slotOff];
    *slot = u->closed;
    MsObjUpvalue* prev = NULL;
    MsObjUpvalue* cur = vm->openUpvalues;
    while (cur != NULL && cur->location > slot) {
      prev = cur;
      cur = cur->next;
    }
    u->location = slot;
    u->next = cur;
    if (prev == NULL) {
      vm->openUpvalues = u;
    } else {
      prev->next = u;
    }
  }
  co->linkCount = 0;  // 已全部重开（表容量保留复用）
}

bool msVmResumeGenerator(MsVM* vm, MsObjCoroutine* co, MsValue* out) {
  if (co->state == MS_CORO_DONE) {
    return false;
  }
  if (vm->callDepth >= MS_VM_MAX_CALL_DEPTH) {  // 复用 T018 C 层重入守卫
    snprintf(vm->errMsg, sizeof(vm->errMsg), "StackOverflowError: call nesting limit exceeded (%d)",
             MS_VM_MAX_CALL_DEPTH);
    vm->errLine = -1;
    vm->hasError = true;
    return false;
  }
  if (!msVmPush(vm, msValueObj(&co->head))) {  // 被调槽：协程自身（YIELD 结果写回处；亦是 GC root）
    return false;                              // StackOverflow 槽已置
  }
  int32_t base = vm->stackTop;
  for (int32_t i = 0; i < co->stackTop; i++) {  // 挂起帧槽回拷（逐 push：扩容安全）
    if (!msVmPush(vm, co->stack[i])) {
      return false;
    }
  }
  relinkUpvalues(vm, co, base);
  MsFrame frame = {co->chunk, co->ip, base, co->closure};
  MS_ARRAY_PUSH(vm->frames, vm->frameCount, vm->frameCap, frame);
  co->state = MS_CORO_RUNNING;
  vm->callDepth++;
  msVmRunFrames(vm);  // YIELD 挂起退出（entry 机制）或 RETURN 耗尽
  vm->callDepth--;
  if (vm->hasError) {
    co->state = MS_CORO_DONE;  // 出错即终态：后续 next 恒 nil（错误路径不恢复 stackTop，整场 run 废弃）
    return false;
  }
  if (co->state == MS_CORO_SUSPENDED) {
    *out = vm->stack[--vm->stackTop];  // YIELD 值在被调槽
    return true;
  }
  co->state = MS_CORO_DONE;  // RUNNING 期间自然 RETURN：耗尽（返回值按 05 §4 丢弃）
  vm->stackTop--;            // 弹被调槽残留（RETURN 写入的返回值）
  return false;
}
