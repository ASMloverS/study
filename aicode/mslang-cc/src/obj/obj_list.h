// MsObjList（T014，22 §4）：倍增数组 + 结构版本号；T013 数据面（push/reserve/get/mark/free）延续
// 方法族/切片/迭代器壳实现分批落位（任务书 §2）；split/join 收集与 BUILD_LIST 直填路径不变
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
  ms_u32 version;  // 结构版本号（22 §4）：结构性修改递增；迭代器快照失配 → IteratorError
} MsObjList;

MsObjList* msObjListNew(MsVM* vm);  // 空表（items NULL）
// 尾加（倍增；结构性修改 version++；分配失败返回 false 且列表不变）
bool msObjListPush(MsVM* vm, MsObjList* list, MsValue v);
// 预留容量（容量变化非结构修改，不 bump；失败 false 且列表不变；BUILD_LIST 直填/split 收集前调用）
bool msObjListReserve(MsVM* vm, MsObjList* list, int32_t n);
// 索引读取（负索引换算；越界返回 false）
bool msObjListGet(const MsObjList* list, ms_i64 i, MsValue* out);
// 索引写入（负索引换算；非结构性：不 bump；越界返回 false）
bool msObjListSet(MsObjList* list, ms_i64 i, MsValue v);
// 插入（负索引换算后钳位 [0,count]；version++；分配失败 false 且列表不变）
bool msObjListInsert(MsObjList* list, ms_i64 i, MsValue v);
// 按下标移除并返回（负索引换算；version++；越界返回 false）
bool msObjListRemoveAt(MsObjList* list, ms_i64 i, MsValue* out);
// 清空（count=0，容量保留；version++）
void msObjListClear(MsObjList* list);
// 原地反转（version++）
void msObjListReverse(MsObjList* list);
// 按值首查（msValueEqual 深等价）；未命中 -1
int32_t msObjListIndexOf(const MsObjList* list, const MsValue* v);
// 原地稳定排序结果（缺省 < 序；比较失败元素多重集不变——偶数趟可能部分重排，快照恢复归 T021）
typedef enum MsListSortResult {
  MS_LIST_SORT_OK = 0,
  MS_LIST_SORT_TYPE,  // 元素两两不可比较
  MS_LIST_SORT_OOM,
} MsListSortResult;
// 规范切片 → 新 list（源由调用方 root；step==0 返回 NULL 且 *errStepZero 置 true）
MsObjList* msObjListSlice(MsVM* vm, const MsObjList* list, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                          bool hasC, ms_i64 c, bool* errStepZero);
// 拼接 → 新 list（源由调用方 root；溢出/OOM 返回 NULL）
MsObjList* msObjListConcat(MsVM* vm, const MsObjList* a, const MsObjList* b);
// 重复 → 新 list（n<=0 空表；源由调用方 root；溢出/OOM 返回 NULL）
MsObjList* msObjListRepeat(MsVM* vm, const MsObjList* a, ms_i64 n);
// 原地稳定归并排序（缺省 < 序，msValueOrder；成功 version++；失败原序保持）
MsListSortResult msObjListSort(MsObjList* list);
// children 标记（gcSetMarkFn 注册转发；进程级表由 vm.c msVmInit 统一注册）
void msObjListMarkChildren(MsVM* vm, MsObj* obj);
// items 数组释放（gcSetFreeFn 注册转发；sweep/heapFree 释放对象前回调）
void msObjListFreeData(MsObj* obj);

// ---- list 迭代器（T014，22 §4 版本号；MS_OBJ_ITERATOR tag，kind 判别见 obj.h） ----

typedef struct MsObjListIter {
  MsObj head;
  ms_u8 kind;       // 恒 MS_ITER_LIST
  MsObjList* list;  // markChildren 标记
  int32_t pos;      // 游标
  ms_u32 version;   // 创建时结构版本快照
} MsObjListIter;

MsObjListIter* msObjListIterNew(MsVM* vm, MsObjList* list);
// 推进一格；耗尽返回 false；版本失配返回 false 且 *errModified 置 true（调用方报 IteratorError）
bool msListIterNext(MsObjListIter* it, MsValue* out, bool* errModified);

static inline bool msIsList(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_LIST;
}

#endif  // INCLUDE_MS_OBJ_OBJ_LIST_H_
