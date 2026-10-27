// MsObjBigInt 数据面（T016，22 §5）：FAM 单块分配；解析/打印/比较/数值塔 hash（运算见文件后半）
// GC 纪律：所有产对象路径先在 malloc 临时区算好、末端单次 msObjAlloc——分配窗口内借用输入
// 可能被回收，故分配后不再读输入 limbs（vm.c BINARY_* 弹栈契约，21 §3.2）
#include "obj/obj_bigint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_string.h"

// —— 规范化：剥离高位零 limb ——
static int32_t normLen(const uint32_t* limbs, int32_t len) {
  while (len > 0 && limbs[len - 1] == 0) {
    len--;
  }
  return len;
}

MsObjBigInt* msObjBigintNew(MsVM* vm, const uint32_t* limbs, int32_t len, int sign) {
  len = limbs != NULL ? normLen(limbs, len) : 0;
  if (len == 0) {
    sign = 0;
  }
  if (len < 0 || (size_t)len > (SIZE_MAX - sizeof(MsObjBigInt)) / sizeof(uint32_t)) {
    return NULL;
  }
  MsObjBigInt* b =
      (MsObjBigInt*)msObjAlloc(vm, MS_OBJ_BIGINT, sizeof(MsObjBigInt) + (size_t)len * sizeof(uint32_t));
  if (b == NULL) {
    return NULL;
  }
  b->len = len;
  b->sign = (int8_t)sign;
  if (len > 0) {
    memcpy(b->limbs, limbs, (size_t)len * sizeof(uint32_t));
  }
  return b;
}

MsObjBigInt* msObjBigintFromI64(MsVM* vm, ms_i64 v) {
  MsBigView view;
  uint32_t buf[2];
  msBigViewI64(&view, buf, v);
  return msObjBigintNew(vm, view.limbs, view.len, view.sign);
}

static int digitVal(uint8_t c, int base) {
  int d = -1;
  if (c >= '0' && c <= '9') {
    d = c - '0';
  } else if (c >= 'a' && c <= 'z') {
    d = c - 'a' + 10;
  } else if (c >= 'A' && c <= 'Z') {
    d = c - 'A' + 10;
  }
  return d >= 0 && d < base ? d : -1;
}

// 解析（02 §3.2 字面量 / 10 §1 转换共用）：逐位 mag = mag·base + d（O(n²/字宽)，转换路径足够）
MsBigErr msObjBigintParse(MsVM* vm, const char* text, int32_t len, int base, MsObjBigInt** out) {
  *out = NULL;
  int32_t i = 0;
  int sign = 1;
  if (i < len && (text[i] == '+' || text[i] == '-')) {
    sign = text[i] == '-' ? -1 : 1;
    i++;
  }
  if (base == 0) {  // 前缀自动识别
    if (i + 1 < len && text[i] == '0') {
      uint8_t p = (uint8_t)text[i + 1];
      if (p == 'x' || p == 'X') {
        base = 16;
        i += 2;
      } else if (p == 'o' || p == 'O') {
        base = 8;
        i += 2;
      } else if (p == 'b' || p == 'B') {
        base = 2;
        i += 2;
      } else {
        base = 10;
      }
    } else {
      base = 10;
    }
  } else if (i + 1 < len && text[i] == '0') {  // 显式 base：匹配前缀亦跳过（int("0xff", 16) 同 Python）
    uint8_t p = (uint8_t)(text[i + 1] | 0x20);
    if ((base == 16 && p == 'x') || (base == 8 && p == 'o') || (base == 2 && p == 'b')) {
      i += 2;
    }
  }
  uint32_t* buf = NULL;
  int32_t cap = 0;
  int32_t ln = 0;
  bool sawDigit = false;
  for (; i < len; i++) {
    uint8_t c = (uint8_t)text[i];
    if (c == '_') {
      continue;  // 分组下划线宽容跳过（词法层错位已在扫描器诊断）
    }
    int d = digitVal(c, base);
    if (d < 0) {
      free(buf);
      return MS_BIG_PARSE;
    }
    sawDigit = true;
    if (ln == cap) {
      int32_t nc = cap == 0 ? 4 : cap * 2;
      uint32_t* nb = (uint32_t*)realloc(buf, (size_t)nc * sizeof(uint32_t));
      if (nb == NULL) {
        free(buf);
        return MS_BIG_OOM;
      }
      buf = nb;
      cap = nc;
    }
    uint64_t carry = (uint64_t)d;  // mag = mag·base + d 进位链
    for (int32_t j = 0; j < ln; j++) {
      uint64_t t = (uint64_t)buf[j] * (uint64_t)(uint32_t)base + carry;
      buf[j] = (uint32_t)t;
      carry = t >> 32;
    }
    if (carry != 0) {
      buf[ln++] = (uint32_t)carry;
    }
  }
  if (!sawDigit) {
    free(buf);
    return MS_BIG_PARSE;
  }
  MsObjBigInt* b = msObjBigintNew(vm, buf, ln, ln == 0 ? 0 : sign);
  free(buf);
  if (b == NULL) {
    return MS_BIG_OOM;
  }
  *out = b;
  return MS_BIG_OK;
}

int64_t msObjBigintBitLen(const MsObjBigInt* b) {
  if (b->len == 0) {
    return 0;
  }
  uint32_t top = b->limbs[b->len - 1];
  int64_t bits = (int64_t)(b->len - 1) * 32;
  while (top != 0) {
    top >>= 1;
    bits++;
  }
  return bits;
}

// 幅值 ÷ 小除数 d（u32）：商就地；返回余数
static uint32_t magDivSmall(uint32_t* limbs, int32_t len, uint32_t d) {
  uint64_t rem = 0;
  for (int32_t i = len - 1; i >= 0; i--) {
    uint64_t cur = (rem << 32) | limbs[i];
    limbs[i] = (uint32_t)(cur / d);
    rem = cur % d;
  }
  return (uint32_t)rem;
}

static const char kHexDigits[] = "0123456789abcdef";

// 字符串化：base10 逐段除 10^9；2/8/16 按位抽取。反序生成后反转（b 只读，均在末端分配前完成）
MsObjString* msObjBigintToString(MsVM* vm, const MsObjBigInt* b, int base) {
  if (base != 2 && base != 8 && base != 10 && base != 16) {
    return NULL;
  }
  if (b->len == 0) {
    return msObjStringNew(vm, "0", 1);
  }
  int32_t bits = (int32_t)msObjBigintBitLen(b);
  int32_t perChar = base == 2 ? 1 : (base == 8 ? 3 : (base == 16 ? 4 : 0));
  // 十进制位上界 ⌈bits·log10(2)⌉+2（10/33 > log10 2）；幂二进制按位长折算；另含符号
  int32_t cap = base == 10 ? (int32_t)(((ms_i64)bits * 10) / 33 + 3) : bits / perChar + 3;
  char* buf = (char*)malloc((size_t)cap);
  uint32_t* tmp = NULL;
  if (base == 10) {
    tmp = (uint32_t*)malloc((size_t)b->len * sizeof(uint32_t));
    if (tmp != NULL) {
      memcpy(tmp, b->limbs, (size_t)b->len * sizeof(uint32_t));
    }
  }
  if (buf == NULL || (base == 10 && tmp == NULL)) {
    free(buf);
    free(tmp);
    return NULL;
  }
  int32_t n = 0;
  if (b->sign < 0) {
    buf[n++] = '-';
  }
  if (base == 10) {
    int32_t len = b->len;
    while (len > 0) {
      uint32_t rem = magDivSmall(tmp, len, 1000000000u);
      while (len > 0 && tmp[len - 1] == 0) {
        len--;
      }
      int32_t emit = 9;  // 非末段零填充 9 位；末段按余数实际位数
      if (len == 0) {
        emit = 0;
        uint32_t r = rem;
        while (r != 0) {
          r /= 10;
          emit++;
        }
        emit = emit == 0 ? 1 : emit;
      }
      for (int32_t k = 0; k < emit; k++) {
        buf[n++] = (char)('0' + rem % 10);
        rem /= 10;
      }
    }
  } else {
    int32_t totalChars = (bits + perChar - 1) / perChar;
    for (int32_t ci = 0; ci < totalChars; ci++) {
      int32_t bitOff = ci * perChar;  // 低位字符组先行（生成序 = 反转序）
      uint32_t v = 0;
      for (int32_t k = 0; k < perChar; k++) {
        int32_t bit = bitOff + k;
        if (bit < bits && ((b->limbs[bit / 32] >> (bit % 32)) & 1u) != 0) {
          v |= 1u << k;
        }
      }
      buf[n++] = kHexDigits[v];
    }
  }
  int32_t lo = b->sign < 0 ? 1 : 0;  // 反转数字区（符号位定址）
  for (int32_t x = lo, y = n - 1; x < y; x++, y--) {
    char t = buf[x];
    buf[x] = buf[y];
    buf[y] = t;
  }
  MsObjString* s = msObjStringNew(vm, buf, n);
  free(buf);
  free(tmp);
  return s;
}

// —— 比较（无分配） ——
static int magCmp(const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen) {
  if (alen != blen) {
    return alen < blen ? -1 : 1;
  }
  for (int32_t i = alen - 1; i >= 0; i--) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

int msObjBigintCompare(const MsObjBigInt* a, const MsObjBigInt* b) {
  if (a->sign != b->sign) {
    return a->sign < b->sign ? -1 : 1;
  }
  int m = magCmp(a->limbs, a->len, b->limbs, b->len);
  return a->sign < 0 ? -m : m;
}

int msObjBigintCmpI64(const MsObjBigInt* b, ms_i64 v) {
  int vs = v < 0 ? -1 : (v > 0 ? 1 : 0);
  if (b->sign != vs) {
    return b->sign < vs ? -1 : 1;
  }
  uint32_t vlimbs[2];
  MsBigView view;
  msBigViewI64(&view, vlimbs, v);
  int m = magCmp(b->limbs, b->len, view.limbs, view.len);
  return b->sign < 0 ? -m : m;
}

// 提取 b 自 bitOff（≥ 0）起的 nbits（≤ 64）位（值域外补零）
static uint64_t bigBitsAt(const MsObjBigInt* b, int64_t bitOff, int32_t nbits) {
  uint64_t v = 0;
  for (int32_t k = 0; k < nbits; k++) {
    int64_t bit = bitOff + k;
    if (bit < (int64_t)b->len * 32 && ((b->limbs[bit / 32] >> (bit % 32)) & 1u) != 0) {
      v |= 1ULL << k;
    }
  }
  return v;
}

static bool magLowBitsNonzero(const MsObjBigInt* b, int64_t upto) {
  int64_t full = upto / 32;
  for (int64_t i = 0; i < full && i < b->len; i++) {
    if (b->limbs[i] != 0) {
      return true;
    }
  }
  int32_t rem = (int32_t)(upto % 32);
  return rem > 0 && full < b->len && (b->limbs[full] & ((1u << rem) - 1)) != 0;
}

static int magCmpU64(const MsObjBigInt* b, uint64_t u) {
  uint32_t ul[2] = {(uint32_t)u, (uint32_t)(u >> 32)};
  int32_t ulen = ul[1] != 0 ? 2 : (ul[0] != 0 ? 1 : 0);
  return magCmp(b->limbs, b->len, ul, ulen);
}

// 幅值 vs 正有限 double：f ≡ mant·2^k 精确分解（frexp；尾数去零归约）。整值比位长+窗口，
// 非整比 floor 后判小数部分——全程精确无舍入（02 §6 数值塔精确化）
static int magCmpF64(const MsObjBigInt* b, double f) {
  int e2 = 0;
  double frac = frexp(f, &e2);  // f = frac·2^e2，frac ∈ [0.5, 1)
  int64_t mant = (int64_t)(frac * 9007199254740992.0);
  int64_t k = (int64_t)e2 - 53;
  while (k < 0 && (mant & 1) == 0) {
    mant >>= 1;
    k++;
  }
  if (k < 0) {  // 非整（mant 已奇）：先比 floor，相等则 |b| < |f|
    int c = magCmpU64(b, (uint64_t)(mant >> (-k)));
    return c != 0 ? c : -1;
  }
  int64_t bBits = msObjBigintBitLen(b);
  int64_t mantBits = 0;
  for (int64_t m = mant; m != 0; m >>= 1) {
    mantBits++;
  }
  if (bBits != mantBits + k) {
    return bBits < mantBits + k ? -1 : 1;
  }
  uint64_t win = bigBitsAt(b, k, (int32_t)mantBits);  // 位长同：比 [k, k+mantBits) 窗口
  if (win != (uint64_t)mant) {
    return win < (uint64_t)mant ? -1 : 1;
  }
  return magLowBitsNonzero(b, k) ? 1 : 0;  // 同窗口：低 k 位非零者大
}

int msObjBigintCmpF64(const MsObjBigInt* b, double v) {
  if (isnan(v)) {
    return 2;  // 无序哨兵（调用方判 2 → 不可比较/不等）
  }
  if (v == 0.0) {
    return b->sign;
  }
  int vs = v < 0 ? -1 : 1;
  if (b->sign != vs) {
    return b->sign < vs ? -1 : 1;
  }
  int m = magCmpF64(b, fabs(v));
  return b->sign > 0 ? m : -m;
}

bool msObjBigintFitsI64(const MsObjBigInt* b, ms_i64* out) {
  if (b->len == 0) {
    if (out != NULL) {
      *out = 0;
    }
    return true;
  }
  if (b->len > 2) {
    return false;
  }
  uint64_t m = b->limbs[0];
  if (b->len == 2) {
    m |= (uint64_t)b->limbs[1] << 32;
    if (m > (uint64_t)INT64_MAX + 1u) {
      return false;
    }
    if (m == (uint64_t)INT64_MAX + 1u) {  // 2^63：仅负号可容（INT64_MIN）
      if (b->sign >= 0) {
        return false;
      }
      if (out != NULL) {
        *out = INT64_MIN;
      }
      return true;
    }
  }
  if (out != NULL) {
    *out = b->sign < 0 ? -(ms_i64)m : (ms_i64)m;
  }
  return true;
}

double msObjBigintToF64(const MsObjBigInt* b) {
  double v = 0.0;
  for (int32_t i = b->len - 1; i >= 0; i--) {
    v = v * 4294967296.0 + (double)b->limbs[i];
  }
  return b->sign < 0 ? -v : v;
}

// —— 数值塔 hash（02 §7）：整值按 mod (2^61-1) 归一（Python 同型），跨 int/bigint/float 同值同
// hash；非整 float 按精确有理数 num·2^k 的模逆合成；NaN/±inf 用固定位型（相等门控在 msValueEqual）
#define MS_HASH_P 2305843009213693951ULL  // 2^61 - 1（梅森素数）

static ms_u64 hashMix64(ms_u64 x) {  // splitmix64 finalizer（与 obj_map.c 同型：终态散列）
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

static ms_u64 modAddP(ms_u64 a, ms_u64 b) {  // a,b < P → 和 < 2P → 至多减一次
  ms_u64 s = a + b;
  return s >= MS_HASH_P ? s - MS_HASH_P : s;
}

static ms_u64 modMul2p32(ms_u64 a) {  // a·2^32 mod P（a < P）：拆半避免 u64 溢出
  ms_u64 hi = a >> 30;
  ms_u64 lo = a & ((1ULL << 30) - 1);
  return (hi * 2 + (lo << 32)) % MS_HASH_P;  // < 2^62 + 2^32 < 2^63
}

static ms_u64 modMulP(ms_u64 a, ms_u64 b) {  // 农夫乘法（逐位倍加，无 128 位依赖）
  ms_u64 r = 0;
  while (b != 0) {
    if ((b & 1) != 0) {
      r = modAddP(r, a);
    }
    a = modAddP(a, a);
    b >>= 1;
  }
  return r;
}

static ms_u64 modPow2(ms_i64 e) {  // 2^e mod P；e 可负（费马小定理：2^(P-1+e)，|e| < P-1）
  ms_u64 exp = e >= 0 ? (ms_u64)e : (ms_u64)(MS_HASH_P - 1 + e);
  ms_u64 r = 1;
  ms_u64 base = 2;
  while (exp != 0) {
    if ((exp & 1) != 0) {
      r = modMulP(r, base);
    }
    base = modMulP(base, base);
    exp >>= 1;
  }
  return r;
}

static ms_u64 hashSigned(ms_u64 magMod, int sign) {  // 有符号归一：负 → P - r（r ≠ 0）
  if (sign >= 0) {
    return hashMix64(magMod);
  }
  return hashMix64(magMod == 0 ? 0 : MS_HASH_P - magMod);
}

ms_u64 msHashNumericI64(ms_i64 v) {
  ms_u64 m = v < 0 ? (ms_u64)(-(v + 1)) + 1 : (ms_u64)v;
  ms_u64 r = 0;
  for (int i = 1; i >= 0; i--) {  // 高 limb → 低：r = r·2^32 + limb
    r = modAddP(modMul2p32(r), (ms_u64)((m >> (32 * i)) & 0xFFFFFFFFu));
  }
  return hashSigned(r, v < 0 ? -1 : (v > 0 ? 1 : 0));
}

ms_u64 msHashNumericBigint(const MsObjBigInt* b) {
  ms_u64 r = 0;
  for (int32_t i = b->len - 1; i >= 0; i--) {
    r = modAddP(modMul2p32(r), b->limbs[i]);
  }
  return hashSigned(r, b->sign);
}

// f 分解 mant·2^k（mant 奇）：hash ≡ hash(mant)·2^k（k < 0 时即模逆——与有理值同构，Python 同型）
ms_u64 msHashNumericF64(double f) {
  if (isnan(f)) {
    return hashMix64(0x9E3779B97F4A7C15ULL);  // 自一致位型（NaN ≠ NaN，探测恒 miss）
  }
  if (isinf(f)) {
    return hashMix64(f < 0 ? 0xFFFF000000000000ULL : 0x7FFF000000000000ULL);
  }
  if (f == 0.0) {
    return hashMix64(0);  // ±0 同 0（-0.0 == 0 数值塔）
  }
  int sign = f < 0 ? -1 : 1;
  int e2 = 0;
  double frac = frexp(fabs(f), &e2);
  int64_t mant = (int64_t)(frac * 9007199254740992.0);
  int64_t k = (int64_t)e2 - 53;
  while ((mant & 1) == 0) {  // 53 位含隐含 1 → mant 恒非零
    mant >>= 1;
    k++;
  }
  ms_u64 rm = 0;
  for (int i = 1; i >= 0; i--) {
    rm = modAddP(modMul2p32(rm), (ms_u64)((uint64_t)mant >> (32 * i)) & 0xFFFFFFFFu);
  }
  return hashSigned(modMulP(rm, modPow2(k)), sign);
}
