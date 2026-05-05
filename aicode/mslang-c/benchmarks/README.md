# mslang-c Benchmark Suite

## Quick Start

```bash
# Configure (Release + stats + benchmarks enabled)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DMSLANG_BUILD_BENCHMARKS=ON -DMSLANG_VM_STATS=ON

# Build
cmake --build build --config Release

# Run all benchmarks (5 runs each, write results/YYYYMMDD-HHMM.md)
python benchmarks/run_all.py --runs 5

# With cache speedup column
python benchmarks/run_all.py --runs 5 --with-cache

# With hyperfine end-to-end wall time (requires hyperfine in PATH)
python benchmarks/run_all.py --runs 5 --hyperfine

# Via CMake target (5 runs, Release exe)
cmake --build build --target bench
```

## Single-Case Commands

```bash
# Human-readable benchmark output
./build/Release/mslang-c --benchmark 5 benchmarks/cases/fib_recursive.ms

# With VM stats (requires MSLANG_VM_STATS=ON build)
./build/Release/mslang-c --benchmark 5 --stats benchmarks/cases/fib_recursive.ms

# JSON output (for scripting)
./build/Release/mslang-c --benchmark 5 --json benchmarks/cases/fib_recursive.ms

# With cache comparison (cold vs warm compile)
./build/Release/mslang-c --benchmark 5 --with-cache --stats benchmarks/cases/fib_recursive.ms
```

## Regenerating the Baseline

```bash
python benchmarks/gen_baseline.py --runs 9
```

This overwrites `benchmarks/baseline.json` with fresh measurements. Re-baseline after:
- A major algorithmic change to the VM dispatch loop or GC
- An intentional peephole / quickening improvement that shifts instruction counts
- A platform or compiler change that meaningfully shifts wall-clock times

Commit the new `baseline.json` together with a note explaining the change.

## Benchmark Cases

| # | File | Subsystem | Hot path |
|---|------|-----------|----------|
| 1 | `arith_loop.ms` | Integer arithmetic + loop | `OP_ADD_II`/`OP_LT_II`/`OP_LOOP` (quickening) |
| 2 | `fib_recursive.ms` | Function call / return | `OP_CALL`/`OP_RETURN` |
| 3 | `fib_iterative.ms` | Loop + local registers | `OP_LT_II`/`OP_ADD_II` (quickening) |
| 4 | `closure_counter.ms` | Upvalue read/write | `OP_GET_UPVAL`/`OP_SET_UPVAL` |
| 5 | `shape_mono_field.ms` | Monomorphic IC | `OP_GET_PROPERTY` hit |
| 6 | `shape_poly_field.ms` | Polymorphic IC (3-way) | `OP_GET_PROPERTY` degraded |
| 7 | `method_dispatch.ms` | Method dispatch | `OP_INVOKE` |
| 8 | `binary_trees_alloc.ms` | Alloc-heavy + GC pressure | `NEW_INSTANCE` + GC; observe `incremental_step_count`/`major_gc_count` |
| 9 | `list_grow_iter.ms` | List growth + `for in` | `OP_BUILD_LIST` / iterator |
| 10 | `map_insert_lookup.ms` | Hash map read/write | `OP_SET_INDEX`/`OP_GET_INDEX` |
| 11 | `string_interp_loop.ms` | String interpolation | Template execution path |
| 12 | `generator_yield.ms` | Coroutine yield/resume | Yield path |
| 13 | `quickening_deopt.ms` | Deopt path (T29) | Mixed int/float triggers `DEOPT_AND_RESPECIALIZE` |

## Adding a New Case

1. Create `benchmarks/cases/<name>.ms` following this structure:

   ```ms
   // purpose : <one-line description>
   // hot-op  : <primary opcode(s)>
   // expected: <last print value for output validation>
   // category: <quickening | gc-pressure | deopt | baseline>
   var N = <tunable parameter>
   // ... benchmark body ...
   print(<checksum>)
   ```

2. Tune `N` so a Release single-run takes **0.5–3 s** on a reference machine.

3. Verify the `expected:` comment matches actual output:
   ```bash
   ./build/Release/mslang-c benchmarks/cases/<name>.ms
   ```

4. Mark the category:
   - `quickening` — exercises specialized opcodes (ADD_II, LT_II, etc.)
   - `gc-pressure` — high allocation rate, GC-heavy
   - `deopt` — intentionally triggers `DEOPT_AND_RESPECIALIZE`
   - `baseline` — general throughput, not category-specific

5. Run `python benchmarks/run_all.py --runs 3` and confirm the case shows `[OK]`.

## Regression Thresholds

| Metric | Threshold | Action |
|--------|-----------|--------|
| Wall-clock time | ±5% vs baseline | Flag `[REGRESS]`/`[IMPROVE]` in report; manual review |
| Instruction count | ±0.5% vs baseline | Flag in warnings; investigate opcode schedule change |
| `deopt_event_count` | Any absolute change | Flag unconditionally; quickening hit-rate regression is high-priority |

The `deopt_event_count` threshold is absolute (not percentage) because even a single extra deopt event can indicate a quickening regression on the `quickening_deopt.ms` case.

## MSLANG_VM_STATS=OFF Zero-Cost Promise

When built with `MSLANG_VM_STATS=OFF` (the default):
- All stat increment paths are compiled out via `#ifdef MSLANG_VM_STATS` guards.
- `MsVMStats` is an empty struct (`sizeof == 0` on GCC/Clang, implementation-defined on MSVC but no fields means no layout cost).
- `--stats` flag is accepted but produces no output.
- Binary size difference vs `MSLANG_VM_STATS=ON` is bounded to function symbol overhead only.

Run `ctest --output-on-failure` after a default (`MSLANG_VM_STATS=OFF`) build to confirm no behavioral difference.

## Report Format

Results are written to `benchmarks/results/YYYYMMDD-HHMM.md` with columns:

```
name | best(ms) | median(ms) | compile(ms) | cache_warm(ms) | instr | minor_gc | major_gc | incr_step | deopt | peak_bytes | peak_frames | live_obj | Δ baseline
```

- `compile(ms)` — cold compile time (no cache); populated only with `--with-cache`
- `cache_warm(ms)` — warm compile time (cache hit); populated only with `--with-cache`
- `incr_step` — incremental GC steps triggered (T30)
- `deopt` — `DEOPT_AND_RESPECIALIZE` events (T29 quickening)
- `Δ baseline` — wall-clock % change vs `baseline.json`

## Baseline Provenance

`baseline.json` was first captured with:
- Build: Release + `MSLANG_VM_STATS=ON`
- Runs: 9 per case
- Mode: `--with-cache` (records both cold and warm compile times)
- Tasks covered: T28 (peephole), T29 (quickening), T30 (incremental GC + .msc cache)
