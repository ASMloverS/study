// MsObjString 核心数据层（T013，22 §3）：一遍扫描定 hash/hash64/isAscii/charLen；FAM 尾随 runeIndex 保留区
#include "obj/obj_string.h"

#include <stdlib.h>
#include <string.h>

// 新建不可变串：一遍扫描 bytes[0..len) 同时算 FNV-1a-32/64、isAscii、charLen（坏序列按 1 字符计）
static MsObjString* newString(MsVM* vm, const char* bytes, int32_t len) {
  if (len < 0) {
    return NULL;  // 长度非法视同失败（防 size_t 回绕溢出）
  }
  ms_u32 hash = 0x811C9DC5u;              // FNV-1a-32 offset basis / prime 见下
  ms_u64 hash64 = 0xCBF29CE484222325ull;  // FNV-1a-64 offset basis
  bool isAscii = true;
  ms_u32 charLen = 0;
  int32_t i = 0;
  while (i < len) {
    ms_u8 b = (ms_u8)bytes[i];
    hash ^= b;
    hash *= 0x01000193u;
    hash64 ^= b;
    hash64 *= 0x100000001B3ull;
    if (b < 0x80) {
      i++;
    } else {
      isAscii = false;
      int32_t width = 0;
      (void)msUtf8Decode(bytes + i, len - i, &width);  // 坏序列 width=1（common.h 契约）
      i += width;
    }
    charLen++;
  }
  size_t runeArea = isAscii ? 0 : ((size_t)charLen + 1) * sizeof(int32_t);
  MsObjString* s =
      (MsObjString*)msObjAlloc(vm, MS_OBJ_STRING, sizeof(MsObjString) + (size_t)len + runeArea);
  if (s == NULL) {
    return NULL;
  }
  s->hash = hash;
  s->hash64 = hash64;
  s->byteLen = (ms_u32)len;
  s->charLen = charLen;  // ASCII 时 = len
  s->isAscii = isAscii;
  s->runeIndex = NULL;  // 保留区不初始化：惰性构建时填（D1）
  memcpy(s->bytes, bytes, (size_t)len);
  return s;
}

MsObjString* msObjStringNew(MsVM* vm, const char* bytes, int32_t len) {
  return newString(vm, bytes, len);
}

MsObjString* msObjStringConcat(MsVM* vm, const MsObjString* a, const char* bytesB, int32_t lenB) {
  if (lenB < 0 || (ms_i64)a->byteLen + (ms_i64)lenB > INT32_MAX) {
    return NULL;
  }
  int32_t total = (int32_t)((ms_i64)a->byteLen + (ms_i64)lenB);
  char* buf = malloc((size_t)total + 1);  // +1：免 malloc(0) 平台差异
  if (buf == NULL) {
    return NULL;
  }
  memcpy(buf, a->bytes, a->byteLen);
  memcpy(buf + a->byteLen, bytesB, (size_t)lenB);
  MsObjString* s = newString(vm, buf, total);
  free(buf);
  return s;
}

MsObjString* msObjStringRepeat(MsVM* vm, const MsObjString* a, ms_i64 n) {
  if (n <= 0) {
    return newString(vm, "", 0);
  }
  // 除法式上限检查：等价 (i64)byteLen * n <= INT32_MAX 且免疫 i64 乘法溢出
  if (a->byteLen > 0 && n > (ms_i64)(INT32_MAX / (int32_t)a->byteLen)) {
    return NULL;
  }
  int32_t total = (int32_t)((ms_i64)a->byteLen * n);
  char* buf = malloc((size_t)total + 1);
  if (buf == NULL) {
    return NULL;
  }
  for (ms_i64 off = 0; off < (ms_i64)total; off += a->byteLen) {
    memcpy(buf + off, a->bytes, a->byteLen);
  }
  MsObjString* s = newString(vm, buf, total);
  free(buf);
  return s;
}

MsObjString* msObjStringFromEscaped(MsVM* vm, const char* src, int32_t len) {
  if (len < 0) {
    return NULL;
  }
  char* buf = malloc((size_t)len + 1);  // 解码只收缩；+1 容 NUL 终止符（计入解码器容量）
  if (buf == NULL) {
    return NULL;
  }
  int32_t decoded = msTextDecodeEscapes(src, len, buf, len + 1);
  MsObjString* s = decoded < 0 ? NULL : newString(vm, buf, decoded);
  free(buf);
  return s;
}

bool msObjStringEqual(const MsObjString* a, const MsObjString* b) {
  return a == b || (a->hash == b->hash && a->byteLen == b->byteLen &&
                    memcmp(a->bytes, b->bytes, (size_t)a->byteLen) == 0);
}

int32_t msObjStringCompare(const MsObjString* a, const MsObjString* b) {
  ms_u32 minLen = a->byteLen < b->byteLen ? a->byteLen : b->byteLen;
  int c = memcmp(a->bytes, b->bytes, (size_t)minLen);
  if (c != 0) {
    return c < 0 ? -1 : 1;
  }
  if (a->byteLen == b->byteLen) {
    return 0;
  }
  return a->byteLen < b->byteLen ? -1 : 1;
}
