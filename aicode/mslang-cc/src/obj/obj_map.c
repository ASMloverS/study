// MsObjMap 数据面（T015，22 §4）：开放寻址线性探测 + 墓碑 + 插入序 seq 链 + 数值塔键 hash
// GC 纪律：msObjMapNew 经 msObjAlloc（GC 触发点）——调用方须将 map 落栈 root 后再做后续操作；
// put/remove/get 的再哈希与槽位 calloc 均为纯 malloc 路径（全程不经 msObjAlloc，非 GC 触发点），
// map 仅持 C 局部亦安全
#include "obj/obj_map.h"

#include <stdlib.h>
#include <string.h>

#include "gc/gc.h"
#include "obj/obj_bigint.h"
#include "obj/obj_string.h"
#include "obj/obj_tuple.h"

#define MS_MAP_INIT_CAP 8

static ms_u64 mix64(ms_u64 x) {  // splitmix64 finalizer：nil/bool 域散列
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

MsHashStatus msValueHash(const MsValue* v, ms_u64* out) {
  switch (v->kind) {
    case MS_VAL_NIL:
      *out = mix64(0x9E3779B97F4A7C15ULL);
      return MS_HASH_OK;
    case MS_VAL_BOOL:
      *out = mix64(v->as.b ? 0x2 : 0x1);  // 与 int 域分离不必要，但相等门控 bool≠int（02 §2）
      return MS_HASH_OK;
    case MS_VAL_I64:
      *out = msHashNumericI64(v->as.i);  // 数值塔归一（T016，02 §7：hash(1) == hash(1n) == hash(1.0)）
      return MS_HASH_OK;
    case MS_VAL_F64:
      *out = msHashNumericF64(v->as.f);
      return MS_HASH_OK;
    case MS_VAL_OBJ:
      switch (v->as.obj->type) {
        case MS_OBJ_BIGINT:  // T016：数值塔可 hash 键
          *out = msHashNumericBigint((const MsObjBigInt*)v->as.obj);
          return MS_HASH_OK;
        case MS_OBJ_STRING:
          *out = ((const MsObjString*)v->as.obj)->hash64;  // FNV-1a-64 创建即定
          return MS_HASH_OK;
        case MS_OBJ_TUPLE: {  // 惰性缓存（22 §4）：FNV-1a-64 复合元素 hash（序敏感）
          MsObjTuple* t = (MsObjTuple*)v->as.obj;
          if (t->hashValid) {
            *out = t->hash;
            return MS_HASH_OK;
          }
          ms_u64 h = 0xcbf29ce484222325ULL;
          for (int32_t i = 0; i < t->count; i++) {
            ms_u64 eh = 0;
            if (msValueHash(&t->items[i], &eh) != MS_HASH_OK) {
              return MS_HASH_UNHASHABLE;  // 元素不可 hash → 整体不可 hash（不缓存）
            }
            h = (h ^ eh) * 0x100000001b3ULL;
          }
          t->hash = h;
          t->hashValid = true;
          *out = h;
          return MS_HASH_OK;
        }
        default:
          return MS_HASH_UNHASHABLE;  // list/map/set/实例（function T018+）
      }
    default:
      return MS_HASH_UNHASHABLE;
  }
}

MsObjMap* msObjMapNew(MsVM* vm) {
  MsObjMap* m = (MsObjMap*)msObjAlloc(vm, MS_OBJ_MAP, sizeof(MsObjMap));
  if (m == NULL) {
    return NULL;
  }
  m->slots = NULL;  // 惰性建表；calloc 零值 MsValue 即 kind==MS_VAL_NIL（GC 首扫安全）
  m->cap = 0;
  m->liveCount = 0;
  m->usedCount = 0;
  m->seqHead = -1;
  m->seqTail = -1;
  return m;
}

// 探测（线性步进 1）：命中返回 active 槽下标（*outFound=true）；未命中 *outFound=false 且
// *outInsert 为首个墓穴（复用免 usedCount 虚增，无墓穴则 EMPTY 落点）；满表无 EMPTY → -1
// （负载门控下不可达，防御路径）
static ms_i64 mapProbe(const MsObjMap* m, ms_u64 hash, const MsValue* key, ms_i64* outInsert,
                       bool* outFound) {
  ms_i64 mask = m->cap - 1;
  ms_i64 idx = (ms_i64)(hash & (ms_u64)mask);
  ms_i64 insert = -1;  // 首个墓碑
  for (int32_t n = 0; n < m->cap; n++) {
    const MsMapSlot* s = &m->slots[idx];
    if (s->state == MS_MSS_EMPTY) {
      *outFound = false;
      *outInsert = insert >= 0 ? insert : idx;
      return -1;
    }
    if (s->state == MS_MSS_TOMBSTONE) {
      if (insert < 0) {
        insert = idx;
      }
    } else if (s->hash == hash && msValueEqual(key, &s->key)) {
      *outFound = true;
      return idx;
    }
    idx = (idx + 1) & mask;
  }
  *outFound = false;  // 全 active+墓碑（防御：ensureRoom 负载门控下不可达）
  *outInsert = -1;
  return -1;
}

// 再哈希：沿旧 seq 链序重探入新表并重建链（插入序保持；墓碑丢弃）
static bool mapRehash(MsObjMap* m, int32_t newCap) {
  ms_i64 oldHead = m->seqHead;
  MsMapSlot* old = m->slots;
  MsMapSlot* slots = calloc((size_t)newCap, sizeof(MsMapSlot));
  if (slots == NULL) {
    return false;
  }
  m->slots = slots;
  m->cap = newCap;
  m->liveCount = m->usedCount = 0;
  m->seqHead = m->seqTail = -1;
  for (ms_i64 cur = oldHead; cur >= 0; cur = old[cur].seqNext) {
    ms_i64 idx = (ms_i64)(old[cur].hash & (ms_u64)(newCap - 1));
    while (slots[idx].state == MS_MSS_ACTIVE) {
      idx = (idx + 1) & (newCap - 1);
    }
    slots[idx] = old[cur];
    slots[idx].seqPrev = m->seqTail;
    slots[idx].seqNext = -1;
    if (m->seqTail >= 0) {
      slots[m->seqTail].seqNext = idx;
    } else {
      m->seqHead = idx;
    }
    m->seqTail = idx;
    m->liveCount++;
    m->usedCount++;
  }
  free(old);
  return true;
}

// 扩容门控：负载 > 75%（usedCount+1 计）再哈希——密集（live 过半）倍容，否则原容净化墓碑
static bool mapEnsureRoom(MsVM* vm, MsObjMap* m) {
  (void)vm;  // 再哈希为 malloc 路径（非 GC 触发点；文件头 GC 纪律）
  if (m->cap == 0) {
    MsMapSlot* slots = calloc(MS_MAP_INIT_CAP, sizeof(MsMapSlot));
    if (slots == NULL) {
      return false;
    }
    m->slots = slots;
    m->cap = MS_MAP_INIT_CAP;
    return true;
  }
  if ((ms_i64)(m->usedCount + 1) * 4 > (ms_i64)m->cap * 3) {  // int64 防大容量乘法溢出
    int32_t newCap = (ms_i64)m->liveCount * 2 >= m->cap ? m->cap * 2 : m->cap;
    return mapRehash(m, newCap);
  }
  return true;
}

MsMapOp msObjMapPut(MsVM* vm, MsObjMap* m, const MsValue* key, const MsValue* value) {
  ms_u64 hash = 0;
  if (msValueHash(key, &hash) != MS_HASH_OK) {
    return MS_MAP_UNHASHABLE;
  }
  if (!mapEnsureRoom(vm, m)) {
    return MS_MAP_OOM;
  }
  for (;;) {
    ms_i64 insert = 0;
    bool found = false;
    ms_i64 hit = mapProbe(m, hash, key, &insert, &found);
    if (found) {
      m->slots[hit].value = *value;  // 改值不动序、不虚增 usedCount（22 §4）
      return MS_MAP_OK;
    }
    if (insert >= 0) {
      bool reuseTomb = m->slots[insert].state == MS_MSS_TOMBSTONE;
      MsMapSlot* s = &m->slots[insert];
      s->key = *key;
      s->value = *value;
      s->hash = hash;
      s->state = MS_MSS_ACTIVE;
      s->seqPrev = m->seqTail;
      s->seqNext = -1;
      if (m->seqTail >= 0) {
        m->slots[m->seqTail].seqNext = insert;
      } else {
        m->seqHead = insert;  // 首条目
      }
      m->seqTail = insert;
      m->liveCount++;
      if (!reuseTomb) {
        m->usedCount++;  // 墓碑复用：占位已计入
      }
      return MS_MAP_OK;
    }
    if (!mapRehash(m, m->cap * 2)) {  // 满表防御路径（负载门控下不可达）
      return MS_MAP_OOM;
    }
  }
}

MsMapOp msObjMapGet(const MsObjMap* m, const MsValue* key, MsValue* outValue) {
  ms_u64 hash = 0;
  if (msValueHash(key, &hash) != MS_HASH_OK) {
    return MS_MAP_UNHASHABLE;
  }
  if (m->cap == 0) {
    return MS_MAP_NOENT;
  }
  ms_i64 insert = 0;
  bool found = false;
  ms_i64 hit = mapProbe(m, hash, key, &insert, &found);
  if (!found) {
    return MS_MAP_NOENT;
  }
  if (outValue != NULL) {
    *outValue = m->slots[hit].value;
  }
  return MS_MAP_OK;
}

MsMapOp msObjMapRemove(MsObjMap* m, const MsValue* key, MsValue* outOld) {
  ms_u64 hash = 0;
  if (msValueHash(key, &hash) != MS_HASH_OK) {
    return MS_MAP_UNHASHABLE;
  }
  if (m->cap == 0) {
    return MS_MAP_NOENT;
  }
  ms_i64 insert = 0;
  bool found = false;
  ms_i64 hit = mapProbe(m, hash, key, &insert, &found);
  if (!found) {
    return MS_MAP_NOENT;
  }
  MsMapSlot* s = &m->slots[hit];
  if (outOld != NULL) {
    *outOld = s->value;
  }
  s->state = MS_MSS_TOMBSTONE;  // 键值载荷留存（探测跳过；mark 仅走 seq 链不误标）
  if (s->seqPrev >= 0) {
    m->slots[s->seqPrev].seqNext = s->seqNext;
  } else {
    m->seqHead = s->seqNext;
  }
  if (s->seqNext >= 0) {
    m->slots[s->seqNext].seqPrev = s->seqPrev;
  } else {
    m->seqTail = s->seqPrev;
  }
  m->liveCount--;  // usedCount 不减：墓碑仍占位（扩容依据）
  return MS_MAP_OK;
}

void msObjMapClear(MsObjMap* m) {
  if (m->slots != NULL) {
    memset(m->slots, 0, sizeof(MsMapSlot) * (size_t)m->cap);
  }
  m->liveCount = 0;
  m->usedCount = 0;
  m->seqHead = -1;
  m->seqTail = -1;
}

ms_i64 msObjMapFirst(const MsObjMap* m) {
  return m->seqHead;
}

ms_i64 msObjMapNext(const MsObjMap* m, ms_i64 slot) {
  MS_ASSERT(slot >= 0 && slot < (ms_i64)m->cap);
  return m->slots[slot].seqNext;
}

const MsValue* msObjMapKeyAt(const MsObjMap* m, ms_i64 slot) {
  MS_ASSERT(slot >= 0 && slot < (ms_i64)m->cap && m->slots[slot].state == MS_MSS_ACTIVE);
  return &m->slots[slot].key;
}

const MsValue* msObjMapValueAt(const MsObjMap* m, ms_i64 slot) {
  MS_ASSERT(slot >= 0 && slot < (ms_i64)m->cap && m->slots[slot].state == MS_MSS_ACTIVE);
  return &m->slots[slot].value;
}

// 仅走 seq 链（active 条目）：墓碑载荷为陈旧引用，误标会悬垂保活
void msObjMapMarkChildren(MsVM* vm, MsObj* obj) {
  MsObjMap* m = (MsObjMap*)obj;
  for (ms_i64 cur = m->seqHead; cur >= 0; cur = m->slots[cur].seqNext) {
    gcMarkValue(vm, m->slots[cur].key);
    gcMarkValue(vm, m->slots[cur].value);
  }
}

void msObjMapFreeData(MsObj* obj) {
  free(((MsObjMap*)obj)->slots);  // slots 可为 NULL（free(NULL) 无害）
}
