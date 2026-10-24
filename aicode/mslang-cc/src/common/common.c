#include "common.h"

ms_u32 msFnv1a32(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u32 hash = 2166136261u;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

ms_u64 msFnv1a64(const void* data, ms_u64 len) {
  const ms_u8* bytes = data;
  ms_u64 hash = 14695981039346656037ULL;
  for (ms_u64 i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

// ---- UTF-8 与转义（T013，01 §3.3）：共享版，语义镜像 scanner.c 的静态副本 ----

// 十六进制数字值（0-15），非十六进制返回 -1；大小写均可，规则同 scanner 的 digitOfBase
static int hexDigit(uint8_t c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

uint32_t msUtf8Decode(const char* p, int32_t avail, int32_t* width) {
  uint8_t b = (uint8_t)p[0];
  if (b < 0x80) {
    *width = 1;
    return b;
  }
  int32_t n = 0;
  uint32_t cp = 0;
  if ((b & 0xE0) == 0xC0 && avail >= 2) {
    n = 2;
    cp = b & 0x1F;
  } else if ((b & 0xF0) == 0xE0 && avail >= 3) {
    n = 3;
    cp = b & 0x0F;
  } else if ((b & 0xF8) == 0xF0 && avail >= 4) {
    n = 4;
    cp = b & 0x07;
  } else {
    *width = 1;
    return 0xFFFD;
  }
  for (int32_t i = 1; i < n; i++) {
    uint8_t cont = (uint8_t)p[i];
    if ((cont & 0xC0) != 0x80) {
      *width = 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    *width = 1;
    return 0xFFFD;
  }
  *width = n;
  return cp;
}

int32_t msUtf8Encode(uint32_t cp, char* out) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = 0xFFFD;
  }
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

int32_t msTextDecodeEscapes(const char* src, int32_t len, char* dst, int32_t cap) {
  int32_t out = 0;
  int32_t i = 0;
  while (i < len) {
    uint8_t c = (uint8_t)src[i];
    if (c != '\\') {
      if (out >= cap) {
        return -1;
      }
      dst[out++] = (char)c;  // 非 \ 字节直拷（含 UTF-8 多字节序列的每个字节）
      i++;
      continue;
    }
    if (out >= cap) {
      return -1;
    }
    if (i + 1 >= len) {  // 源尾孤立反斜杠：仅拷贝反斜杠，前进 1
      dst[out++] = '\\';
      i++;
      continue;
    }
    uint8_t e = (uint8_t)src[i + 1];
    switch (e) {
      case 'n':
        dst[out++] = '\n';
        i += 2;
        break;
      case 'r':
        dst[out++] = '\r';
        i += 2;
        break;
      case 't':
        dst[out++] = '\t';
        i += 2;
        break;
      case '\\':
        dst[out++] = '\\';
        i += 2;
        break;
      case '"':
        dst[out++] = '"';
        i += 2;
        break;
      case '\'':
        dst[out++] = '\'';
        i += 2;
        break;
      case '0':
        dst[out++] = '\0';
        i += 2;
        break;
      case 'x': {
        // \xHH：恰好 2 位十六进制；不足 2 位（防御路径，scanner 已拦截）按字面拷贝 \ 与 x
        int hi = -1;
        int lo = -1;
        if (i + 4 <= len) {
          hi = hexDigit((uint8_t)src[i + 2]);
          lo = hexDigit((uint8_t)src[i + 3]);
        }
        if (hi >= 0 && lo >= 0) {
          dst[out++] = (char)(hi * 16 + lo);
          i += 4;
        } else {
          if (out + 2 > cap) {
            return -1;
          }
          dst[out++] = '\\';
          dst[out++] = 'x';
          i += 2;
        }
        break;
      }
      case 'u': {
        // \u{1-6 位十六进制}；形态残缺或码点越界/代理区（防御路径）按字面拷贝 \ 与 u
        int32_t j = i + 2;
        uint32_t cp = 0;
        int32_t digits = 0;
        if (j < len && src[j] == '{') {
          j++;
          while (digits < 6 && j < len) {
            int d = hexDigit((uint8_t)src[j]);
            if (d < 0) {
              break;
            }
            cp = cp * 16 + (uint32_t)d;
            j++;
            digits++;
          }
        }
        if (digits > 0 && j < len && src[j] == '}' && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF)) {
          char tmp[4];
          int32_t w = msUtf8Encode(cp, tmp);
          if (w > cap - out) {
            return -1;
          }
          for (int32_t k = 0; k < w; k++) {
            dst[out++] = tmp[k];
          }
          i = j + 1;  // 过 '}'
        } else {
          if (out + 2 > cap) {
            return -1;
          }
          dst[out++] = '\\';
          dst[out++] = 'u';
          i += 2;
        }
        break;
      }
      default:
        dst[out++] = '\\';  // 仅拷贝反斜杠，前进 1；后续字节下轮按普通字节处理
        i++;
        break;
    }
  }
  if (out >= cap) {  // 无余量写终止符视同 cap 不足（防御：输出不超过 len）
    return -1;
  }
  dst[out] = '\0';
  return out;
}
