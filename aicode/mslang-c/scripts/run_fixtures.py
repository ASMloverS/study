#!/usr/bin/env python3
"""Run all *.ms fixture scripts and report pass/fail."""

import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_executable(build_dir: str) -> str:
    for name in ("mslang-c.exe", "mslang-c"):
        path = os.path.join(build_dir, name)
        if os.path.isfile(path):
            return path
    return ""


def run_fixtures(fixture_dir: str, exe: str, verbose: bool) -> bool:
    scripts = sorted(glob.glob(os.path.join(fixture_dir, "**", "*.ms"), recursive=True))
    if not scripts:
        print(f"No *.ms files found in {fixture_dir}")
        return False

    passed = failed = 0
    for script in scripts:
        rel = os.path.relpath(script, ROOT)
        try:
            result = subprocess.run(
                [exe, script],
                capture_output=True,
                text=True,
                timeout=10,
            )
            ok = result.returncode == 0
        except subprocess.TimeoutExpired:
            result = None
            ok = False

        if ok:
            passed += 1
            status = "PASS"
        else:
            failed += 1
            status = "FAIL"

        print(f"  [{status}] {rel}")

        if not ok or verbose:
            if result and result.stdout.strip():
                for line in result.stdout.strip().splitlines():
                    print(f"         {line}")
            if result and result.stderr.strip():
                for line in result.stderr.strip().splitlines():
                    print(f"  stderr:{line}")
            if result is None:
                print("         (timeout)")

    total = passed + failed
    print(f"\n{passed}/{total} passed", end="")
    if failed:
        print(f"  ({failed} failed)")
    else:
        print()
    return failed == 0


def main() -> None:
    parser = argparse.ArgumentParser(description="Run mslang-c fixture scripts")
    parser.add_argument("--build", default=os.path.join(ROOT, "build/Debug"),
                        help="Build directory (default: <root>/build/Debug)")
    parser.add_argument("--fixtures", default=os.path.join(ROOT, "tests", "fixtures"),
                        help="Fixture directory (default: tests/fixtures)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show output for passing tests too")
    args = parser.parse_args()

    exe = find_executable(args.build)
    if not exe:
        print(f"Error: mslang-c executable not found in {args.build}")
        print("Run: cmake --build build")
        sys.exit(1)

    print(f"Executable: {exe}")
    print(f"Fixtures:   {args.fixtures}\n")

    ok = run_fixtures(args.fixtures, exe, args.verbose)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
