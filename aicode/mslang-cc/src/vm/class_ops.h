// 类运行时操作（T020，06 §2/22 §6）：MRO 线性化 + 属性查找公共面（vm.c/builtins.c 共用）
#ifndef INCLUDE_MS_VM_CLASS_OPS_H_
#define INCLUDE_MS_VM_CLASS_OPS_H_

#include <stdbool.h>

#include "obj/obj_class.h"
#include "vm/vm.h"

// MRO 线性化并写 klass->mro（[本类] + mixins 逆序 + 基类 MRO；去重保首；
// 逐 base 验证其 mro 为结果子序列）。冲突/类型错误/OOM：置错误槽返回 false。
// 纪律：klass 须已由调用方 root（INHERIT_INIT 栈上类值）
bool msClassBuildMro(MsVM* vm, MsObjClass* klass);

// 实例属性查找（06 §4）：字段 → MRO methods → MRO statics。
// 命中写 *out（isMethod=true 表示 *out 为方法函数值）返回 true；未命中 false
bool msInstanceFindAttr(const MsObjInstance* inst, const MsObjString* name, MsValue* out, bool* isMethod);

// 类属性查找：MRO 各级 methods（isMethod=true）→ statics（isMethod=false）；未命中 false
bool msClassFindAttr(const MsObjClass* klass, const MsObjString* name, MsValue* out, bool* isMethod);

// super 查找：start 之后（不含）首个 methods/statics 命中类（06 §2；start 为定义类）
bool msClassFindAttrAfter(const MsObjClass* start, const MsObjString* name, MsValue* out, bool* isMethod);

#endif  // INCLUDE_MS_VM_CLASS_OPS_H_
