// MsObjIter（T017，22 §4 / 05 §4 迭代协议）：泛化迭代器对象
// str/list 沿用各自壳结构（MsObjStrIter/MsObjListIter），其余种类统一 MsObjIter；
// kind 判别见 obj.h MsIterKind；推进统一入口 msIterNext（耗尽 false，错误置 vm 错误槽）
#ifndef INCLUDE_MS_OBJ_OBJ_ITER_H_
#define INCLUDE_MS_OBJ_OBJ_ITER_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

struct MsObjMap;    // obj_map.h（vm.h 亦前置；此处自包含补齐）
struct MsObjSet;    // obj_set.h
struct MsObjTuple;  // obj_tuple.h

typedef struct MsObjIter {
  MsObj head;
  ms_u8 kind;      // MS_ITER_RANGE 起（str/list 壳不走本结构）
  MsValue src;     // 源容器（MAP/SET/TUPLE/REVERSE）或源迭代器（ENUMERATE/ZIP/MAPFN/FILTER/TAKE/DROP）
  MsValue src2;    // ZIP 第二源迭代器；其余 nil
  MsValue fn;      // MAPFN/FILTER 原生函数；其余 nil
  ms_i64 cur;      // RANGE 当前值 / ENUMERATE 计数 / TAKE 剩余
  ms_i64 stop;     // RANGE 上界（饱和防 INT64 回绕）
  ms_i64 step;     // RANGE 步长
  ms_i64 i;        // MAP/SET 槽游标（-1 = 未起）/ TUPLE/REVERSE 索引 / DROP 剩余
  ms_u32 version;  // REVERSE over list：创建时结构版本快照
  ms_u8 flags;     // bit0 = ZIP strict；bit1 = DROP 已跳过
} MsObjIter;

// 构造族（失败返回 NULL）
MsObjIter* msObjIterRangeNew(MsVM* vm, ms_i64 start, ms_i64 stop, ms_i64 step);  // step != 0 由调用方保证
MsObjIter* msObjIterMapNew(MsVM* vm, struct MsObjMap* m);       // 键迭代（插入序）
MsObjIter* msObjIterSetNew(MsVM* vm, struct MsObjSet* st);
MsObjIter* msObjIterTupleNew(MsVM* vm, struct MsObjTuple* t);
MsObjIter* msObjIterReverseNew(MsVM* vm, MsValue seq);          // list/string/tuple 反向
MsObjIter* msObjIterEnumerateNew(MsVM* vm, MsValue srcIter, ms_i64 start);
MsObjIter* msObjIterZipNew(MsVM* vm, MsValue a, MsValue b, bool strict);
MsObjIter* msObjIterMapFnNew(MsVM* vm, MsValue srcIter, MsValue fn);
MsObjIter* msObjIterFilterNew(MsVM* vm, MsValue srcIter, MsValue fn);
MsObjIter* msObjIterTakeNew(MsVM* vm, MsValue srcIter, ms_i64 n);
MsObjIter* msObjIterDropNew(MsVM* vm, MsValue srcIter, ms_i64 n);

// 统一推进（it 必为 MS_OBJ_ITERATOR 壳，含 str/list）：true = *out 产出下一值；
// false = 耗尽（无错误）或错误（vm->hasError 已置）。调用方须保证 it 自身在其调用窗口内为 GC root
bool msIterNext(MsVM* vm, MsValue it, MsValue* out);

// 值 → 迭代器：迭代器原样直通；string/list/map/set/tuple → 新壳；其余 TypeError（errMsg 已置，不置 hasError）
// false = 不可迭代 / OOM（errMsg 已置）
bool msIterFromValue(MsVM* vm, MsValue v, MsValue* out);

// 排空 it → 新 list（元素逐个压 VM 栈 root 后汇总；错误路径不恢复 stackTop）
bool msIterCollectList(MsVM* vm, MsValue it, MsValue* out);

// children 标记（str/list 壳由 vm.c 分派，其余走本函数）
void msObjIterMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsIterator(const MsValue* v) {
  return v->kind == MS_VAL_OBJ &&
         (v->as.obj->type == MS_OBJ_ITERATOR || v->as.obj->type == MS_OBJ_COROUTINE);  // T019：生成器直通
}

#endif  // INCLUDE_MS_OBJ_OBJ_ITER_H_
