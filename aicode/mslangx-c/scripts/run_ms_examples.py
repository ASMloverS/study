#!/usr/bin/env python3
"""Run all runnable .ms example scripts under the examples directory."""

import argparse
import pathlib
import shutil
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_RUNNER = REPO_ROOT / "build" / "Debug" / "mslangc.exe"
DEFAULT_CONTAINERS_RUNNER = REPO_ROOT / "build" / "tests" / "Debug" / "containers_e2e_runner.exe"
DEFAULT_EXAMPLES_DIR = "examples"


def parse_args(argv=None):
  parser = argparse.ArgumentParser(
    description="Execute all runnable .ms example scripts.")
  parser.add_argument(
    "--runner",
    default=str(DEFAULT_RUNNER),
    help="Path or command name for the mslangc executable.")
  parser.add_argument(
    "--containers-runner",
    default=str(DEFAULT_CONTAINERS_RUNNER),
    help="Path or command name for the containers examples runner.")
  parser.add_argument(
    "--root",
    default=str(REPO_ROOT),
    help="Repository root or directory containing the examples tree.")
  parser.add_argument(
    "--examples-dir",
    default=DEFAULT_EXAMPLES_DIR,
    help="Examples directory name relative to --root.")
  return parser.parse_args(argv)


def resolve_runner(runner_arg):
  candidate = pathlib.Path(runner_arg)
  if candidate.exists():
    return candidate.resolve()

  resolved = shutil.which(runner_arg)
  if resolved is None:
    return None
  return pathlib.Path(resolved)


def resolve_examples_root(root, examples_dir_name):
  root = pathlib.Path(root)
  candidate = root / examples_dir_name
  if candidate.is_dir():
    return candidate
  return root


def find_ms_files(root, examples_dir_name=DEFAULT_EXAMPLES_DIR):
  base = resolve_examples_root(root, examples_dir_name)
  files = []
  for path in base.rglob("*.ms"):
    if not path.is_file():
      continue
    expected_stdout = path.with_name(path.name + ".out")
    if not expected_stdout.is_file():
      continue
    files.append(path.relative_to(root))
  return sorted(files, key=lambda path: path.as_posix())


def run_script(runner, script_path):
  return subprocess.run(
    [str(runner), str(script_path)],
    capture_output=True,
    text=True,
    check=False,
  )


def resolve_runner_for_script(default_runner, containers_runner, script_path):
  if len(script_path.parts) >= 2 and script_path.parts[0] == "examples" and script_path.parts[1] == "containers":
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

  scripts = find_ms_files(root, args.examples_dir)
  if not scripts:
    examples_root = resolve_examples_root(root, args.examples_dir)
    print(f"No .ms example scripts found under {examples_root}")
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
