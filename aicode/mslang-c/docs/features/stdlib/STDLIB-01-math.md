# STDLIB-01: math 模块

## 职责

纯数学函数与常量，无 IO、无副作用、无异步。直接包装 `<math.h>`（C99），补充取整系列和随机数。

---

## 函数清单

### 基础

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `math.abs(x)` | num | num | 绝对值（浮点） |
| `math.floor(x)` | num | int | 下取整 |
| `math.ceil(x)` | num | int | 上取整 |
| `math.round(x)` | num | int | 四舍五入（half-even/ties-to-even）|
| `math.trunc(x)` | num | int | 向零截断 |
| `math.sign(x)` | num | int | -1 / 0 / 1 |
| `math.fmod(x, y)` | num,num | num | 浮点余数（同 C `fmod`）|

### 幂与对数

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `math.sqrt(x)` | num | num | 平方根 |
| `math.pow(x, y)` | num,num | num | x^y（同 `pow(x,y)`）|
| `math.exp(x)` | num | num | e^x |
| `math.log(x)` | num | num | 自然对数 |
| `math.log2(x)` | num | num | 以 2 为底 |
| `math.log10(x)` | num | num | 以 10 为底 |
| `math.hypot(x, y)` | num,num | num | sqrt(x²+y²)，避免溢出 |

### 三角

| 函数 | 参数 | 返回 |
|---|---|---|
| `math.sin(x)` / `cos(x)` / `tan(x)` | num (rad) | num |
| `math.asin(x)` / `acos(x)` / `atan(x)` | num | num (rad) |
| `math.atan2(y, x)` | num,num | num | 四象限反正切 |
| `math.sinh(x)` / `cosh(x)` / `tanh(x)` | num | num | 双曲 |
| `math.degrees(x)` | num | num | 弧度 → 角度 |
| `math.radians(x)` | num | num | 角度 → 弧度 |

### 聚合

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `math.min(a, b, ...)` | num... | num | 最小值，≥1 个参数 |
| `math.max(a, b, ...)` | num... | num | 最大值 |
| `math.clamp(x, lo, hi)` | num,num,num | num | 钳制到 [lo, hi] |
| `math.sum(list)` | list[num] | num | 列表求和（Kahan 补偿）|

### 整数

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `math.gcd(a, b)` | int,int | int | 最大公约数（迭代 Euclidean）|
| `math.lcm(a, b)` | int,int | int | 最小公倍数 |
| `math.is_nan(x)` | num | bool | `isnan` 包装 |
| `math.is_inf(x)` | num | bool | `isinf` 包装 |
| `math.is_finite(x)` | num | bool | `isfinite` 包装 |

### 随机

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `math.random()` | – | num | [0.0, 1.0) 均匀分布（`rand()/RAND_MAX`）|
| `math.randint(lo, hi)` | int,int | int | [lo, hi] 整数均匀分布 |
| `math.seed(n)` | int | nil | 设随机种子（`srand`）|

### 常量（`export_value` 导出）

| 名称 | 值 |
|---|---|
| `math.PI` | `M_PI`（3.14159265358979）|
| `math.E` | `M_E`（2.71828182845904）|
| `math.TAU` | `2 * M_PI` |
| `math.INF` | `INFINITY` |
| `math.NAN` | `NAN` |

---

## 实现（`src/stdlib/math.c`）

```c
#include "ms/module.h"
#include "ms/value.h"
#include <math.h>
#include <stdlib.h>

static MsValue ms_math_sqrt(MsVM* vm, int argc, MsValue* argv) {
    (void)vm; (void)argc;
    double x = MS_IS_INT(argv[0]) ? (double)MS_AS_INT(argv[0])
                                  : MS_AS_NUMBER(argv[0]);
    return MS_NUMBER_VAL(sqrt(x));
}

// ... 其余函数类似 ...

static MsValue ms_math_min(MsVM* vm, int argc, MsValue* argv) {
    if (argc == 0) { ms_vm_runtime_error(vm, "math.min: no arguments"); return MS_NIL_VAL(); }
    double m = MS_AS_NUMBER(argv[0]);
    for (int i = 1; i < argc; i++) {
        double v = MS_AS_NUMBER(argv[i]);
        if (v < m) m = v;
    }
    return MS_NUMBER_VAL(m);
}

static const MsNativeDef ms_math_defs[] = {
    {"abs",      ms_math_abs,     1},
    {"floor",    ms_math_floor,   1},
    {"ceil",     ms_math_ceil,    1},
    {"round",    ms_math_round,   1},
    {"trunc",    ms_math_trunc,   1},
    {"sign",     ms_math_sign,    1},
    {"fmod",     ms_math_fmod,    2},
    {"sqrt",     ms_math_sqrt,    1},
    {"pow",      ms_math_pow,     2},
    {"exp",      ms_math_exp,     1},
    {"log",      ms_math_log,     1},
    {"log2",     ms_math_log2,    1},
    {"log10",    ms_math_log10,   1},
    {"hypot",    ms_math_hypot,   2},
    {"sin",      ms_math_sin,     1},
    {"cos",      ms_math_cos,     1},
    {"tan",      ms_math_tan,     1},
    {"asin",     ms_math_asin,    1},
    {"acos",     ms_math_acos,    1},
    {"atan",     ms_math_atan,    1},
    {"atan2",    ms_math_atan2,   2},
    {"sinh",     ms_math_sinh,    1},
    {"cosh",     ms_math_cosh,    1},
    {"tanh",     ms_math_tanh,    1},
    {"degrees",  ms_math_degrees, 1},
    {"radians",  ms_math_radians, 1},
    {"min",      ms_math_min,    -1},
    {"max",      ms_math_max,    -1},
    {"clamp",    ms_math_clamp,   3},
    {"sum",      ms_math_sum,     1},
    {"gcd",      ms_math_gcd,     2},
    {"lcm",      ms_math_lcm,     2},
    {"is_nan",   ms_math_is_nan,  1},
    {"is_inf",   ms_math_is_inf,  1},
    {"is_finite",ms_math_is_finite,1},
    {"random",   ms_math_random,  0},
    {"randint",  ms_math_randint, 2},
    {"seed",     ms_math_seed,    1},
    {NULL, NULL, 0}
};

void ms_module_math_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_math_defs);
    ms_module_export_value(vm, mod, "PI",  MS_NUMBER_VAL(M_PI));
    ms_module_export_value(vm, mod, "E",   MS_NUMBER_VAL(M_E));
    ms_module_export_value(vm, mod, "TAU", MS_NUMBER_VAL(2.0 * M_PI));
    ms_module_export_value(vm, mod, "INF", MS_NUMBER_VAL(INFINITY));
    ms_module_export_value(vm, mod, "NAN", MS_NUMBER_VAL(NAN));
}
```

---

## 测试（`tests/unit/test_stdlib_math.c`）

```c
// 典型断言
ms_vm_interpret(&vm, "import math; assert(math.sqrt(9) == 3.0)", ...);
ms_vm_interpret(&vm, "import math; assert(math.floor(1.9) == 1)", ...);
ms_vm_interpret(&vm, "import math; assert(math.PI > 3.14)", ...);
ms_vm_interpret(&vm, "import math; assert(math.gcd(12, 8) == 4)", ...);
ms_vm_interpret(&vm, "import math; assert(math.clamp(10, 0, 5) == 5)", ...);
ms_vm_interpret(&vm, "import math; assert(math.is_nan(math.NAN))", ...);
```

---

## 依赖

- CAPI-01（注册表）
- CAPI-02（NativeDef / register_natives）
- `<math.h>`（C99），链接 `-lm`（Unix）
