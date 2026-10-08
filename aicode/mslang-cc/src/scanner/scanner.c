// Scanner 主循环（T004）：标识符/数字/运算符/ASI；字符串/rune/插值归 T005
#include "scanner/scanner.h"

#include "scanner/unicode_table.h"

// 解码 p 处一个 rune 并写回字节宽 width；非法/截断/代理区/超长序列按 1 字节返回 0xFFFD
static uint32_t utf8Decode(const char* p, int32_t avail, int32_t* width) {
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

static uint8_t peek(const Scanner* s) {
  return s->pos < s->len ? (uint8_t)s->src[s->pos] : 0;
}

static uint8_t peekAt(const Scanner* s, int32_t off) {
  int32_t i = s->pos + off;
  return i < s->len ? (uint8_t)s->src[i] : 0;
}

static void addDiag(Scanner* s, uint32_t code, int32_t line, int32_t col, int32_t len, const char* message) {
  if (s->diags == NULL) {
    return;
  }
  MsDiagnostic diag = {MS_DIAG_ERROR, code, s->file, line, col, len, message};
  msDiagAdd(s->diags, &diag);
}

static void makeToken(const Scanner* s, MsToken* out, MsTokenType type, int32_t start, int32_t line, int32_t col) {
  out->type = type;
  out->start = s->src + start;
  out->len = s->pos - start;
  out->file = s->file;
  out->line = line;
  out->col = col;
  out->offset = start;
}

// 前进一个 rune：\n 计行重置列，其余列 +1（col 按 rune 计，tab 记 1 列）
static void advanceRune(Scanner* s) {
  if (s->pos >= s->len) {
    return;
  }
  int32_t width = 0;
  utf8Decode(s->src + s->pos, s->len - s->pos, &width);
  if (s->src[s->pos] == '\n') {
    s->line++;
    s->col = 1;
  } else {
    s->col++;
  }
  s->pos += width;
}

static int digitOfBase(uint8_t c, int base) {
  if (c >= '0' && c <= '9') {
    return c - '0' < base ? c - '0' : -1;
  }
  if (base == 16) {
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
  }
  return -1;
}

// 连续消费 base 进制数字与下划线分组（错位下划线报 MSE_UNDERSCORE 后继续恢复）；
// 溢出 uint64 时置 *overflow 并保持近似值（BIGINT 占位语义，T016 接管精确值）
static uint64_t scanDigits(Scanner* s, int base, bool* overflow) {
  uint64_t value = 0;
  bool sawDigit = false;
  for (;;) {
    uint8_t c = peek(s);
    int d = digitOfBase(c, base);
    if (d >= 0) {
      if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
        *overflow = true;
      } else {
        value = value * (uint64_t)base + (uint64_t)d;
      }
      sawDigit = true;
      advanceRune(s);
    } else if (c == '_') {
      if (!sawDigit || digitOfBase(peekAt(s, 1), base) < 0) {
        addDiag(s, MSE_UNDERSCORE, s->line, s->col, 1, "underscore must separate digits");
      }
      advanceRune(s);
    } else {
      break;
    }
  }
  return value;
}

// 数字后紧跟 . 且 . 后非数字时：判断是否成员访问（1.toString，03 §3 后缀成员）；
// 是则豁免诊断（DOT 留给主循环产出），否则按 5. 报缺小数位
static bool isMemberContinuation(const Scanner* s) {
  uint8_t c = peekAt(s, 1);
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
    return true;
  }
  if (c >= 0x80 && s->pos + 1 < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos + 1, s->len - s->pos - 1, &width);
    return msUnicodeIdStart(cp);
  }
  return false;
}

// // 行注释：吃到行尾（不含 \n）；/// 文档注释透出行号供 ms doc（T042）
static void skipLineComment(Scanner* s) {
  if (peekAt(s, 2) == '/') {
    s->docLine = s->line;
  }
  do {
    advanceRune(s);
  } while (s->pos < s->len && s->src[s->pos] != '\n');
}

// /* */ 块注释非法（01 §5）：报 MSE 后跳到闭合或 EOF 恢复
static void skipBlockComment(Scanner* s) {
  addDiag(s, MSE_BLOCK_COMMENT, s->line, s->col, 2, "block comments are not allowed; use //");
  advanceRune(s);
  advanceRune(s);
  while (s->pos < s->len) {
    if (peek(s) == '*' && peekAt(s, 1) == '/') {
      advanceRune(s);
      advanceRune(s);
      return;
    }
    advanceRune(s);
  }
}

// 标识符：ID_START 起始，ID_CONTINUE 续行；命中关键字表改写类型
static void scanIdentifier(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  while (s->pos < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    bool ok = s->pos == start ? msUnicodeIdStart(cp) : msUnicodeIdContinue(cp);
    if (!ok) {
      break;
    }
    advanceRune(s);
  }
  makeToken(s, out, TK_ID, start, line, col);
  out->type = msKeywordLookup(out->start, out->len);
}

// 数字字面量：十/十六(0x 0X)/八(0o 0O)/二(0b)进制、下划线分组、n 后缀、浮点（. 与 e/E）
// 恢复策略：诊断后仍产出 token（01 §3/§9；溢出升 BIGINT 由 T016 接管精确值）
static void scanNumber(Scanner* s, MsToken* out) {
  int32_t start = s->pos, line = s->line, col = s->col;
  int base = 10;
  uint8_t n1 = peekAt(s, 1);
  if (peek(s) == '0') {
    if (n1 == 'x' || n1 == 'X') {
      base = 16;
    } else if (n1 == 'o' || n1 == 'O') {
      base = 8;
    } else if (n1 == 'b') {
      base = 2;
    }
  }
  if (base != 10) {
    advanceRune(s);
    advanceRune(s);
    uint8_t d = peek(s);
    if (digitOfBase(d, base) < 0 && d != '_') {
      if (digitOfBase(d, 10) >= 0) {
        addDiag(s, MSE_DIGIT_BASE, line, col, s->pos - start, "digit out of range for literal base");
      } else {
        addDiag(s, MSE_BASE_PREFIX, line, col, s->pos - start, "digits expected after base prefix");
      }
      makeToken(s, out, TK_INT, start, line, col);
      return;
    }
  }
  bool overflow = false;
  uint64_t value = scanDigits(s, base, &overflow);
  bool isFloat = false;
  if (base == 10 && peek(s) == '.') {
    if (digitOfBase(peekAt(s, 1), 10) >= 0) {
      isFloat = true;
      advanceRune(s);
      scanDigits(s, 10, &overflow);
    } else if (!isMemberContinuation(s)) {
      addDiag(s, MSE_FLOAT_TRAILING_DOT, s->line, s->col, 1, "fractional digits expected after '.'");
    }
  }
  if (base == 10 && (peek(s) == 'e' || peek(s) == 'E')) {
    int32_t off = (peekAt(s, 1) == '+' || peekAt(s, 1) == '-') ? 2 : 1;
    if (digitOfBase(peekAt(s, off), 10) >= 0) {
      isFloat = true;
      while (off-- > 0) {
        advanceRune(s);
      }
      scanDigits(s, 10, &overflow);
    } else {
      // 1e / 1e+：残缺指数，e 与符号并入恢复 FLOAT
      advanceRune(s);
      if (peek(s) == '+' || peek(s) == '-') {
        advanceRune(s);
      }
      addDiag(s, MSE_FLOAT_EXPONENT, line, col, s->pos - start, "exponent expects digits");
      makeToken(s, out, TK_FLOAT, start, line, col);
      return;
    }
  }
  bool isBig = false;
  if (!isFloat && peek(s) == 'n') {
    isBig = true;
    advanceRune(s);
  }
  if (s->pos < s->len) {
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    if (cp != 0 && msUnicodeIdContinue(cp)) {
      addDiag(s, MSE_INT_TRAILING, s->line, s->col, width, "identifier character cannot follow number");
    }
  }
  if (isFloat) {
    makeToken(s, out, TK_FLOAT, start, line, col);
  } else if (isBig || overflow || value > (uint64_t)INT64_MAX) {
    if (!isBig) {
      addDiag(s, MSE_INT_OVERFLOW, line, col, s->pos - start, "integer literal exceeds int64 range; promoted to bigint");
    }
    makeToken(s, out, TK_BIGINT, start, line, col);
  } else {
    makeToken(s, out, TK_INT, start, line, col);
  }
}

void scannerInit(Scanner* s, const char* src, int32_t len, const char* file, MsDiagSink* diags) {
  s->src = src;
  s->len = len;
  s->pos = 0;
  s->line = 1;
  s->col = 1;
  s->parenDepth = 0;
  s->file = file;
  s->diags = diags;
  s->pendingAsi = false;
  s->docLine = -1;
}

bool scannerNext(Scanner* s, MsToken* out) {
  for (;;) {
    uint8_t c = peek(s);
    if (c == 0) {
      makeToken(s, out, TK_EOF, s->pos, s->line, s->col);
      out->len = 0;
      return false;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advanceRune(s);
      continue;
    }
    if (c == '/' && peekAt(s, 1) == '/') {
      skipLineComment(s);
      continue;
    }
    if (c == '/' && peekAt(s, 1) == '*') {
      skipBlockComment(s);
      continue;
    }
    int32_t start = s->pos;
    int32_t line = s->line, col = s->col;
    if (c == '.' && digitOfBase(peekAt(s, 1), 10) >= 0) {
      // .5：缺整数部（01 §3.2），恢复为 FLOAT
      addDiag(s, MSE_FLOAT_LEADING_DOT, line, col, 1, "integer part expected before '.'");
      advanceRune(s);
      bool overflow = false;
      scanDigits(s, 10, &overflow);
      makeToken(s, out, TK_FLOAT, start, line, col);
      return true;
    }
    if (digitOfBase(c, 10) >= 0) {
      scanNumber(s, out);
      return true;
    }
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    if (cp != 0 && msUnicodeIdStart(cp)) {
      scanIdentifier(s, out);
      return true;
    }
    // 运算符由后续任务落地；此前一律按非法字符跳过恢复
    addDiag(s, MSE_UNEXPECTED_CHAR, line, col, width, "unexpected character");
    advanceRune(s);
  }
}
