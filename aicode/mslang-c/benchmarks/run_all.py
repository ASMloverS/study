#!/usr/bin/env python3
"""mslang-c benchmark driver.

Usage:
    python benchmarks/run_all.py [--runs N] [--exe PATH] [--baseline PATH]
                                 [--hyperfine] [--with-cache]
"""
import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

CASES_DIR = Path(__file__).parent / "cases"
RESULTS_DIR = Path(__file__).parent / "results"
DEFAULT_BASELINE = Path(__file__).parent / "baseline.json"

REGRESSION_WALL_PCT = 5.0      # ±5% wall-clock triggers review
REGRESSION_INSTR_PCT = 0.5     # ±0.5% instruction count triggers review


def parse_expected(ms_file: Path) -> str | None:
    for line in ms_file.read_text(encoding="utf-8").splitlines():
        m = re.match(r"//\s*expected\s*:\s*(.+)", line)
        if m:
            return m.group(1).strip()
    return None


def run_for_output(exe: str, ms_file: Path) -> str:
    """Run script once (no benchmark) and return last non-empty stdout line."""
    result = subprocess.run(
        [exe, str(ms_file)],
        capture_output=True,
        text=True,
    )
    lines = [l.strip() for l in result.stdout.splitlines() if l.strip()]
    return lines[-1] if lines else ""


def run_benchmark(exe: str, ms_file: Path, runs: int, with_cache: bool) -> dict:
    """Run exe in benchmark mode; return parsed JSON result dict."""
    cmd = [exe, "--benchmark", str(runs), "--stats", "--json"]
    if with_cache:
        cmd.append("--with-cache")
    cmd.append(str(ms_file))

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{ms_file.name}: exe exited {result.returncode}\n"
            f"stderr: {result.stderr[:400]}"
        )

    # stdout contains only the JSON line (script output is silenced in --json mode)
    lines = [l.strip() for l in result.stdout.splitlines() if l.strip()]
    json_line = next((l for l in lines if l.startswith("{") and l.endswith("}")), None)

    if not json_line:
        raise RuntimeError(
            f"{ms_file.name}: no JSON line in output.\nstdout: {result.stdout[:400]}"
        )

    return json.loads(json_line)


def run_hyperfine(exe: str, ms_file: Path, runs: int) -> float | None:
    """Run hyperfine and return median wall-time in ms, or None if unavailable."""
    try:
        tmp = RESULTS_DIR / f"_hf_{ms_file.stem}.json"
        cmd = [
            "hyperfine",
            "--warmup", "1",
            "--runs", str(runs),
            "--export-json", str(tmp),
            "--",
            f"{exe} {ms_file}",
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return None
        hf = json.loads(tmp.read_text())
        median_s = hf["results"][0]["median"]
        tmp.unlink(missing_ok=True)
        return median_s * 1000.0
    except (FileNotFoundError, KeyError, json.JSONDecodeError):
        return None


def fmt_delta(pct: float) -> str:
    if pct > REGRESSION_WALL_PCT:
        return f"[REGRESS] +{pct:.1f}%"
    if pct < -REGRESSION_WALL_PCT:
        return f"[IMPROVE] {pct:.1f}%"
    return f"{pct:+.1f}%"


def build_row(name: str, data: dict, cache_data: dict | None,
              baseline: dict | None, expected: str | None,
              hf_median: float | None,
              actual_last: str = "") -> tuple[list, list]:
    """Return (row_cells, warnings)."""
    best = data.get("best_ms", 0.0)
    median = data.get("median_ms", 0.0)
    compile_ms = cache_data.get("compile_ms_cold", 0.0) if cache_data else 0.0
    cache_warm = cache_data.get("compile_ms_warm", 0.0) if cache_data else 0.0
    instr = data.get("instruction_count", "N/A")
    minor_gc = data.get("minor_gc_count", "N/A")
    major_gc = data.get("major_gc_count", "N/A")
    incr = data.get("incremental_step_count", "N/A")
    deopt = data.get("deopt_event_count", "N/A")
    peak_b = data.get("bytes_allocated_peak", "N/A")
    peak_f = data.get("peak_frame_count", "N/A")
    live_o = data.get("live_objects_after_final_gc", "N/A")

    if hf_median is not None:
        best_disp = f"{best:.1f} (hf:{hf_median:.1f})"
    else:
        best_disp = f"{best:.1f}"

    delta_str = ""
    warnings = []
    if baseline and name in baseline:
        bl = baseline[name]
        bl_best = bl.get("best_ms", 0)
        if bl_best > 0:
            wall_pct = (best - bl_best) / bl_best * 100.0
            delta_str = fmt_delta(wall_pct)
            if wall_pct > REGRESSION_WALL_PCT:
                warnings.append(
                    f"WALL regression {wall_pct:+.1f}% on {name}"
                )
            elif wall_pct < -REGRESSION_WALL_PCT:
                warnings.append(
                    f"WALL improvement {wall_pct:+.1f}% on {name} [informational]"
                )
        bl_instr = bl.get("instruction_count", 0)
        if isinstance(instr, int) and bl_instr > 0:
            instr_pct = (instr - bl_instr) / bl_instr * 100.0
            if instr_pct > REGRESSION_INSTR_PCT:
                warnings.append(
                    f"INSTR regression {instr_pct:+.1f}% on {name}"
                )
            elif instr_pct < -REGRESSION_INSTR_PCT:
                warnings.append(
                    f"INSTR improvement {instr_pct:+.1f}% on {name} [informational]"
                )
        bl_deopt = bl.get("deopt_event_count", None)
        if isinstance(deopt, int) and bl_deopt is not None and deopt != bl_deopt:
            warnings.append(
                f"DEOPT count changed ({bl_deopt} → {deopt}) on {name} — review quickening"
            )

    # Validate expected output
    if expected is not None and actual_last != expected:
        warnings.append(
            f"OUTPUT MISMATCH on {name}: expected={expected!r} got={actual_last!r}"
        )
        name = name + " [FAIL]"
    elif expected is not None:
        name = name + " [OK]"

    row = [
        name,
        best_disp,
        f"{median:.1f}",
        f"{compile_ms:.1f}" if compile_ms else "—",
        f"{cache_warm:.1f}" if cache_warm else "—",
        str(instr) if isinstance(instr, int) else "—",
        str(minor_gc) if isinstance(minor_gc, int) else "—",
        str(major_gc) if isinstance(major_gc, int) else "—",
        str(incr) if isinstance(incr, int) else "—",
        str(deopt) if isinstance(deopt, int) else "—",
        str(peak_b) if isinstance(peak_b, int) else "—",
        str(peak_f) if isinstance(peak_f, int) else "—",
        str(live_o) if isinstance(live_o, int) else "—",
        delta_str or "—",
    ]
    return row, warnings


HEADERS = [
    "name", "best(ms)", "median(ms)", "compile(ms)", "cache_warm(ms)",
    "instr", "minor_gc", "major_gc", "incr_step", "deopt",
    "peak_bytes", "peak_frames", "live_obj", "Δ baseline",
]


def render_markdown(rows: list[list], timestamp: str, runs: int,
                    warnings: list[str]) -> str:
    col_widths = [max(len(h), max((len(r[i]) for r in rows), default=0))
                  for i, h in enumerate(HEADERS)]

    def fmt_row(cells):
        return "| " + " | ".join(
            c.ljust(col_widths[i]) for i, c in enumerate(cells)
        ) + " |"

    sep = "| " + " | ".join("-" * w for w in col_widths) + " |"

    lines = [
        f"# mslang-c Benchmark Results",
        f"",
        f"**Date:** {timestamp}  **Runs:** {runs}",
        f"",
        fmt_row(HEADERS),
        sep,
    ]
    for row in rows:
        lines.append(fmt_row(row))

    if warnings:
        lines += ["", "## Warnings / Review Required", ""]
        for w in warnings:
            lines.append(f"- {w}")

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="mslang-c benchmark driver")
    parser.add_argument("--runs", type=int, default=5, metavar="N")
    parser.add_argument("--exe", default=None,
                        help="Path to mslang-c executable")
    parser.add_argument("--baseline", default=None,
                        help="Path to baseline.json (default: benchmarks/baseline.json)")
    parser.add_argument("--hyperfine", action="store_true",
                        help="Run hyperfine for end-to-end wall time if available")
    parser.add_argument("--with-cache", action="store_true", dest="with_cache",
                        help="Also run with --with-cache and report cache speedup")
    args = parser.parse_args()

    # Resolve exe
    exe = args.exe
    if exe is None:
        # Try common build paths relative to repo root
        repo = Path(__file__).parent.parent
        candidates = [
            repo / "build" / "mslang-c",
            repo / "build" / "mslang-c.exe",
            repo / "build" / "Debug" / "mslang-c.exe",
            repo / "build" / "Release" / "mslang-c.exe",
        ]
        for c in candidates:
            if c.exists():
                exe = str(c)
                break
        if exe is None:
            print("ERROR: mslang-c executable not found. Use --exe PATH.", file=sys.stderr)
            sys.exit(1)

    # Load baseline
    baseline_path = Path(args.baseline) if args.baseline else DEFAULT_BASELINE
    baseline = None
    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    cases = sorted(CASES_DIR.glob("*.ms"))
    if not cases:
        print("No .ms files found in benchmarks/cases/", file=sys.stderr)
        sys.exit(1)

    timestamp = datetime.now().strftime("%Y%m%d-%H%M")
    all_rows = []
    all_warnings = []

    for ms_file in cases:
        name = ms_file.stem
        expected = parse_expected(ms_file)
        print(f"  {name} ...", flush=True)

        # Capture script output for expected-value check
        actual_last = run_for_output(exe, ms_file) if expected is not None else ""

        try:
            data = run_benchmark(exe, ms_file, args.runs, with_cache=False)
        except RuntimeError as e:
            print(f"  FAILED: {e}", file=sys.stderr)
            row = [name + " FAIL"] + ["ERR"] * (len(HEADERS) - 1)
            all_rows.append(row)
            all_warnings.append(f"RUN FAILED: {name}")
            continue

        cache_data = None
        if args.with_cache:
            try:
                cache_data = run_benchmark(exe, ms_file, args.runs, with_cache=True)
            except RuntimeError as e:
                print(f"  (cache run failed: {e})", file=sys.stderr)

        hf_median = None
        if args.hyperfine:
            hf_median = run_hyperfine(exe, ms_file, args.runs)

        row, warnings = build_row(
            name, data, cache_data, baseline, expected, hf_median, actual_last
        )
        all_rows.append(row)
        all_warnings.extend(warnings)

    md = render_markdown(all_rows, timestamp, args.runs, all_warnings)
    out_path = RESULTS_DIR / f"{timestamp}.md"
    out_path.write_text(md, encoding="utf-8")
    print(f"\nResults written to {out_path}")

    if all_warnings:
        print("\nWarnings:")
        for w in all_warnings:
            print(f"  {w}")
        # Exit 1 only for actual regressions or output mismatches, not improvements.
        regressions = [w for w in all_warnings if "[informational]" not in w]
        if regressions:
            sys.exit(1)


if __name__ == "__main__":
    main()
