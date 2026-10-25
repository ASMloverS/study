// MsObjTuple（T015，22 §4）：FAM 定长不可变序列 + 惰性 hash 缓存
#ifndef INCLUDE_MS_OBJ_OBJ_TUPLE_H_
#define INCLUDE_MS_OBJ_OBJ_TUPLE_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjTuple {
  MsObj head;
  ms_u64 hash;   // 惰性缓存：首次入 map/set 时计算（obj_map.c msValueHash 写入，T015 Task 2）
  bool hashValid;
  int32_t count;
  MsValue items[];  // FAM；创建即全 nil，调用方覆写
} MsObjTuple;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 创建 count 元组（items 全 nil——分配点 GC 安全）；失败 NULL
MsObjTuple* msObjTupleNew(MsVM* vm, int32_t count);
// 索引读取（负索引换算；越界 NULL）
const MsValue* msObjTupleGet(const MsObjTuple* t, ms_i64 i);
// children 标记（gcSetMarkFn 注册转发）
void msObjTupleMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsTuple(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_TUPLE;
}

#endif  // INCLUDE_MS_OBJ_OBJ_TUPLE_H_
