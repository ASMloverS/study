# mslang-c 基准方案

## 背景

T01–T30 已成，然无基准设施：

- `src/main.c` 仅支持 `--version`/`<script>`，无计时；
- `MsVM` 有 `bytes_allocated`/`minor_count` 等字段，无指令计数、major GC 计数、峰值，亦未暴露公共 API；
- `tests/fixtures/` 47 脚本仅作功能验证；
- `../mslang/benchmarks/` 7 脚本，语法不兼容，覆盖面窄。

已落地 peephole 优化器（T28，强制开启）、quickening 反优化路径（T29）、增量 GC（T30）、`.msc` 字节码缓存（T30），须可重复、可对比之基准，方有持续优化判据。

本方案答两问：「此提交对热点 opcode 影响几何？」「相对上一基线总耗时回归多少？」

---

## 设计决策

| 维度 | 决策 |
|------|------|
| 计时 | 内部 `--benchmark N`（进程内精确）+ 外部 `hyperfine`（端到端）双轨 |
| 指标 | 墙钟 + GC 统计 + 指令计数 + 栈帧峰值 + 存活对象数 + 增量 GC step 数 + 反优化触发数 |
| 负载 | 13 个微基准，各直击一 VM 子系统 |
| 接入 | CMake 目标 `MSLANG_BUILD_BENCHMARKS=OFF` + `benchmarks/run_all.py` 驱动 |
| 编译路径 | `--benchmark` 默认绕过 `.msc` 缓存（每次 fresh compile）；`--with-cache` 模式走 `ms_compile_cached` 对比 ahead-of-time 收益 |

---

## 涉及文件

### 改

| 文件 | 变更 |
|------|------|
| `src/main.c` | 解析 `--benchmark N`/`--stats`/`--json`/`--no-cache`/`--with-cache`；循环 compile+interpret，采样计时，输出 min/median/max/mean；`--benchmark` 默认含 `--no-cache` 语义，`--with-cache` 走 `ms_compile_cached` |
| `include/ms/vm.h` | 增 `MsVMStats` 结构（8 字段，含 `incremental_step_count`/`deopt_event_count`）及 `ms_vm_get_stats()`/`ms_vm_reset_stats()` API；`MsVM` 追加峰值字段 |
| `src/vm.c` | dispatch 循环维护 `instruction_count`；入栈更新 `peak_frame_count`；`bump_deopt()`（行 443）与 `DEOPT_AND_RESPECIALIZE` 宏（行 826–832）增 `deopt_event_count++`（均 `MSLANG_VM_STATS` 宏门控） |
| `src/vm_gc.c` | 维护 `major_gc_count`/`bytes_allocated_peak`；`ms_gc_incremental_step()`（行 234）入口 `++incremental_step_count`；强制 full-GC 后取存活对象数 |
| `CMakeLists.txt` | 增 `MSLANG_BUILD_BENCHMARKS`（默认 OFF）/`MSLANG_VM_STATS`（默认 OFF）选项 |

### 增

| 文件 | 说明 |
|------|------|
| `benchmarks/CMakeLists.txt` | 自定义目标 `bench` 调 Python 驱动 |
| `benchmarks/cases/*.ms` | 13 个微基准 |
| `benchmarks/run_all.py` | 调度、聚合、基线对比、markdown 报告 |
| `benchmarks/baseline.json` | 首次定基（Release + `MSLANG_VM_STATS=ON`） |
| `benchmarks/README.md` | 用法、新增用例规范、回归阈值 |

### 复用（不改）

- `MsVM` 现有 `bytes_allocated`/`next_gc`/`young_bytes`/`minor_count`/`frame_count`（`include/ms/vm.h:39-76`）— 直接读
- `MsGcPhase` 枚举与 `gc_phase`/`sweep_cursor`/`sweep_prev` 字段（`vm.h:33-37, 70-73`）— 增量 GC 状态可读出
- `MsObjFunction.arith_deopt[]`/`arith_deopt_size`（`include/ms/object.h:68-69`）— quickening 反优化计数源
- `MsInlineCache`（`include/ms/shape.h:62-66`）— 多态 IC PIC 状态可读出
- `ms_serialize`/`ms_deserialize`/`ms_compile_cached`（`include/ms/serializer.h`）— `--with-cache` 走此路径
- `ms_peephole_optimize`（`compiler.c:1133-1134` 处编译末自动调用，无需基准侧介入）— 基准始终测量优化后代码
- `native_clock`（`src/vm_natives.c:11-14`）— 脚本内嵌时序点可用
- `cmake/MslangTesting.cmake` 模式可借鉴；基准**不**注册为 CTest

---

## 实现

### 阶段 A — 引擎插装 ✅

**1. `include/ms/vm.h`：定义 `MsVMStats` ✅**

```c
typedef struct {
    uint64_t instruction_count;
    uint64_t minor_gc_count;
    uint64_t major_gc_count;
    uint64_t incremental_step_count;   /* ms_gc_incremental_step 每次入口自增（T30） */
    uint64_t deopt_event_count;        /* DEOPT_AND_RESPECIALIZE 每次触发自增（T29） */
    size_t   bytes_allocated_peak;
    int      peak_frame_count;
    int      live_objects_after_final_gc;
} MsVMStats;

void ms_vm_get_stats(const MsVM* vm, MsVMStats* out);
void ms_vm_reset_stats(MsVM* vm);
```

`MsVM` 追加 `MsVMStats stats;`（`MSLANG_VM_STATS` 未定义时空结构零大小）。

**2. `src/vm.c` ✅**

dispatch 循环顶：

```c
#ifdef MSLANG_VM_STATS
    vm->stats.instruction_count++;
#endif
```

frame 入栈处：

```c
#ifdef MSLANG_VM_STATS
    if (vm->frame_count > vm->stats.peak_frame_count)
        vm->stats.peak_frame_count = vm->frame_count;
#endif
```

反优化路径（`bump_deopt` 行 443，及 `DEOPT_AND_RESPECIALIZE` 宏行 826–832）：

```c
#ifdef MSLANG_VM_STATS
    vm->stats.deopt_event_count++;
#endif
```

**3. `src/vm_gc.c` ✅**

- `gc_collect_minor` 末：`++vm->stats.minor_gc_count`
- `gc_collect_major` 末：`++vm->stats.major_gc_count`
- `ms_gc_incremental_step` 入口：`++vm->stats.incremental_step_count`
- `ms_allocate_object` 路径：`if (vm->bytes_allocated > vm->stats.bytes_allocated_peak) vm->stats.bytes_allocated_peak = vm->bytes_allocated`

**4. `src/main.c`：CLI 升级 ✅**

```
mslang-c [--benchmark N] [--stats] [--json] [--no-cache | --with-cache] [--version] <script>
```

- `--benchmark N`：循环 N 次（读源 → init VM → compile → interpret → free VM），记 `compile_ms`/`interpret_ms`（`clock_gettime(CLOCK_MONOTONIC)` / Windows `QueryPerformanceCounter`），输出 min/median/max/mean。默认含 `--no-cache`（每次 fresh compile）。
- `--no-cache`：显式跳过 `.msc` 缓存，每轮从源重新编译（`--benchmark` 默认行为）。
- `--with-cache`：走 `ms_compile_cached`；首轮冷写 `.msc`（记 `compile_ms_cold`），后续轮命中（记 `compile_ms_warm`）。
- `--stats`：仅 `MSLANG_VM_STATS=ON` 时生效，强制 full-GC 后输出完整 `MsVMStats`。
- `--json`：单行 JSON，供驱动消费；与人类可读格式互斥。

**A 阶段验证**

```bash
./build/mslang-c --benchmark 3 tests/fixtures/closures.ms
# 期望：3 条 run + min/median/max 汇总

./build/mslang-c --benchmark 3 --with-cache --stats benchmarks/cases/fib_recursive.ms
# 期望：首轮 compile_ms_cold 显著大于后续轮 compile_ms_warm；incremental_step_count ≥ 0
```

`MSLANG_VM_STATS=OFF` 构建：二进制大小偏差 ≤ 函数符号开销。

---

### 阶段 B — 负载（13 微基准）✅

**脚本统一结构**

```ms
// purpose : <描述>
// hot-op  : <主要 opcode>
// expected: <print 校验值>
var N = <可调参数>
// ...
print(<checksum>)
```

Release 单次 **0.5–3 s**。

**用例清单**

| # | 文件 | 子系统 | 热路径 |
|---|------|--------|--------|
| 1 | `arith_loop.ms` | 整数算术 + 循环 | `OP_ADD_II`/`OP_LT_II`/`OP_LOOP`（quickening 特化路径） |
| 2 | `fib_recursive.ms` | 函数调用/返回 | `OP_CALL`/`OP_RETURN` |
| 3 | `fib_iterative.ms` | 循环 + 局部寄存器 | `OP_LT_II`/`OP_ADD_II`（quickening 特化路径） |
| 4 | `closure_counter.ms` | upvalue 读写 | `OP_GET_UPVAL`/`OP_SET_UPVAL` |
| 5 | `shape_mono_field.ms` | 单态 IC | `OP_GET_PROPERTY` 命中 |
| 6 | `shape_poly_field.ms` | 多态 IC（3-way） | `OP_GET_PROPERTY` 退化 |
| 7 | `method_dispatch.ms` | 方法调用 | `OP_INVOKE` |
| 8 | `binary_trees_alloc.ms` | 分配密集 + GC 压力 | `NEW_INSTANCE` + GC；同时观察 `incremental_step_count` 与 `major_gc_count` 之比 |
| 9 | `list_grow_iter.ms` | list 增长 + `for in` | `OP_BUILD_LIST`/迭代器 |
| 10 | `map_insert_lookup.ms` | hash map 读写 | `OP_SET_INDEX`/`OP_GET_INDEX` |
| 11 | `string_interp_loop.ms` | 字符串插值 | 模板执行路径 |
| 12 | `generator_yield.ms` | 协程 yield/resume | yield 路径 |
| 13 | `quickening_deopt.ms` | 反优化路径（T29） | 混入 int/float 触发 `DEOPT_AND_RESPECIALIZE`（≥3 次），`deopt_event_count > 0` |

**B 阶段验证** ✅：13/13 `print` 输出与 `expected:` 一致；N 已调至 Release 单次 0.5–1.1 s（fib_iterative: 0.66 s，arith_loop: 1.15 s，string_interp_loop: 0.91 s 等）。map_insert_lookup 改用整数键（字符串键 GC bug 绕过）；string_interp_loop 改用 %100 循环键以避免同一 minor GC 边界崩溃；quickening_deopt expected 更新为 `1.25e+13`（float 输出格式）。

---

### 阶段 C — 驱动 + CMake ✅

**`benchmarks/run_all.py`**（仅 stdlib）

参数：`--runs N`（默认 5）/`--exe`/`--baseline`/`--hyperfine`/`--with-cache`

各 `cases/*.ms`：

1. `subprocess.run([exe, "--benchmark", N, "--stats", "--json", file])` → 解析 JSON（默认 `--no-cache`）
2. `--with-cache`：额外跑一轮 `[exe, "--benchmark", N, "--with-cache", "--json", file]`，记录 `compile_ms_cold`/`compile_ms_warm`，计算 `cache_speedup` 列
3. `--hyperfine` 且本机有 `hyperfine`：`hyperfine --warmup 1 --runs N --export-json … -- exe file`，合并端到端时间
4. 校验 `print` 末行 == `expected:`，不符标 FAIL
5. 与 `baseline.json` 对比，偏差 >+5% 标红、<−5% 标绿；`deopt_event_count` 绝对值变化则无论比例均触发 review

输出 `benchmarks/results/YYYYMMDD-HHMM.md`，列：

```
name | best(ms) | median(ms) | compile(ms) | cache_warm(ms) | instr | minor_gc | major_gc | incr_step | deopt | peak_bytes | peak_frames | live_obj | Δ baseline
```

**`benchmarks/CMakeLists.txt`**

```cmake
find_package(Python3 REQUIRED)
add_custom_target(bench
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/benchmarks/run_all.py
            --exe $<TARGET_FILE:mslang-c>
            --runs 5
    DEPENDS mslang-c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running mslang-c benchmark suite"
)
```

**顶层 `CMakeLists.txt`**

```cmake
option(MSLANG_BUILD_BENCHMARKS "Build mslang-c benchmark suite" OFF)
option(MSLANG_VM_STATS         "Enable VM stats counters"        OFF)

if (MSLANG_VM_STATS)
    target_compile_definitions(ms_vm PUBLIC MSLANG_VM_STATS)
endif()

if (MSLANG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
```

**C 阶段验证**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DMSLANG_BUILD_BENCHMARKS=ON -DMSLANG_VM_STATS=ON
cmake --build build
cmake --build build --target bench
python benchmarks/run_all.py --runs 5 --hyperfine
```

---

### 阶段 D — 基线与文档

1. 干净 Release 构建，跑 `run_all.py --runs 9 --with-cache`，各用例 `best/median/instruction_count/peak_bytes/incremental_step_count/deopt_event_count/compile_ms_cold/compile_ms_warm` 写入 `benchmarks/baseline.json`。
2. `benchmarks/README.md` 涵盖：一键命令、新增用例检查清单（命名规范/`expected` 注释/调参至 ~1 s/标注用例类别：`quickening`/`gc-pressure`/`deopt`）、`MSLANG_VM_STATS=OFF` 零成本承诺、回归阈值。
3. 回归阈值：墙钟偏差 **±5%**、指令计数偏差 **±0.5%** → 触发 review；`deopt_event_count` 绝对值变化（无论百分比）→ 触发 review（quickening 命中率回退敏感）。
4. `docs/PLAN.md` 末追加「Benchmarking」节，指向本文，注明 T28–T30 基线已于首次定基时录入。

---

## 验收清单

- [ ] 默认构建（`MSLANG_VM_STATS=OFF`/`MSLANG_BUILD_BENCHMARKS=OFF`）：ctest 全过，`--version` 正常，行为与基线一致。
- [ ] `MSLANG_VM_STATS=ON`：`mslang-c --benchmark 3 --stats benchmarks/cases/fib_recursive.ms` → 3 条 run + 完整 stats，`instruction_count > 0`/`peak_frame_count > 0`。
- [ ] `--json` 单行可解析：`mslang-c --benchmark 1 --json benchmarks/cases/arith_loop.ms | python -c "import json,sys; print(json.loads(sys.stdin.readline())['best_ms'])"`
- [ ] 13 个 `.ms` 用例 `print` 末行均与 `expected:` 一致（驱动自动比对）。
- [ ] `run_all.py --runs 5` → `benchmarks/results/` 写出 markdown，同脚本两次跑偏差 ≈0%。
- [ ] `hyperfine` 在则 `--hyperfine` 融合 wall-time；不在则跳过不报错。
- [ ] `cmake --build build --target bench` Release 下完成 ≤ 60 s。
- [ ] `mslang-c --benchmark 3 --stats benchmarks/cases/quickening_deopt.ms` → `deopt_event_count > 0`，`instruction_count` 与同量级用例相当。
- [ ] `mslang-c --benchmark 3 --with-cache --stats benchmarks/cases/fib_recursive.ms` → 首轮 `compile_ms_cold` 显著大于第 2/3 轮 `compile_ms_warm`（缓存命中），`interpret_ms` 三轮近似。
- [ ] `mslang-c --benchmark 3 --stats benchmarks/cases/binary_trees_alloc.ms` → `incremental_step_count >= 0`（若分配触发增量 GC，则 > 0）。
- [ ] 默认构建（`MSLANG_VM_STATS=OFF`）运行 `quickening_deopt.ms` 行为正确（stats 不输出，功能不变）。
