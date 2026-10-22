// intern 表（T013，22 §4）：开放寻址线性探测；命中复用既有指针，miss 建 PERMANENT 对象入表
#include "obj/obj_string.h"

#include <stdlib.h>
#include <string.h>

#include "common/common.h"

#define INTERN_INIT_CAP 256  // 初始容量（2^n；负载 > 75% 倍容再哈希）

void msInternInit(MsInternTable* t) {
  t->slots = NULL;  // 惰性建表：首 intern 时 calloc（未用 VM 零开销）
  t->cap = 0;
  t->count = 0;
}

void msInternFree(MsInternTable* t) {
  free(t->slots);  // 仅槽位数组；对象归堆 gcHeapFree 统一释放
  msInternInit(t);
}

// 探测落点：空槽或与 (hash, len, bytes) 全等的首个槽（线性步进 1）
static size_t internProbe(const MsInternTable* t, ms_u32 hash, const char* bytes, int32_t len) {
  MS_ASSERT(t->slots != NULL);
  size_t mask = t->cap - 1;
  size_t idx = hash & mask;
  while (t->slots[idx] != NULL) {
    MsObjString* s = t->slots[idx];
    if (s->hash == hash && s->byteLen == (ms_u32)len &&
        memcmp(s->bytes, bytes, (size_t)len) == 0) {
      break;
    }
    idx = (idx + 1) & mask;
  }
  return idx;
}

static void internInsert(MsInternTable* t, MsObjString* s) {
  size_t idx = internProbe(t, s->hash, s->bytes, (int32_t)s->byteLen);
  MS_ASSERT(t->slots[idx] == NULL);  // 调用方保证 miss 后插入；命中即去重已破
  t->slots[idx] = s;
  t->count++;
}

// 倍容再哈希：分配新槽位数组后重新探入全部既有对象（对象本体不动）
static void internGrow(MsInternTable* t) {
  MsObjString** oldSlots = t->slots;
  size_t oldCap = t->cap;
  t->cap = oldCap * 2;
  t->slots = calloc(t->cap, sizeof(*t->slots));
  MS_ASSERT(t->slots != NULL);
  t->count = 0;
  for (size_t i = 0; i < oldCap; i++) {
    if (oldSlots[i] != NULL) {
      internInsert(t, oldSlots[i]);
    }
  }
  free(oldSlots);
}

MsObjString* msInternBytes(MsVM* vm, const char* bytes, int32_t len) {
  if (len < 0) {
    return NULL;  // 与 msObjStringNew 同口径（防 size_t 回绕）
  }
  MsInternTable* t = vm->intern;
  if (t->slots == NULL) {
    t->slots = calloc(INTERN_INIT_CAP, sizeof(*t->slots));
    MS_ASSERT(t->slots != NULL);
    t->cap = INTERN_INIT_CAP;
  }
  // msFnv1a32 与 newString 内联 FNV-1a-32 同步（obj_string.c 头注）：先查后建，命中路径零对象分配
  ms_u32 hash = msFnv1a32(bytes, (ms_u64)len);
  MsObjString* hit = t->slots[internProbe(t, hash, bytes, len)];
  if (hit != NULL) {
    return hit;
  }
  // 分配窗口安全：msObjStringNew 内 guard 收集先于对象 malloc（obj.c msObjAlloc）；
  // New 返回至入表间无 vm 分配点，grow 的 calloc 亦非 GC 触发点——置 PERMANENT 后入表即闭合窗口
  MsObjString* s = msObjStringNew(vm, bytes, len);
  if (s == NULL) {
    return NULL;
  }
  s->head.flags |= MS_OBJ_PERMANENT;
  internInsert(t, s);
  if (t->count * 4 > t->cap * 3) {  // 负载 > 75%
    internGrow(t);
  }
  return s;
}
