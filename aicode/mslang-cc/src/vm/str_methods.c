// string 方法族（T013，10 §2）：args[0] 恒为接收者（D5）；chars/bytes 产 MsObjStrIter（Task 12）
// 错误约定：置 vm->hasError + errMsg 后返回 nil（OP_CALL_METHOD 检槽统一 unwind）
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "obj/obj_list.h"
#include "obj/obj_string.h"
#include "obj/unicode_case.h"
#include "scanner/unicode_table.h"
#include "vm/vm.h"

// ---- 公共助手 ----

static void methodError(MsVM* vm, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm->errMsg, sizeof(vm->errMsg), fmt, ap);
  va_end(ap);
  vm->hasError = true;
}

// 接收者（CALL_METHOD/GET_ATTR 已保证 string，无需再验；惰性 runeIndex 需可写）
static MsObjString* recvStr(const MsValue* args) {
  return (MsObjString*)args[0].as.obj;
}

// args[i] 须为 string 否则 ArgumentError 置槽（调用方先完成元数检查）
static bool argStr(MsVM* vm, int32_t i, const MsValue* args, const char* mname, const MsObjString** out) {
  if (!msIsString(&args[i])) {
    methodError(vm, "ArgumentError: %s expects a string argument", mname);
    return false;
  }
  *out = (const MsObjString*)args[i].as.obj;
  return true;
}

static bool argInt(MsVM* vm, int32_t i, const MsValue* args, const char* mname, ms_i64* out) {
  if (args[i].kind != MS_VAL_I64) {
    methodError(vm, "ArgumentError: %s expects an int argument", mname);
    return false;
  }
  *out = args[i].as.i;
  return true;
}

// 字节子串搜索（Windows 无 memmem）：h[from..hn) 内找 n[0..nn)，命中返回起点指针；
// nn==0 时 from ≤ hn 即命中（空串语义：contains/indexOf 空 sub 自匹配）
static const char* findBytes(const char* h, int32_t hn, const char* n, int32_t nn, int32_t from) {
  for (int32_t i = from; (ms_i64)i + nn <= hn; i++) {
    if (nn == 0 || (h[i] == n[0] && memcmp(h + i, n, (size_t)nn) == 0)) {
      return h + i;
    }
  }
  return NULL;
}

// ---- 动态字节缓冲（toUpper/replace/join/pad 结果拼装；malloc 非 GC 分配点） ----

typedef struct ByteBuf {
  char* data;
  int32_t len;
  int32_t cap;
} ByteBuf;

static bool bufAppend(ByteBuf* b, const char* p, int32_t n) {
  if (n < 0 || b->len > INT32_MAX - n) {
    return false;  // 长度域溢出视同失败
  }
  if (b->len + n > b->cap) {
    ms_i64 cap = b->cap == 0 ? 16 : b->cap;
    while (cap < (ms_i64)b->len + n) {
      cap *= 2;
    }
    if (cap > INT32_MAX) {
      return false;
    }
    char* data = realloc(b->data, (size_t)cap);
    if (data == NULL) {
      return false;
    }
    b->data = data;
    b->cap = (int32_t)cap;
  }
  if (n > 0) {
    memcpy(b->data + b->len, p, (size_t)n);
  }
  b->len += n;
  return true;
}

// 缓冲落串：成功清空 b 返回新串；失败置 OOM 错误槽返回 NULL
static MsObjString* bufToString(MsVM* vm, ByteBuf* b) {
  MsObjString* r = b->data == NULL ? msObjStringNew(vm, "", 0) : msObjStringNew(vm, b->data, b->len);
  free(b->data);
  b->data = NULL;
  b->len = b->cap = 0;
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
  }
  return r;
}

// ---- split/splitLines 收集（D6 GC 纪律：片段经 VM 栈 root，list 后建） ----

// 片段落 VM 栈 root：分配点 GC 不误收；栈溢出 false 且 hasError 已置
static bool pushPiece(MsVM* vm, const MsObjString* piece) {
  msVmPush(vm, msValueObj((MsObj*)&piece->head));
  return !vm->hasError;
}

// s[start..end) 建片段并 root；失败（OOM/栈溢出）false 且错误槽已置
static bool pushRange(MsVM* vm, const MsObjString* s, int32_t start, int32_t end) {
  MsObjString* piece = msObjStringNew(vm, s->bytes + start, end - start);
  if (piece == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return false;
  }
  return pushPiece(vm, piece);
}

// 栈顶 n 个已 root 片段 → list：共用 msVmCollectStackToList（value.c，T014 抽出）

// ---- rune 层小工具 ----

// 空白集（split 空白模式 / trim 默认 / isSpace 同集，D9：非 ASCII 暂 false）
static bool isWsCp(ms_u32 cp) {
  return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f';
}

static bool isWsByte(char c) { return isWsCp((ms_u8)c); }

// rune 索引 → 字节偏移（调用方已钳 idx ∈ [0, charLen]；ASCII 无表直通）
static int32_t runeToByteOff(MsVM* vm, MsObjString* s, ms_i64 runeIdx) {
  const int32_t* ri = msObjStringRuneIndex(vm, s);
  return ri == NULL ? (int32_t)runeIdx : ri[runeIdx];
}

// 尾端 rune 解码：[k, j) 末 rune 码点与起始 k（坏序列退化 1 字节）
static ms_u32 decodeLastRune(const char* b, int32_t j, int32_t* kOut) {
  int32_t k = j - 1;
  int32_t limit = j - 4 > 0 ? j - 4 : 0;
  while (k > limit && (b[k] & 0xC0) == 0x80) {
    k--;  // 跳过续字节，至多回溯 3
  }
  int32_t w = 0;
  ms_u32 cp = msUtf8Decode(b + k, j - k, &w);
  if (k + w == j) {
    *kOut = k;
    return cp;
  }
  *kOut = j - 1;
  return (ms_u8)b[j - 1];
}

// cp 是否在剪除集（set NULL = 默认空白集；否则线性扫 set 的 rune，trim 只探边缘可接受）
static bool runeInSet(ms_u32 cp, const MsObjString* set) {
  if (set == NULL) {
    return isWsCp(cp);
  }
  int32_t i = 0;
  while (i < (int32_t)set->byteLen) {
    int32_t w = 0;
    if (msUtf8Decode(set->bytes + i, (int32_t)set->byteLen - i, &w) == cp) {
      return true;
    }
    i += w;
  }
  return false;
}

// ---- 方法实现（表序同 10 §2） ----

static MsValue strmCharLen(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: charLen expects no arguments");
    return msValueNil();
  }
  (void)vm;
  return msValueInt((ms_i64)recvStr(args)->charLen);
}

// chars/bytes（Task 12）：产 MsObjStrIter 落栈由调用方持（CALL_METHOD peek 接收者期间分配安全）
static MsValue strmChars(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: chars expects no arguments");
    return msValueNil();
  }
  MsObjStrIter* it = msObjStrIterNew(vm, recvStr(args), false);
  if (it == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

static MsValue strmBytes(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: bytes expects no arguments");
    return msValueNil();
  }
  MsObjStrIter* it = msObjStrIterNew(vm, recvStr(args), true);
  if (it == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&it->head);
}

// 大小写映射主体：逐 rune 查表（未命中/坏序列透传原字节保形）
static MsValue caseMapImpl(MsVM* vm, int32_t argc, const MsValue* args, bool toUpper) {
  const char* mname = toUpper ? "toUpper" : "toLower";
  if (argc != 1) {
    methodError(vm, "ArgumentError: %s expects no arguments", mname);
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  ByteBuf buf = {NULL, 0, 0};
  int32_t i = 0;
  while (i < (int32_t)s->byteLen) {
    int32_t w = 0;
    ms_u32 cp = msUtf8Decode(s->bytes + i, (int32_t)s->byteLen - i, &w);
    ms_u32 m = toUpper ? msUnicodeToUpper(cp) : msUnicodeToLower(cp);
    if (m == cp) {
      if (!bufAppend(&buf, s->bytes + i, w)) {
        goto oom;
      }
    } else {
      char enc[4];
      int32_t ew = msUtf8Encode(m, enc);
      if (!bufAppend(&buf, enc, ew)) {
        goto oom;
      }
    }
    i += w;
  }
  {
    MsObjString* r = bufToString(vm, &buf);
    return r == NULL ? msValueNil() : msValueObj(&r->head);
  }
oom:
  free(buf.data);
  methodError(vm, "RuntimeError: out of memory");
  return msValueNil();
}

static MsValue strmToUpper(MsVM* vm, int32_t argc, const MsValue* args) {
  return caseMapImpl(vm, argc, args, true);
}

static MsValue strmToLower(MsVM* vm, int32_t argc, const MsValue* args) {
  return caseMapImpl(vm, argc, args, false);
}

// 分离 sep 模式：非重叠切至多 n 刀（n<0 不限），末段含剩余 sep 原样（Python maxsplit）
static MsValue splitBySep(MsVM* vm, MsObjString* s, const MsObjString* sep, ms_i64 n) {
  int32_t pos = 0;
  int32_t made = 0;
  int32_t seps = 0;
  while (n < 0 || seps < n) {
    const char* f = findBytes(s->bytes, (int32_t)s->byteLen, sep->bytes, (int32_t)sep->byteLen, pos);
    if (f == NULL) {
      break;
    }
    int32_t off = (int32_t)(f - s->bytes);
    if (!pushRange(vm, s, pos, off)) {
      return msValueNil();
    }
    made++;
    pos = off + (int32_t)sep->byteLen;
    seps++;
  }
  if (!pushRange(vm, s, pos, (int32_t)s->byteLen)) {
    return msValueNil();
  }
  made++;
  return msVmCollectStackToList(vm, made);
}

// 空白模式：空白段聚一刀、首尾空白不产空段；n 到额后余文原样收尾（尾空白保留）
static MsValue splitByWs(MsVM* vm, MsObjString* s, ms_i64 n) {
  int32_t len = (int32_t)s->byteLen;
  int32_t i = 0;
  while (i < len && isWsByte(s->bytes[i])) {
    i++;
  }
  if (i == len) {
    return msVmCollectStackToList(vm, 0);  // 全空白/空串 → []（Python split() 一致）
  }
  int32_t made = 0;
  int32_t seps = 0;
  for (;;) {
    if (n >= 0 && seps == n) {
      if (!pushRange(vm, s, i, len)) {
        return msValueNil();
      }
      made++;
      break;
    }
    int32_t j = i;
    while (j < len && !isWsByte(s->bytes[j])) {
      j++;
    }
    if (!pushRange(vm, s, i, j)) {
      return msValueNil();
    }
    made++;
    int32_t k = j;
    while (k < len && isWsByte(s->bytes[k])) {
      k++;
    }
    if (k == len) {
      break;  // 尾部全空白：收尾不补空段
    }
    seps++;
    i = k;
  }
  return msVmCollectStackToList(vm, made);
}

static MsValue strmSplit(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 2 || argc > 3) {
    methodError(vm, "ArgumentError: split expects 1 or 2 arguments");
    return msValueNil();
  }
  const MsObjString* sep = NULL;
  if (!argStr(vm, 1, args, "split", &sep)) {
    return msValueNil();
  }
  ms_i64 n = -1;
  if (argc == 3 && !argInt(vm, 2, args, "split", &n)) {
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  if (sep->byteLen == 0) {
    return splitByWs(vm, s, n);
  }
  return splitBySep(vm, s, sep, n);
}

static MsValue strmSplitLines(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: splitLines expects no arguments");
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  int32_t len = (int32_t)s->byteLen;
  int32_t made = 0;
  if (len > 0) {
    int32_t pos = 0;
    for (;;) {
      const char* nl = memchr(s->bytes + pos, '\n', (size_t)(len - pos));
      if (nl == NULL) {
        if (!pushRange(vm, s, pos, len)) {
          return msValueNil();
        }
        made++;
        break;
      }
      int32_t off = (int32_t)(nl - s->bytes);
      if (!pushRange(vm, s, pos, off)) {
        return msValueNil();
      }
      made++;
      pos = off + 1;
      if (pos == len) {
        break;  // 尾随 \n 不再补空段；中间空行保留
      }
    }
  }
  return msVmCollectStackToList(vm, made);
}

// join 仅接受 list（D6：沿 data 直读不依赖迭代协议）；元素须全 string
static MsValue strmJoin(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2 || !msIsObj(&args[1]) || args[1].as.obj->type != MS_OBJ_LIST) {
    methodError(vm, "ArgumentError: join expects a list argument");
    return msValueNil();
  }
  const MsObjList* xs = (const MsObjList*)args[1].as.obj;  // 列表为实参在栈：元素全 root
  const MsObjString* sep = (const MsObjString*)args[0].as.obj;
  ByteBuf buf = {NULL, 0, 0};
  for (int32_t i = 0; i < xs->count; i++) {
    if (!msIsString(&xs->items[i])) {
      methodError(vm, "ArgumentError: join expects string elements");
      goto fail;
    }
    const MsObjString* e = (const MsObjString*)xs->items[i].as.obj;
    if ((i > 0 && !bufAppend(&buf, sep->bytes, (int32_t)sep->byteLen)) ||
        !bufAppend(&buf, e->bytes, (int32_t)e->byteLen)) {
      methodError(vm, "RuntimeError: out of memory");
      goto fail;
    }
  }
  {
    MsObjString* r = bufToString(vm, &buf);
    return r == NULL ? msValueNil() : msValueObj(&r->head);
  }
fail:
  free(buf.data);
  return msValueNil();
}

static MsValue strmContains(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: contains expects 1 argument");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "contains", &sub)) {
    return msValueNil();
  }
  (void)vm;
  const MsObjString* s = (const MsObjString*)args[0].as.obj;
  return msValueBool(findBytes(s->bytes, (int32_t)s->byteLen, sub->bytes, (int32_t)sub->byteLen, 0) != NULL);
}

static MsValue strmStartsWith(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: startsWith expects 1 argument");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "startsWith", &sub)) {
    return msValueNil();
  }
  (void)vm;
  const MsObjString* s = (const MsObjString*)args[0].as.obj;
  return msValueBool(sub->byteLen <= s->byteLen &&
                     memcmp(s->bytes, sub->bytes, (size_t)sub->byteLen) == 0);
}

static MsValue strmEndsWith(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: endsWith expects 1 argument");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "endsWith", &sub)) {
    return msValueNil();
  }
  (void)vm;
  const MsObjString* s = (const MsObjString*)args[0].as.obj;
  return msValueBool(sub->byteLen <= s->byteLen &&
                     memcmp(s->bytes + (s->byteLen - sub->byteLen), sub->bytes,
                            (size_t)sub->byteLen) == 0);
}

static MsValue strmIndexOf(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 2 || argc > 3) {
    methodError(vm, "ArgumentError: indexOf expects 1 or 2 arguments");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "indexOf", &sub)) {
    return msValueNil();
  }
  ms_i64 from = 0;
  if (argc == 3 && !argInt(vm, 2, args, "indexOf", &from)) {
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  if (from < 0) {
    from += (ms_i64)s->charLen;  // 负 from 自尾折算（Python 语义）
    if (from < 0) {
      from = 0;
    }
  }
  if (from > (ms_i64)s->charLen) {
    return msValueInt(-1);
  }
  int32_t byteFrom = runeToByteOff(vm, s, from);
  const char* f = findBytes(s->bytes, (int32_t)s->byteLen, sub->bytes, (int32_t)sub->byteLen, byteFrom);
  if (f == NULL) {
    return msValueInt(-1);
  }
  return msValueInt((ms_i64)msObjStringRuneAtByte(s, (int32_t)(f - s->bytes)));
}

static MsValue strmLastIndexOf(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: lastIndexOf expects 1 argument");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "lastIndexOf", &sub)) {
    return msValueNil();
  }
  (void)vm;
  MsObjString* s = recvStr(args);
  if (sub->byteLen == 0) {
    return msValueInt((ms_i64)s->charLen);  // 空串匹配尾后（Python rfind("")==len）
  }
  int32_t nlen = (int32_t)sub->byteLen;
  for (int32_t i = (int32_t)s->byteLen - nlen; i >= 0; i--) {  // 最右起
    if (s->bytes[i] == sub->bytes[0] && memcmp(s->bytes + i, sub->bytes, (size_t)nlen) == 0) {
      return msValueInt((ms_i64)msObjStringRuneAtByte(s, i));
    }
  }
  return msValueInt(-1);
}

// old 为空按“无替换”返回拷贝（语义注记：Python 的逐缝插入不采）；n 到额后余文原样
static MsValue strmReplace(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 3 || argc > 4) {
    methodError(vm, "ArgumentError: replace expects 2 or 3 arguments");
    return msValueNil();
  }
  const MsObjString* old = NULL;
  const MsObjString* nw = NULL;
  if (!argStr(vm, 1, args, "replace", &old) || !argStr(vm, 2, args, "replace", &nw)) {
    return msValueNil();
  }
  ms_i64 n = -1;
  if (argc == 4 && !argInt(vm, 3, args, "replace", &n)) {
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  if (old->byteLen == 0) {
    MsObjString* r = msObjStringNew(vm, s->bytes, (int32_t)s->byteLen);
    if (r == NULL) {
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&r->head);
  }
  ByteBuf buf = {NULL, 0, 0};
  int32_t pos = 0;
  int32_t done = 0;
  for (;;) {
    if (n >= 0 && done == n) {
      break;
    }
    const char* f = findBytes(s->bytes, (int32_t)s->byteLen, old->bytes, (int32_t)old->byteLen, pos);
    if (f == NULL) {
      break;
    }
    int32_t off = (int32_t)(f - s->bytes);
    if (!bufAppend(&buf, s->bytes + pos, off - pos) ||
        !bufAppend(&buf, nw->bytes, (int32_t)nw->byteLen)) {
      goto oom;
    }
    pos = off + (int32_t)old->byteLen;
    done++;
  }
  if (!bufAppend(&buf, s->bytes + pos, (int32_t)s->byteLen - pos)) {
    goto oom;
  }
  {
    MsObjString* r = bufToString(vm, &buf);
    return r == NULL ? msValueNil() : msValueObj(&r->head);
  }
oom:
  free(buf.data);
  methodError(vm, "RuntimeError: out of memory");
  return msValueNil();
}

// 剪除主体：mode 0=两端 1=左 2=右；chars 省略 NULL = 默认空白集，给定则按其 rune 集剪
static MsValue trimImpl(MsVM* vm, int32_t argc, const MsValue* args, int mode, const char* mname) {
  if (argc > 2) {
    methodError(vm, "ArgumentError: %s expects at most 1 argument", mname);
    return msValueNil();
  }
  const MsObjString* chars = NULL;
  if (argc == 2 && !argStr(vm, 1, args, mname, &chars)) {
    return msValueNil();
  }
  MsObjString* s = recvStr(args);
  int32_t i = 0;
  int32_t j = (int32_t)s->byteLen;
  if (mode != 2) {
    while (i < j) {
      int32_t w = 0;
      ms_u32 cp = msUtf8Decode(s->bytes + i, j - i, &w);
      if (!runeInSet(cp, chars)) {
        break;
      }
      i += w;
    }
  }
  if (mode != 1) {
    while (j > i) {
      int32_t k = 0;
      ms_u32 cp = decodeLastRune(s->bytes, j, &k);
      if (!runeInSet(cp, chars)) {
        break;
      }
      j = k;
    }
  }
  MsObjString* r = msObjStringNew(vm, s->bytes + i, j - i);
  if (r == NULL) {
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static MsValue strmTrim(MsVM* vm, int32_t argc, const MsValue* args) {
  return trimImpl(vm, argc, args, 0, "trim");
}

static MsValue strmTrimLeft(MsVM* vm, int32_t argc, const MsValue* args) {
  return trimImpl(vm, argc, args, 1, "trimLeft");
}

static MsValue strmTrimRight(MsVM* vm, int32_t argc, const MsValue* args) {
  return trimImpl(vm, argc, args, 2, "trimRight");
}

static MsValue strmRepeat(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: repeat expects 1 argument");
    return msValueNil();
  }
  ms_i64 n = 0;
  if (!argInt(vm, 1, args, "repeat", &n)) {
    return msValueNil();
  }
  MsObjString* r = msObjStringRepeat(vm, recvStr(args), n);  // n<=0 → 空串（Python 语义）
  if (r == NULL) {
    methodError(vm, "OverflowError: string repetition result too large");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

static MsValue strmCount(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: count expects 1 argument");
    return msValueNil();
  }
  const MsObjString* sub = NULL;
  if (!argStr(vm, 1, args, "count", &sub)) {
    return msValueNil();
  }
  (void)vm;
  const MsObjString* s = (const MsObjString*)args[0].as.obj;
  if (sub->byteLen == 0) {
    return msValueInt(0);  // 空子串计 0（任务语义；Python len+1 不采）
  }
  ms_i64 cnt = 0;
  int32_t pos = 0;
  for (;;) {
    const char* f = findBytes(s->bytes, (int32_t)s->byteLen, sub->bytes, (int32_t)sub->byteLen, pos);
    if (f == NULL) {
      break;
    }
    cnt++;
    pos = (int32_t)(f - s->bytes) + (int32_t)sub->byteLen;  // 非重叠推进
  }
  return msValueInt(cnt);
}

// 填充主体：目标按 rune 宽度；ch 形参接受 rune（int 码点，01 §3.4）或单 rune string，默认空格
static MsValue padImpl(MsVM* vm, int32_t argc, const MsValue* args, bool left, const char* mname) {
  if (argc < 2 || argc > 3) {
    methodError(vm, "ArgumentError: %s expects 1 or 2 arguments", mname);
    return msValueNil();
  }
  ms_i64 w = 0;
  if (!argInt(vm, 1, args, mname, &w)) {
    return msValueNil();
  }
  ms_u32 ch = ' ';
  if (argc == 3) {
    if (args[2].kind == MS_VAL_I64) {
      ch = (ms_u32)args[2].as.i;  // 非法码点经 msUtf8Encode 归一 U+FFFD（common.h 契约）
    } else if (msIsString(&args[2])) {
      const MsObjString* cs = (const MsObjString*)args[2].as.obj;
      int32_t wd = 0;
      if (cs->charLen != 1) {
        methodError(vm, "ValueError: %s pad character must be a single rune", mname);
        return msValueNil();
      }
      ch = msUtf8Decode(cs->bytes, (int32_t)cs->byteLen, &wd);
    } else {
      methodError(vm, "ArgumentError: %s expects an int or string pad character", mname);
      return msValueNil();
    }
  }
  MsObjString* s = recvStr(args);
  ms_i64 pad = w - (ms_i64)s->charLen;
  if (pad <= 0) {
    MsObjString* r = msObjStringNew(vm, s->bytes, (int32_t)s->byteLen);
    if (r == NULL) {
      methodError(vm, "RuntimeError: out of memory");
      return msValueNil();
    }
    return msValueObj(&r->head);
  }
  char enc[4];
  int32_t ew = msUtf8Encode(ch, enc);
  if (pad > ((ms_i64)INT32_MAX - (ms_i64)s->byteLen) / ew) {  // 除法式上限（免疫 i64 乘溢出）
    methodError(vm, "OverflowError: string padding result too large");
    return msValueNil();
  }
  ByteBuf buf = {NULL, 0, 0};
  bool ok = left || bufAppend(&buf, s->bytes, (int32_t)s->byteLen);  // padRight：s 先行（padLeft 先垫）
  for (ms_i64 k = 0; ok && k < pad; k++) {
    ok = bufAppend(&buf, enc, ew);
  }
  if (ok && left) {
    ok = bufAppend(&buf, s->bytes, (int32_t)s->byteLen);  // padLeft：s 收尾
  }
  if (!ok) {
    free(buf.data);
    methodError(vm, "RuntimeError: out of memory");
    return msValueNil();
  }
  MsObjString* r = bufToString(vm, &buf);
  return r == NULL ? msValueNil() : msValueObj(&r->head);
}

static MsValue strmPadLeft(MsVM* vm, int32_t argc, const MsValue* args) {
  return padImpl(vm, argc, args, true, "padLeft");
}

static MsValue strmPadRight(MsVM* vm, int32_t argc, const MsValue* args) {
  return padImpl(vm, argc, args, false, "padRight");
}

static MsValue strmSlice(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc < 2 || argc > 4) {
    methodError(vm, "ArgumentError: slice expects 1 to 3 arguments");
    return msValueNil();
  }
  ms_i64 a = 0, b = 0, c = 0;
  if (!argInt(vm, 1, args, "slice", &a) || (argc >= 3 && !argInt(vm, 2, args, "slice", &b)) ||
      (argc >= 4 && !argInt(vm, 3, args, "slice", &c))) {
    return msValueNil();
  }
  bool errStepZero = false;
  MsObjString* r = msObjStringSlice(vm, recvStr(args), true, a, argc >= 3, b, argc >= 4, c, &errStepZero);
  if (r == NULL) {
    methodError(vm, errStepZero ? "ValueError: slice step must not be zero"
                                : "RuntimeError: out of memory");
    return msValueNil();
  }
  return msValueObj(&r->head);
}

// ---- isX 族（D9：ASCII 精确；非 ASCII isAlpha 借 ID_START 剔 '_'；isDigit/isSpace 暂 false） ----

typedef enum StrIsKind {
  IS_DIGIT,
  IS_SPACE,
  IS_ALPHA,
} StrIsKind;

static bool isRuneMatch(ms_u32 cp, StrIsKind kind) {
  if (cp < 0x80) {
    switch (kind) {
      case IS_DIGIT:
        return cp >= '0' && cp <= '9';
      case IS_SPACE:
        return isWsCp(cp);
      case IS_ALPHA:
        return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    }
  }
  if (kind == IS_ALPHA) {
    return msUnicodeIdStart(cp) && cp != '_';
  }
  return false;  // D9：isDigit/isSpace 非 ASCII 暂 false（Tier1/T034 复核）
}

// 逐 rune 判定（全真才真；空串 false）
static MsValue isImpl(MsVM* vm, int32_t argc, const MsValue* args, StrIsKind kind) {
  if (argc != 1) {
    methodError(vm, "ArgumentError: isDigit/isSpace/isAlpha expect no arguments");
    return msValueNil();
  }
  (void)vm;
  MsObjString* s = recvStr(args);
  if (s->charLen == 0) {
    return msValueBool(false);
  }
  int32_t i = 0;
  while (i < (int32_t)s->byteLen) {
    int32_t w = 0;
    ms_u32 cp = msUtf8Decode(s->bytes + i, (int32_t)s->byteLen - i, &w);
    if (!isRuneMatch(cp, kind)) {
      return msValueBool(false);
    }
    i += w;
  }
  return msValueBool(true);
}

static MsValue strmIsDigit(MsVM* vm, int32_t argc, const MsValue* args) {
  return isImpl(vm, argc, args, IS_DIGIT);
}

static MsValue strmIsSpace(MsVM* vm, int32_t argc, const MsValue* args) {
  return isImpl(vm, argc, args, IS_SPACE);
}

static MsValue strmIsAlpha(MsVM* vm, int32_t argc, const MsValue* args) {
  return isImpl(vm, argc, args, IS_ALPHA);
}

static MsValue strmCompareTo(MsVM* vm, int32_t argc, const MsValue* args) {
  if (argc != 2) {
    methodError(vm, "ArgumentError: compareTo expects 1 argument");
    return msValueNil();
  }
  const MsObjString* other = NULL;
  if (!argStr(vm, 1, args, "compareTo", &other)) {
    return msValueNil();
  }
  return msValueInt((ms_i64)msObjStringCompare(recvStr(args), other));
}

// ---- 方法表（10 §2 表序） ----

typedef struct StrMethodEntry {
  const char* name;
  MsNativeFn fn;
} StrMethodEntry;

static const StrMethodEntry kStrMethods[] = {
    {"charLen", strmCharLen},
    {"chars", strmChars},
    {"bytes", strmBytes},
    {"toUpper", strmToUpper},
    {"toLower", strmToLower},
    {"split", strmSplit},
    {"splitLines", strmSplitLines},
    {"join", strmJoin},
    {"contains", strmContains},
    {"startsWith", strmStartsWith},
    {"endsWith", strmEndsWith},
    {"indexOf", strmIndexOf},
    {"lastIndexOf", strmLastIndexOf},
    {"replace", strmReplace},
    {"trim", strmTrim},
    {"trimLeft", strmTrimLeft},
    {"trimRight", strmTrimRight},
    {"repeat", strmRepeat},
    {"count", strmCount},
    {"padLeft", strmPadLeft},
    {"padRight", strmPadRight},
    {"slice", strmSlice},
    {"isDigit", strmIsDigit},
    {"isSpace", strmIsSpace},
    {"isAlpha", strmIsAlpha},
    {"compareTo", strmCompareTo},
};

bool msStringMethodLookup(const char* name, int32_t len, MsMethodRef* out) {
  for (size_t i = 0; i < sizeof(kStrMethods) / sizeof(kStrMethods[0]); i++) {
    const char* n = kStrMethods[i].name;
    if ((int32_t)strlen(n) == len && memcmp(n, name, (size_t)len) == 0) {
      *out = (MsMethodRef){kStrMethods[i].fn, n, (int32_t)strlen(n)};
      return true;
    }
  }
  return false;
}
