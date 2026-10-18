#!/usr/bin/env python3
"""Generate or check `ms --dump-ast` snapshot baselines (.ast) for conformance cases.

Baselines are test assets: regenerate only via this tool (never hand-edit).
Usage:
  python tools/gen_ast_snapshots.py <ms-exe> <case.ms> [<case.ms> ...]          # write .ast
  python tools/gen_ast_snapshots.py --check <ms-exe> <case.ms> [<case.ms> ...]  # verify, exit 1 on diff
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def dump_bytes(ms_exe: Path, case: Path) -> bytes:
    proc = subprocess.run(
        [str(ms_exe), "--dump-ast", str(case)], capture_output=True, check=False
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"{case}: ms exited {proc.returncode}\n{proc.stderr.decode(errors='replace')}"
        )
    return proc.stdout.replace(b"\r\n", b"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare instead of writing")
    parser.add_argument("ms_exe", type=Path)
    parser.add_argument("cases", type=Path, nargs="+")
    args = parser.parse_args()
    mismatch = 0
    for case in args.cases:
        snapshot = case.with_suffix(".ast")
        output = dump_bytes(args.ms_exe, case)
        if args.check:
            if not snapshot.exists():
                print(f"MISSING {snapshot}")
                mismatch += 1
            elif snapshot.read_bytes() != output:
                print(f"MISMATCH {snapshot}")
                mismatch += 1
        else:
            snapshot.write_bytes(output)
            print(f"wrote {snapshot}")
    if args.check and mismatch == 0:
        print(f"ok: {len(args.cases)} snapshots match")
    return 1 if mismatch else 0


if __name__ == "__main__":
    sys.exit(main())
