// MsObjString（T013，22 §3）：UTF-8 不可变存储、FNV-1a 32/64、ASCII 快路径、惰性 runeIndex
// 结构与分批实现依据 docs/superpowers/specs/2026-10-22-t013-string-object-design.md D1：
// FAM 尾随 runeIndex 保留区、hash/hash64 创建即定、runeIndex 惰性构建
#ifndef INCLUDE_MS_OBJ_OBJ_STRING_H_
#define INCLUDE_MS_OBJ_OBJ_STRING_H_

#include <stdbool.h>
#include <stdint.h>

#include "obj/obj.h"

// MSVC 以零长数组扩展实现 C11 FAM，定义处理 C4200（/WX 致错）；GCC/Clang 原生无警告
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct MsObjString {
  MsObj head;
  ms_u32 hash;      // FNV-1a-32（相等短路）
  ms_u64 hash64;    // FNV-1a-64（map 键，T015 对接）
  ms_u32 byteLen;
  ms_u32 charLen;   // 创建时一次扫描定（ASCII 时 = byteLen）
  bool isAscii;
  int32_t* runeIndex;  // 惰性：非 ASCII 首次按需构建；指向对象内 bytes+byteLen+pad 保留区（D1）
  char bytes[];        // FAM；非 ASCII 时尾随 runeIndex 保留区：4 字节对齐填充 pad（= (4 - byteLen % 4) % 4）+ (charLen+1)*4 字节
} MsObjString;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 运行时新建（拷贝 bytes；不 intern）
MsObjString* msObjStringNew(MsVM* vm, const char* bytes, int32_t len);
// 拼接（a + bytes[0..lenB)）；失败（长度溢出/分配失败）返回 NULL
MsObjString* msObjStringConcat(MsVM* vm, const MsObjString* a, const char* bytesB, int32_t lenB);
// 重复 n 次（n <= 0 返回空串）；失败返回 NULL
MsObjString* msObjStringRepeat(MsVM* vm, const MsObjString* a, ms_i64 n);
// 解码 01 §3.3 转义原文后新建（src 借用）
MsObjString* msObjStringFromEscaped(MsVM* vm, const char* src, int32_t len);

bool msObjStringEqual(const MsObjString* a, const MsObjString* b);  // hash/len 短路 → memcmp
int32_t msObjStringCompare(const MsObjString* a, const MsObjString* b);  // 字典序（<0/0/>0）

// rune 访问（负索引换算；越界返回 false）
bool msObjStringRuneAt(MsVM* vm, MsObjString* s, ms_i64 i, ms_u32* out);
// 惰性构建并返回 rune 索引表（charLen+1 项，末项 = byteLen 哨兵；ASCII 串返回 NULL）
const int32_t* msObjStringRuneIndex(MsVM* vm, MsObjString* s);

// 规范切片算法（T014 list 复用；Python 语义）。step==0 返回 false（调用方报 ValueError）
bool msSliceRange(ms_i64 len, bool hasA, ms_i64 a, bool hasB, ms_i64 b, bool hasC, ms_i64 c,
                  ms_i64* startOut, ms_i64* stopOut, ms_i64* stepOut);
// 按 rune 索引切片（端点省略由 has* 标记）；step==0 返回 NULL 且 *errStepZero 置 true
MsObjString* msObjStringSlice(MsVM* vm, MsObjString* s, bool hasA, ms_i64 a, bool hasB, ms_i64 b,
                              bool hasC, ms_i64 c, bool* errStepZero);
// 字节偏移 → rune 索引（byteOffset 落在 rune 边界；非边界取最近前驱边界的 rune 索引）
int32_t msObjStringRuneAtByte(const MsObjString* s, int32_t byteOffset);

// ---- string 迭代器（chars/bytes；MS_OBJ_ITERATOR tag，T017 泛化壳） ----
typedef struct MsObjStrIter {
  MsObj head;
  MsObjString* str;  // markChildren 标记（D8）
  ms_u32 pos;        // chars：字节游标；bytes：字节下标
  bool byteMode;
} MsObjStrIter;

MsObjStrIter* msObjStrIterNew(MsVM* vm, MsObjString* s, bool byteMode);
bool msStrIterNext(MsVM* vm, MsObjStrIter* it, MsValue* out);  // 耗尽返回 false

// ---- intern 表（intern.c；挂 MsVM，PERMANENT 代） ----
typedef struct MsInternTable {
  MsObjString** slots;
  size_t cap;  // 2^n
  size_t count;
} MsInternTable;

void msInternInit(MsInternTable* t);
void msInternFree(MsInternTable* t);  // 仅释放槽位数组（对象归堆统一释放）
MsObjString* msInternBytes(MsVM* vm, const char* bytes, int32_t len);  // 存在即返回既有指针
// 消费式 intern：probe 既有等值串则返回之（s 成为待回收垃圾）；miss 则将 s 本身标永久并插表。
// 全程零分配——s 在调用期间不可达 GC 亦不被回收（msValueFromConst 临时串唯一安全路径）
MsObjString* msInternObj(MsVM* vm, MsObjString* s);

static inline bool msIsString(const MsValue* v) {
  return v->kind == MS_VAL_OBJ && v->as.obj->type == MS_OBJ_STRING;
}

#endif  // INCLUDE_MS_OBJ_OBJ_STRING_H_
