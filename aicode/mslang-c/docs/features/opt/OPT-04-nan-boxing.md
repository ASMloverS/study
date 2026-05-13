# OPT-04: NaN-Boxing — MsValue 16B → 8B

## 背景

当前 `MsValue`（`include/ms/value.h:14-22`）是 tagged union：

```c
struct MsValue {
    MsValueType type; // enum，4 bytes
    union {
        bool boolean;
        double number;
        ms_i64 integer;
        MsObject* object;
    } as;             // 8 bytes（64-bit）
};
// sizeof(MsValue) == 16（含 4 字节 padding）
```

`MsValue` 出现在：VM 栈（`MsVM.stack[MS_STACK_SIZE]`）、  
常量池（`chunk.constants`）、局部变量、upvalue closed 值、  
`ObjList.items`、`ObjMap` 值、`ObjTuple` 元素、`ObjInstance` 内联字段（8 个 × 16B = 128B）。  
16B vs 8B 对 cache line 利用率影响显著。

## NaN-Boxing 编码

IEEE 754 double-precision 浮点数有 `2^53 - 2` 个静态 NaN 表示（quiet NaN + 非零 payload）。  
将非 double 值藏入 NaN 的 payload 中：

```
[ sign:1 | exponent:11 | quiet:1 | type:3 | payload:48 ]
  63       62-52          51        50-48    47-0
```

| 类型 | 位模式 | payload |
|---|---|---|
| double（普通浮点）| 非 NaN 模式，直接存 | — |
| nil | `0x7FFC000000000000` | 0 |
| false | `0x7FFD000000000000` | 0 |
| true | `0x7FFD000000000001` | 1 |
| integer (i64) | `0x7FFE000000000000 \| (uint32_t)n` | 低 32 位（±2^31 快速路径）；超出范围 boxed |
| object ptr | `0x7FFF000000000000 \| ptr` | 低 48 位（x86_64 用户空间指针合法范围）|

> **Windows / x86_64**：用户空间地址 `< 0x800000000000`（47 bit），可放入 48 bit payload。  
> ARM64 / macOS：同样 47 bit 用户空间，兼容。  
> MSVC fallback：`MS_NAN_BOXING=0` 保留 16B union，编译期切换。

### 宏接口（`include/ms/value_nan.h`，新文件）

```c
#define MS_NAN_BOXING 1  // 默认开启，可在 CMakeLists 覆盖

#if MS_NAN_BOXING

typedef uint64_t MsValue;

#define MS_NAN_MASK   UINT64_C(0x7FFC000000000000)
#define MS_TAG_NIL    UINT64_C(0x7FFC000000000000)
#define MS_TAG_FALSE  UINT64_C(0x7FFD000000000000)
#define MS_TAG_TRUE   UINT64_C(0x7FFD000000000001)
#define MS_TAG_INT    UINT64_C(0x7FFE000000000000)
#define MS_TAG_OBJ    UINT64_C(0x7FFF000000000000)
#define MS_QNAN       UINT64_C(0x7FF8000000000000)

static inline bool MS_IS_DOUBLE(MsValue v) { return (v & MS_NAN_MASK) != MS_NAN_MASK; }
static inline bool MS_IS_NIL(MsValue v)    { return v == MS_TAG_NIL; }
static inline bool MS_IS_BOOL(MsValue v)   { return (v & ~(uint64_t)1) == MS_TAG_FALSE; }
static inline bool MS_IS_TRUE(MsValue v)   { return v == MS_TAG_TRUE; }
/* MS_IS_INT：高 32 位 == 0x7FFE0000。
   安全性：0x7FFE 在指数全1（0x7FF）+ bit51=1 区域，是 quiet NaN 空间，
   普通有限 double 的指数 ≤ 0x7FE，不会产生假阳性。
   算术 NaN 被 MS_DOUBLE_VAL 归一化后只有 MS_QNAN（0x7FF8...），同样不冲突。 */
static inline bool MS_IS_INT(MsValue v)    { return (v >> 32) == (MS_TAG_INT >> 32); }
/* MS_IS_OBJECT：高 16 位 == 0x7FFF，与 INT（0x7FFE）区分明确，!MS_IS_INT 可去除但保留做防御。 */
static inline bool MS_IS_OBJECT(MsValue v) { return (v & MS_TAG_OBJ) == MS_TAG_OBJ && !MS_IS_INT(v); }

static inline double      MS_AS_DOUBLE(MsValue v) { double d; memcpy(&d, &v, 8); return d; }
static inline ms_i64      MS_AS_INT(MsValue v)    { return (ms_i64)(int32_t)(v & 0xFFFFFFFF); }
static inline MsObject*   MS_AS_OBJECT(MsValue v) { return (MsObject*)(uintptr_t)(v & 0x0000FFFFFFFFFFFF); }
static inline bool        MS_AS_BOOL(MsValue v)   { return (bool)(v & 1); }

static inline MsValue MS_NIL_VAL()          { return MS_TAG_NIL; }
static inline MsValue MS_BOOL_VAL(bool b)   { return b ? MS_TAG_TRUE : MS_TAG_FALSE; }
/* NaN 归一化：double 运算可能产生各种 NaN 位模式；统一归一到 MS_QNAN，
   防止算术 NaN 的位模式与标签空间（0x7FFC..~0x7FFF..）碰撞。 */
static inline MsValue MS_DOUBLE_VAL(double d) {
    uint64_t v; memcpy(&v, &d, 8);
    if ((v & MS_NAN_MASK) == MS_NAN_MASK) return MS_QNAN; /* 归一化 NaN */
    return v;
}
/* MS_INT_VAL 仅支持 ±2^31 范围（payload 限 32 位）。
   超出范围调用方应改用 MS_DOUBLE_VAL 降级（精度损失说明见下方）。 */
static inline MsValue MS_INT_VAL(ms_i64 n) {
    if (n >= INT32_MIN && n <= INT32_MAX)
        return MS_TAG_INT | (uint32_t)(int32_t)n;
    return MS_DOUBLE_VAL((double)n); /* 降级：>2^31 时精度可能损失，见下注 */
}
static inline MsValue MS_OBJECT_VAL(MsObject* p) { return MS_TAG_OBJ | (uintptr_t)p; }

#else  // fallback: tagged union
// ... 保持原 value.h 的实现
#endif
```

### CMake 控制

```cmake
# CMakeLists.txt
option(MS_NAN_BOXING "Enable NaN-boxing for MsValue (8 bytes)" ON)
if(MS_NAN_BOXING AND (CMAKE_SIZEOF_VOID_P EQUAL 8))
    target_compile_definitions(mslang-c PRIVATE MS_NAN_BOXING=1)
else()
    target_compile_definitions(mslang-c PRIVATE MS_NAN_BOXING=0)
endif()
```

## 影响范围（侵入面）

### 必改文件

| 文件 | 修改内容 |
|---|---|
| `include/ms/value.h` | 加 `#if MS_NAN_BOXING` 分支，引入 `value_nan.h` |
| `include/ms/value_nan.h` | **新建**，含上述宏/inline |
| `src/value.c` | `ms_value_to_string`、`ms_values_equal` 按新宏重写 |
| `src/vm.c` | 所有 `MS_IS_*`、`MS_AS_*`、`MS_NUMBER_VAL` 等调用自动适配（宏）|
| `src/vm_gc.c:trace_value` | GC mark object：从 NaN 编码取指针需用 `MS_AS_OBJECT` |
| `src/chunk.c` | 常量池 `MsValue` 类型跟随变化（自动） |
| `src/serializer.c` | `.msc` 内存布局改变，version bump 到 v2 |
| `src/object.c` | `MsObjUpvalue.closed`（16B→8B，结构体变小） |
| `include/ms/object.h` | `MsObjInstance` 内联字段：`MsValue fields[MS_SBO_FIELDS]` → 节省 8B×8=64B/instance |

### 不用改（自动跟随宏）

所有使用 `MS_IS_*` / `MS_AS_*` / `MS_*_VAL` 的调用方，只要宏定义正确即可。

### 大整数路径（i64 超出 ±2^31）

`MS_INT_VAL` payload 限 32 位，仅表示 `[INT32_MIN, INT32_MAX]`。

**v1 策略（已内置于 `MS_INT_VAL` 实现）**：超出范围自动降级为 `double`。  
**精度损失说明**：`double` 尾数 53 位，可精确表示 `±2^53` 以内的整数；`> 2^53` 会静默丢精度（无运行时警告）。  
完整 `ms_i64` box（`ObjBoxedInt`）留后续迭代，v1 不实现。

## .msc 版本 Bump

`src/serializer.c` 文件头中将 `MSC_FORMAT_VERSION` 从当前版本加 1。  
旧缓存读取时 `version != MSC_FORMAT_VERSION` 直接返回失败 → 重新编译。

> **与 OPT-03 协调**：OPT-03（新 opcode）和本优化都需要 bump 版本号。
> 推荐合并为一次 bump 避免冲突；若分批实现，各自各加一个版本号，不得两分支并行写同一版本值。
> 详见 OPT-03 §.msc 兼容性。

## 验证

```bash
cmake -S . -B build_nan -DMS_NAN_BOXING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_nan
cd build_nan && ctest --output-on-failure
python ../benchmarks/run_all.py --compare baseline.json --runs 5 --build build_nan
```

重点指标：
- `list_grow_iter.ms`：内存峰值预期 -30~45%
- `binary_trees_alloc.ms`：同上
- `arith_loop.ms`：GC 次数减少（stack 更小 → 更少 cache miss）
- conformance 100% 通过

同时跑 fallback 版本：

```bash
cmake -S . -B build_nonan -DMS_NAN_BOXING=OFF
cmake --build build_nonan
cd build_nonan && ctest --output-on-failure
```

两套 ctest 都要绿。
