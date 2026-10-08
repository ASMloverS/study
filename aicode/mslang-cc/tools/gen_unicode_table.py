#!/usr/bin/env python3
# 生成 src/scanner/unicode_table.h（T004）：ID_START = L*、ID_CONTINUE = L*|N* 的码点区间表
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
// 区间表二分查找
static bool msUnicodeInRange(const MsUnicodeRange* ranges, size_t count, uint32_t cp) {
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
static bool msUnicodeIdStart(uint32_t cp) {
  if (cp < 0x80) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
  }
  return msUnicodeInRange(MS_UNICODE_ID_START, sizeof(MS_UNICODE_ID_START) / sizeof(MS_UNICODE_ID_START[0]), cp);
}

// ID_CONTINUE：ID_START | Unicode 数字（ASCII 快路径）
static bool msUnicodeIdContinue(uint32_t cp) {
  if (cp < 0x80) {
    return msUnicodeIdStart(cp) || (cp >= '0' && cp <= '9');
  }
  return msUnicodeInRange(MS_UNICODE_ID_CONTINUE, sizeof(MS_UNICODE_ID_CONTINUE) / sizeof(MS_UNICODE_ID_CONTINUE[0]), cp);
}

#endif  // INCLUDE_MS_SCANNER_UNICODE_TABLE_H_
"""


def main() -> None:
    default_out = Path(__file__).resolve().parents[1] / "src" / "scanner" / "unicode_table.h"
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_out
    start_ranges = collect_ranges(LETTERS)
    continue_ranges = collect_ranges(LETTERS + NUMBERS)
    parts = [HEADER.format(version=unicodedata.unidata_version), "// L*：ID_START"]
    parts.extend(emit_table("MS_UNICODE_ID_START", start_ranges))
    parts.extend(["", "// L* | N*：ID_CONTINUE"])
    parts.extend(emit_table("MS_UNICODE_ID_CONTINUE", continue_ranges))
    parts.extend(["", FOOTER])
    out_path.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"wrote {out_path}: {len(start_ranges)} start ranges, {len(continue_ranges)} continue ranges")


if __name__ == "__main__":
    main()
