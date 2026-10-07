// 关键字表与诊断格式化（T003）；查表零分配，格式化写入调用方缓冲
#include "scanner/token.h"

#include <string.h>

typedef struct MsKeywordEntry {
  const char* name;
  MsTokenType type;
} MsKeywordEntry;

// 01-lexical.md §2 恰好 37 保留字（字典序）；struct/interface/package/type 不是关键字
static const MsKeywordEntry kKeywords[] = {
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

_Static_assert(sizeof(kKeywords) / sizeof(kKeywords[0]) == 37, "01-lexical.md §2 fixes exactly 37 keywords");

MsTokenType msKeywordLookup(const char* start, int32_t len) {
  for (size_t i = 0; i < sizeof(kKeywords) / sizeof(kKeywords[0]); i++) {
    const char* name = kKeywords[i].name;
    if ((int32_t)strlen(name) == len && memcmp(start, name, (size_t)len) == 0) {
      return kKeywords[i].type;
    }
  }
  return TK_ID;
}

// TDD RED 占位：循环 3 以真实实现替换
void msDiagAdd(MsDiagSink* sink, const MsDiagnostic* diag) {
  if (sink->count >= MS_DIAG_MAX) {
    sink->overflow++;
    return;
  }
  sink->items[sink->count++] = *diag;
}

void msDiagFormat(const MsDiagnostic* diag, const char* source, char* out, size_t outCap) {
  (void)diag;
  (void)source;
  (void)out;
  (void)outCap;
}
