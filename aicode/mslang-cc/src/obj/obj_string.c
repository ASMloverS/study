// MsObjString 核心数据层（T013，22 §3）：一遍扫描定 hash/hash64/isAscii/charLen；FAM 尾随 runeIndex 保留区
#include "obj/obj_string.h"

#include <stdlib.h>
#include <string.h>

// runeIndex 保留区起始的 4 字节对齐填充（obj_string.h bytes[] 注释）；
// newString 分配与 msObjStringRuneIndex 惰性构建两处必须一致，抽公共函数防漂移
static size_t runeIndexPad(ms_u32 byteLen) {
  return (size_t)((4 - (byteLen & 3)) & 3);
}

// 新建不可变串：一遍扫描 bytes[0..len) 同时算 FNV-1a-32/64、isAscii、charLen（坏序列按 1 字符计）
static MsObjString* newString(MsVM* vm, const char* bytes, int32_t len) {
  if (len < 0) {
    return NULL;  // 长度非法视同失败（防 size_t 回绕溢出）
  }
  // FNV-1a 常数与 common.c 的 msFnv1a32/msFnv1a64（common.h 声明）保持同步：两处独立展开，改须同改
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
  size_t pad = isAscii ? 0 : runeIndexPad((ms_u32)len);  // runeIndex 尾区 4 字节对齐填充
  size_t runeArea = isAscii ? 0 : ((size_t)charLen + 1) * sizeof(int32_t);
  MsObjString* s = (MsObjString*)msObjAlloc(vm, MS_OBJ_STRING,
                                            sizeof(MsObjString) + (size_t)len + pad + runeArea);
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

// ---- rune 索引与规范切片（T013 Task 3，22 §3；表结构见 obj_string.h runeIndex 注释） ----

const int32_t* msObjStringRuneIndex(MsVM* vm, MsObjString* s) {
  (void)vm;  // 就地填充对象内保留区，无需分配；vm 参数为 API 对称保留
  if (s->isAscii) {
    return NULL;  // ASCII 快路径：字节偏移即 rune 索引，永不建表
  }
  if (s->runeIndex != NULL) {
    return s->runeIndex;
  }
  int32_t* ri = (int32_t*)(void*)(s->bytes + s->byteLen + runeIndexPad(s->byteLen));
  int32_t off = 0;
  ms_u32 n = 0;
  while (off < (int32_t)s->byteLen) {
    ri[n] = off;  // 每 rune 起始字节偏移
    int32_t width = 0;
    (void)msUtf8Decode(s->bytes + off, (int32_t)s->byteLen - off, &width);  // 坏序列 width=1
    off += width;
    n++;
  }
  ri[n] = (int32_t)s->byteLen;  // 末哨兵；n == charLen（与 newString 同一扫描口径）
  s->runeIndex = ri;
  return ri;
}

bool msObjStringRuneAt(MsVM* vm, MsObjString* s, ms_i64 i, ms_u32* out) {
  if (i < 0) {
    i += (ms_i64)s->charLen;  // 负索引自尾部起
  }
  if (i < 0 || i >= (ms_i64)s->charLen) {
    return false;
  }
  if (s->isAscii) {
    *out = (ms_u8)s->bytes[i];  // 快路径：无表直查
    return true;
  }
  const int32_t* ri = msObjStringRuneIndex(vm, s);
  int32_t off = ri[i];
  if ((ms_u8)s->bytes[off] < 0x80) {
    *out = (ms_u8)s->bytes[off];
    return true;
  }
  int32_t width = 0;
  *out = msUtf8Decode(s->bytes + off, (int32_t)s->byteLen - off, &width);
  return true;
}

int32_t msObjStringRuneAtByte(const MsObjString* s, int32_t byteOffset) {
  if (s->isAscii) {
    return byteOffset;  // 字节偏移即 rune 索引
  }
  const int32_t* ri = s->runeIndex;  // const 签名：只读既有表；无表则线性扫描（不触发惰性构建）
  if (ri != NULL) {
    // 二分取 ≤ byteOffset 的最大边界下标（ri[0]=0 恒成立；ri[charLen]=byteLen 兜底越尾）
    int32_t lo = 0;
    int32_t hi = (int32_t)s->charLen;
    while (lo < hi) {
      int32_t mid = lo + (hi - lo + 1) / 2;  // 上取中防死循环
      if (ri[mid] <= byteOffset) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }
    return lo;
  }
  int32_t idx = 0;
  int32_t off = 0;
  while (off < byteOffset) {
    int32_t width = 0;
    (void)msUtf8Decode(s->bytes + off, (int32_t)s->byteLen - off, &width);
    off += width;
    idx++;
  }
  return off > byteOffset ? idx - 1 : idx;  // 非边界取最近前驱边界
}

// Python 切片端点规范化（D7）：负值 +len 折算后按步进方向钳位
static ms_i64 clampPos(ms_i64 x, ms_i64 len) {  // step>0：钳到 [0, len]
  if (x < 0) {
    x += len;
  }
  return x < 0 ? 0 : (x > len ? len : x);
}

static ms_i64 clampNeg(ms_i64 x, ms_i64 len) {  // step<0：钳到 [-1, len-1]
  if (x < 0) {
    x += len;
  }
  return x < -1 ? -1 : (x > len - 1 ? len - 1 : x);
}

bool msSliceRange(ms_i64 len, bool hasA, ms_i64 a, bool hasB, ms_i64 b, bool hasC, ms_i64 c,
                  ms_i64* startOut, ms_i64* stopOut, ms_i64* stepOut) {
  ms_i64 step = hasC ? c : 1;
  if (step == 0) {
    return false;  // 调用方报 ValueError（T014 同口径）
  }
  // 步幅幅值钳到 len：start/stop ∈ [-1,len] 下 |step| > len 至多迭代一次，钳后观测等价，
  // 且防切片拷贝循环 i += step 有符号溢出（UB）；len==0 时循环零迭代无溢出风险，
  // 且钳位会产生 step==0 违反"true ⇒ step!=0"契约，故不钳
  if (len > 0) {
    if (step > len) {
      step = len;
    } else if (step < -len) {
      step = -len;
    }
  }
  if (step > 0) {
    *startOut = hasA ? clampPos(a, len) : 0;
    *stopOut = hasB ? clampPos(b, len) : len;
  } else {
    *startOut = hasA ? clampNeg(a, len) : len - 1;
    *stopOut = hasB ? clampNeg(b, len) : -1;
  }
  *stepOut = step;
  return true;
}

MsObjString* msObjStringSlice(MsVM* vm, MsObjString* s, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                              bool hasC, ms_i64 c, bool* errStepZero) {
  *errStepZero = false;
  ms_i64 start, stop, step;
  if (!msSliceRange((ms_i64)s->charLen, hasA, a, hasB, b, hasC, c, &start, &stop, &step)) {
    *errStepZero = true;  // 规范化仅因 step==0 失败
    return NULL;
  }
  char* buf = malloc((size_t)s->byteLen + 1);  // 结果为字节子集 ≤ byteLen；+1 免 malloc(0)
  if (buf == NULL) {
    return NULL;
  }
  int32_t outLen = 0;
  if (s->isAscii) {
    for (ms_i64 i = start; step > 0 ? i < stop : i > stop; i += step) {
      buf[outLen++] = s->bytes[i];
    }
  } else {
    const int32_t* ri = msObjStringRuneIndex(vm, s);
    for (ms_i64 i = start; step > 0 ? i < stop : i > stop; i += step) {
      int32_t w = ri[i + 1] - ri[i];  // rune 字节段 [ri[i], ri[i+1])
      memcpy(buf + outLen, s->bytes + ri[i], (size_t)w);
      outLen += w;
    }
  }
  MsObjString* r = msObjStringNew(vm, buf, outLen);
  free(buf);
  return r;
}

// ---- string 迭代器（Task 12，D8：MS_OBJ_ITERATOR tag，T017 泛化壳） ----

MsObjStrIter* msObjStrIterNew(MsVM* vm, MsObjString* s, bool byteMode) {
  MsObjStrIter* it = (MsObjStrIter*)msObjAlloc(vm, MS_OBJ_ITERATOR, sizeof(MsObjStrIter));
  if (it == NULL) {
    return NULL;
  }
  it->str = s;  // 指针直存（非 MsValue）：分配期间接收者由调用方 root（GET_ITER peek / CALL_METHOD 栈上）
  it->pos = 0;
  it->byteMode = byteMode;
  return it;
}

bool msStrIterNext(MsVM* vm, MsObjStrIter* it, MsValue* out) {
  (void)vm;  // 无分配：pos 前进出码点/字节
  if (it->pos >= it->str->byteLen) {
    return false;
  }
  if (it->byteMode) {
    *out = msValueInt((ms_u8)it->str->bytes[it->pos++]);
    return true;
  }
  int32_t w = 0;
  ms_u32 cp = msUtf8Decode(it->str->bytes + it->pos, (int32_t)it->str->byteLen - (int32_t)it->pos, &w);
  it->pos += (ms_u32)w;  // 坏序列 w=1 前进一字节（common.h 契约）
  *out = msValueInt((ms_i64)cp);
  return true;
}
