// MsObjMap（T015，22 §4）：开放寻址线性探测 + 墓碑 + 插入序 seq 链 + 键 hash 缓存
// 共享值 hash（msValueHash）：数值塔统一（02 §7 hash(1)==hash(1.0)）
#ifndef INCLUDE_MS_OBJ_OBJ_MAP_H_
#define INCLUDE_MS_OBJ_OBJ_MAP_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

typedef enum MsMapSlotState {
  MS_MSS_EMPTY = 0,
  MS_MSS_ACTIVE = 1,
  MS_MSS_TOMBSTONE = 2,
} MsMapSlotState;

typedef struct MsMapSlot {
  MsValue key;
  MsValue value;
  ms_u64 hash;             // 键 hash 缓存（22 §4）
  ms_i64 seqPrev, seqNext; // 插入序双向链（slot 下标；-1 端哨兵）
  ms_u8 state;             // MsMapSlotState
} MsMapSlot;

typedef struct MsObjMap {
  MsObj head;
  MsMapSlot* slots;  // cap 2^n（初始 8；负载 0.75 再哈希）
  int32_t cap;
  int32_t liveCount;  // active 数
  int32_t usedCount;  // active + tombstone（扩容依据）
  ms_i64 seqHead, seqTail;  // 插入序链端点（slot 下标；-1 空）
} MsObjMap;

typedef enum MsMapOp {
  MS_MAP_OK = 0,
  MS_MAP_NOENT,       // get/remove 未命中
  MS_MAP_UNHASHABLE,  // 键不可 hash（调用方翻译 TypeError）
  MS_MAP_OOM,
} MsMapOp;

typedef enum MsHashStatus {
  MS_HASH_OK = 0,
  MS_HASH_UNHASHABLE,  // list/map/set/实例等（02 §7；bigint 键 T016、__hash__ T021）
} MsHashStatus;

// 值 hash（nil/bool/int/float/string/tuple；tuple 首次计算后写缓存）
MsHashStatus msValueHash(const MsValue* v, ms_u64* out);

MsObjMap* msObjMapNew(MsVM* vm);
MsMapOp msObjMapGet(const MsObjMap* m, const MsValue* key, MsValue* outValue);  // outValue 可 NULL
MsMapOp msObjMapPut(MsVM* vm, MsObjMap* m, const MsValue* key, const MsValue* value);
MsMapOp msObjMapRemove(MsObjMap* m, const MsValue* key, MsValue* outOld);  // outOld 可 NULL
void msObjMapClear(MsObjMap* m);
// 插入序遍历：First 返回首 slot 或 -1；Next 返回后继或 -1（slot 失效勿跨变更使用）
ms_i64 msObjMapFirst(const MsObjMap* m);
ms_i64 msObjMapNext(const MsObjMap* m, ms_i64 slot);
const MsValue* msObjMapKeyAt(const MsObjMap* m, ms_i64 slot);
const MsValue* msObjMapValueAt(const MsObjMap* m, ms_i64 slot);
void msObjMapMarkChildren(MsVM* vm, MsObj* obj);
void msObjMapFreeData(MsObj* obj);

static inline bool msIsMap(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_MAP;
}

#endif  // INCLUDE_MS_OBJ_OBJ_MAP_H_
