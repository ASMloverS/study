// 生成器执行面（T019，05 §4 / 21 §3.6）：协程挂起/恢复驱动
#ifndef INCLUDE_MS_VM_GENERATOR_H_
#define INCLUDE_MS_VM_GENERATOR_H_

#include <stdbool.h>

#include "vm/vm.h"

struct MsObjCoroutine;  // obj_coroutine.h

// OP_CALL 生成器分支：setupCall 装配好的帧即时转协程（实参/默认值/rest 已入槽，体不执行）；
// 协程写入被调槽（结果位）、stackTop=base（OP_RETURN 纪律）；调用方须重取 frame/续 ip
void msVmGeneratorSuspendEntry(MsVM* vm);

// 恢复协程至下一 YIELD：true = *out 产出值；false = 耗尽（协程置 DONE）或错误槽已置。
// next()/FOR_ITER/迭代适配器/OP_RESUME 共用本面
bool msVmResumeGenerator(MsVM* vm, struct MsObjCoroutine* co, MsValue* out);

#endif  // INCLUDE_MS_VM_GENERATOR_H_
