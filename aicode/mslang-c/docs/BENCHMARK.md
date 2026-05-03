# mslang-c 基准方案

## 背景

T01–T27 已成，然无基准设施：

- `src/main.c` 仅支持 `--version`/`<script>`，无计时；
- `MsVM` 有 `bytes_allocated`/`minor_count` 等字段，无指令计数、major GC 计数、峰值，亦未暴露公共 API；
- `tests/fixtures/` 47 脚本仅作功能验证；
- `../mslang/benchmarks/` 7 脚本，语法不兼容，覆盖面窄。

T28/T29/T30 在即，须可重复、可对比之基准，方有优化判据。

本方案答两问：「此提交对热点 opcode 影响几何？」「相对上一基线总耗时回归多少？」

---

## 设计决策

| 维度 | 决策 |
|------|------|
| 计时 | 内部 `--benchmark N`（进程内精确）+ 外部 `hyperfine`（端到端）双轨 |
| 指标 | 墙钟 + GC 统计 + 指令计数 + 栈帧峰值 + 存活对象数 |
| 负载 | 12 个微基准，各直击一 VM 子系统 |
| 接入 | CMake 目标 `MSLANG_BUILD_BENCHMARKS=OFF` + `benchmarks/run_all.py` 驱动 |

---

## 涉及文件

### 改

| 文件 | 变更 |
|------|------|
| `src/main.c` | 解析 `--benchmark N`/`--stats`/`--json`；循环 compile+interpret，采样计时，输出 min/median/max/mean |
| `include/ms/vm.h` | 增 `MsVMStats` 结构及 `ms_vm_get_stats()`/`ms_vm_reset_stats()` API；`MsVM` 追加峰值字段 |
| `src/vm.c` | dispatch 循环维护 `instruction_count`；入栈更新 `peak_frame_count`（`MSLANG_VM_STATS` 宏门控） |
| `src/vm_gc.c` | 维护 `major_gc_count`/`bytes_allocated_peak`；强制 full-GC 后取存活对象数 |
| `CMakeLists.txt` | 增 `MSLANG_BUILD_BENCHMARKS`（默认 OFF）/`MSLANG_VM_STATS`（默认 OFF）选项 |

### 增

| 文件 | 说明 |
|------|------|
| `benchmarks/CMakeLists.txt` | 自定义目标 `bench` 调 Python 驱动 |
| `benchmarks/cases/*.ms` | 12 个微基准 |
| `benchmarks/run_all.py` | 调度、聚合、基线对比、markdown 报告 |
| `benchmarks/baseline.json` | 首次定基（Release + `MSLANG_VM_STATS=ON`） |
| `benchmarks/README.md` | 用法、新增用例规范、回归阈值 |

### 复用（不改）

- `MsVM` 现有 `bytes_allocated`/`next_gc`/`young_bytes`/`minor_count`/`frame_count`（`include/ms/vm.h:33-66`）— 直接读
- `native_clock`（`src/vm_natives.c:11-14`）— 脚本内嵌时序点可用
- `cmake/MslangTesting.cmake` 模式可借鉴；基准**不**注册为 CTest

---

## 实现

### 阶段 A — 引擎插装

**1. `include/ms/vm.h`：定义 `MsVMStats`**

```c
typedef struct {
    uint64_t instruction_count;
    uint64_t minor_gc_count;
    uint64_t major_gc_count;
    size_t   bytes_allocated_peak;
    int      peak_frame_count;
    int      live_objects_after_final_gc;
} MsVMStats;

void ms_vm_get_stats(const MsVM* vm, MsVMStats* out);
void ms_vm_reset_stats(MsVM* vm);
```

`MsVM` 追加 `MsVMStats stats;`（`MSLANG_VM_STATS` 未定义时空结构零大小）。

**2. `src/vm.c`**

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

**3. `src/vm_gc.c`**

- `gc_collect_minor` 末：`++vm->stats.minor_gc_count`
- `gc_collect_major` 末：`++vm->stats.major_gc_count`
- `ms_allocate_object` 路径：`if (vm->bytes_allocated > vm->stats.bytes_allocated_peak) vm->stats.bytes_allocated_peak = vm->bytes_allocated`

**4. `src/main.c`：CLI 升级**

```
mslang-c [--benchmark N] [--stats] [--json] [--version] <script>
```

- `--benchmark N`：循环 N 次（读源 → init VM → compile → interpret → free VM），记 `compile_ms`/`interpret_ms`（`clock_gettime(CLOCK_MONOTONIC)` / Windows `QueryPerformanceCounter`），输出 min/median/max/mean。
- `--stats`：仅 `MSLANG_VM_STATS=ON` 时生效，强制 full-GC 后输出完整 `MsVMStats`。
- `--json`：单行 JSON，供驱动消费；与人类可读格式互斥。

**A 阶段验证**

```bash
./build/mslang-c --benchmark 3 tests/fixtures/closures.ms
# 期望：3 条 run + min/median/max 汇总
```

`MSLANG_VM_STATS=OFF` 构建：二进制大小偏差 ≤ 函数符号开销。

---

### 阶段 B — 负载（12 微基准）

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
| 1 | `arith_loop.ms` | 整数算术 + 循环 | `OP_ADD`/`OP_LT`/`OP_LOOP` |
| 2 | `fib_recursive.ms` | 函数调用/返回 | `OP_CALL`/`OP_RETURN` |
| 3 | `fib_iterative.ms` | 循环 + 局部寄存器 | `OP_LT`/`OP_ADD` |
| 4 | `closure_counter.ms` | upvalue 读写 | `OP_GET_UPVAL`/`OP_SET_UPVAL` |
| 5 | `shape_mono_field.ms` | 单态 IC | `OP_GET_PROPERTY` 命中 |
| 6 | `shape_poly_field.ms` | 多态 IC（3-way） | `OP_GET_PROPERTY` 退化 |
| 7 | `method_dispatch.ms` | 方法调用 | `OP_INVOKE` |
| 8 | `binary_trees_alloc.ms` | 分配密集 + GC 压力 | `NEW_INSTANCE` + GC |
| 9 | `list_grow_iter.ms` | list 增长 + `for in` | `OP_BUILD_LIST`/迭代器 |
| 10 | `map_insert_lookup.ms` | hash map 读写 | `OP_SET_INDEX`/`OP_GET_INDEX` |
| 11 | `string_interp_loop.ms` | 字符串插值 | 模板执行路径 |
| 12 | `generator_yield.ms` | 协程 yield/resume | yield 路径 |

**B 阶段验证**：手跑各文件，`print` 输出与 `expected:` 一致；调 `N` 使 Release 单次 ≈ 1 s。

---

### 阶段 C — 驱动 + CMake

**`benchmarks/run_all.py`**（仅 stdlib）

参数：`--runs N`（默认 5）/`--exe`/`--baseline`/`--hyperfine`

各 `cases/*.ms`：

1. `subprocess.run([exe, "--benchmark", N, "--stats", "--json", file])` → 解析 JSON
2. `--hyperfine` 且本机有 `hyperfine`：`hyperfine --warmup 1 --runs N --export-json … -- exe file`，合并端到端时间
3. 校验 `print` 末行 == `expected:`，不符标 FAIL
4. 与 `baseline.json` 对比，偏差 >+5% 标红、<−5% 标绿

输出 `benchmarks/results/YYYYMMDD-HHMM.md`，列：

```
name | best(ms) | median(ms) | compile(ms) | instr | minor_gc | major_gc | peak_bytes | peak_frames | live_obj | Δ baseline
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

1. 干净 Release 构建，跑 `run_all.py --runs 9`，各用例 `best/median/instruction_count/peak_bytes` 写入 `benchmarks/baseline.json`。
2. `benchmarks/README.md` 涵盖：一键命令、新增用例检查清单（命名规范/`expected` 注释/调参至 ~1 s）、`MSLANG_VM_STATS=OFF` 零成本承诺、回归阈值。
3. 回归阈值：墙钟偏差 **±5%**、指令计数偏差 **±0.5%** → 触发 review。
4. `docs/PLAN.md` 末追加「Benchmarking」节，指向本文，注明 T28/T29 后须重定基线。

---

## 验收清单

- [ ] 默认构建（`MSLANG_VM_STATS=OFF`/`MSLANG_BUILD_BENCHMARKS=OFF`）：ctest 全过，`--version` 正常，行为与基线一致。
- [ ] `MSLANG_VM_STATS=ON`：`mslang-c --benchmark 3 --stats benchmarks/cases/fib_recursive.ms` → 3 条 run + 完整 stats，`instruction_count > 0`/`peak_frame_count > 0`。
- [ ] `--json` 单行可解析：`mslang-c --benchmark 1 --json benchmarks/cases/arith_loop.ms | python -c "import json,sys; print(json.loads(sys.stdin.readline())['best_ms'])"`
- [ ] 12 个 `.ms` 用例 `print` 末行均与 `expected:` 一致（驱动自动比对）。
- [ ] `run_all.py --runs 5` → `benchmarks/results/` 写出 markdown，同脚本两次跑偏差 ≈0%。
- [ ] `hyperfine` 在则 `--hyperfine` 融合 wall-time；不在则跳过不报错。
- [ ] `cmake --build build --target bench` Release 下完成 ≤ 60 s。
