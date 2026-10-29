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
  if (base != 0 && (base < 2 || base > 36)) {  // base 域外前置拒绝（Task 4 内置转换亦校验，入口须自洽）
    return MS_BIG_PARSE;
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
    uint64_t fl = -k >= 64 ? 0u : (uint64_t)(mant >> -k);  // mant < 2^53 → -k ≥ 64 时 floor 恒 0（移位数 UB 钳制）
    int c = magCmpU64(b, fl);
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
  if (isinf(v)) {  // ±inf：任何有限 bigint 恒小/大于 inf（frexp(inf) UB，须先行短路）
    return v > 0 ? -1 : 1;
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

// ---- 运算（T016，22 §5）：malloc 临时区计算 → 末端单次分配（文件头 GC 纪律） ----

static MsBigErr bigFromMag(MsVM* vm, const uint32_t* limbs, int32_t len, int sign, MsObjBigInt** out) {
  MsObjBigInt* b = msObjBigintNew(vm, limbs, len, sign);  // limbs 为 malloc/栈缓冲（非 GC 域）
  if (b == NULL) {
    return MS_BIG_OOM;
  }
  *out = b;
  return MS_BIG_OK;
}

// 视图整体拷入 malloc（对象内部 limbs 先离 GC 域再入分配窗口）
static MsBigErr bigFromViewCopy(MsVM* vm, const MsBigView* v, MsObjBigInt** out) {
  if (v->len == 0) {
    return bigFromMag(vm, NULL, 0, 0, out);
  }
  uint32_t* tmp = (uint32_t*)malloc((size_t)v->len * sizeof(uint32_t));
  if (tmp == NULL) {
    return MS_BIG_OOM;
  }
  memcpy(tmp, v->limbs, (size_t)v->len * sizeof(uint32_t));
  MsBigErr e = bigFromMag(vm, tmp, v->len, v->sign, out);
  free(tmp);
  return e;
}

// 幅值加：r = a + b（alen ≥ blen；r 容量 alen+1；*outLen 规范前长度）
static void magAdd(uint32_t* r, const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen,
                   int32_t* outLen) {
  uint64_t carry = 0;
  int32_t i = 0;
  for (; i < blen; i++) {
    uint64_t t = (uint64_t)a[i] + b[i] + carry;
    r[i] = (uint32_t)t;
    carry = t >> 32;
  }
  for (; i < alen; i++) {
    uint64_t t = (uint64_t)a[i] + carry;
    r[i] = (uint32_t)t;
    carry = t >> 32;
  }
  r[i] = (uint32_t)carry;
  *outLen = carry != 0 ? i + 1 : i;
}

// 幅值减：r = a - b（a ≥ b；rlen = alen，调用方规范化）
static void magSub(uint32_t* r, const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen) {
  uint64_t borrow = 0;
  for (int32_t i = 0; i < alen; i++) {
    uint64_t bi = i < blen ? b[i] : 0;
    uint64_t cur = (uint64_t)a[i] - bi - borrow;  // u64 下溢回绕即借位
    r[i] = (uint32_t)cur;
    borrow = (cur >> 32) != 0 ? 1 : 0;
  }
}

static void magAddAny(uint32_t* r, const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen,
                      int32_t* outLen) {  // 任意长度序
  if (alen >= blen) {
    magAdd(r, a, alen, b, blen, outLen);
  } else {
    magAdd(r, b, blen, a, alen, outLen);
  }
}

// 有符号加减统一：同号幅值加、异号大幅减小幅（符号随大者）
static MsBigErr bigSignedAdd(MsVM* vm, const MsBigView* a, const MsBigView* b, bool sub, MsObjBigInt** out) {
  int8_t bs = sub ? (int8_t)-b->sign : b->sign;
  if (a->len == 0) {
    MsBigView v = {b->limbs, b->len, bs};
    return bigFromViewCopy(vm, &v, out);
  }
  if (b->len == 0) {
    return bigFromViewCopy(vm, a, out);
  }
  int32_t maxLen = a->len > b->len ? a->len : b->len;
  uint32_t* r = (uint32_t*)malloc((size_t)(maxLen + 1) * sizeof(uint32_t));
  if (r == NULL) {
    return MS_BIG_OOM;
  }
  int sign;
  int32_t rlen = 0;
  if (a->sign == bs) {
    magAddAny(r, a->limbs, a->len, b->limbs, b->len, &rlen);
    sign = a->sign;
  } else {
    int c = magCmp(a->limbs, a->len, b->limbs, b->len);
    if (c == 0) {
      free(r);
      return bigFromMag(vm, NULL, 0, 0, out);
    }
    if (c > 0) {
      magSub(r, a->limbs, a->len, b->limbs, b->len);
      rlen = a->len;
      sign = a->sign;
    } else {
      magSub(r, b->limbs, b->len, a->limbs, a->len);
      rlen = b->len;
      sign = bs;
    }
  }
  MsBigErr e = bigFromMag(vm, r, rlen, sign, out);
  free(r);
  return e;
}

MsBigErr msBigAdd(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  return bigSignedAdd(vm, a, b, false, out);
}

MsBigErr msBigSub(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  return bigSignedAdd(vm, a, b, true, out);
}

MsBigErr msBigNeg(MsVM* vm, const MsBigView* a, MsObjBigInt** out) {
  MsBigView v = *a;
  v.sign = (int8_t)-a->sign;
  return bigFromViewCopy(vm, &v, out);
}

// ~a = -(a+1)：正 → 幅值+1 取负；负 → 幅值-1 取正
MsBigErr msBigNot(MsVM* vm, const MsBigView* a, MsObjBigInt** out) {
  if (a->len == 0) {  // ~0 = -1
    uint32_t one = 1;
    return bigFromMag(vm, &one, 1, -1, out);
  }
  uint32_t* tmp = (uint32_t*)malloc((size_t)(a->len + 1) * sizeof(uint32_t));
  if (tmp == NULL) {
    return MS_BIG_OOM;
  }
  memcpy(tmp, a->limbs, (size_t)a->len * sizeof(uint32_t));
  int32_t len = a->len;
  int sign;
  if (a->sign > 0) {
    uint64_t carry = 1;
    for (int32_t i = 0; i < len && carry != 0; i++) {
      uint64_t t = (uint64_t)tmp[i] + carry;
      tmp[i] = (uint32_t)t;
      carry = t >> 32;
    }
    if (carry != 0) {
      tmp[len++] = (uint32_t)carry;
    }
    sign = -1;
  } else {
    int32_t i = 0;  // 幅值-1（幅值 ≥ 1 → 无跨位借穿）
    while (i < len && tmp[i] == 0) {
      tmp[i] = 0xFFFFFFFFu;
      i++;
    }
    tmp[i] -= 1;
    sign = 1;
  }
  MsBigErr e = bigFromMag(vm, tmp, len, sign, out);
  free(tmp);
  return e;
}

// —— 乘法：朴素 O(n·m) + Karatsuba（位宽 > 4096 bit = 128 limb，22 §5） ——

#define MS_BIG_KARATSUBA_LIMBS 128

// 朴素：r 容量 alen+blen，先清零
static void magMulNaive(uint32_t* r, const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen) {
  memset(r, 0, (size_t)(alen + blen) * sizeof(uint32_t));
  for (int32_t i = 0; i < alen; i++) {
    uint64_t carry = 0;
    uint64_t ai = a[i];
    for (int32_t j = 0; j < blen; j++) {
      uint64_t t = ai * b[j] + r[i + j] + carry;  // ≤ (2^32-1)² + 2(2^32-1) < 2^64
      r[i + j] = (uint32_t)t;
      carry = t >> 32;
    }
    int32_t k = i + blen;
    while (carry != 0) {
      uint64_t t = (uint64_t)r[k] + carry;
      r[k] = (uint32_t)t;
      carry = t >> 32;
      k++;
    }
  }
}

static int64_t bitLenView(const uint32_t* limbs, int32_t len) {
  len = normLen(limbs, len);
  if (len == 0) {
    return 0;
  }
  uint32_t top = limbs[len - 1];
  int64_t bits = (int64_t)(len - 1) * 32;
  while (top != 0) {
    top >>= 1;
    bits++;
  }
  return bits;
}

// Karatsuba（幅值；r 容量 alen+blen）：仅两侧均 ≥ 阈值且大致均衡时递归（a 长 b 短落朴素——
// 正确性优先，非均衡优化归 T044）。scratch 供本层 sa/sb/z0/z2/z1 + 子递归复用
static bool magMulRec(uint32_t* r, const uint32_t* a, int32_t alen, const uint32_t* b, int32_t blen,
                      uint32_t* scratch, int32_t scratchCap) {
  if (alen < blen) {
    const uint32_t* tp = a;
    a = b;
    b = tp;
    int32_t tl = alen;
    alen = blen;
    blen = tl;
  }
  int32_t m = alen / 2;
  if (blen <= m || blen < MS_BIG_KARATSUBA_LIMBS) {  // 短侧无有效高位半段或阈下 → 朴素
    magMulNaive(r, a, alen, b, blen);
    return true;
  }
  const uint32_t* a0 = a;
  int32_t a0len = normLen(a0, m);
  const uint32_t* a1 = a + m;
  int32_t a1len = normLen(a1, alen - m);
  const uint32_t* b0 = b;
  int32_t b0len = normLen(b0, blen < m ? blen : m);
  const uint32_t* b1 = b + (blen < m ? blen : m);
  int32_t b1len = blen - (blen < m ? blen : m);
  // 槽位按精确上界分配（计划勘误）：固定 zLen/2m+2 槽宽在 alen 奇或 blen 近 alen 时不足——
  // z2 乘积（a1len+b1len 可达 2m+2）溢出位落入 z1 槽，被后算的 z1 乘积覆盖，后续
  // normLen/magSub 即读回脏值；z1 槽（saLen+sbLen 可达 2m+4）同理压 subScratch
  int32_t zLen = 2 * m;
  int32_t saCap = (a0len > a1len ? a0len : a1len) + 1;
  int32_t sbCap = (b0len > b1len ? b0len : b1len) + 1;
  int32_t z2Len = a1len + b1len;
  int32_t z1Cap = saCap + sbCap;
  int32_t need = saCap + sbCap + zLen + z2Len + z1Cap;
  if (scratchCap < need) {
    return false;
  }
  uint32_t* sa = scratch;
  uint32_t* sb = scratch + saCap;
  uint32_t* z0 = sb + sbCap;
  uint32_t* z2 = z0 + zLen;
  uint32_t* z1 = z2 + z2Len;
  uint32_t* subScratch = z1 + z1Cap;
  int32_t subCap = scratchCap - (int32_t)(subScratch - scratch);
  int32_t saLen = 0;
  int32_t sbLen = 0;
  int32_t z0Len = 0;
  int32_t z1Len = 0;
  magAddAny(sa, a0, a0len, a1, a1len, &saLen);
  magAddAny(sb, b0, b0len, b1, b1len, &sbLen);
  // z0 槽全零化：a0len+b0len < zLen（低半窗含零 limb）时子递归只写前缀——不清零则
  // memcpy(z0, zLen) 会把未初始化 scratch 带进结果（计划勘误）
  memset(z0, 0, (size_t)zLen * sizeof(uint32_t));
  if (!magMulRec(z0, a0, a0len, b0, b0len, subScratch, subCap) ||
      !magMulRec(z2, a1, a1len, b1, b1len, subScratch, subCap) ||
      !magMulRec(z1, sa, saLen, sb, sbLen, subScratch, subCap)) {
    return false;
  }
  z0Len = a0len + b0len;  // 调用方依赖 normLen 收敛；乘积长上界即两因子长和
  z1Len = saLen + sbLen;
  // z1 = sa·sb - z0 - z2（sa·sb ≥ z0 + z2 恒成立，顺序借位安全）
  magSub(z1, z1, z1Len, z0, normLen(z0, z0Len));
  magSub(z1, z1, z1Len, z2, normLen(z2, z2Len));
  // r = z2·B^2m + z1·B^m + z0
  memcpy(r, z0, (size_t)zLen * sizeof(uint32_t));
  int32_t rlen = alen + blen;
  memset(r + zLen, 0, (size_t)(rlen - zLen) * sizeof(uint32_t));
  int32_t z2n = normLen(z2, z2Len);
  memcpy(r + 2 * m, z2, (size_t)z2n * sizeof(uint32_t));
  int32_t z1n = normLen(z1, z1Len);
  uint64_t carry = 0;
  int32_t i = 0;
  for (; i < z1n; i++) {  // 叠加 z1·B^m（重叠带进位加）
    uint64_t t = (uint64_t)r[m + i] + z1[i] + carry;
    r[m + i] = (uint32_t)t;
    carry = t >> 32;
  }
  for (int32_t k = m + z1n; carry != 0 && k < rlen; k++) {
    uint64_t t = (uint64_t)r[k] + carry;
    r[k] = (uint32_t)t;
    carry = t >> 32;
  }
  return true;
}

MsBigErr msBigMul(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  if (a->len == 0 || b->len == 0) {
    return bigFromMag(vm, NULL, 0, 0, out);
  }
  if ((bitLenView(a->limbs, a->len) + bitLenView(b->limbs, b->len)) > MS_BIGINT_MAX_BITS) {
    return MS_BIG_RANGE;
  }
  int32_t rlen = a->len + b->len;
  uint32_t* r = (uint32_t*)calloc((size_t)rlen, sizeof(uint32_t));
  int32_t maxLen = a->len > b->len ? a->len : b->len;
  int32_t scratchCap = 8 * (maxLen + 4);  // 各层几何递减（m/2 链），8× 顶层层长充足
  uint32_t* scratch = (uint32_t*)malloc((size_t)scratchCap * sizeof(uint32_t));
  if (r == NULL || scratch == NULL) {
    free(r);
    free(scratch);
    return MS_BIG_OOM;
  }
  if (!magMulRec(r, a->limbs, a->len, b->limbs, b->len, scratch, scratchCap)) {
    free(r);
    free(scratch);
    return MS_BIG_OOM;
  }
  free(scratch);
  MsBigErr e = bigFromMag(vm, r, rlen, a->sign * b->sign, out);
  free(r);
  return e;
}

// —— 除法/取模：Knuth 算法 D（Hacker's Delight divmnu64 同型，32 位 limb；02 §3.5 截断语义） ——

static int clz32(uint32_t x) {
  int n = 0;
  if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
  if (x <= 0x00FFFFFFu) { n += 8; x <<= 8; }
  if (x <= 0x0FFFFFFFu) { n += 4; x <<= 4; }
  if (x <= 0x3FFFFFFFu) { n += 2; x <<= 2; }
  if (x <= 0x7FFFFFFFu) { n += 1; }
  return n;
}

// 幅值除内核：u（就地改写为规格化余数）÷ v → q；ulen ≥ vlen ≥ 1，v 顶层非零。
// 返回 false 仅当内部暂存分配失败（OOM）
static bool magDivKernel(uint32_t* q, uint32_t* u, int32_t ulen, const uint32_t* v, int32_t vlen,
                         int32_t* outULen) {
  int32_t n = vlen;
  int32_t m = ulen - vlen;
  if (m < 0) {  // 防御（调用方门控）
    *outULen = ulen;
    return true;
  }
  if (n == 1) {  // 单 limb 除数：估计式会读 vn[n-2]/un[j+n-2] 越界——逐位 64/32 精除（divmnu n==1 同型）
    uint64_t rem = 0;
    for (int32_t j = ulen - 1; j >= 0; j--) {
      uint64_t cur = (rem << 32) | u[j];
      q[j] = (uint32_t)(cur / v[0]);
      rem = cur % v[0];
    }
    u[0] = (uint32_t)rem;
    *outULen = rem != 0 ? 1 : 0;
    return true;
  }
  int s = clz32(v[n - 1]);
  uint32_t* un = (uint32_t*)calloc((size_t)ulen + 1, sizeof(uint32_t));
  uint32_t* vn = un != NULL ? (uint32_t*)calloc((size_t)n, sizeof(uint32_t)) : NULL;
  if (un == NULL || vn == NULL) {
    free(un);
    free(vn);
    return false;
  }
  if (s > 0) {  // 左移规格化：v 顶位置 1
    for (int32_t i = n - 1; i > 0; i--) {
      vn[i] = (v[i] << s) | (v[i - 1] >> (32 - s));
    }
    vn[0] = v[0] << s;
    un[ulen] = u[ulen - 1] >> (32 - s);
    for (int32_t i = ulen - 1; i > 0; i--) {
      un[i] = (u[i] << s) | (u[i - 1] >> (32 - s));
    }
    un[0] = u[0] << s;
  } else {
    memcpy(vn, v, (size_t)n * sizeof(uint32_t));
    memcpy(un, u, (size_t)ulen * sizeof(uint32_t));
  }
  for (int32_t j = m; j >= 0; j--) {
    uint64_t num = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
    uint64_t qhat = num / vn[n - 1];
    uint64_t rhat = num % vn[n - 1];
    while (qhat >= (1ULL << 32) ||
           qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {  // 短路先判 qhat 域再乘（无溢出）
      qhat--;
      rhat += vn[n - 1];
      if (rhat >= (1ULL << 32)) {
        break;
      }
    }
    int64_t borrow = 0;  // 乘减
    uint64_t carry = 0;
    for (int32_t i = 0; i < n; i++) {
      uint64_t p = qhat * vn[i] + carry;
      carry = p >> 32;
      int64_t t = (int64_t)un[i + j] - (int64_t)(uint32_t)p - borrow;
      un[i + j] = (uint32_t)t;
      borrow = t < 0 ? 1 : 0;
    }
    int64_t t = (int64_t)un[j + n] - (int64_t)carry - borrow;
    un[j + n] = (uint32_t)t;
    if (t < 0) {  // qhat 过估：回加一个 v
      qhat--;
      uint64_t c = 0;
      for (int32_t i = 0; i < n; i++) {
        uint64_t tt = (uint64_t)un[i + j] + vn[i] + c;
        un[i + j] = (uint32_t)tt;
        c = tt >> 32;
      }
      un[j + n] = (uint32_t)((uint64_t)un[j + n] + c);
    }
    q[j] = (uint32_t)qhat;
  }
  int32_t rlen = ulen;  // 余数去规格化（右移 s 回写 u）
  if (s > 0) {
    for (int32_t i = 0; i < ulen; i++) {
      u[i] = (un[i] >> s) | (un[i + 1] << (32 - s));
    }
  } else {
    memcpy(u, un, (size_t)ulen * sizeof(uint32_t));
  }
  free(un);
  free(vn);
  *outULen = rlen;
  return true;
}

// 幅值除/模完整包装：quot 非 NULL 时产商、rem 非 NULL 时产余（按需单次分配）
static MsBigErr magDivModFull(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** quot,
                              MsObjBigInt** rem) {
  int qs = a->len == 0 ? 0 : a->sign * b->sign;  // 截断商符号
  int rs = a->sign;                              // 余符号随被除数（02 §3.5）
  if (a->len == 0) {  // 0 / x = 0，0 % x = 0
    MsBigErr e = MS_BIG_OK;
    if (quot != NULL) {
      e = bigFromMag(vm, NULL, 0, 0, quot);
    }
    if (rem != NULL && e == MS_BIG_OK) {
      e = bigFromMag(vm, NULL, 0, 0, rem);
    }
    return e;
  }
  if (magCmp(a->limbs, a->len, b->limbs, b->len) < 0) {  // |a| < |b|：商 0、余 a
    if (quot != NULL) {
      MsBigErr e = bigFromMag(vm, NULL, 0, 0, quot);
      if (e != MS_BIG_OK) {
        return e;
      }
    }
    return rem != NULL ? bigFromViewCopy(vm, a, rem) : MS_BIG_OK;  // rem 重新签名 rs（= a->sign，同源）
  }
  int32_t qlen = a->len - b->len + 1;
  uint32_t* q = (uint32_t*)calloc((size_t)qlen, sizeof(uint32_t));
  uint32_t* u = (uint32_t*)malloc((size_t)a->len * sizeof(uint32_t));
  if (q == NULL || u == NULL) {
    free(q);
    free(u);
    return MS_BIG_OOM;
  }
  memcpy(u, a->limbs, (size_t)a->len * sizeof(uint32_t));
  int32_t ulen = a->len;
  if (!magDivKernel(q, u, ulen, b->limbs, b->len, &ulen)) {  // 内核 OOM 透传
    free(q);
    free(u);
    return MS_BIG_OOM;
  }
  MsBigErr e = MS_BIG_OK;
  if (quot != NULL) {
    e = bigFromMag(vm, q, qlen, qs, quot);  // 此后 a/b 视图弃用（文件头纪律）
  }
  if (e == MS_BIG_OK && rem != NULL) {
    e = bigFromMag(vm, u, ulen, rs, rem);
  }
  free(q);
  free(u);
  return e;
}

MsBigErr msBigDiv(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  if (b->len == 0) {
    return MS_BIG_DIVZERO;
  }
  return magDivModFull(vm, a, b, out, NULL);
}

MsBigErr msBigMod(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  if (b->len == 0) {
    return MS_BIG_DIVZERO;
  }
  return magDivModFull(vm, a, b, NULL, out);
}

// —— 幂（平方乘；结果位长护栏前置判定） ——

static MsBigErr mulObjects(MsVM* vm, const MsObjBigInt* a, const MsObjBigInt* b, MsObjBigInt** out) {
  MsBigView va, vb;
  msBigViewOf(&va, a);
  msBigViewOf(&vb, b);
  return msBigMul(vm, &va, &vb, out);
}

// exp ≥ 0；负指数由调用方转浮点幂（vm.c，与 int 路径一致）
MsBigErr msBigPow(MsVM* vm, const MsBigView* a, ms_i64 exp, MsObjBigInt** out) {
  if (exp < 0) {
    return MS_BIG_RANGE;  // 防御（调用方门控）
  }
  int64_t aBits = bitLenView(a->limbs, a->len);
  if (aBits > 1 && exp > (MS_BIGINT_MAX_BITS - 1) / (aBits - 1)) {  // 位长 ≈ exp·(aBits-1)+1
    return MS_BIG_RANGE;
  }
  int32_t mark = vm->stackTop;  // 中间对象经 VM 栈 root（D6 纪律；弹净自压部分）
  MsObjBigInt* r = msObjBigintFromI64(vm, 1);
  if (r == NULL) {
    return MS_BIG_OOM;
  }
  if (!msVmPush(vm, msValueObj(&r->head))) {
    vm->stackTop = mark;
    return MS_BIG_OOM;  // StackOverflow 槽已置（vm.c bigErrSet 不覆盖）
  }
  MsObjBigInt* base = NULL;
  MsBigErr e = bigFromViewCopy(vm, a, &base);  // base 先离源（a 视图分配窗口即弃）
  if (e != MS_BIG_OK) {
    vm->stackTop = mark;
    return e;
  }
  if (!msVmPush(vm, msValueObj(&base->head))) {
    vm->stackTop = mark;
    return MS_BIG_OOM;
  }
  ms_i64 k = exp;
  while (k > 0 && e == MS_BIG_OK) {
    if ((k & 1) != 0) {
      e = mulObjects(vm, r, base, &r);
      if (e == MS_BIG_OK) {
        vm->stack[mark] = msValueObj(&r->head);
      }
    }
    k >>= 1;
    if (k > 0 && e == MS_BIG_OK) {
      e = mulObjects(vm, base, base, &base);
      if (e == MS_BIG_OK) {
        vm->stack[mark + 1] = msValueObj(&base->head);
      }
    }
  }
  vm->stackTop = mark;
  if (e != MS_BIG_OK) {
    return e;
  }
  *out = r;
  return MS_BIG_OK;
}

// —— 位运算：无限符号扩展补码（Python 语义；extLen = max+1 冗余位规范化剥除） ——

static void twosNegate(uint32_t* x, int32_t n) {  // x = 2^(32n) - x（补码取负）
  uint64_t carry = 1;
  for (int32_t i = 0; i < n; i++) {
    uint64_t t = (uint64_t)(uint32_t)~x[i] + carry;
    x[i] = (uint32_t)t;
    carry = t >> 32;
  }
}

MsBigErr msBigBitOp(MsVM* vm, MsBigBitOp op, const MsBigView* a, const MsBigView* b, MsObjBigInt** out) {
  int32_t n = (a->len > b->len ? a->len : b->len) + 1;
  uint32_t* ta = (uint32_t*)calloc((size_t)n, sizeof(uint32_t));
  uint32_t* tb = (uint32_t*)calloc((size_t)n, sizeof(uint32_t));
  uint32_t* tr = (uint32_t*)malloc((size_t)n * sizeof(uint32_t));
  if (ta == NULL || tb == NULL || tr == NULL) {
    free(ta);
    free(tb);
    free(tr);
    return MS_BIG_OOM;
  }
  memcpy(ta, a->limbs, (size_t)a->len * sizeof(uint32_t));
  memcpy(tb, b->limbs, (size_t)b->len * sizeof(uint32_t));
  if (a->sign < 0) {
    twosNegate(ta, n);
  }
  if (b->sign < 0) {
    twosNegate(tb, n);
  }
  for (int32_t i = 0; i < n; i++) {
    tr[i] = op == MS_BIG_BIT_AND ? (ta[i] & tb[i]) : (op == MS_BIG_BIT_OR ? (ta[i] | tb[i]) : (ta[i] ^ tb[i]));
  }
  int sign = ((tr[n - 1] >> 31) & 1) != 0 ? -1 : 1;
  if (sign < 0) {
    twosNegate(tr, n);  // 幅值还原
  }
  MsBigErr e = bigFromMag(vm, tr, n, sign, out);
  free(ta);
  free(tb);
  free(tr);
  return e;
}

// —— 移位：SHL 符号保持；SHR 负数向下取整（floor(-x/2^n) = -((x+2^n-1)>>n)） ——

MsBigErr msBigShl(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out) {
  if (n < 0) {
    return MS_BIG_RANGE;  // 防御（调用方门控 ValueError）
  }
  if (a->len == 0) {
    return bigFromMag(vm, NULL, 0, 0, out);
  }
  if (n > MS_BIGINT_MAX_BITS ||  // 短路：n 近 INT64_MAX 时求和会带符号溢出（UB）绕过护栏
      bitLenView(a->limbs, a->len) + n > MS_BIGINT_MAX_BITS) {
    return MS_BIG_RANGE;
  }
  int32_t limbOff = (int32_t)(n >> 5);
  int32_t bitOff = (int32_t)(n & 31);
  int32_t len = a->len + limbOff + 1;
  uint32_t* r = (uint32_t*)calloc((size_t)len, sizeof(uint32_t));
  if (r == NULL) {
    return MS_BIG_OOM;
  }
  for (int32_t i = 0; i < a->len; i++) {
    uint64_t v = (uint64_t)a->limbs[i] << bitOff;
    r[i + limbOff] |= (uint32_t)v;
    r[i + limbOff + 1] |= (uint32_t)(v >> 32);
  }
  MsBigErr e = bigFromMag(vm, r, len, a->sign, out);
  free(r);
  return e;
}

// 输入幅值低 n 位是否非零（floor 修正判定）
static bool viewLowBitsNonzero(const MsBigView* a, int64_t n) {
  int64_t full = n >> 5;
  for (int64_t i = 0; i < full && i < a->len; i++) {
    if (a->limbs[i] != 0) {
      return true;
    }
  }
  int32_t rem = (int32_t)(n & 31);
  return rem > 0 && full < a->len && (a->limbs[full] & ((1u << rem) - 1)) != 0;
}

MsBigErr msBigShr(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out) {
  if (n < 0) {
    return MS_BIG_RANGE;
  }
  if (a->len == 0) {
    return bigFromMag(vm, NULL, 0, 0, out);
  }
  int64_t limbOff = n >> 5;
  if (limbOff >= a->len) {  // 全移出：正 → 0；负 → -1（floor 语义）
    if (a->sign < 0) {
      uint32_t one = 1;
      return bigFromMag(vm, &one, 1, -1, out);
    }
    return bigFromMag(vm, NULL, 0, 0, out);
  }
  int32_t bitOff = (int32_t)(n & 31);
  int32_t len = a->len - (int32_t)limbOff;
  uint32_t* r = (uint32_t*)calloc((size_t)len, sizeof(uint32_t));
  if (r == NULL) {
    return MS_BIG_OOM;
  }
  for (int32_t i = 0; i < len; i++) {  // 幅值右移
    uint32_t lo = a->limbs[i + (int32_t)limbOff] >> bitOff;
    uint32_t hi = bitOff != 0 && i + (int32_t)limbOff + 1 < a->len ? a->limbs[i + (int32_t)limbOff + 1]
                                                                    << (32 - bitOff)
                                                                    : 0;
    r[i] = lo | hi;
  }
  int sign = a->sign;
  if (sign < 0 && viewLowBitsNonzero(a, n)) {  // 负数 floor 修正：低 n 位非零则幅值 +1
    uint64_t carry = 1;
    for (int32_t i = 0; i < len && carry != 0; i++) {
      uint64_t t = (uint64_t)r[i] + carry;
      r[i] = (uint32_t)t;
      carry = t >> 32;
    }
  }
  MsBigErr e = bigFromMag(vm, r, len, sign, out);
  free(r);
  return e;
}
