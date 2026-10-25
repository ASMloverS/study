// MsObjSet（T015，22 §4）：map 特例（值占位 true）+ 集合运算族
#ifndef INCLUDE_MS_OBJ_OBJ_SET_H_
#define INCLUDE_MS_OBJ_OBJ_SET_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj_list.h"
#include "obj/obj_map.h"

typedef struct MsObjSet {
  MsObj head;
  MsObjMap* map;  // 值恒占位 true
} MsObjSet;

// 构造：分配 set 后借 VM 栈临时 root 再建内层 map（D6：两次 GC 触发点间不失根）
MsObjSet* msObjSetNew(MsVM* vm);
MsMapOp msObjSetAdd(MsVM* vm, MsObjSet* st, const MsValue* v);
bool msObjSetHas(const MsObjSet* st, const MsValue* v);  // 不可 hash 一律 false
MsMapOp msObjSetRemove(MsObjSet* st, const MsValue* v);
void msObjSetClear(MsObjSet* st);
int32_t msObjSetCount(const MsObjSet* st);
// 快照/拷贝/运算族：分配点——结果先落 VM 栈 root（D6）；源 a/b 由调用方 root；OOM 返回 NULL
MsObjList* msObjSetToList(MsVM* vm, const MsObjSet* st);  // 插入序快照
MsObjSet* msObjSetCopy(MsVM* vm, const MsObjSet* st);
// 运算结果序：左操作数序在前，并入右操作数新元素（symDiff：a-only 后 b-only）
MsObjSet* msObjSetUnion(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetIntersect(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
MsObjSet* msObjSetSymDiff(MsVM* vm, const MsObjSet* a, const MsObjSet* b);
void msObjSetMarkChildren(MsVM* vm, MsObj* obj);

static inline bool msIsSet(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_SET;
}

#endif  // INCLUDE_MS_OBJ_OBJ_SET_H_
