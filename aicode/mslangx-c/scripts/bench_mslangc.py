#!/usr/bin/env python3
"""Run mslangc runtime benchmarks."""

import argparse
import pathlib
import statistics
import subprocess
import sys
import time


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_RUNNER = REPO_ROOT / "build" / "Release" / "mslangc.exe"
DEFAULT_BENCH_DIR = REPO_ROOT / "benchmarks" / "runtime"


def parse_args(argv=None):
  parser = argparse.ArgumentParser(description="Run mslangc benchmarks.")
  parser.add_argument(
    "--runner",
    default=str(DEFAULT_RUNNER),
    help="Path to the mslangc executable.")
  parser.add_argument(
    "--bench-dir",
    default=str(DEFAULT_BENCH_DIR),
    help="Directory containing .ms benchmark scripts.")
  parser.add_argument(
    "--iterations",
    type=int,
    default=5,
    help="Timed iterations per benchmark.")
  parser.add_argument(
    "--warmup",
    type=int,
    default=1,
    help="Untimed warmup iterations per benchmark.")
  return parser.parse_args(argv)


def run_once(runner, script):
  start = time.perf_counter()
  result = subprocess.run(
    [str(runner), "--no-cache", str(script)],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    text=True,
    check=False,
  )
  elapsed = time.perf_counter() - start
  if result.returncode != 0:
    raise RuntimeError(
      f"{script.name} failed with exit {result.returncode}: {result.stderr}")
  return elapsed


def main(argv=None):
  args = parse_args(argv)
  runner = pathlib.Path(args.runner)
  bench_dir = pathlib.Path(args.bench_dir)

  if args.iterations <= 0 or args.warmup < 0:
    print("error: iterations must be positive and warmup non-negative",
          file=sys.stderr)
    return 2
  if not runner.is_file():
    print(f"error: runner not found: {runner}", file=sys.stderr)
    return 2
  if not bench_dir.is_dir():
    print(f"error: benchmark directory not found: {bench_dir}", file=sys.stderr)
    return 2

  scripts = sorted(bench_dir.glob("*.ms"))
  if not scripts:
    print(f"error: no benchmarks found in {bench_dir}", file=sys.stderr)
    return 2

  print("benchmark,iterations,min_ms,median_ms,mean_ms")
  for script in scripts:
    for _ in range(args.warmup):
      run_once(runner, script)

    samples = [run_once(runner, script) * 1000.0
               for _ in range(args.iterations)]
    print(f"{script.stem},{args.iterations},"
          f"{min(samples):.3f},{statistics.median(samples):.3f},"
          f"{statistics.fmean(samples):.3f}")

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
