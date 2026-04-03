#!/usr/bin/env python3
"""Run mslang4 C unit tests.

Usage:
    python run_unit_tests.py              # build + run all unit tests
    python run_unit_tests.py chunk        # build + run test_chunk only
    python run_unit_tests.py ast scanner  # build + run multiple tests
    python run_unit_tests.py --list       # list available test targets
    python run_unit_tests.py --no-build   # skip build, just run tests
"""

import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent
BUILD_DIR = PROJECT_ROOT / "build"
TESTS_DIR = BUILD_DIR / "tests"

EXCLUDED = {"test_value"}

ALL_TESTS = [
    "test_common",
    "test_memory",
    "test_platform",
    "test_logger",
    "test_token",
    "test_value",
    "test_object_string",
    "test_table",
    "test_scanner",
    "test_ast",
    "test_parser",
    "test_chunk",
]


def run(cmd, **kwargs):
    return subprocess.run(cmd, shell=True, **kwargs).returncode


def configure():
    print(">> Configuring CMake ...")
    rc = run(
        'cmake -B build -DCMAKE_C_COMPILER=clang -G "NMake Makefiles"',
        cwd=PROJECT_ROOT,
    )
    if rc != 0:
        print("ERROR: CMake configure failed.", file=sys.stderr)
    return rc


def build(target=None):
    cmd = f"cmake --build {BUILD_DIR}"
    if target:
        cmd += f" --target {target}"
    print(f">> Building {target or 'all'} ...")
    return run(cmd)


def run_one(name):
    exe = TESTS_DIR / (name + ".exe")
    if not exe.is_file():
        print(f"  [SKIP] {name} (executable not found)")
        return -1
    print(f">> Running {name} ...")
    r = subprocess.run([str(exe)], cwd=PROJECT_ROOT)
    return r.returncode


def resolve_names(args):
    names = []
    for a in args:
        t = a if a.startswith("test_") else f"test_{a}"
        names.append(t)
    return names


def main():
    args = sys.argv[1:]

    if "--list" in args:
        print("Available test targets:")
        for t in ALL_TESTS:
            flag = " (excluded)" if t in EXCLUDED else ""
            print(f"  {t}{flag}")
        return

    do_build = "--no-build" not in args
    args = [a for a in args if not a.startswith("--")]

    if do_build:
        if not (BUILD_DIR / "CMakeCache.txt").is_file():
            rc = configure()
            if rc != 0:
                sys.exit(rc)

    if args:
        targets = resolve_names(args)
    else:
        targets = [t for t in ALL_TESTS if t not in EXCLUDED]

    if do_build:
        for t in targets:
            rc = build(t)
            if rc != 0:
                print(f"ERROR: Build {t} failed.", file=sys.stderr)
                sys.exit(rc)

    passed = 0
    failed = 0
    errors = []

    for t in targets:
        rc = run_one(t)
        if rc == 0:
            passed += 1
            print(f"  [PASS] {t}")
        else:
            failed += 1
            errors.append((t, rc))
            print(f"  [FAIL] {t} (exit {rc})")

    print(f"\n{'=' * 50}")
    print(f"Results: {passed} passed, {failed} failed, {len(targets)} total")
    if errors:
        print("Failed:")
        for name, rc in errors:
            print(f"  {name} (exit {rc})")
        sys.exit(1)


if __name__ == "__main__":
    main()
