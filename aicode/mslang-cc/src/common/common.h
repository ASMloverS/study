#ifndef INCLUDE_MS_COMMON_COMMON_H_
#define INCLUDE_MS_COMMON_COMMON_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t ms_u8;
typedef int32_t ms_i32;
typedef uint32_t ms_u32;
typedef int64_t ms_i64;
typedef uint64_t ms_u64;
typedef double ms_f64;

#define MS_VERSION "0.0.1 (dev)"

// 仅内部不变量断言：Release（NDEBUG）编译剔除
#if defined(NDEBUG)
#define MS_ASSERT(cond) ((void)0)
#else
#define MS_ASSERT(cond) assert(cond)
#endif

// 统一动态数组增长：容量从 0 起按 2 倍扩张，arr/count/cap 均为左值
// Release（NDEBUG）下 OOM 为设计上致命：断言被剔除后直接解引用 NULL 崩溃
// count 与 cap 在宏内各被求值多次，禁止传入带副作用的表达式
#define MS_ARRAY_PUSH(arr, count, cap, elem)                  \
  do {                                                        \
    if ((count) == (cap)) {                                   \
      (cap) = (cap) == 0 ? 8 : (cap) * 2;                     \
      (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
      MS_ASSERT((arr) != NULL);                               \
    }                                                         \
    (arr)[(count)] = (elem);                                  \
    (count)++;                                                \
  } while (0)

ms_u32 msFnv1a32(const void* data, ms_u64 len);
ms_u64 msFnv1a64(const void* data, ms_u64 len);

// ---- UTF-8 与转义（T013，01 §3.3；scanner 静态副本不动，此处为共享版） ----
// 解码 p 处一个 rune 并写回字节宽 width；非法/截断/代理区/超长序列按 1 字节返回 0xFFFD
uint32_t msUtf8Decode(const char* p, int32_t avail, int32_t* width);
// 编码 cp 到 out（容量 ≥4）；越界/代理区码点写 U+FFFD；返回字节宽 1-4
int32_t msUtf8Encode(uint32_t cp, char* out);
// 解码 01 §3.3 转义序列（\n \r \t \\ \" \' \0 \xHH \u{...}）；返回解码长度并 NUL 终止 dst（终止符计入容量），cap 不足返回 -1
// 源文本经 scanner 校验后到达，非法形态按字面拷贝（防御路径）
int32_t msTextDecodeEscapes(const char* src, int32_t len, char* dst, int32_t cap);

#endif
