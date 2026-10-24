#!/usr/bin/env python3
# 生成 src/scanner/unicode_table.h（T004）：ID_START = L*、ID_CONTINUE = L*|N* 的码点区间表
# 生成 src/obj/unicode_case.h（T013 D9）：简单大小写映射对表（10 §2 toUpper/toLower）
# 生成物入库；改脚本或升级 Python（unicodedata 随版本变化）后手动重跑：
#   cmake --build build --config Debug --target gen_unicode_table
import sys
import unicodedata
from pathlib import Path

MAX_CP = 0x10FFFF
LETTERS = ("Lu", "Ll", "Lt", "Lm", "Lo")
NUMBERS = ("Nd", "Nl", "No")


def collect_ranges(categories: tuple[str, ...]) -> list[tuple[int, int]]:
    """收集给定 Unicode 类别的连续码点区间（ASCII 段走 C 快路径，不进表）。"""
    wanted = frozenset(categories)
    ranges: list[tuple[int, int]] = []
    start = -1
    prev = -1
    for cp in range(0x80, MAX_CP + 1):
        if unicodedata.category(chr(cp)) in wanted:
            if start < 0:
                start = cp
            prev = cp
        elif start >= 0:
            ranges.append((start, prev))
            start = -1
    if start >= 0:
        ranges.append((start, prev))
    return ranges


def emit_table(name: str, ranges: list[tuple[int, int]]) -> list[str]:
    lines = [f"static const MsUnicodeRange {name}[] = {{"]
    for lo, hi in ranges:
        lines.append(f"    {{0x{lo:X}, 0x{hi:X}}},")
    lines.append("};")
    return lines

HEADER = """\
// 自动生成（tools/gen_unicode_table.py，Unicode {version}）：请勿手改
// ID_START = L*、ID_CONTINUE = L*|N*（01-lexical.md §1；ASCII 走快路径不进表）
#ifndef INCLUDE_MS_SCANNER_UNICODE_TABLE_H_
#define INCLUDE_MS_SCANNER_UNICODE_TABLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MsUnicodeRange {{
  uint32_t lo, hi;
}} MsUnicodeRange;

"""

FOOTER = """\
// 区间表二分查找（static inline：未用函数的引入方不触发 -Wunused-function）
static inline bool msUnicodeInRange(const MsUnicodeRange* ranges, size_t count, uint32_t cp) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < ranges[mid].lo) {
      hi = mid;
    } else if (cp > ranges[mid].hi) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

// ID_START：Unicode 字母或 '_'（ASCII 快路径）
static inline bool msUnicodeIdStart(uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
  }
  return msUnicodeInRange(MS_UNICODE_ID_START, sizeof(MS_UNICODE_ID_START) / sizeof(MS_UNICODE_ID_START[0]), cp);
}

// ID_CONTINUE：ID_START | Unicode 数字（ASCII 快路径）
static inline bool msUnicodeIdContinue(uint32_t cp) {
  if (cp < 0x80) {
    return msUnicodeIdStart(cp) || (cp >= '0' && cp <= '9');
  }
  return msUnicodeInRange(MS_UNICODE_ID_CONTINUE, sizeof(MS_UNICODE_ID_CONTINUE) / sizeof(MS_UNICODE_ID_CONTINUE[0]), cp);
}

#endif  // INCLUDE_MS_SCANNER_UNICODE_TABLE_H_
"""


def collect_case_maps() -> tuple[list[tuple[int, int]], list[tuple[int, int]]]:
    """收集简单大小写映射（0x80 起非 ASCII 段，码点升序）。

    str.upper/lower 做完整映射（如 ß→SS 展开多字符）；仅单字符结果属
    SimpleCaseMapping 进表，其余跳过；映射结果等于自身的码点也无条目。
    """
    upper: list[tuple[int, int]] = []
    lower: list[tuple[int, int]] = []
    for cp in range(0x80, MAX_CP + 1):
        ch = chr(cp)
        up = ch.upper()
        if len(up) == 1 and up != ch:
            upper.append((cp, ord(up)))
        low = ch.lower()
        if len(low) == 1 and low != ch:
            lower.append((cp, ord(low)))
    return upper, lower


def emit_case_table(name: str, pairs: list[tuple[int, int]]) -> list[str]:
    lines = [f"static const MsUnicodeCaseMap {name}[] = {{"]
    for frm, to in pairs:
        lines.append(f"    {{0x{frm:X}, 0x{to:X}}},")
    lines.append("};")
    return lines


CASE_HEADER = """\
// 自动生成（tools/gen_unicode_table.py，Unicode {version}）：请勿手改
// 简单大小写映射（T013 D9，10 §2 toUpper/toLower；ASCII 走快路径不进表）
#ifndef INCLUDE_MS_OBJ_UNICODE_CASE_H_
#define INCLUDE_MS_OBJ_UNICODE_CASE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MsUnicodeCaseMap {{
  uint32_t from, to;
}} MsUnicodeCaseMap;

"""

CASE_FOOTER = """\
// 映射表二分查找（按 from 升序；未命中返回 cp 原值；static inline 同 unicode_table.h）
static inline uint32_t msUnicodeCaseLookup(const MsUnicodeCaseMap* table, size_t count, uint32_t cp) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cp < table[mid].from) {
      hi = mid;
    } else if (cp > table[mid].from) {
      lo = mid + 1;
    } else {
      return table[mid].to;
    }
  }
  return cp;
}

// 简单大写映射（ASCII 快路径 'a'-'z'；无映射返回原码点）
static inline uint32_t msUnicodeToUpper(uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'a' && cp <= 'z') ? cp - 'a' + 'A' : cp;
  }
  return msUnicodeCaseLookup(MS_UNICODE_CASE_UPPER, sizeof(MS_UNICODE_CASE_UPPER) / sizeof(MS_UNICODE_CASE_UPPER[0]), cp);
}

// 简单小写映射（ASCII 快路径 'A'-'Z'；无映射返回原码点）
static inline uint32_t msUnicodeToLower(uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'A' && cp <= 'Z') ? cp - 'A' + 'a' : cp;
  }
  return msUnicodeCaseLookup(MS_UNICODE_CASE_LOWER, sizeof(MS_UNICODE_CASE_LOWER) / sizeof(MS_UNICODE_CASE_LOWER[0]), cp);
}

#endif  // INCLUDE_MS_OBJ_UNICODE_CASE_H_
"""


def write_case_table(out_path: Path, upper: list[tuple[int, int]],
                     lower: list[tuple[int, int]]) -> None:
    parts = [CASE_HEADER.format(version=unicodedata.unidata_version), "// 上向：cp → 大写"]
    parts.extend(emit_case_table("MS_UNICODE_CASE_UPPER", upper))
    parts.extend(["", "// 下向：cp → 小写"])
    parts.extend(emit_case_table("MS_UNICODE_CASE_LOWER", lower))
    parts.extend(["", CASE_FOOTER])
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "src" / "scanner" / "unicode_table.h"
    case_path = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "src" / "obj" / "unicode_case.h"
    start_ranges = collect_ranges(LETTERS)
    continue_ranges = collect_ranges(LETTERS + NUMBERS)
    parts = [HEADER.format(version=unicodedata.unidata_version), "// L*：ID_START"]
    parts.extend(emit_table("MS_UNICODE_ID_START", start_ranges))
    parts.extend(["", "// L* | N*：ID_CONTINUE"])
    parts.extend(emit_table("MS_UNICODE_ID_CONTINUE", continue_ranges))
    parts.extend(["", FOOTER])
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    upper, lower = collect_case_maps()
    write_case_table(case_path, upper, lower)
    print(f"wrote {out_path}: {len(start_ranges)} start ranges, {len(continue_ranges)} continue ranges")
    print(f"wrote {case_path}: {len(upper)} upper maps, {len(lower)} lower maps")


if __name__ == "__main__":
    main()
