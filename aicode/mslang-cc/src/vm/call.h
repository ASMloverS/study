// 调用装配与 C 层调用面（T018）：帧切换参数装配 + 脚本函数可被原生高阶函数回调
#ifndef INCLUDE_MS_VM_CALL_H_
#define INCLUDE_MS_VM_CALL_H_

#include "vm/vm.h"

// 帧装配（栈形 [callee, a1..argc]，callee 为 FUNCTION/CLOSURE 对象）：
// 个数校验（ArgumentError）→ 默认参填充 → rest 收 list → nil 区 → 入帧；帧上限 2048
// 成功后新帧已压（ip=0），调用方重载 code/consts/ip；失败置错误槽（errLine 由调用方补）
bool msVmSetupCall(MsVM* vm, int32_t argc);

// CALL/CALL_METHOD 入口展平末位 * 展开实参（flags 位声明）：栈顶 list 元素逐一压栈（GC root）
// 失败置错误槽（TypeError 非 list / 栈溢出）；成功改写 *argc = 原值 - 1 + 元素数
bool msVmFlattenSpread(MsVM* vm, int32_t* argc);

// upvalue 捕获/闭合（OP_CLOSURE/OP_RETURN/CLOSE_UPVAL 共用；实现于 call.c）
MsObjUpvalue* msVmCaptureUpvalue(MsVM* vm, MsValue* slot);
void msVmCloseUpvalues(MsVM* vm, MsValue* fromSlot);

// vm.c：可重入 dispatch 核心（执行至当前入口帧 RETURN 弹出；msVmRun 与后续 msVmCallValue 共用）
void msVmRunFrames(MsVM* vm);

// 任意可调用值的 C 层调用（原生经 msVmCallNative；脚本函数入帧 + 嵌套 runFrames 至该帧 RETURN）
// callee+argv 先压栈 root；失败（含被调运行时错误）置槽返回 false
bool msVmCallValue(MsVM* vm, const MsValue* callee, const MsValue* argv, int32_t argc, MsValue* out);

#endif  // INCLUDE_MS_VM_CALL_H_
