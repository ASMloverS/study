// 关键字表与诊断格式化（T003）；查表零分配，格式化写入调用方缓冲
#include "scanner/token.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct MsKeywordEntry {
  const char* name;
  MsTokenType type;
} MsKeywordEntry;

// 01-lexical.md §2 恰好 37 保留字（字典序）；struct/interface/package/type 不是关键字
static const MsKeywordEntry KEYWORDS[] = {
    {"and", TK_AND},
    {"as", TK_AS},
    {"async", TK_ASYNC},
    {"await", TK_AWAIT},
    {"break", TK_BREAK},
    {"case", TK_CASE},
    {"catch", TK_CATCH},
    {"chan", TK_CHAN},
    {"class", TK_CLASS},
    {"const", TK_CONST},
    {"continue", TK_CONTINUE},
    {"default", TK_DEFAULT},
    {"defer", TK_DEFER},
    {"else", TK_ELSE},
    {"false", TK_FALSE},
    {"finally", TK_FINALLY},
    {"for", TK_FOR},
    {"from", TK_FROM},
    {"func", TK_FUNC},
    {"go", TK_GO},
    {"if", TK_IF},
    {"import", TK_IMPORT},
    {"in", TK_IN},
    {"is", TK_IS},
    {"nil", TK_NIL},
    {"not", TK_NOT},
    {"or", TK_OR},
    {"return", TK_RETURN},
    {"switch", TK_SWITCH},
    {"this", TK_THIS},
    {"throw", TK_THROW},
    {"trait", TK_TRAIT},
    {"true", TK_TRUE},
    {"try", TK_TRY},
    {"var", TK_VAR},
    {"while", TK_WHILE},
    {"yield", TK_YIELD},
};

_Static_assert(sizeof(KEYWORDS) / sizeof(KEYWORDS[0]) == 37, "01-lexical.md §2 fixes exactly 37 keywords");

MsTokenType msKeywordLookup(const char* start, int32_t len) {
  for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
    const char* name = KEYWORDS[i].name;
    if ((int32_t)strlen(name) == len && memcmp(start, name, (size_t)len) == 0) {
      return KEYWORDS[i].type;
    }
  }
  return TK_ID;
}

void msDiagAdd(MsDiagSink* sink, const MsDiagnostic* diag) {
  if (sink->count >= MS_DIAG_MAX) {
    sink->overflow++;
    return;
  }
  sink->items[sink->count++] = *diag;
}

// 追加格式化片段到 out，容量不足时截断（保持 NUL 结尾）；调用方保证 *pos < outCap
static void diagAppend(char* out, size_t outCap, size_t* pos, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(out + *pos, outCap - *pos, fmt, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  size_t advance = (size_t)written;
  if (advance > outCap - *pos - 1) {
    advance = outCap - *pos - 1;
  }
  *pos += advance;
}

// 定位 lineNum（1-based）所在行；返回行首指针并写 end 为行尾（不含换行），越界返回 NULL
static const char* diagFindLine(const char* source, int32_t lineNum, const char** end) {
  const char* line = source;
  for (int32_t i = 1; i < lineNum; i++) {
    line = strchr(line, '\n');
    if (line == NULL) {
      return NULL;
    }
    line++;
  }
  if (lineNum > 1 && *line == '\0') {
    return NULL;
  }
  const char* lineEnd = strchr(line, '\n');
  *end = lineEnd != NULL ? lineEnd : line + strlen(line);
  return line;
}

void msDiagFormat(const MsDiagnostic* diag, const char* source, char* out, size_t outCap) {
  if (outCap == 0) {
    return;
  }
  const char* levelName = diag->level == MS_DIAG_WARNING ? "warning" : "error";
  size_t pos = 0;
  diagAppend(out, outCap, &pos, "%s:%d:%d: %s[MSE%04u]: %s\n", diag->file, diag->line, diag->col, levelName, diag->code,
             diag->message);
  if (source == NULL) {
    return;
  }
  const char* lineEnd = NULL;
  const char* line = diagFindLine(source, diag->line, &lineEnd);
  if (line == NULL) {
    return;
  }
  diagAppend(out, outCap, &pos, "%.*s\n", (int)(lineEnd - line), line);
  int32_t indent = diag->col > 1 ? diag->col - 1 : 0;
  int32_t carets = diag->len > 0 ? diag->len : 1;
  for (int32_t i = 0; i < indent; i++) {
    if (pos == outCap - 1) {
      break;
    }
    diagAppend(out, outCap, &pos, " ");
  }
  for (int32_t i = 0; i < carets; i++) {
    if (pos == outCap - 1) {
      break;
    }
    diagAppend(out, outCap, &pos, "^");
  }
  diagAppend(out, outCap, &pos, "\n");
}
