// 实例字段表（T020，22 §6 基础版）：intern 名 → 值直查；
// T022 升级为 Shape 转移链 + 内联槽 + 溢出数组（本文件 API 不变）
#ifndef INCLUDE_MS_OBJ_SHAPE_H_
#define INCLUDE_MS_OBJ_SHAPE_H_

#include <stdbool.h>

#include "obj/obj.h"

struct MsObjInstance;  // obj_class.h（按指针避免头环）

// 字段读取：命中写 *out 返回 true；未命中 false（无错误场景）
bool msShapeFieldGet(const struct MsObjInstance* inst, const MsObjString* name, MsValue* out);
// 字段写入（首赋创建）；OOM 返回 false（调用方翻译错误槽）
bool msShapeFieldSet(struct MsVM* vm, struct MsObjInstance* inst, const MsObjString* name, const MsValue* v);

#endif  // INCLUDE_MS_OBJ_SHAPE_H_
