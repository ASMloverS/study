// 最小 MsObjList 桥（T013，22 §4；D6）：split/join 依赖的数据面；方法族/切片/迭代器壳归 T014
#ifndef INCLUDE_MS_OBJ_OBJ_LIST_H_
#define INCLUDE_MS_OBJ_OBJ_LIST_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef struct MsObjList {
  MsObj head;
  MsValue* items;  // 倍增（初始 8）
  int32_t count;
  int32_t cap;
} MsObjList;

MsObjList* msObjListNew(MsVM* vm);  // 空表（items NULL）
// 追加（倍增；分配失败返回 false 且列表不变）
bool msObjListPush(MsVM* vm, MsObjList* list, MsValue v);
// 预留容量（一次 realloc 到 max(8, n)；失败 false 且列表不变；BUILD_LIST 直填/split 收集前调用）
bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n);
// 索引读取（负索引换算；越界返回 false）
bool msObjListGet(const MsObjList* list, ms_i64 i, MsValue* out);
// children 标记（gcSetMarkFn 注册转发；进程级表由 vm.c msVmInit 统一注册）
void msObjListMarkChildren(MsVM* vm, MsObj* obj);

#endif  // INCLUDE_MS_OBJ_OBJ_LIST_H_
