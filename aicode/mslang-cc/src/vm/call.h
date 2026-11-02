// 调用装配与 C 层调用面（T018）：帧切换参数装配 + 脚本函数可被原生高阶函数回调
#ifndef INCLUDE_MS_VM_CALL_H_
#define INCLUDE_MS_VM_CALL_H_

#include "vm/vm.h"

// 帧装配（栈形 [callee, a1..argc]，callee 为 FUNCTION/CLOSURE 对象）：
// 个数校验（ArgumentError）→ 默认参填充 → rest 收 list → nil 区 → 入帧；帧上限 2048
// 成功后新帧已压（ip=0），调用方重载 code/consts/ip；失败置错误槽（errLine 由调用方补）
bool msVmSetupCall(MsVM* vm, int32_t argc);

// upvalue 捕获/闭合（OP_CLOSURE/OP_RETURN/CLOSE_UPVAL 共用；实现于 call.c）
MsObjUpvalue* msVmCaptureUpvalue(MsVM* vm, MsValue* slot);
void msVmCloseUpvalues(MsVM* vm, MsValue* fromSlot);

// vm.c：可重入 dispatch 核心（执行至当前入口帧 RETURN 弹出；msVmRun 与后续 msVmCallValue 共用）
void msVmRunFrames(MsVM* vm);

#endif  // INCLUDE_MS_VM_CALL_H_
