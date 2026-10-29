# T016 bigint 与数值塔 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 MsObjBigInt（uint32 limb + 符号）、朴素/Karatsuba 四则、int64 溢出自动提升、混算规则表（02 §3.4）、Go 除模语义、位运算域、数值塔 hash 归一与转换内置，满足 T016 DoD。

**Architecture:** 新增 `src/obj/obj_bigint.c/.h`（FAM 单块分配，运算先落 malloc 临时区、末端单次 `msObjAlloc`——分配窗口内不触碰输入视图）；`vm.c` 的 `arithBinary` 数值路径统一为 int/int 快路径 + bigint 域 + float 域三支；数值塔相等/序/hash 精确化（`value.c` + `obj_map.c` 回填 `msHashNumeric*`）。溢出检测三件套抽到 `src/plat/plat_math.h`。

**Tech Stack:** C11（MSVC 19.38 / cl，`/W4 /WX`）、CMake preset `msvc`、ctest。

**Worktree:** `C:\Workspace\Repositories\study\aicode\mslang-cc\.worktrees\t016-bigint\aicode\mslang-cc`（分支 `feature/t016-bigint`）。所有路径相对此根。基线：116 tests 全绿。

**规范（必读）:** `docs/language/28-style-c.md`（命名/格式/`//` 注释/禁 Tab）、`docs/language/11-style-ms.md`（fixtures）。所有新文件 UTF-8 无 BOM、LF、无行尾空白。构建唯一入口 `cmake --build build --config Debug` + `ctest --test-dir build -C Debug`。

**与任务文档的偏差（实现前确认，均有依据）:**

1. `src/gc/gc.c` **不修改**：bigint 采用 FAM 单块分配（同 tuple/string 模式），limbs 内联无子分配，mark/freeFn 均不需注册。
2. 扫描器 `MSE_INT_OVERFLOW` **停止发射**（枚举保留并改注释）：字面量溢出升 bigint 现在是精确合法行为，不再诊断；"精确解析"落在 `msValueFromConst` 的 CONST_BIGINT 文本→对象（parser 已剥 `n` 后缀，token 只携带文本）。
3. `1 << 64` 纯 int 移位由 Go-0 语义改为**精确提升 bigint**（数值塔溢出提升哲学一致；无既有测试断言旧行为）。
4. NaN 有序比较由"cmp=0 当相等"改为 **TypeError**（02 §6 仅定义 NaN≠NaN；旧行为是缺陷）。
5. i64↔float 相等/比较**精确化**（不再经 double 舍入；修复 T015 精度注记 ≥2^53 域）。
6. `int()`/`int("...")` 超出 i64 时**返回 bigint**（与字面量自动升格同策略，不报错）。
7. MSVC 无 `_add_overflow_i64` 内建（任务文档 §6 记载有误）：三件套用 `__builtin_*_overflow`（GCC/Clang）+ 手写 UB 安全检查（MSVC）。
8. bigint 字面量常量**每次 LOAD_CONST 现解析**（不做常量池缓存/根化；性能门归 T044）。

**任务索引:** Task 1 bigint 核心（表示/解析/打印/hash/比较）→ Task 2 运算（四则/Karatsuba/Knuth-D/位运算）→ Task 3 VM 与值系统接线 → Task 4 内置函数与方法 → Task 5 ms fixtures → Task 6 文档状态与全量验证。

---

### Task 1: MsObjBigInt 核心表示 / 解析 / 打印 / hash / 比较

**Files:**
- Create: `src/obj/obj_bigint.h`
- Create: `src/obj/obj_bigint.c`
- Modify: `CMakeLists.txt`（ms_obj 加源文件）
- Create: `tests/unit/test_obj_bigint.c`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1.1: 写 `src/obj/obj_bigint.h`（完整内容）**

```c
// MsObjBigInt（T016，22 §5）：小端 uint32 limb + 符号；FAM 单块分配（同 tuple 模式，无子分配）
#ifndef INCLUDE_MS_OBJ_OBJ_BIGINT_H_
#define INCLUDE_MS_OBJ_OBJ_BIGINT_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjBigInt {
  MsObj head;
  int32_t len;       // limb 数（规范化：高位零 limb 已剥离；0 ⇔ 值为 0）
  int8_t sign;       // -1 / 0 / +1；len == 0 时必为 0
  uint32_t limbs[];  // 小端序（limbs[0] 最低位）
} MsObjBigInt;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 运算错误码（调用方映射错误类：OOM→RuntimeError、PARSE→ValueError、DIVZERO→ArithmeticError、
// RANGE→OverflowError；docs/tasks/T016 §2）
typedef enum MsBigErr {
  MS_BIG_OK = 0,
  MS_BIG_OOM,
  MS_BIG_PARSE,
  MS_BIG_DIVZERO,
  MS_BIG_RANGE,
} MsBigErr;

#define MS_BIGINT_MAX_BITS (1LL << 24)  // 结果位长护栏（16M bit ≈ 8MB limbs；基准调优归 T044）

// 位运算种类（msBigBitOp）
typedef enum MsBigBitOp {
  MS_BIG_BIT_AND,
  MS_BIG_BIT_OR,
  MS_BIG_BIT_XOR,
} MsBigBitOp;

// 借用幅值视图：limbs 指向对象内部或调用方栈缓冲（i64 经 msBigViewI64 就地构造）
typedef struct MsBigView {
  const uint32_t* limbs;
  int32_t len;
  int8_t sign;
} MsBigView;

static inline void msBigViewI64(MsBigView* v, uint32_t buf[2], ms_i64 x) {
  ms_u64 m = x < 0 ? (ms_u64)(-(x + 1)) + 1 : (ms_u64)x;
  buf[0] = (uint32_t)m;
  buf[1] = (uint32_t)(m >> 32);
  v->limbs = buf;
  v->len = buf[1] != 0 ? 2 : (buf[0] != 0 ? 1 : 0);
  v->sign = x < 0 ? (int8_t)-1 : (x > 0 ? (int8_t)1 : (int8_t)0);
}

static inline void msBigViewOf(MsBigView* v, const MsObjBigInt* b) {
  v->limbs = b->limbs;
  v->len = b->len;
  v->sign = b->sign;
}

// —— 构造 ——
// limbs 须存活至分配完成（栈缓冲/malloc/常量文本安全；未 root 的 GC 对象内部存储须先经
// malloc 暂存——本文件运算函数均遵守，外部直接调用者注意）
MsObjBigInt* msObjBigintNew(MsVM* vm, const uint32_t* limbs, int32_t len, int sign);
MsObjBigInt* msObjBigintFromI64(MsVM* vm, ms_i64 v);
// 文本解析：base 0 = 0x/0o/0b 前缀自动识别（默认十进制）；显式 base 时匹配前缀亦跳过；
// 允许前导 +/- 与 '_' 分组；空数字/非法字符 → MS_BIG_PARSE
MsBigErr msObjBigintParse(MsVM* vm, const char* text, int32_t len, int base, MsObjBigInt** out);
// 字符串化（base 2/8/10/16；负数带 '-'；非法 base/OOM → NULL）
MsObjString* msObjBigintToString(MsVM* vm, const MsObjBigInt* b, int base);

// —— 只读比较 / 转换（无分配） ——
int msObjBigintCompare(const MsObjBigInt* a, const MsObjBigInt* b);  // 全序 -1/0/1
int msObjBigintCmpI64(const MsObjBigInt* b, ms_i64 v);
int msObjBigintCmpF64(const MsObjBigInt* b, double v);  // NaN → 2（无序哨兵）
bool msObjBigintFitsI64(const MsObjBigInt* b, ms_i64* out);
double msObjBigintToF64(const MsObjBigInt* b);  // 允许精度损失（02 §3.3）
int64_t msObjBigintBitLen(const MsObjBigInt* b);  // 值幅位长（0 → 0）

// —— 运算（输入为借用视图；结果单块分配。GC 纪律：运算先落 malloc 临时区、末端单次分配，
//     分配窗口后不再读输入视图 limbs（vm.c BINARY_* 弹栈契约，21 §3.2） ——
MsBigErr msBigAdd(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigSub(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigMul(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // Karatsuba > 4096 bit
MsBigErr msBigDiv(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // 截断除法（02 §3.5）
MsBigErr msBigMod(MsVM* vm, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);  // 余符号随被除数
MsBigErr msBigPow(MsVM* vm, const MsBigView* a, ms_i64 exp, MsObjBigInt** out);          // exp ≥ 0
MsBigErr msBigNeg(MsVM* vm, const MsBigView* a, MsObjBigInt** out);
MsBigErr msBigNot(MsVM* vm, const MsBigView* a, MsObjBigInt** out);  // ~a = -(a+1)
MsBigErr msBigBitOp(MsVM* vm, MsBigBitOp op, const MsBigView* a, const MsBigView* b, MsObjBigInt** out);
MsBigErr msBigShl(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out);  // 符号保持
MsBigErr msBigShr(MsVM* vm, const MsBigView* a, ms_i64 n, MsObjBigInt** out);  // 负数向下取整

// —— 数值塔 hash 归一（02 §7：hash(1) == hash(1n) == hash(1.0)；obj_map.c 回填） ——
ms_u64 msHashNumericI64(ms_i64 v);
ms_u64 msHashNumericF64(double f);
ms_u64 msHashNumericBigint(const MsObjBigInt* b);

static inline bool msIsBigint(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_BIGINT;
}

#endif  // INCLUDE_MS_OBJ_OBJ_BIGINT_H_
```

- [ ] **Step 1.2: 写 `src/obj/obj_bigint.c` Part A（核心：规范化/构造/解析/打印/比较/hash）**

文件头与核心实现（Task 2 在此文件追加运算部分）：

```c
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
```

```c
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
```

```c
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
```

```c
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
```

（Task 2 将在此文件追加 `magAdd/magSub/magMul*/magDivMod*/pow/bitops/shl/shr` 与各 `msBig*` 公共函数。）

- [ ] **Step 1.3: CMake 注册**

`CMakeLists.txt`：在 `target_sources(ms_obj PRIVATE src/obj/obj_set.c)`（约 93 行）之后按同型追加：

```cmake
target_sources(ms_obj PRIVATE src/obj/obj_bigint.c)  # T016：bigint 对象（FAM limb）
```

`tests/CMakeLists.txt` 末尾按 obj_map 同型追加：

```cmake
ms_add_test(obj_bigint ms_obj ms_gc ms_vm ms_compiler ms_binder)
```

- [ ] **Step 1.4: 写失败测试 `tests/unit/test_obj_bigint.c` Part 1**

文件头 + 执行桥（与 `tests/unit/test_obj_map.c` 第 1-83 行同型的 include 组、`RunResult`、`slurpTmp`、`runSrc`、`MV_INT`——逐行复制该文件 1-83 行，仅改文件头注释为本行）：

```c
// T016：bigint 数据面 + 数值塔单测（docs/tasks/T016 §5.1；VM 级用例随 Task 3+ 追加）
```

其后追加确定性随机源与数据面测试（完整）：

```c
// ---------- 数据面（Task 1） ----------

// 确定性 LCG（跨平台一致；不依赖 rand()）
static ms_u32 rngState = 0x12345678u;

static ms_u32 rngNext(void) {
  rngState = rngState * 1664525u + 1013904223u;
  return rngState >> 8;
}

static MsObjBigInt* randomBig(MsVM* vm, int32_t limbCount) {  // 顶层 limb 非零
  uint32_t* buf = malloc((size_t)limbCount * sizeof(uint32_t));
  for (int32_t i = 0; i < limbCount; i++) {
    buf[i] = (rngNext() << 24) ^ rngNext();
  }
  buf[limbCount - 1] |= 0x80000000u;  // 保证位长
  int sign = (int)(rngNext() % 3) - 1;  // -1/0/+1（0 时 New 规范化为 0）
  MsObjBigInt* b = msObjBigintNew(vm, buf, limbCount, sign == 0 ? 1 : sign);
  free(buf);
  return b;
}

// C 字符串快照（toString(10)；调用方 free；失败 NULL）——vm 供末端 msObjStringNew 分配
static char* bigDec(MsVM* vm, const MsObjBigInt* b) {
  MsObjString* s = msObjBigintToString(vm, b, 10);
  if (s == NULL) {
    return NULL;
  }
  char* r = malloc(s->byteLen + 1);
  memcpy(r, s->bytes, s->byteLen);
  r[s->byteLen] = '\0';
  return r;
}

// root 辅助：bigint 经 VM 栈 root 后再继续（防后续分配窗口回收）
static void rootBig(MsVM* vm, const MsObjBigInt* b) {
  msVmPush(vm, msValueObj((MsObj*)&b->head));
}
```

Task 1 测试用例（追加于辅助函数之后）：

```c
TEST(bigintParsePrintRoundtrip) {  // §5.1 解析/打印：往返一致
  MsVM vm;
  msVmInit(&vm);
  const char* cases[] = {"0", "1", "-1", "123456789012345678901234567890",
                         "-987654321098765432109876543210", "18446744073709551616"};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    MsObjBigInt* b = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, cases[i], (int32_t)strlen(cases[i]), 10, &b) == MS_BIG_OK);
    rootBig(&vm, b);
    char* s = bigDec(&vm, b);
    EXPECT_STREQ(ctx, cases[i], s);
    free(s);
  }
  // 前缀/下划线/显式 base 前缀
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "0xff", 4, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  char* s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "255", s);
  free(s);
  EXPECT(ctx, msObjBigintParse(&vm, "0b1010", 6, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "10", s);
  free(s);
  EXPECT(ctx, msObjBigintParse(&vm, "1_000_000", 9, 0, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  s = bigDec(&vm, b);
  EXPECT_STREQ(ctx, "1000000", s);
  free(s);
  // 非法：空串 / 坏字符 / 空前缀
  EXPECT(ctx, msObjBigintParse(&vm, "", 0, 10, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "1e3", 3, 10, &b) == MS_BIG_PARSE);
  EXPECT(ctx, msObjBigintParse(&vm, "0x", 2, 0, &b) == MS_BIG_PARSE);
  msVmFree(&vm);
}

TEST(bigintToStringBase) {  // §5.1：255 → 2/8/16；负数；0；非法 base
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "255", 3, 10, &b) == MS_BIG_OK);
  rootBig(&vm, b);
  const int bases[] = {2, 8, 16};
  const char* want[] = {"11111111", "377", "ff"};
  for (int i = 0; i < 3; i++) {
    MsObjString* s = msObjBigintToString(&vm, b, bases[i]);
    EXPECT(ctx, s != NULL);
    EXPECT(ctx, s->byteLen == (int32_t)strlen(want[i]) && memcmp(s->bytes, want[i], s->byteLen) == 0);
  }
  EXPECT(ctx, msObjBigintToString(&vm, b, 3) == NULL);  // 非法 base
  msVmFree(&vm);
}

TEST(bigintHashNumeric) {  // §5.1：hash(1) == hash(1n) == hash(1.0)；跨塔一致性
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* one = msObjBigintFromI64(&vm, 1);
  rootBig(&vm, one);
  MsObjBigInt* big = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "9223372036854775808", 19, 10, &big) == MS_BIG_OK);  // 2^63
  rootBig(&vm, big);
  EXPECT(ctx, msHashNumericI64(1) == msHashNumericBigint(one));
  EXPECT(ctx, msHashNumericI64(1) == msHashNumericF64(1.0));
  EXPECT(ctx, msHashNumericBigint(big) == msHashNumericF64(9223372036854775808.0));
  EXPECT(ctx, msHashNumericI64(9007199254740993LL) != msHashNumericF64(9007199254740992.0));  // ≥2^53 精确化（值不等 → hash 不等；hashMix64 为双射）
  EXPECT(ctx, msHashNumericI64(9007199254740992LL) == msHashNumericF64(9007199254740992.0));  // 值相等 → hash 相等
  EXPECT(ctx, msHashNumericI64(-1) != msHashNumericI64(1));
  EXPECT(ctx, msHashNumericF64(-0.0) == msHashNumericI64(0));
  msVmFree(&vm);
}

TEST(bigintCompare) {  // 符号域 + i64/f64 精确比较
  MsVM vm;
  msVmInit(&vm);
  MsObjBigInt* a = NULL;
  MsObjBigInt* b = NULL;
  EXPECT(ctx, msObjBigintParse(&vm, "9223372036854775808", 19, 10, &a) == MS_BIG_OK);  // 2^63
  rootBig(&vm, a);
  EXPECT(ctx, msObjBigintParse(&vm, "-9223372036854775808", 20, 10, &b) == MS_BIG_OK);  // -2^63
  rootBig(&vm, b);
  EXPECT_EQ(ctx, 1, msObjBigintCompare(a, b));
  EXPECT_EQ(ctx, 0, msObjBigintCmpF64(a, 9223372036854775808.0));   // 2^63 float 精确相等
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(a, 9223372036854777856.0));  // 2^63+2048（2^63 后首个 double）
  EXPECT_EQ(ctx, 2, msObjBigintCmpF64(a, NAN));                     // NaN 无序
  EXPECT_EQ(ctx, 1, msObjBigintCmpI64(a, INT64_MAX));   // 2^63 > 2^63-1
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(b, INT64_MIN));   // -2^63 == INT64_MIN
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(b, -1.5));  // -2^63 < -1.5
  // 非整邻域：bigint 5 vs 5.5 / 5.0
  MsObjBigInt* five = msObjBigintFromI64(&vm, 5);
  rootBig(&vm, five);
  EXPECT_EQ(ctx, -1, msObjBigintCmpF64(five, 5.5));
  EXPECT_EQ(ctx, 0, msObjBigintCmpF64(five, 5.0));
  // FitsI64 边界
  ms_i64 iv = 0;
  EXPECT(ctx, !msObjBigintFitsI64(a, &iv));
  EXPECT(ctx, msObjBigintFitsI64(b, &iv));
  EXPECT_EQ(ctx, INT64_MIN, iv);
  msVmFree(&vm);
}
```

`main` 按仓库模式逐个调用 TEST 并 `RUN_TEST`（照抄 `tests/unit/test_obj_map.c` 末尾 main 结构，逐行替换用例名）。

- [ ] **Step 1.5: 构建并确认编译失败点被消除（新增源生效）**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error" | Select-Object -First 5`
预期：无 error（数据面自洽；obj_bigint.c 未被任何既有代码引用）。`ctest --test-dir build -C Debug -L unit` 全绿（新测试 bigint* 4 项通过）。

- [ ] **Step 1.6: Commit**

```powershell
git add src/obj/obj_bigint.c src/obj/obj_bigint.h CMakeLists.txt tests/unit/test_obj_bigint.c tests/CMakeLists.txt
git commit -m "✨ feat(obj/bigint): add MsObjBigInt core with parse/print/hash/compare"
```

---

### Task 2: 运算——加/减/乘（朴素+Karatsuba）/除模（Knuth D）/幂/位运算/移位

> **实现期勘误（2678aec7b 落盘，代码以提交为准）**：下方 Step 2.4 计划代码有 4 处缺陷已由实现者修复——
> ① Karatsuba scratch 槽位欠配（z2 槽须 `a1len+b1len`、z1 槽须 `saCap+sbCap`，固定 `2m` 会在奇数 alen 溢出污染邻槽）；
> ② z0 尾部未清零（低半段顶 limb 为零时 memcpy 拷入未初始化 scratch，递归前须 memset）；
> ③ Knuth-D 内核 `n==1` 单 limb 除数越界读 `vn[n-2]`（须 divmnu 式 64/32 直除专用路径）；
> ④ Step 2.2 符号表 4 行而循环 `i < 8`（按实际行数循环；实现者扩至 6 行补 6/2 与 (10^24+7)/3 锚点）。
> 另：乘法 oracle 容量统一 8200（正文 4600 作废）；计划中小写 `msBigErr` 均为 `MsBigErr`。

**Files:**
- Modify: `src/obj/obj_bigint.c`（文件后半追加）
- Modify: `tests/unit/test_obj_bigint.c`（追加十进制 oracle 与运算 TEST）

- [ ] **Step 2.1: 在 `tests/unit/test_obj_bigint.c` 追加十进制 oracle（独立第二实现，§6 风险注记）**

主实现的 `toString(10)` 由 Step 1.4 的固定用例锚定后，oracle 复用其字符串做入口（运算独立性才是 oracle 目标）：

```c
// ---------- 十进制软参考（T016 §6：独立朴素实现防同源错误；仅 +、-、×、比较） ----------
// 大端十进制数字数组（d[0] 最高位）；sign ∈ {-1,0,1}；容量覆盖 2×(5000 bit) 乘积（~3011 位）

typedef struct DecNum {
  int sign;
  int len;       // 有效位数（0 ⇔ 值 0）
  char d[4600];
} DecNum;

static void decNorm(DecNum* n) {
  int32_t i = 0;
  while (i < n->len && n->d[i] == 0) {
    i++;
  }
  memmove(n->d, n->d + i, (size_t)(n->len - i));
  n->len -= i;
  if (n->len == 0) {
    n->sign = 0;
  }
}

static void decFromString(DecNum* out, const char* s) {
  out->sign = s[0] == '-' ? -1 : 1;
  const char* p = s[0] == '-' || s[0] == '+' ? s + 1 : s;
  out->len = (int32_t)strlen(p);
  memcpy(out->d, p, (size_t)out->len);
  for (int32_t i = 0; i < out->len; i++) {
    out->d[i] = (char)(out->d[i] - '0');
  }
  decNorm(out);
}

static void decToChars(const DecNum* n, char* buf, size_t cap) {
  if (n->len == 0) {
    snprintf(buf, cap, "0");
    return;
  }
  int32_t o = 0;
  if (n->sign < 0) {
    buf[o++] = '-';
  }
  for (int32_t i = 0; i < n->len; i++) {
    buf[o + i] = (char)('0' + n->d[i]);
  }
  buf[o + n->len] = '\0';
}

// |a| + |b|（大端）
static void decMagAdd(DecNum* a, const DecNum* b) {
  char r[4600];
  int32_t al = a->len, bl = b->len;
  int32_t rl = al > bl ? al : bl;
  int carry = 0;
  for (int32_t i = 0; i < rl; i++) {
    int da = al - 1 - i >= 0 ? a->d[al - 1 - i] : 0;
    int db = bl - 1 - i >= 0 ? b->d[bl - 1 - i] : 0;
    int t = da + db + carry;
    r[rl - 1 - i] = (char)(t % 10);
    carry = t / 10;
  }
  if (carry != 0) {
    memmove(r + 1, r, (size_t)rl);
    r[0] = (char)carry;
    rl++;
  }
  memcpy(a->d, r, (size_t)rl);
  a->len = rl;
}

// |a| - |b|（|a| ≥ |b|）
static void decMagSub(DecNum* a, const DecNum* b) {
  int32_t al = a->len, bl = b->len;
  int borrow = 0;
  for (int32_t i = 0; i < al; i++) {
    int da = a->d[al - 1 - i];
    int db = bl - 1 - i >= 0 ? b->d[bl - 1 - i] : 0;
    int t = da - db - borrow;
    if (t < 0) {
      t += 10;
      borrow = 1;
    } else {
      borrow = 0;
    }
    a->d[al - 1 - i] = (char)t;
  }
  decNorm(a);
}

static int decCmpAbs(const DecNum* a, const DecNum* b) {
  if (a->len != b->len) {
    return a->len < b->len ? -1 : 1;
  }
  for (int32_t i = 0; i < a->len; i++) {
    if (a->d[i] != b->d[i]) {
      return a->d[i] < b->d[i] ? -1 : 1;
    }
  }
  return 0;
}

// 有符号 a ± b
static void decAdd(DecNum* a, const DecNum* b, bool sub) {
  int bs = sub ? -b->sign : b->sign;
  if (a->sign == 0) {
    *a = *b;
    a->sign = bs;
    return;
  }
  if (bs == 0) {
    return;
  }
  if (a->sign == bs) {
    decMagAdd(a, b);
  } else {
    int c = decCmpAbs(a, b);
    if (c == 0) {
      a->len = 0;
      a->sign = 0;
    } else if (c > 0) {
      decMagSub(a, b);
    } else {
      DecNum t = *b;
      decMagSub(&t, a);
      *a = t;
      a->sign = bs;
    }
  }
}

// |a|·|b|（大端卷积）
static void decMul(DecNum* out, const DecNum* a, const DecNum* b) {
  char r[4600] = {0};
  int32_t rl = a->len + b->len;
  for (int32_t i = 0; i < a->len; i++) {
    int carry = 0;
    for (int32_t j = 0; j < b->len; j++) {
      int t = r[rl - 1 - (i + j)] + a->d[a->len - 1 - i] * b->d[b->len - 1 - j] + carry;
      r[rl - 1 - (i + j)] = (char)(t % 10);
      carry = t / 10;
    }
    int32_t k = i + b->len;
    while (carry != 0) {
      int t = r[rl - 1 - k] + carry;
      r[rl - 1 - k] = (char)(t % 10);
      carry = t / 10;
      k++;
    }
  }
  memcpy(out->d, r, (size_t)rl);
  out->len = rl;
  out->sign = a->sign * b->sign;
  decNorm(out);
}
```

- [ ] **Step 2.2: 写失败测试（运算 TEST，追加到 test_obj_bigint.c）**

```c
// 运算包装：两 bigint 运算并以十进制比对（oracle 由 dec* 提供）
static void checkBinOp(TestCtx* ctx, MsVM* vm, const char* op, const MsObjBigInt* a, const MsObjBigInt* b,
                       const DecNum* want) {
  rootBig(vm, a);
  rootBig(vm, b);
  MsBigView va, vb;
  msBigViewOf(&va, a);
  msBigViewOf(&vb, b);
  MsObjBigInt* r = NULL;
  MsBigErr e = MS_BIG_OK;
  if (strcmp(op, "+") == 0) {
    e = msBigAdd(vm, &va, &vb, &r);
  } else if (strcmp(op, "-") == 0) {
    e = msBigSub(vm, &va, &vb, &r);
  } else if (strcmp(op, "*") == 0) {
    e = msBigMul(vm, &va, &vb, &r);
  } else {
    e = msBigDiv(vm, &va, &vb, &r);
  }
  EXPECT(ctx, e == MS_BIG_OK);
  if (e != MS_BIG_OK) {
    return;
  }
  rootBig(vm, r);
  char* got = bigDec(vm, r);
  char wantChars[4600];
  decToChars(want, wantChars, sizeof(wantChars));
  EXPECT_STREQ(ctx, wantChars, got);
  free(got);
}

TEST(bigintAddSubOracle) {  // §5.1：随机 2048 位 ± 与十进制参考一致
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 100; iter++) {
    int32_t la = 1 + (int32_t)(rngNext() % 64);   // ≤ 2048 bit
    int32_t lb = 1 + (int32_t)(rngNext() % 64);
    MsObjBigInt* a = randomBig(&vm, la);
    MsObjBigInt* b = randomBig(&vm, lb);
    char* as = bigDec(&vm, a);
    char* bs = bigDec(&vm, b);
    DecNum da, db, want;
    decFromString(&da, as);
    decFromString(&db, bs);
    want = da;
    decAdd(&want, &db, false);
    checkBinOp(ctx, &vm, "+", a, b, &want);
    want = da;
    decAdd(&want, &db, true);
    checkBinOp(ctx, &vm, "-", a, b, &want);
    free(as);
    free(bs);
  }
  msVmFree(&vm);
}

TEST(bigintMulOracle) {  // §5.1/DoD：阈值两侧 200 组随机（含 Karatsuba 路径）
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 200; iter++) {
    // 100..300 limb（3200..9600 bit）跨 4096 bit 阈值；DecNum 容量按 300 limb（9028 bit ≈ 2718 位）乘积上限
    int32_t la = 100 + (int32_t)(rngNext() % 201);
    int32_t lb = 100 + (int32_t)(rngNext() % 201);
    MsObjBigInt* a = randomBig(&vm, la);
    MsObjBigInt* b = randomBig(&vm, lb);
    char* as = bigDec(&vm, a);
    char* bs = bigDec(&vm, b);
    DecNum da, db, want;
    decFromString(&da, as);
    decFromString(&db, bs);
    decMul(&want, &da, &db);
    checkBinOp(ctx, &vm, "*", a, b, &want);
    free(as);
    free(bs);
  }
  msVmFree(&vm);
}
```

注意：`DecNum.d` 容量 4600 覆盖 300 limb × 2（≈ 5436 位十进制）不足——将 `d` 容量与 `decMul` 内 `r` 容量统一提到 **8200**（300 limb = 9600 bit → 2890 位；乘积 ≤ 5780 位 + 进位裕量）。落盘时以 8200 为准（上述 `4600` 全部替换）。

```c
TEST(bigintDivModSemantics) {  // §5.1：除/模符号 + 恒等式性质
  MsVM vm;
  msVmInit(&vm);
  // 固定符号用例（02 §3.5 Go 语义）
  const char* triples[] = {
      "-7", "2", "-3", "-1", "7", "-2", "-3", "1", "5", "2", "2", "1", "0", "5", "0", "0",
  };
  for (int i = 0; i < 8; i++) {
    MsObjBigInt* a = NULL;
    MsObjBigInt* b = NULL;
    EXPECT(ctx, msObjBigintParse(&vm, triples[4 * i], (int32_t)strlen(triples[4 * i]), 10, &a) == MS_BIG_OK);
    rootBig(&vm, a);
    EXPECT(ctx, msObjBigintParse(&vm, triples[4 * i + 1], (int32_t)strlen(triples[4 * i + 1]), 10, &b) == MS_BIG_OK);
    rootBig(&vm, b);
    MsBigView va, vb;
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    MsObjBigInt* q = NULL;
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigDiv(&vm, &va, &vb, &q) == MS_BIG_OK);
    rootBig(&vm, q);
    EXPECT(ctx, msBigMod(&vm, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    char* qs = bigDec(&vm, q);
    char* rs = bigDec(&vm, r);
    EXPECT_STREQ(ctx, triples[4 * i + 2], qs);
    EXPECT_STREQ(ctx, triples[4 * i + 3], rs);
    free(qs);
    free(rs);
  }
  // 除零
  MsObjBigInt* zero = msObjBigintFromI64(&vm, 0);
  rootBig(&vm, zero);
  MsBigView va2 = {NULL, 0, 0};
  msBigViewOf(&va2, zero);
  MsObjBigInt* r2 = NULL;
  EXPECT(ctx, msBigMod(&vm, &va2, &va2, &r2) == MS_BIG_DIVZERO);
  // 随机大数恒等式：a == q·b + r ∧ |r| < |b| ∧ (r == 0 ∨ sign(r) == sign(a))
  for (int iter = 0; iter < 50; iter++) {
    MsObjBigInt* a = randomBig(&vm, 2 + (int32_t)(rngNext() % 80));
    MsObjBigInt* b = randomBig(&vm, 1 + (int32_t)(rngNext() % 40));
    rootBig(&vm, a);
    rootBig(&vm, b);
    MsBigView vwa, vwb;
    msBigViewOf(&vwa, a);
    msBigViewOf(&vwb, b);
    MsObjBigInt* q = NULL;
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigDiv(&vm, &vwa, &vwb, &q) == MS_BIG_OK);
    rootBig(&vm, q);
    EXPECT(ctx, msBigMod(&vm, &vwa, &vwb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    // q·b + r == a
    MsBigView vq, vr;
    msBigViewOf(&vq, q);
    msBigViewOf(&vwb, b);
    msBigViewOf(&vr, r);
    MsObjBigInt* qb = NULL;
    MsObjBigInt* sum = NULL;
    EXPECT(ctx, msBigMul(&vm, &vq, &vwb, &qb) == MS_BIG_OK);
    rootBig(&vm, qb);
    msBigViewOf(&vq, qb);
    EXPECT(ctx, msBigAdd(&vm, &vq, &vr, &sum) == MS_BIG_OK);
    rootBig(&vm, sum);
    EXPECT(ctx, msObjBigintCompare(sum, a) == 0);
    EXPECT(ctx, r->len == 0 || r->sign == a->sign);  // 余符号随被除数
  }
  msVmFree(&vm);
}

TEST(bigintBitwiseVsI64) {  // §5.1：小数值域以 i64 位运算为模型（含负数补码语义）
  MsVM vm;
  msVmInit(&vm);
  for (int iter = 0; iter < 200; iter++) {
    ms_i64 x = (ms_i64)((rngNext() % 2001) - 1000);
    ms_i64 y = (ms_i64)((rngNext() % 2001) - 1000);
    MsObjBigInt* a = msObjBigintFromI64(&vm, x);
    MsObjBigInt* b = msObjBigintFromI64(&vm, y);
    rootBig(&vm, a);
    rootBig(&vm, b);
    MsBigView va, vb;
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    MsObjBigInt* r = NULL;
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_AND, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x & y));
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_OR, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x | y));
    msBigViewOf(&va, a);
    msBigViewOf(&vb, b);
    EXPECT(ctx, msBigBitOp(&vm, MS_BIG_BIT_XOR, &va, &vb, &r) == MS_BIG_OK);
    rootBig(&vm, r);
    EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, x ^ y));
  }
  // 移位/幂/~ 语义锚点
  MsObjBigInt* m3 = msObjBigintFromI64(&vm, -3);
  rootBig(&vm, m3);
  MsBigView v3;
  msBigViewOf(&v3, m3);
  MsObjBigInt* r = NULL;
  EXPECT(ctx, msBigShr(&vm, &v3, 1, &r) == MS_BIG_OK);  // floor(-3/2) = -2
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, -2));
  msBigViewOf(&v3, m3);
  EXPECT(ctx, msBigNot(&vm, &v3, &r) == MS_BIG_OK);  // ~(-3) = 2
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, 2));
  MsObjBigInt* m1 = msObjBigintFromI64(&vm, -1);
  rootBig(&vm, m1);
  MsBigView v1;
  msBigViewOf(&v1, m1);
  EXPECT(ctx, msBigShr(&vm, &v1, 1000, &r) == MS_BIG_OK);  // -1 >> 大数 = -1
  rootBig(&vm, r);
  EXPECT_EQ(ctx, 0, msObjBigintCmpI64(r, -1));
  MsObjBigInt* two = msObjBigintFromI64(&vm, 2);
  rootBig(&vm, two);
  MsBigView v2;
  msBigViewOf(&v2, two);
  EXPECT(ctx, msBigPow(&vm, &v2, 100, &r) == MS_BIG_OK);
  rootBig(&vm, r);
  char* s = bigDec(&vm, r);
  EXPECT_STREQ(ctx, "1267650600228229401496703205376", s);  // 2^100
  free(s);
  msVmFree(&vm);
}
```

`main` 注册新 TEST。

- [ ] **Step 2.3: 构建，确认链接失败（msBig* 未定义）**

Run: `cmake --build build --config Debug 2>&1 | Select-String "unresolved|error"`
预期：`msBigAdd/msBigMul/...` 未解析外部符号。

- [ ] **Step 2.4: 实现 `src/obj/obj_bigint.c` Part B（运算，追加在 hash 段之后）**

```c
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
```

```c
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
  int32_t sumLen = m + 1;
  int32_t zLen = 2 * m;
  int32_t need = 2 * sumLen + 2 * zLen + (2 * m + 2);
  if (scratchCap < need) {
    return false;
  }
  uint32_t* sa = scratch;
  uint32_t* sb = scratch + sumLen;
  uint32_t* z0 = scratch + 2 * sumLen;
  uint32_t* z2 = z0 + zLen;
  uint32_t* z1 = z2 + zLen;
  uint32_t* subScratch = z1 + 2 * m + 2;
  int32_t subCap = scratchCap - (int32_t)(subScratch - scratch);
  int32_t saLen = 0;
  int32_t sbLen = 0;
  int32_t z0Len = 0;
  int32_t z2Len = 0;
  int32_t z1Len = 0;
  magAddAny(sa, a0, a0len, a1, a1len, &saLen);
  magAddAny(sb, b0, b0len, b1, b1len, &sbLen);
  if (!magMulRec(z0, a0, a0len, b0, b0len, subScratch, subCap) ||
      !magMulRec(z2, a1, a1len, b1, b1len, subScratch, subCap) ||
      !magMulRec(z1, sa, saLen, sb, sbLen, subScratch, subCap)) {
    return false;
  }
  z0Len = a0len + b0len;  // 调用方依赖 normLen 收敛；乘积长上界即两因子长和
  z2Len = a1len + b1len;
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
```

```c
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

// 幅值除内核：u（就地改写为规格化余数）÷ v → q；ulen ≥ vlen ≥ 1，v 顶层非零
static void magDivKernel(uint32_t* q, uint32_t* u, int32_t ulen, const uint32_t* v, int32_t vlen,
                         int32_t* outULen) {
  int32_t n = vlen;
  int32_t m = ulen - vlen;
  if (m < 0) {  // 防御（调用方门控）
    *outULen = ulen;
    return;
  }
  int s = clz32(v[n - 1]);
  uint32_t* un = (uint32_t*)calloc((size_t)ulen + 1, sizeof(uint32_t));
  uint32_t* vn = (uint32_t*)calloc((size_t)n, sizeof(uint32_t));
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
  magDivKernel(q, u, ulen, b->limbs, b->len, &ulen);
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
```

```c
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
  if (bitLenView(a->limbs, a->len) + n > MS_BIGINT_MAX_BITS) {
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
```

- [ ] **Step 2.5: 构建并跑单测**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug -L unit --output-on-failure`
预期：全部通过（含 `bigintAddSubOracle`、`bigintMulOracle`（200 组跨阈值）、`bigintDivModSemantics`、`bigintBitwiseVsI64`）。若 Karatsuba/除法随机组失败，先用朴素路径交叉定位（临时将阈值宏改 10^9 强制朴素重跑判定层数据）。

- [ ] **Step 2.6: Commit**

```powershell
git add src/obj/obj_bigint.c tests/unit/test_obj_bigint.c
git commit -m "✨ feat(obj/bigint): arithmetic with Karatsuba mul, Knuth-D divmod and bitwise ops"
```

---

### Task 3: VM 与值系统接线（溢出提升 / 混算 / 比较 / hash 回填 / 词法衔接）

**Files:**
- Create: `src/plat/plat_math.h`
- Modify: `src/vm/vm.c`、`src/vm/value.c`、`src/obj/obj_map.c`、`src/scanner/scanner.c`、`src/scanner/scanner.h`
- Modify: `tests/unit/test_obj_bigint.c`（VM 级 runSrc 用例）、`tests/unit/test_vm_basic.c`、`tests/unit/test_scanner_core.c`

- [ ] **Step 3.1: 写 `src/plat/plat_math.h`（溢出三件套；纯头文件无 CMake 改动）**

```c
// i64 溢出检测三件套（T016，22 §5）：GCC/Clang 走内建；MSVC 无等价内建，手写 UB 安全检查
#ifndef INCLUDE_MS_PLAT_PLAT_MATH_H_
#define INCLUDE_MS_PLAT_PLAT_MATH_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)

static inline bool msAddOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_add_overflow(a, b, r);
}

static inline bool msSubOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_sub_overflow(a, b, r);
}

static inline bool msMulOverflowI64(int64_t a, int64_t b, int64_t* r) {
  return __builtin_mul_overflow(a, b, r);
}

#else  // MSVC 等：分支检查（与原 vm.c 手写同型）

static inline bool msAddOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = (int64_t)((uint64_t)a + (uint64_t)b);
  return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
}

static inline bool msSubOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = (int64_t)((uint64_t)a - (uint64_t)b);
  return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
}

static inline uint64_t msAbsU64(int64_t v) {
  return v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
}

static inline bool msMulOverflowI64(int64_t a, int64_t b, int64_t* r) {
  *r = 0;
  if (a == 0 || b == 0) {
    return false;
  }
  uint64_t ua = msAbsU64(a);
  uint64_t ub = msAbsU64(b);
  if (ua > (uint64_t)INT64_MAX / ub) {
    return true;
  }
  uint64_t um = ua * ub;  // |积| ≤ INT64_MAX，正负号均可表示
  *r = (a < 0) != (b < 0) ? -(int64_t)um : (int64_t)um;
  return false;
}

#endif

#endif  // INCLUDE_MS_PLAT_PLAT_MATH_H_
```

- [ ] **Step 3.2: `src/vm/vm.c`——替换溢出助手与 arithBinary 数值路径**

3.2.1 头部 include 增加（放在 `#include "obj/obj_list.h"` 同组按字母序合适位置）：

```c
#include "obj/obj_bigint.h"
#include "plat/plat_math.h"
```

3.2.2 删除 vm.c 124-157 的 `addOv/subOv/absI64/mulOv`（注释行 124 一并改为：

```c
// ---- 数值塔（T016，02 §3）：int/int 快路径（plat_math 溢出三件套）+ bigint 域 + float 域 ----
```

`intPow`（159-175）内 `mulOv(acc, base, &acc)` 改为 `!msMulOverflowI64(acc, base, &acc)` 取反：

```c
static bool intPow(ms_i64 base, ms_i64 e, ms_i64* r) {
  if (base >= -1 && base <= 1) {  // |base| ≤ 1：任意非负 e 结果在 {-1,0,1}
    *r = base == -1 ? (e % 2 == 0 ? 1 : -1) : base;
    return true;
  }
  if (e >= 64) {  // |base| ≥ 2 且 e ≥ 64 必溢出
    return false;
  }
  ms_i64 acc = 1;
  for (ms_i64 i = 0; i < e; i++) {
    if (msMulOverflowI64(acc, base, &acc)) {
      return false;
    }
  }
  *r = acc;
  return true;
}
```

3.2.3 在 `arithBinary` 之前新增 bigint 域入口（完整）：

```c
static bool isNumVal(const MsValue* v) {  // 数值域（02 §3.4）：int/float/bigint（bool 排除）
  if (v->kind == MS_VAL_I64 || v->kind == MS_VAL_F64) {
    return true;
  }
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_BIGINT;
}

static double numToF64(const MsValue* v) {
  return v->kind == MS_VAL_I64 ? (double)v->as.i
                               : (v->kind == MS_VAL_F64 ? v->as.f
                                                        : msObjBigintToF64((const MsObjBigInt*)v->as.obj));
}

// bigErr → 错误槽（已置槽错误如 StackOverflow 不覆盖）
static bool bigErrSet(MsVM* vm, MsBigErr e) {
  if (vm->hasError) {
    return false;
  }
  switch (e) {
    case MS_BIG_OOM:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "RuntimeError: out of memory");
      break;
    case MS_BIG_PARSE:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid bigint literal");
      break;
    case MS_BIG_DIVZERO:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "ArithmeticError: division by zero");
      break;
    case MS_BIG_RANGE:
      snprintf(vm->errMsg, sizeof(vm->errMsg), "OverflowError: bigint result too large");
      break;
    case MS_BIG_OK:
      break;
  }
  return false;
}

// int⊗bigint / bigint⊗{int,bigint} 全域（02 §3.4 表）。操作数为 C 局部（弹栈）：
// obj_bigint 层约定分配窗口后不再读输入 limbs → 安全（21 §3.2，同 string concat 契约）
static bool bigArith(MsVM* vm, BinArith k, MsValue a, MsValue b, MsValue* out) {
  MsBigView va, vb;
  uint32_t ba[2], bb[2];
  if (a.kind == MS_VAL_I64) {
    msBigViewI64(&va, ba, a.as.i);
  } else {
    msBigViewOf(&va, (const MsObjBigInt*)a.as.obj);
  }
  if (b.kind == MS_VAL_I64) {
    msBigViewI64(&vb, bb, b.as.i);
  } else {
    msBigViewOf(&vb, (const MsObjBigInt*)b.as.obj);
  }
  MsObjBigInt* r = NULL;
  MsBigErr e = MS_BIG_OK;
  if ((k == BA_DIV || k == BA_MOD) && vb.len == 0) {
    snprintf(vm->errMsg, sizeof(vm->errMsg),
             k == BA_DIV ? "ArithmeticError: division by zero" : "ArithmeticError: modulo by zero");
    return false;
  }
  switch (k) {
    case BA_ADD:
      e = msBigAdd(vm, &va, &vb, &r);
      break;
    case BA_SUB:
      e = msBigSub(vm, &va, &vb, &r);
      break;
    case BA_MUL:
      e = msBigMul(vm, &va, &vb, &r);
      break;
    case BA_DIV:
      e = msBigDiv(vm, &va, &vb, &r);
      break;
    case BA_MOD:
      e = msBigMod(vm, &va, &vb, &r);
      break;
    case BA_POW: {
      ms_i64 ev = 0;
      if (b.kind != MS_VAL_I64) {  // bigint 指数：可容 i64 且非负才可用
        if (!msObjBigintFitsI64((const MsObjBigInt*)b.as.obj, &ev) || ev < 0) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: pow exponent must be a non-negative int");
          return false;
        }
        e = msBigPow(vm, &va, ev, &r);
        break;
      }
      if (b.as.i < 0) {  // 负指数浮点幂（与 int 路径一致，02 §3）
        *out = msValueFloat(pow(numToF64(&a), (double)b.as.i));
        return true;
      }
      e = msBigPow(vm, &va, b.as.i, &r);
      break;
    }
    case BA_BIT_AND:
      e = msBigBitOp(vm, MS_BIG_BIT_AND, &va, &vb, &r);
      break;
    case BA_BIT_OR:
      e = msBigBitOp(vm, MS_BIG_BIT_OR, &va, &vb, &r);
      break;
    case BA_BIT_XOR:
      e = msBigBitOp(vm, MS_BIG_BIT_XOR, &va, &vb, &r);
      break;
    case BA_SHL:
    case BA_SHR: {
      if (b.kind != MS_VAL_I64) {
        snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: shift count must be int");
        return false;
      }
      if (b.as.i < 0) {
        snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: negative shift count");
        return false;
      }
      e = k == BA_SHL ? msBigShl(vm, &va, b.as.i, &r) : msBigShr(vm, &va, b.as.i, &r);
      break;
    }
  }
  if (e != MS_BIG_OK) {
    return bigErrSet(vm, e);
  }
  *out = msValueObj(&r->head);
  return true;
}
```

3.2.4 `arithBinary` 主体改写（197 行起）。改后完整形态：

```c
static bool arithBinary(MsVM* vm, BinArith k, MsValue a, MsValue b, MsValue* out) {
  bool aNum = isNumVal(&a);
  bool bNum = isNumVal(&b);
  if (aNum && bNum && a.kind == MS_VAL_I64 && b.kind == MS_VAL_I64) {  // int/int 快路径
    ms_i64 r = 0;
    switch (k) {
      case BA_ADD:
        if (msAddOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);  // 溢出升 bigint（02 §3.1）
        }
        break;
      case BA_SUB:
        if (msSubOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);
        }
        break;
      case BA_MUL:
        if (msMulOverflowI64(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);
        }
        break;
      case BA_DIV:
        if (b.as.i == 0) goto divZero;
        if (a.as.i == INT64_MIN && b.as.i == -1) {
          return bigArith(vm, k, a, b, out);  // 2^63 超 int64 → bigint
        }
        r = a.as.i / b.as.i;  // C 截断除法 == Go 语义（-7/2 == -3）
        break;
      case BA_MOD:
        if (b.as.i == 0) goto modZero;
        r = b.as.i == -1 ? 0 : a.as.i % b.as.i;  // INT64_MIN % -1 == 0（避免 UB）
        break;
      case BA_POW:
        if (b.as.i < 0) {  // 负指数转浮点幂（Python 语义）
          *out = msValueFloat(pow((double)a.as.i, (double)b.as.i));
          return true;
        }
        if (!intPow(a.as.i, b.as.i, &r)) {
          return bigArith(vm, k, a, b, out);  // 溢出升 bigint
        }
        break;
      case BA_SHL:
        if (b.as.i < 0) goto negShift;
        if (b.as.i >= 64) {
          return bigArith(vm, k, a, b, out);  // 超宽移位精确升 bigint（T016，替代 Go-0）
        }
        r = a.as.i << b.as.i;
        break;
      case BA_SHR:
        if (b.as.i < 0) goto negShift;
        r = b.as.i >= 64 ? (a.as.i < 0 ? -1 : 0) : a.as.i >> b.as.i;  // 算术右移 = floor
        break;
      case BA_BIT_AND:
        r = a.as.i & b.as.i;
        break;
      case BA_BIT_OR:
        r = a.as.i | b.as.i;
        break;
      case BA_BIT_XOR:
        r = a.as.i ^ b.as.i;
        break;
    }
    *out = msValueInt(r);
    return true;
  }
  if (aNum && bNum) {
    if (a.kind != MS_VAL_F64 && b.kind != MS_VAL_F64) {
      return bigArith(vm, k, a, b, out);  // bigint 域（02 §3.4 表）
    }
    if (k >= BA_BIT_AND) {  // 位运算拒绝 float（02 §3.1）
      snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: bit operation '%s' requires int or bigint operands",
               kBinArithNames[k]);
      return false;
    }
    double da = numToF64(&a);  // 含 float：升 double（bigint 转 double 允许精度损失，02 §3.3）
    double db = numToF64(&b);
    switch (k) {
      case BA_ADD:
        *out = msValueFloat(da + db);
        return true;
      case BA_SUB:
        *out = msValueFloat(da - db);
        return true;
      case BA_MUL:
        *out = msValueFloat(da * db);
        return true;
      case BA_DIV:
        if (db == 0.0) goto divZero;
        *out = msValueFloat(da / db);  // float 真除
        return true;
      case BA_MOD:
        if (db == 0.0) goto modZero;
        *out = msValueFloat(fmod(da, db));  // 符号随被除数 == C fmod
        return true;
      case BA_POW:
        *out = msValueFloat(pow(da, db));
        return true;
      default:
        break;  // 位运算已在上方拒绝（防御）
    }
    return false;
  }
  // —— string concat / repeat 与 TypeError 兜底（原 276-300 行原样保留） ——
  ...（保留原 string + string、string * int 两段与 TypeError snprintf/divZero/modZero/negShift 标签；
      删除 overflow: 标签与 307-309 三行）
}
```

3.2.5 `OP_UNARY_NEG`（755-768）替换为：

```c
      OPCODE(OP_UNARY_NEG) {
        MsValue a = pop(vm);
        if (a.kind == MS_VAL_I64) {
          if (a.as.i == INT64_MIN) {  // 溢出升 bigint：-INT64_MIN = 2^63（02 §3.1）
            uint32_t limbs[2] = {0, 0x80000000u};
            MsObjBigInt* r = msObjBigintNew(vm, limbs, 2, 1);
            if (r == NULL) {
              RUNTIME_ERROR("RuntimeError: out of memory");
            }
            msVmPush(vm, msValueObj(&r->head));
            NEXT();
          }
          msVmPush(vm, msValueInt(-a.as.i));
        } else if (a.kind == MS_VAL_F64) {
          msVmPush(vm, msValueFloat(-a.as.f));
        } else if (a.kind == MS_VAL_OBJ && a.as.obj->type == MS_OBJ_BIGINT) {  // T016 bigint 取负
          MsBigView v;
          msBigViewOf(&v, (const MsObjBigInt*)a.as.obj);
          MsObjBigInt* r = NULL;
          if (msBigNeg(vm, &v, &r) != MS_BIG_OK) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          msVmPush(vm, msValueObj(&r->head));
        } else {
          RUNTIME_ERROR("TypeError: unary '-' requires a numeric operand, got '%s'", msValueTypeName(&a));
        }
        NEXT();
      }
```

3.2.6 `OP_UNARY_INVERT`（776-783）替换为：

```c
      OPCODE(OP_UNARY_INVERT) {
        MsValue a = pop(vm);
        if (a.kind == MS_VAL_I64) {
          msVmPush(vm, msValueInt(~a.as.i));
        } else if (a.kind == MS_VAL_OBJ && a.as.obj->type == MS_OBJ_BIGINT) {  // ~a = -(a+1)
          MsBigView v;
          msBigViewOf(&v, (const MsObjBigInt*)a.as.obj);
          MsObjBigInt* r = NULL;
          if (msBigNot(vm, &v, &r) != MS_BIG_OK) {
            RUNTIME_ERROR("RuntimeError: out of memory");
          }
          msVmPush(vm, msValueObj(&r->head));
        } else {
          RUNTIME_ERROR("TypeError: '~' requires int or bigint operand, got '%s'", msValueTypeName(&a));
        }
        NEXT();
      }
```

3.2.7 `OP_LOAD_CONST` 的占位错误文案（512 行）改为：

```c
          RUNTIME_ERROR("RuntimeError: invalid constant (interp strings land in Task 11)");
```

- [ ] **Step 3.3: `src/vm/value.c`——isNumeric/相等/序/类型名/形态/常量**

3.3.1 include 组增加 `#include "obj/obj_bigint.h"`（与既有 obj 头同组）。

3.3.2 `isNumeric`（45 行）替换 + 新增精确比较族（置于其下）：

```c
// 数值域（T016 精确化）：int/float/bigint（bool 排除）
static bool isNumeric(const MsValue* v) {
  if (v->kind == MS_VAL_I64 || v->kind == MS_VAL_F64) {
    return true;
  }
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_BIGINT;
}

// int vs float 精确比较（不经 double 舍入：≥2^53 域 i64 不失真）；NaN → 2 无序哨兵
static int cmpI64F64(ms_i64 i, double f) {
  if (isnan(f)) {
    return 2;
  }
  if (f >= 9223372036854775808.0) {  // f ≥ 2^63 > 任何 i64
    return -1;
  }
  if (f < -9223372036854775808.0) {  // f < -2^63 < 任何 i64
    return 1;
  }
  ms_i64 ti = (ms_i64)trunc(f);  // |trunc(f)| ≤ 2^63 可表示（-2^63 恰为 INT64_MIN）
  if (i != ti) {
    return i < ti ? -1 : 1;
  }
  return f == trunc(f) ? 0 : (f > ti ? 1 : -1);  // i == floor(f)：分数为正 → i < f
}

// 数值塔全序比较（02 §6）：NaN 无序 → false（相等门控同走 false → 不等）
static bool numericCmp(const MsValue* a, const MsValue* b, int32_t* outCmp) {
  bool aBig = a->kind == MS_VAL_OBJ;  // isNumeric 已保证 obj ⇒ bigint
  bool bBig = b->kind == MS_VAL_OBJ;
  int c = 0;
  if (!aBig && !bBig) {
    if (a->kind == MS_VAL_I64 && b->kind == MS_VAL_I64) {
      c = a->as.i < b->as.i ? -1 : (a->as.i > b->as.i ? 1 : 0);
    } else if (a->kind == MS_VAL_F64 && b->kind == MS_VAL_F64) {
      if (isnan(a->as.f) || isnan(b->as.f)) {
        return false;
      }
      c = a->as.f < b->as.f ? -1 : (a->as.f > b->as.f ? 1 : 0);
    } else if (a->kind == MS_VAL_I64) {
      c = cmpI64F64(a->as.i, b->as.f);
      if (c == 2) {
        return false;
      }
    } else {
      c = cmpI64F64(b->as.i, a->as.f);
      if (c == 2) {
        return false;
      }
      c = -c;
    }
  } else if (aBig && bBig) {
    c = msObjBigintCompare((const MsObjBigInt*)a->as.obj, (const MsObjBigInt*)b->as.obj);
  } else if (aBig) {
    if (b->kind == MS_VAL_I64) {
      c = msObjBigintCmpI64((const MsObjBigInt*)a->as.obj, b->as.i);
    } else {
      c = msObjBigintCmpF64((const MsObjBigInt*)a->as.obj, b->as.f);
      if (c == 2) {
        return false;
      }
    }
  } else {
    if (a->kind == MS_VAL_I64) {
      c = msObjBigintCmpI64((const MsObjBigInt*)b->as.obj, a->as.i);
      c = -c;
    } else {
      c = msObjBigintCmpF64((const MsObjBigInt*)b->as.obj, a->as.f);
      if (c == 2) {
        return false;
      }
      c = -c;
    }
  }
  *outCmp = c;
  return true;
}
```

3.3.3 `valueEqualDepth` 数值分支（148-156）替换：

```c
  if (isNumeric(a) && isNumeric(b)) {  // 数值塔：int/float/bigint 数值互比（02 §6）；bool 不参与
    int32_t cmp = 0;
    return numericCmp(a, b, &cmp) && cmp == 0;  // NaN 无序 → 不等
  }
```

3.3.4 `msValueOrder`（196-213）数值段替换（string 分支保留）：

```c
bool msValueOrder(const MsValue* a, const MsValue* b, int32_t* outCmp) {
  if (isNumeric(a) && isNumeric(b)) {
    return numericCmp(a, b, outCmp);  // NaN → 不可比较（调用方 TypeError）
  }
  if (a->kind == MS_VAL_OBJ && b->kind == MS_VAL_OBJ && a->as.obj->type == MS_OBJ_STRING &&
      b->as.obj->type == MS_OBJ_STRING) {  // string 字典序（22 §3）
    *outCmp = msObjStringCompare((const MsObjString*)a->as.obj, (const MsObjString*)b->as.obj);
    return true;
  }
  return false;  // 其余组合不可比较
}
```

3.3.5 `msValueTypeName` MS_VAL_OBJ switch（229-242）在 `case MS_OBJ_STRING:` 后加：

```c
        case MS_OBJ_BIGINT:  // T016
          return "bigint";
```

3.3.6 `strFormDepth`（528-547）与 `reprFormDepth`（553-578）各自在 string 分支后加（str/repr 同形：十进制无 `n` 后缀，02 §3.2）：

```c
    if (v->as.obj->type == MS_OBJ_BIGINT) {  // T016：v 由调用方持有处 root；toString 内部末端才分配
      return msObjBigintToString(vm, (const MsObjBigInt*)v->as.obj, 10);
    }
```

3.3.7 `msValueFromConst` CONST_BIGINT（654-656）替换：

```c
    case CONST_BIGINT: {
      MsObjBigInt* b = NULL;
      if (msObjBigintParse(vm, e->as.text.start, e->as.text.len, 0, &b) != MS_BIG_OK) {
        *okOut = false;  // 字面量已过扫描器与 parser 剥缀，理论不可达；防御路径
        return msValueNil();
      }
      return msValueObj(&b->head);  // 每次 LOAD_CONST 现解析（常量池 root 化归后续任务，门 T044）
    }
```

- [ ] **Step 3.4: `src/obj/obj_map.c`——数值塔 hash 回填**

include 组加 `#include "obj/obj_bigint.h"`。删除 26-35 行静态 `hashNumeric` 与 37-38 行精度注记。`msValueHash` 内：

```c
    case MS_VAL_I64:
      *out = msHashNumericI64(v->as.i);  // 数值塔归一（T016，02 §7：hash(1) == hash(1n) == hash(1.0)）
      return MS_HASH_OK;
    case MS_VAL_F64:
      *out = msHashNumericF64(v->as.f);
      return MS_HASH_OK;
```

MS_VAL_OBJ switch 内 `case MS_OBJ_STRING:` 前加：

```c
        case MS_OBJ_BIGINT:  // T016：数值塔可 hash 键
          *out = msHashNumericBigint((const MsObjBigInt*)v->as.obj);
          return MS_HASH_OK;
```

default 注释（78 行）改为 `// list/map/set/实例（function T018+）`。本地 `mix64`（17-24）保留（nil/bool 用）。

- [ ] **Step 3.5: `src/scanner/scanner.c/.h`——字面量溢出静默升格**

scanner.c 259-267 的分支改为（删除 MSE_INT_OVERFLOW 诊断发射）：

```c
  if (isFloat) {
    makeToken(s, out, TK_FLOAT, start, line, col);
  } else if (isBig || overflow || value > (uint64_t)INT64_MAX) {
    makeToken(s, out, TK_BIGINT, start, line, col);  // 超 int64 静默升 bigint（T016 精确解析在 const 装载）
  } else {
    makeToken(s, out, TK_INT, start, line, col);
  }
```

同时更新两处注释：scanDigits 头注释（101-102）"溢出 uint64 时置 *overflow 并保持近似值（值不落 token——BIGINT 文本在 T016 const 装载精确解析）"；scanNumber 头注释（185-186）去掉"溢出升 BIGINT 由 T016 接管"改为"溢出升 BIGINT（T016：const 装载精确解析）"。

scanner.h 15 行注释改为（枚举值保留不重编号）：

```c
  MSE_INT_OVERFLOW = 3,  // retired in T016：字面量超 int64 静默升 bigint（枚举位保留防码漂移）
```

- [ ] **Step 3.6: 更新受影响既有测试**

1. `tests/unit/test_scanner_core.c` 169 行所在 TEST（读 150-185 行上下文）：由"溢出字面量报 MSE_INT_OVERFLOW"改为"静默 TK_BIGINT、0 诊断"——按原测试结构改断言：token 类型 `TK_BIGINT`、`diags.count == 0`。
2. `tests/unit/test_vm_basic.c` 209-214 `runIntOverflowPlaceholder`（先读 195-220 上下文确认断言形态）：改名 `runIntOverflowPromotes`，期望 `hasError == false`、输出 `"9223372036854775808\n"`。
3. 全库检索 NaN 有序比较断言：`rg -n "nan|NaN" tests/`——若既有用例断言 `x <= nan` 之类旧行为，更新为 TypeError 期望（本步内提交说明）。

- [ ] **Step 3.7: 追加 VM 级 runSrc 测试（test_obj_bigint.c）**

```c
// ---------- VM 级（Task 3）：混算表 / TypeError / 提升 / 词法衔接 ----------

TEST(vmOverflowPromote) {  // DoD：max + 1 → bigint；字面量静默升格
  RunResult r = runSrc("var a = 9223372036854775807\nvar b = 1\nprint(a + b)");
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "9223372036854775808\n", r.out);
  r = runSrc("print(9223372036854775808)");  // 无 n 后缀：静默升 bigint（不再 MSE_INT_OVERFLOW）
  EXPECT(ctx, r.compileOk);
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "9223372036854775808\n", r.out);
  r = runSrc("print(type(9223372036854775807 * 2))");
  EXPECT_STREQ(ctx, "bigint\n", r.out);
  freeRunResult(&r);
}
```

（`freeRunResult`：若复制自 test_obj_map.c 的形态不含该助手，直接对每个 `r.out` free；统一在文件内新增 `static void freeRunResult(RunResult* r) { free(r->out); }` 并在各 TEST 末尾调用。）

```c
TEST(vmMixedArithTable) {  // 02 §3.4 混算表
  RunResult r = runSrc("print(1 + 1n)\nprint(type(1 + 1n))\nprint(1n + 0.5)\nprint(1 == 1n)\nprint(1n == 1.0)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "2\nbigint\n1.5\ntrue\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(2 ** 64 == 1n << 64)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\n", r.out);
  freeRunResult(&r);
}

TEST(vmDivModGoSemantics) {  // 02 §3.5
  RunResult r = runSrc("print(-7n / 2n)\nprint(-7n % 2n)\nprint(7n / -2n)\nprint(7n % -2n)\nprint(5n / 2n)\nprint(5n % 2n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-3\n-1\n-3\n1\n2\n1\n", r.out);
  freeRunResult(&r);
}

TEST(vmBitopDomain) {  // 02 §3.1：float 拒绝；bigint 域与超宽左移
  RunResult r = runSrc("print(1.5 & 1)");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "TypeError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(1 << 100)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "1267650600228229401496703205376\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(-3n >> 1)\nprint(~5n)");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "-2\n-6\n", r.out);
  freeRunResult(&r);
}
```

`main` 注册四个新 TEST。

- [ ] **Step 3.8: 构建 + 全量测试**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
预期：116 项既有全部通过（除按 3.6 更新者）+ 新增 `obj_bigint`（现含 4+4 用例）。

- [ ] **Step 3.9: Commit**

```powershell
git add src/plat/plat_math.h src/vm/vm.c src/vm/value.c src/obj/obj_map.c src/scanner/scanner.c src/scanner/scanner.h tests/unit/test_obj_bigint.c tests/unit/test_vm_basic.c tests/unit/test_scanner_core.c
git commit -m "✨ feat(vm): numeric tower unification with bigint overflow promotion"
```

---

### Task 4: 内置转换函数与数值方法

**Files:**
- Modify: `src/vm/builtins_bridge.c`、`src/vm/vm.h`、`src/vm/vm.c`
- Create: `src/vm/num_methods.c`
- Modify: `CMakeLists.txt`、`tests/unit/test_obj_bigint.c`

- [ ] **Step 4.1: 写失败测试（test_obj_bigint.c 追加）**

```c
// ---------- 内置与方法（Task 4，10 §1/§4） ----------

TEST(vmConversions) {
  RunResult r = runSrc("print(int(\"ff\", 16))\nprint(int(1.9))\nprint(int(-1.9))\nprint(int(2n))\nprint(float(\"1.5\"))\nprint(bigint(\"123\") * 2)\nprint(bigint(1e3) + 1n)\nprint(abs(-2n))\nprint(abs(-5))\nprint(int(1e19))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "255\n1\n-1\n2\n1.5\n246\n1001\n2\n5\n10000000000000000000\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(int(\"1e3\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(bigint(\"1e3\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(bigint(1.5))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
  r = runSrc("print(float(\"x\"))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
}

TEST(vmNumericMethods) {  // 10 §4：toString([base]) / char / pow / hash
  RunResult r = runSrc("print(255n.toString(16))\nprint(255.toString(16))\nprint(255n.toString(2))\nprint(int(255n.toString(8), 8))\nprint(2n.pow(10))\nprint(2.pow(10))\nprint(65.char())");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "ff\nff\n11111111\n255\n1024\n1024\nA\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(hash(1) == hash(1n))\nprint(hash(1n) == hash(1.0))");
  EXPECT(ctx, !r.hasError);
  EXPECT_STREQ(ctx, "true\ntrue\n", r.out);
  freeRunResult(&r);
  r = runSrc("print(255n.toString(3))");
  EXPECT(ctx, r.hasError);
  EXPECT(ctx, strstr(r.err, "ValueError") != NULL);
  freeRunResult(&r);
}
```

- [ ] **Step 4.2: 构建，确认失败（int/float/bigint/hash/abs 未定义行为：undefined name）**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug -R obj_bigint --output-on-failure`
预期：`vmConversions`/`vmNumericMethods` 失败（`undefined name 'int'`）。

- [ ] **Step 4.3: 实现 builtins（builtins_bridge.c）**

include 组加 `#include <math.h>` 与 `#include "obj/obj_bigint.h"`。文件内追加（`msBuiltinLookup` 之前）：

```c
// —— 数值转换族（T016，10 §1）：int/float/bigint/bool/hash/abs ——
// int()：float 截断 / string 按 base（失败 ValueError）/ bigint 可容则降、不容保持 bigint
// （int 域闭合于 i64，超界升格与字面量同策略，02 §3.1）
static MsValue nativeInt(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    bridgeErr(vm, "ArgumentError: int expects 1-2 arguments");
    return msValueNil();
  }
  if (argc == 2 && args[1].kind != MS_VAL_I64) {
    bridgeErr(vm, "TypeError: int base must be int");
    return msValueNil();
  }
  ms_i64 base = argc == 2 ? args[1].as.i : 10;
  if (base != 0 && (base < 2 || base > 36)) {
    bridgeErr(vm, "ValueError: int base must be 0 or in 2-36");
    return msValueNil();
  }
  const MsValue* x = &args[0];
  switch (x->kind) {
    case MS_VAL_BOOL:
      return msValueInt(x->as.b ? 1 : 0);
    case MS_VAL_I64:
      return *x;
    case MS_VAL_F64: {
      double f = x->as.f;
      if (isnan(f)) {
        bridgeErr(vm, "ValueError: cannot convert float NaN to integer");
        return msValueNil();
      }
      if (isinf(f)) {
        bridgeErr(vm, "OverflowError: cannot convert float infinity to integer");
        return msValueNil();
      }
      double t = trunc(f);
      if (t >= -9223372036854775808.0 && t < 9223372036854775808.0) {
        return msValueInt((ms_i64)t);
      }
      int e2 = 0;  // 超界：frexp 精确分解 mant·2^k 升 bigint
      double frac = frexp(fabs(t), &e2);
      ms_i64 mant = (ms_i64)(frac * 9007199254740992.0);
      int64_t k = (int64_t)e2 - 53;
      while (k > 0 && (mant & 1) == 0) {  // 压缩移位量
        mant >>= 1;
        k--;
      }
      MsBigView v;
      uint32_t buf[2];
      msBigViewI64(&v, buf, t < 0 ? -mant : mant);
      MsObjBigInt* b = NULL;
      if (k < 0 || msBigShl(vm, &v, k, &b) != MS_BIG_OK) {  // t 已截断必整：k < 0 不可达防御
        bridgeErr(vm, "OverflowError: cannot convert float to integer");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_OBJ:
      if (x->as.obj->type == MS_OBJ_BIGINT) {
        ms_i64 iv = 0;
        if (msObjBigintFitsI64((const MsObjBigInt*)x->as.obj, &iv)) {
          return msValueInt(iv);
        }
        return *x;  // 超界保持 bigint（升格不回降）
      }
      if (x->as.obj->type == MS_OBJ_STRING) {
        const MsObjString* s = (const MsObjString*)x->as.obj;
        MsObjBigInt* b = NULL;
        MsBigErr e = msObjBigintParse(vm, s->bytes, (int32_t)s->byteLen, (int)base, &b);
        if (e == MS_BIG_PARSE) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for int with base %lld: '%.*s'",
                   (long long)base, (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        ms_i64 iv = 0;
        if (msObjBigintFitsI64(b, &iv)) {
          return msValueInt(iv);
        }
        return msValueObj(&b->head);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: int() cannot convert the given type");
  return msValueNil();
}
```

（其余 native 函数体见下方；随后 `msBuiltinLookup` 增加六项。）

```c
static MsValue nativeFloat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: float expects 1 argument");
    return msValueNil();
  }
  switch (args[0].kind) {
    case MS_VAL_BOOL:
      return msValueFloat(args[0].as.b ? 1.0 : 0.0);
    case MS_VAL_I64:
      return msValueFloat((double)args[0].as.i);
    case MS_VAL_F64:
      return args[0];
    case MS_VAL_OBJ:
      if (args[0].as.obj->type == MS_OBJ_BIGINT) {  // 精度损失不报错（02 §3.3）
        return msValueFloat(msObjBigintToF64((const MsObjBigInt*)args[0].as.obj));
      }
      if (args[0].as.obj->type == MS_OBJ_STRING) {  // strtod 严格全消费
        const MsObjString* s = (const MsObjString*)args[0].as.obj;
        char* tmp = (char*)malloc((size_t)s->byteLen + 1);
        if (tmp == NULL) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        memcpy(tmp, s->bytes, s->byteLen);
        tmp[s->byteLen] = '\0';
        char* end = NULL;
        double d = strtod(tmp, &end);
        bool ok = end != tmp && *end == '\0';
        free(tmp);
        if (!ok) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for float: '%.*s'",
                   (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        return msValueFloat(d);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: float() cannot convert the given type");
  return msValueNil();
}

static MsValue nativeBigint(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: bigint expects 1 argument");
    return msValueNil();
  }
  switch (args[0].kind) {
    case MS_VAL_I64: {
      MsObjBigInt* b = msObjBigintFromI64(vm, args[0].as.i);
      if (b == NULL) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_F64: {
      double f = args[0].as.f;
      if (isnan(f) || isinf(f) || f != floor(f)) {  // 须整（10 §1）
        char buf[64];
        msValueToStr(&args[0], buf, sizeof(buf));
        snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: cannot convert float %s to bigint", buf);
        vm->hasError = true;
        return msValueNil();
      }
      if (f >= -9223372036854775808.0 && f < 9223372036854775808.0) {
        return msValueObj((MsObj*)msObjBigintFromI64(vm, (ms_i64)f));
      }
      int e2 = 0;  // 大值精确分解（同 nativeInt）
      double frac = frexp(fabs(f), &e2);
      ms_i64 mant = (ms_i64)(frac * 9007199254740992.0);
      int64_t k = (int64_t)e2 - 53;
      while (k > 0 && (mant & 1) == 0) {
        mant >>= 1;
        k--;
      }
      MsBigView v;
      uint32_t buf[2];
      msBigViewI64(&v, buf, f < 0 ? -mant : mant);
      MsObjBigInt* b = NULL;
      if (k < 0 || msBigShl(vm, &v, k, &b) != MS_BIG_OK) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    case MS_VAL_OBJ:
      if (args[0].as.obj->type == MS_OBJ_BIGINT) {
        return args[0];
      }
      if (args[0].as.obj->type == MS_OBJ_STRING) {  // 严格十进制（不识别前缀，同 Python int(s)）
        const MsObjString* s = (const MsObjString*)args[0].as.obj;
        MsObjBigInt* b = NULL;
        MsBigErr e = msObjBigintParse(vm, s->bytes, (int32_t)s->byteLen, 10, &b);
        if (e == MS_BIG_PARSE) {
          snprintf(vm->errMsg, sizeof(vm->errMsg), "ValueError: invalid literal for bigint: '%.*s'",
                   (int)s->byteLen, s->bytes);
          vm->hasError = true;
          return msValueNil();
        }
        if (e != MS_BIG_OK) {
          bridgeErr(vm, "RuntimeError: out of memory");
          return msValueNil();
        }
        return msValueObj(&b->head);
      }
      break;
    default:
      break;
  }
  bridgeErr(vm, "TypeError: bigint() cannot convert the given type");
  return msValueNil();
}
```

**注**：`msObjBigintFromI64` 失败返回 NULL 须门控——nativeBigint 的 I64 分支改为：

```c
    case MS_VAL_I64: {
      MsObjBigInt* b = msObjBigintFromI64(vm, args[0].as.i);
      if (b == NULL) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
```

**注**：bigint/float 分支里的 `msBigShl` 与 `msObjBigintFromI64` 均为分配点——实参在 OP_CALL 栈 root，跨分配安全。

其余三个简单内置与注册项：

```c
static MsValue nativeBool(MsVM* vm, int32_t argc, const MsValue* args) {  // 真值规则（02 §2）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: bool expects 1 argument");
    return msValueNil();
  }
  return msValueBool(msValueIsTruthy(&args[0]));
}

static MsValue nativeHash(MsVM* vm, int32_t argc, const MsValue* args) {  // 数值塔归一（02 §7）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: hash expects 1 argument");
    return msValueNil();
  }
  ms_u64 h = 0;
  if (msValueHash(&args[0], &h) != MS_HASH_OK) {
    snprintf(vm->errMsg, sizeof(vm->errMsg), "TypeError: unhashable type: '%s'", msValueTypeName(&args[0]));
    vm->hasError = true;
    return msValueNil();
  }
  return msValueInt((ms_i64)h);
}

static MsValue nativeAbs(MsVM* vm, int32_t argc, const MsValue* args) {  // 保类型（10 §1）
  if (argc != 1) {
    bridgeErr(vm, "ArgumentError: abs expects 1 argument");
    return msValueNil();
  }
  const MsValue* x = &args[0];
  if (x->kind == MS_VAL_I64) {
    if (x->as.i == INT64_MIN) {  // |INT64_MIN| = 2^63 升 bigint
      uint32_t limbs[2] = {0, 0x80000000u};
      MsObjBigInt* b = msObjBigintNew(vm, limbs, 2, 1);
      if (b == NULL) {
        bridgeErr(vm, "RuntimeError: out of memory");
        return msValueNil();
      }
      return msValueObj(&b->head);
    }
    return msValueInt(x->as.i < 0 ? -x->as.i : x->as.i);
  }
  if (x->kind == MS_VAL_F64) {
    return msValueFloat(fabs(x->as.f));
  }
  if (x->kind == MS_VAL_OBJ && x->as.obj->type == MS_OBJ_BIGINT) {  // 实参栈 root：New 跨分配安全
    const MsObjBigInt* b = (const MsObjBigInt*)x->as.obj;
    MsObjBigInt* r = msObjBigintNew(vm, b->limbs, b->len, 1);
    if (r == NULL) {
      bridgeErr(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&r->head);
  }
  bridgeErr(vm, "TypeError: abs() expects an int, bigint or float");
  return msValueNil();
}
```

`msBuiltinLookup` 内按现有形态追加六项（与 `print` 同型）：

```c
  if (len == 3 && memcmp(name, "int", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeInt;
    return true;
  }
  if (len == 5 && memcmp(name, "float", 5) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeFloat;
    return true;
  }
  if (len == 6 && memcmp(name, "bigint", 6) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeBigint;
    return true;
  }
  if (len == 4 && memcmp(name, "bool", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeBool;
    return true;
  }
  if (len == 4 && memcmp(name, "hash", 4) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeHash;
    return true;
  }
  if (len == 3 && memcmp(name, "abs", 3) == 0) {
    out->kind = MS_VAL_NATIVE;
    out->as.native = nativeAbs;
    return true;
  }
```

- [ ] **Step 4.4: 实现 `src/vm/num_methods.c`（数值实例方法表；结构照 `src/vm/tuple_methods.c`）**

先读 `src/vm/tuple_methods.c` 全文，按其 `methodError` 助手、表结构与 lookup 形态写：

```c
// int/bigint 实例方法表（T016，10 §4）：toString([base]) / char / pow
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

#include "obj/obj_bigint.h"
#include "obj/obj_string.h"

// methodError：照抄 tuple_methods.c 的 vsnprintf 错误槽助手（同名同型）

// i64 → base 字符串（2/8/10/16；负数 '-'）
static MsValue intToStringBase(MsVM* vm, ms_i64 v, ms_i64 base) {
  char buf[80];
  int32_t n = 0;
  if (base == 10) {
    n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
  } else {
    ms_u64 m = v < 0 ? (ms_u64)(-(v + 1)) + 1 : (ms_u64)v;
    const char* digits = "0123456789abcdef";
    do {
      buf[n++] = digits[m % (ms_u64)base];
      m /= (ms_u64)base;
    } while (m != 0);
    if (v < 0) {
      buf[n++] = '-';
    }
    for (int32_t x = 0, y = n - 1; x < y; x++, y--) {
      char t = buf[x];
      buf[x] = buf[y];
      buf[y] = t;
    }
  }
  MsObjString* s = msObjStringNew(vm, buf, n);
  if (s == NULL) {
    return msValueNil();  // OOM：调用方 hasError 门控（照 tuple_methods 形态置槽）
  }
  return msValueObj(&s->head);
}

static MsValue numToString(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 1 || argc > 2) {
    // methodError(vm, "ArgumentError: toString expects 0-1 arguments") 同型
    return msValueNil();
  }
  ms_i64 base = 10;
  if (argc == 2) {
    if (args[1].kind != MS_VAL_I64) {
      // methodError "TypeError: toString base must be int"
      return msValueNil();
    }
    base = args[1].as.i;
  }
  if (base != 2 && base != 8 && base != 10 && base != 16) {
    // methodError "ValueError: toString base must be one of 2, 8, 10 or 16"
    return msValueNil();
  }
  if (args[0].kind == MS_VAL_I64) {
    return intToStringBase(vm, args[0].as.i, base);
  }
  MsObjString* s = msObjBigintToString(vm, (const MsObjBigInt*)args[0].as.obj, (int)base);
  if (s == NULL) {
    // methodError "RuntimeError: out of memory"
    return msValueNil();
  }
  return msValueObj(&s->head);
}

static MsValue numChar(MsVM* vm, int32_t argc, const MsValue* args) {  // 合法码点 → 单字符 string
  if (argc != 1) {
    // methodError "ArgumentError: char expects 0 arguments"
    return msValueNil();
  }
  if (args[0].kind != MS_VAL_I64) {
    // methodError "TypeError: char requires an int receiver"
    return msValueNil();
  }
  ms_i64 cp = args[0].as.i;
  if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    // methodError "ValueError: invalid codepoint"
    return msValueNil();
  }
  char buf[4];
  int32_t n = 0;
  ms_u32 c = (ms_u32)cp;
  if (c < 0x80) {
    buf[n++] = (char)c;
  } else if (c < 0x800) {
    buf[n++] = (char)(0xC0 | (c >> 6));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    buf[n++] = (char)(0xE0 | (c >> 12));
    buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  } else {
    buf[n++] = (char)(0xF0 | (c >> 18));
    buf[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
    buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  }
  MsObjString* s = msObjStringNew(vm, buf, n);
  if (s == NULL) {
    return msValueNil();
  }
  return msValueObj(&s->head);
}

static MsValue numPow(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    // methodError "ArgumentError: pow expects 1 argument"
    return msValueNil();
  }
  const MsValue* b = &args[0];
  const MsValue* e = &args[1];
  ms_i64 ev = 0;
  if (e->kind == MS_VAL_I64) {
    ev = e->as.i;
  } else if (e->kind == MS_VAL_OBJ && e->as.obj->type == MS_OBJ_BIGINT) {
    if (!msObjBigintFitsI64((const MsObjBigInt*)e->as.obj, &ev)) {
      // methodError "ValueError: pow exponent must be a non-negative int"
      return msValueNil();
    }
  } else {
    // methodError "TypeError: pow exponent must be int or bigint"
    return msValueNil();
  }
  if (ev < 0) {  // 负指数浮点幂（与运算符一致，02 §3）
    double base = b->kind == MS_VAL_I64 ? (double)b->as.i : msObjBigintToF64((const MsObjBigInt*)b->as.obj);
    return msValueFloat(pow(base, (double)ev));
  }
  MsBigView v;
  uint32_t buf[2];
  if (b->kind == MS_VAL_I64) {
    msBigViewI64(&v, buf, b->as.i);
  } else {
    msBigViewOf(&v, (const MsObjBigInt*)b->as.obj);
  }
  MsObjBigInt* r = NULL;
  MsBigErr err = msBigPow(vm, &v, ev, &r);
  if (err == MS_BIG_RANGE) {
    // methodError "OverflowError: bigint result too large"
    return msValueNil();
  }
  if (err != MS_BIG_OK) {
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static const MsMethodRef kNumMethods[] = {
    {numToString, "toString", 8},
    {numChar, "char", 4},
    {numPow, "pow", 3},
};

bool msNumMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kNumMethods) / sizeof(kNumMethods[0]); i++) {
    if (kNumMethods[i].nameLen == len && memcmp(name, kNumMethods[i].name, (size_t)len) == 0) {
      *out = kNumMethods[i];
      return true;
    }
  }
  return false;
}
```

（注释处的 `// methodError ...` 均为实际调用 `methodError(vm, "...")`——照抄 tuple_methods.c 的助手实现，此处不再重复。）

- [ ] **Step 4.5: 接线**

`src/vm/vm.h` 在 `msTupleMethodLookup` 声明后加：

```c
// num_methods.c：int/bigint 实例方法表（T016，10 §4）
bool msNumMethodLookup(const char* name, int32_t len, MsMethodRef* out);
```

`src/vm/vm.c` `methodRefLookup`（37-54）在 tuple 分支后加：

```c
  if (recv->kind == MS_VAL_I64 || (recv->kind == MS_VAL_OBJ && recv->as.obj->type == MS_OBJ_BIGINT)) {
    return msNumMethodLookup(name, len, out) ? 0 : -1;
  }
```

`CMakeLists.txt` ms_vm 的 `target_sources` 区（95-100 行段）追加：

```cmake
target_sources(ms_vm PRIVATE src/vm/num_methods.c)  # T016：int/bigint 方法表
```

- [ ] **Step 4.6: 构建 + 测试**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
预期：全绿（含 vmConversions / vmNumericMethods）。

- [ ] **Step 4.7: Commit**

```powershell
git add src/vm/builtins_bridge.c src/vm/num_methods.c src/vm/vm.h src/vm/vm.c CMakeLists.txt tests/unit/test_obj_bigint.c
git commit -m "✨ feat(vm/builtins): int/float/bigint/bool/hash/abs and numeric methods"
```

---

### Task 5: ms fixtures（numeric/ 端到端 + arith.ms 激活溢出用例）

**Files:**
- Create: `tests/fixtures/numeric/`（8 个 .ms + golden）
- Modify: `tests/fixtures/vm-basic/arith.ms`、`arith.out`

fixtures 由 `cmake/Tests.cmake` GLOB 自动注册，无需 CMake 改动。先读 `tests/fixtures/vm-basic/divzero.ms` 与其 `.err`/`.code` 确认错误用例文件格式。

- [ ] **Step 5.1: `overflow_promote.ms` / `.out`（DoD：溢出提升 + 字面量升格）**

```ms
// T016 溢出提升（02 §3.1）：int 运算溢出自动升 bigint，不报错
var big = 9223372036854775807 + 1
print(type(big))
print(big)
print(9223372036854775807n * 2n + 1)
print(9223372036854775808)
print(-9223372036854775807 - 2)
print(4611686018427387904 * 2)
```

`.out`：

```text
bigint
9223372036854775808
18446744073709551615
9223372036854775808
-9223372036854775809
9223372036854775808
```

- [ ] **Step 5.2: `factorial.ms` / `.out`（DoD：100! 精确值）**

```ms
// T016 阶乘 100 精确值（DoD）
var f = 1n
for i in range(2, 101) {
    f = f * i
}
print(f)
```

`.out`（单行 158 位）：

```text
93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000
```

- [ ] **Step 5.3: `fibonacci.ms` / `.out`（DoD：fib(500) 精确值）**

```ms
// T016 fib(500) 精确值（DoD）
var a = 0n
var b = 1n
for i in range(1, 501) {
    var t = a + b
    a = b
    b = t
}
print(a)
```

`.out`（单行，Python 生成值）：

```text
139423224561697880139724382870407283950070256587697307264108962948325571622863290691557658876222521294125
```

- [ ] **Step 5.4: `mixed.ms` / `.out`（02 §3.4 混算表逐格 + 02 §6/§7 归一）**

```ms
// T016 混算表逐格（02 §3.4）+ 相等/hash 归一（02 §6/§7）
print(1 + 1n)
print(1n + 1)
print(1n + 1n)
print(1 + 0.5)
print(1n + 0.5)
print(0.5 + 1n)
print(1 == 1n)
print(1n == 1.0)
print(1 == 1.0)
print(hash(1) == hash(1n))
print(hash(1n) == hash(1.0))
print(type(1 + 1n))
print(type(1n + 0.5))
print(2n ** 10)
print(2 ** 64 == 1n << 64)
```

`.out`：

```text
2
2
2
1.5
1.5
1.5
true
true
true
true
true
bigint
float
1024
true
```

- [ ] **Step 5.5: `divmod.ms` / `.out`（02 §3.5 Go 语义表：int 与 bigint 一致）**

```ms
// T016 Go 截断除法/模符号（02 §3.5）：int 与 bigint 一致
print(-7 / 2)
print(-7 % 2)
print(7 / -2)
print(7 % -2)
print(-7n / 2n)
print(-7n % 2n)
print(7n / -2n)
print(7n % -2n)
print(5n / 2n)
print(5n % 2n)
print(9223372036854775807 / -1)
print(-9223372036854775807 % 9223372036854775807)
```

`.out`：

```text
-3
-1
-3
1
-3
-1
-3
1
2
1
-9223372036854775807
-9223372036854775807
```

- [ ] **Step 5.6: `bitops.ms` / `.out`（02 §3.1 位运算域）**

```ms
// T016 位运算域（02 §3.1）：int/bigint；超宽左移精确提升
print(255 & 15)
print(255n & 15n)
print(255n | 15n)
print(255n ^ 15n)
print(~5)
print(~5n)
print(-1n >> 10)
print(1 << 4)
print(1n << 100)
print(1 << 100)
print(-3n >> 1)
print(3n.toString(2))
```

`.out`：

```text
15
15
255
240
-6
-6
-1
16
1267650600228229401496703205376
1267650600228229401496703205376
-2
11
```

- [ ] **Step 5.7: `bitops_typeerr.ms` + `.err` + `.code`（float 域拒绝）**

```ms
// T016 位运算 float 拒绝（02 §3.1）
print(1.5 & 1)
```

`.err`：`TypeError: bit operation '&' requires int or bigint operands`；`.code`：`1`（照 divzero 同型）。

- [ ] **Step 5.8: `conversions.ms` / `.out`（10 §1 转换族）**

```ms
// T016 转换族（10 §1）：int/float/bigint/abs/hash
print(int("ff", 16))
print(int("101", 2))
print(int(1.9))
print(int(-1.9))
print(int(2n))
print(float("1.5"))
print(float(3n))
print(bigint("123") * 2)
print(bigint(1e3) + 1n)
print(abs(-2n))
print(abs(-5))
print(abs(-2.5))
print(int(1e19))
```

`.out`：

```text
255
5
1
-1
2
1.5
3.0
246
1001
2
5
2.5
10000000000000000000
```

- [ ] **Step 5.9: 转换错误 fixtures（4 组，`.ms` + `.err` + `.code`）**

| .ms | 内容（单行） | .err |
|-----|------|------|
| `int_err.ms` | `print(int("zz"))` | `ValueError: invalid literal for int with base 10: 'zz'` |
| `float_err.ms` | `print(float("x"))` | `ValueError: invalid literal for float: 'x'` |
| `bigint_err.ms` | `print(bigint("1e3"))` | `ValueError: invalid literal for bigint: '1e3'` |
| `bigint_float_err.ms` | `print(bigint(1.5))` | `ValueError: cannot convert float 1.5 to bigint` |

`.code` 均为 `1`。

- [ ] **Step 5.10: `tostring_base.ms` / `.out`（DoD：base 2/8/16 回解析）**

```ms
// T016 toString base 2/8/10/16 与回解析（10 §4，DoD）
print(255n.toString(2))
print(255n.toString(8))
print(255n.toString(16))
print(255n.toString())
print(255.toString(16))
print(-255n.toString(16))
print(int(255n.toString(2), 2))
print(int(255n.toString(8), 8))
print(int(255n.toString(16), 16))
print(int(-255n.toString(16), 16))
```

`.out`：

```text
11111111
377
ff
255
ff
-ff
255
255
255
-255
```

- [ ] **Step 5.11: `arith.ms` 激活溢出用例（任务书 §3"激活"项）**

`tests/fixtures/vm-basic/arith.ms` 末尾追加一行 `print(9223372036854775807 + 1)`；`arith.out` 末尾追加对应行 `9223372036854775808`。

- [ ] **Step 5.12: 重新配置 + 全量测试**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
预期：fixture_numeric_*（约 13 项）新增全绿 + 既有全绿。

- [ ] **Step 5.13: Commit**

```powershell
git add tests/fixtures/numeric tests/fixtures/vm-basic/arith.ms tests/fixtures/vm-basic/arith.out
git commit -m "🧪 test(fixtures/numeric): bigint and numeric tower end-to-end cases"
```

（若仓库 git log 无 🧪 先例，用既有测试提交的 gitmoji——`git log --oneline --all | Select-String test` 确认后统一。）

---

### Task 6: 文档状态、任务索引与全量验证

**Files:**
- Modify: `docs/tasks/T016-bigint-numerics.md`、`docs/tasks/README.md`

- [ ] **Step 6.1: 更新任务文档**

`docs/tasks/T016-bigint-numerics.md`：头部 `- 状态: 未开始` 改 `- 状态: 已完成`（照 T015 完成态格式：先读 `docs/tasks/T015-map-set-tuple.md` 头部对齐）；§4 DoD 全部 `- [ ]` 改 `- [x]`；§6 末尾追加一行备注：实现偏差见 `docs/superpowers/plans/2026-10-28-t016-bigint-numerics.md` 开篇清单（FAM 单块分配替代 gc.c 子分配、MSVC 溢出内建记载勘误等）。

`docs/tasks/README.md` 总表 T016 行 `⬜` 改 `✅`。

- [ ] **Step 6.2: 全量重建与验证**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

预期：100% 通过（既有 116 + obj_bigint 单测 + fixture_numeric_* 等新增）。

- [ ] **Step 6.3: 文件卫生检查**

`git diff --check`（行尾空白）；确认新文件均 LF/UTF-8 无 BOM（`git ls-files --eol` 抽查新文件）；无 `/* */` 注释、无 Tab（`rg -n "/\*|\t" src/obj/obj_bigint.c src/obj/obj_bigint.h src/vm/num_methods.c src/plat/plat_math.h` 应无输出）。

- [ ] **Step 6.4: Commit**

```powershell
git add docs/tasks/T016-bigint-numerics.md docs/tasks/README.md
git commit -m "📝 docs(tasks): mark T016 done"
```

- [ ] **Step 6.5: DoD 对照自查（完成声明前）**

- [ ] 02 §3 全部规则用例通过（mixed/divmod/bitops fixtures + 单测混算表）
- [ ] `9223372036854775807 + 1` → `9223372036854775808`（overflow_promote + vmOverflowPromote）
- [ ] 阶乘 100、fib 500 精确值正确（factorial/fibonacci fixtures golden）
- [ ] Karatsuba 与朴素乘阈值两侧一致（bigintMulOracle 200 组跨阈值）
- [ ] `int("ff",16)` / `bigint("1e3")`（非法）/ `float("1.5")` 全过（conversions + 4 err fixtures）
- [ ] toString base 2/8/16 回解析（tostring_base fixture）
- [ ] `-7n/2n == -3n`、`-7n%2n == -1n`、`1+1n == 2n`、`1n+0.5 == 1.5`、`1.5&1` TypeError、hash 三相等（单测 + fixtures）

---

## Self-Review 记录

- 规格覆盖：T016 §1 全部要点（表示/四则/比较/幂/Karatsuba/toString/解析/溢出提升/混算表/除模 Go 语义/位运算/转换/hash 回填/词法衔接）均有对应 Step；§3 涉及文件 gc.c 经偏差 1 论证免除。
- 类型一致性：`MsBigView/msBigViewI64/msBigViewOf`、`MsBigErr` 五值、`msObjBigint*` 命名全程一致；错误文案在 fixtures `.err` 与实现代码逐字对齐。
- 已知简化（显式声明，非遗漏）：数值常量无缓存（每次 LOAD_CONST 解析）；Karatsuba 非均衡输入回退朴素；`int()` 超 i64 返回 bigint。


















