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
    int32_t line = s->line, col = s->col;
    int32_t width = 0;
    uint32_t cp = utf8Decode(s->src + s->pos, s->len - s->pos, &width);
    if (cp != 0 && msUnicodeIdStart(cp)) {
      scanIdentifier(s, out);
      return true;
    }
    // 数字与运算符由后续任务落地；此前一律按非法字符跳过恢复
    addDiag(s, MSE_UNEXPECTED_CHAR, line, col, width, "unexpected character");
    advanceRune(s);
  }
}
