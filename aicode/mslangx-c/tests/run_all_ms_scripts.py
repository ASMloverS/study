#!/usr/bin/env python3
"""Run all .ms scripts with the configured mslangc runner."""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_RUNNER = REPO_ROOT / "build" / "Debug" / "mslangc.exe"
DEFAULT_CONTAINERS_RUNNER = REPO_ROOT / "build" / "tests" / "Debug" / "containers_e2e_runner.exe"
EXCLUDED_DIR_NAMES = {"build", "fixtures", "lexer", "parser_expr", "parser_decl_stmt"}
RUNNABLE_SCRIPTS_CACHE = {}


def parse_args(argv=None):
  parser = argparse.ArgumentParser(
    description="Execute all .ms scripts under a directory.")
  parser.add_argument(
    "--runner",
    default=str(DEFAULT_RUNNER),
    help="Path or command name for the mslangc executable.")
  parser.add_argument(
    "--containers-runner",
    default=str(DEFAULT_CONTAINERS_RUNNER),
    help="Path or command name for the containers e2e runner.")
  parser.add_argument(
    "--root",
    default=str(REPO_ROOT),
    help="Directory to scan recursively for .ms files.")
  return parser.parse_args(argv)


def resolve_runner(runner_arg):
  candidate = pathlib.Path(runner_arg)
  if candidate.exists():
    return candidate.resolve()

  resolved = shutil.which(runner_arg)
  if resolved is None:
    return None
  return pathlib.Path(resolved)


def find_ms_files(root):
  root = pathlib.Path(root)
  runnable_scripts = load_runnable_script_paths(root)
  files = []
  for path in root.rglob("*.ms"):
    if not path.is_file():
      continue
    if any(part in EXCLUDED_DIR_NAMES for part in path.parts):
      continue
    relative_path = path.relative_to(root)
    if runnable_scripts is None:
      expected_stdout = path.with_name(path.name + ".out")
      if not expected_stdout.is_file():
        continue
    elif relative_path.as_posix() not in runnable_scripts:
      continue
    files.append(relative_path)
  return sorted(files, key=lambda path: path.as_posix())


def load_runnable_script_paths(root):
  root = pathlib.Path(root)
  cmake_path = root / "tests" / "CMakeLists.txt"
  cache_key = str(cmake_path.resolve()) if cmake_path.exists() else None

  if cache_key is not None and cache_key in RUNNABLE_SCRIPTS_CACHE:
    return RUNNABLE_SCRIPTS_CACHE[cache_key]
  if not cmake_path.is_file():
    return None

  text = cmake_path.read_text(encoding="utf-8")
  runnable_scripts = set()

  for match in re.finditer(r"mslangc_add_e2e_basic_test\(\s*[^\s]+\s+([^) \r\n]+)\)", text):
    runnable_scripts.add(f"tests/e2e/basic/{match.group(1)}")

  for match in re.finditer(r'mslangc_add_e2e_named_test\(\s*[^\s]+\s+"([^"]+\.ms)"', text):
    runnable_scripts.add(f"tests/{match.group(1)}")

  for match in re.finditer(r"mslangc_add_cli_test\((.*?)(?=\nmslangc_add_|\Z)", text, re.DOTALL):
    block = match.group(1)
    if "-DEXPECT_EXIT=0" not in block:
      continue
    script_match = re.search(r'-DSCRIPT="\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"]+\.ms)"', block)
    if script_match is not None:
      runnable_scripts.add(f"tests/{script_match.group(1)}")

  if cache_key is not None:
    RUNNABLE_SCRIPTS_CACHE[cache_key] = runnable_scripts
  return runnable_scripts


def run_script(runner, script_path):
  return subprocess.run(
    [str(runner), str(script_path)],
    capture_output=True,
    text=True,
    check=False,
  )


def resolve_runner_for_script(default_runner, containers_runner, script_path):
  if len(script_path.parts) >= 2 and script_path.parts[0] == "e2e" and script_path.parts[1] == "containers":
    return containers_runner
  return default_runner


def main(argv=None):
  args = parse_args(argv)
  root = pathlib.Path(args.root).resolve()
  runner = resolve_runner(args.runner)
  containers_runner = resolve_runner(args.containers_runner)
  if runner is None:
    print(f"error: runner not found: {args.runner}", file=sys.stderr)
    return 2

  scripts = find_ms_files(root)
  if not scripts:
    print(f"No .ms scripts found under {root}")
    return 0

  passed = 0
  failed = 0
  for relative_path in scripts:
    script_path = root / relative_path
    script_runner = resolve_runner_for_script(runner, containers_runner, relative_path)
    if script_runner is None:
      print(
        f"error: runner not found for {relative_path.as_posix()}",
        file=sys.stderr,
      )
      return 2

    result = run_script(script_runner, script_path)
    if result.stdout:
      sys.stdout.write(result.stdout)
      if not result.stdout.endswith("\n"):
        sys.stdout.write("\n")
    if result.stderr:
      sys.stderr.write(result.stderr)
      if not result.stderr.endswith("\n"):
        sys.stderr.write("\n")

    if result.returncode == 0:
      passed += 1
      print(f"PASS {relative_path.as_posix()}")
    else:
      failed += 1
      print(f"FAIL {relative_path.as_posix()} (exit {result.returncode})")

  print(f"Summary: {passed} passed, {failed} failed, {len(scripts)} total")
  return 0 if failed == 0 else 1


if __name__ == "__main__":
  raise SystemExit(main())
