"""
formatter.py — C source file formatter (Google C Style, UTF-8, LF).

Pipeline per file (order fixed):
  1. Encoding  — detect with charset_normalizer; re-encode to UTF-8 if needed
  2. Line endings + trailing whitespace — CRLF→LF, rstrip each line, ensure final newline
  3. Code style — clang-format -i
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from charset_normalizer import from_path


# ---------------------------------------------------------------------------
# Result type
# ---------------------------------------------------------------------------

@dataclass
class FormatResult:
    path: Path
    issues: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return len(self.issues) == 0


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _fix_encoding(path: Path, check: bool) -> list[str]:
    """Step 1: ensure UTF-8 encoding. Returns list of issue strings."""
    result = from_path(path)
    best = result.best()
    if best is None:
        return [f"encoding: could not detect encoding"]
    encoding = best.encoding.lower().replace("-", "")
    if encoding in ("utf8", "ascii"):
        return []
    # Not UTF-8 — need re-encoding
    if check:
        return [f"encoding: file is {best.encoding}, expected UTF-8"]
    path.write_bytes(str(best).encode("utf-8"))
    return []


def _fix_line_endings(path: Path, check: bool) -> list[str]:
    """Step 2: CRLF→LF, strip trailing whitespace per line, ensure final newline."""
    raw = path.read_bytes()
    fixed = raw.replace(b"\r\n", b"\n")
    lines = fixed.split(b"\n")
    stripped = [line.rstrip() for line in lines]
    # Ensure single trailing newline
    while stripped and stripped[-1] == b"":
        stripped.pop()
    result = b"\n".join(stripped) + b"\n"
    if result == raw:
        return []
    if check:
        issues = []
        if b"\r\n" in raw:
            issues.append("line-endings: CRLF found")
        if any(line != line.rstrip() for line in fixed.split(b"\n")):
            issues.append("line-endings: trailing whitespace found")
        return issues or ["line-endings: formatting differs"]
    path.write_bytes(result)
    return []


def _fix_style(path: Path, check: bool) -> list[str]:
    """Step 3: clang-format."""
    if check:
        proc = subprocess.run(
            ["clang-format", "--dry-run", "--Werror", str(path)],
            capture_output=True,
        )
        if proc.returncode != 0:
            return ["clang-format: style issues found"]
        return []
    proc = subprocess.run(
        ["clang-format", "-i", str(path)],
        capture_output=True,
    )
    if proc.returncode != 0:
        return [f"clang-format: failed — {proc.stderr.decode().strip()}"]
    return []


# ---------------------------------------------------------------------------
# Shared utilities (also imported by format_watch)
# ---------------------------------------------------------------------------

def _parse_extensions(raw: str) -> list[str]:
    """Parse a comma-separated extension string into a list of bare extensions."""
    return [e.strip().lstrip(".") for e in raw.split(",")]


def _print_result(result: "FormatResult", verbose: bool) -> None:
    """Print a FormatResult status line and any issues."""
    if verbose or not result.ok:
        status = "FAIL" if not result.ok else "OK  "
        print(f"[{status}] {result.path}")
    for issue in result.issues:
        print(f"       {issue}")


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def format_file(path: Path, check: bool = False) -> FormatResult:
    """Format (or check) a single C/H source file.

    Pipeline: encoding → line endings → clang-format.
    In check mode no files are modified; issues are reported instead.
    """
    result = FormatResult(path=path)
    for step in (_fix_encoding, _fix_line_endings, _fix_style):
        result.issues.extend(step(path, check))
    return result


def format_files(paths: list[Path], check: bool = False) -> list[FormatResult]:
    """Format (or check) a list of files. Returns one FormatResult per file."""
    return [format_file(p, check) for p in paths]


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _collect_paths(targets: list[str], extensions: list[str]) -> list[Path]:
    """Expand files/dirs into a flat, deduplicated list of matching source files."""
    paths: list[Path] = []
    for t in targets:
        p = Path(t)
        if p.is_file():
            paths.append(p)
        elif p.is_dir():
            for ext in extensions:
                paths.extend(sorted(p.rglob(f"*.{ext}")))
    return list(dict.fromkeys(paths))


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="formatter.py",
        description="Format C/H source files to Google C Style with UTF-8/LF.",
    )
    parser.add_argument(
        "-c", "--check",
        action="store_true",
        help="Check mode: do not modify files, exit 1 if issues found",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print each processed file",
    )
    parser.add_argument(
        "-e", "--ext",
        default="c,h",
        help="Comma-separated file extensions to process (default: c,h)",
    )
    parser.add_argument(
        "targets",
        nargs="*",
        metavar="FILE|DIR",
        help="Files or directories to process (reads from stdin if omitted)",
    )
    args = parser.parse_args()

    extensions = _parse_extensions(args.ext)

    if args.targets:
        paths = _collect_paths(args.targets, extensions)
    else:
        # Read file paths from stdin, one per line
        stdin_lines = [line.rstrip("\n") for line in sys.stdin if line.strip()]
        paths = _collect_paths(stdin_lines, extensions)

    if not paths:
        return

    results = format_files(paths, check=args.check)

    has_issues = False
    for r in results:
        _print_result(r, args.verbose)
        if not r.ok:
            has_issues = True

    if args.check and has_issues:
        sys.exit(1)


if __name__ == "__main__":
    main()
