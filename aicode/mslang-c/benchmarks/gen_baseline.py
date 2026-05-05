#!/usr/bin/env python3
"""Generate benchmarks/baseline.json from 9-run benchmark suite.

Usage:
    python benchmarks/gen_baseline.py [--exe PATH] [--runs N]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

CASES_DIR = Path(__file__).parent / "cases"
BASELINE_PATH = Path(__file__).parent / "baseline.json"

BASELINE_FIELDS = [
    "best_ms", "median_ms",
    "instruction_count", "bytes_allocated_peak",
    "incremental_step_count", "deopt_event_count",
    "minor_gc_count", "major_gc_count",
    "peak_frame_count", "live_objects_after_final_gc",
]


def run_benchmark(exe: str, ms_file: Path, runs: int, with_cache: bool,
                  timeout: int = 120) -> dict:
    cmd = [exe, "--benchmark", str(runs), "--stats", "--json"]
    if with_cache:
        cmd.append("--with-cache")
    cmd.append(str(ms_file))
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"{ms_file.name}: timed out after {timeout}s")
    if result.returncode != 0:
        raise RuntimeError(
            f"{ms_file.name}: exited {result.returncode}\nstderr: {result.stderr[:400]}"
        )
    lines = [l.strip() for l in result.stdout.splitlines() if l.strip()]
    json_line = next((l for l in lines if l.startswith("{") and l.endswith("}")), None)
    if not json_line:
        raise RuntimeError(
            f"{ms_file.name}: no JSON in output.\nstdout: {result.stdout[:400]}"
        )
    return json.loads(json_line)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=None)
    parser.add_argument("--runs", type=int, default=9)
    args = parser.parse_args()

    exe = args.exe
    if exe is None:
        repo = Path(__file__).parent.parent
        candidates = [
            repo / "build" / "Release" / "mslang-c.exe",
            repo / "build" / "mslang-c",
            repo / "build" / "Debug" / "mslang-c.exe",
        ]
        for c in candidates:
            if c.exists():
                exe = str(c)
                break
        if exe is None:
            print("ERROR: executable not found. Use --exe PATH.", file=sys.stderr)
            sys.exit(1)

    print(f"Using exe: {exe}")
    print(f"Runs: {args.runs}")

    cases = sorted(CASES_DIR.glob("*.ms"))
    baseline = {}

    for ms_file in cases:
        name = ms_file.stem
        print(f"  {name} ...", flush=True)

        # timeout: 30s per run + 30s margin; 1-run fallback uses 60s
        multi_timeout = args.runs * 30 + 30
        data = None
        for attempt_runs, tmo in ((args.runs, multi_timeout), (1, 60)):
            try:
                data = run_benchmark(exe, ms_file, attempt_runs, with_cache=False,
                                     timeout=tmo)
                if attempt_runs < args.runs:
                    print(f"  (fell back to {attempt_runs} run due to crash/timeout)",
                          file=sys.stderr)
                break
            except RuntimeError as e:
                if attempt_runs == 1:
                    print(f"  FAILED even at 1 run: {e}", file=sys.stderr)
                else:
                    print(f"  ({args.runs}-run failed ({e}), retrying with 1 run)",
                          file=sys.stderr)

        if data is None:
            continue

        entry = {f: data.get(f) for f in BASELINE_FIELDS if f in data}

        for attempt_runs, tmo in ((args.runs, multi_timeout), (1, 60)):
            try:
                cache_data = run_benchmark(exe, ms_file, attempt_runs, with_cache=True,
                                           timeout=tmo)
                entry["compile_ms_cold"] = cache_data.get("compile_ms_cold")
                entry["compile_ms_warm"] = cache_data.get("compile_ms_warm")
                break
            except RuntimeError as e:
                if attempt_runs == 1:
                    print(f"  (cache run failed: {e})", file=sys.stderr)
                else:
                    print(f"  (cache {attempt_runs}-run failed ({e}), retrying 1 run)",
                          file=sys.stderr)

        baseline[name] = entry
        print(f"    best={entry.get('best_ms', '?'):.1f}ms "
              f"instr={entry.get('instruction_count', 'N/A')}")

    BASELINE_PATH.write_text(
        json.dumps(baseline, indent=2) + "\n",
        encoding="utf-8"
    )
    print(f"\nBaseline written to {BASELINE_PATH}")
    print(f"Cases: {len(baseline)}/13")


if __name__ == "__main__":
    main()
