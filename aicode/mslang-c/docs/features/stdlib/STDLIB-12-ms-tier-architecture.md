# STDLIB-12: .ms 层架构设计

## 职责

定义用 `.ms` 脚本编写的标准库模块如何被嵌入二进制、注册为内置模块、调用 C 原语，以及如何测试。

---

## 架构总览

```
用户代码：import "slices"
           ↓
ms_module_load → builtin_registry 命中 → thunk init
           ↓
ms_module_run_embedded(vm, mod, slices_ms_src, "slices")
           ↓
ms_compile(vm, src, "<stdlib:slices>")    # 内存编译
ms_vm_execute_module(vm, fn, mod)         # 顶层声明 → mod->exports
           ↓
mod->exports["map"] = ObjClosure(...)     # .ms 定义的函数
```

```
.ms 模块内部：import "_sort"  ← 私有 C 原语
              from "_sort" import sort_list
              fun sorted(list, key=nil) { ... sort_list(list, cmp) ... }
```

---

## 嵌入构建（CMake Codegen）

### 文件布局

```
stdlib/ms/              # .ms stdlib 源文件（人类维护）
    strings.ms
    strconv.ms
    fmt.ms
    errors.ms
    slices.ms
    maps.ms
    sort.ms
    set.ms
    heap.ms
    itertools.ms
    random.ms
    hex.ms
    bytes.ms
    path.ms
    bufio.ms
    csv.ms
    base32.ms
    cmp.ms
    deque.ms
    linkedlist.ms
    ring.ms
    flag.ms
    url.ms
    sync.ms
    context.ms
    hmac.ms
    testing.ms
    template.ms

cmake/embed_ms.cmake    # codegen 脚本
src/generated/          # CMake 自动生成，勿手动编辑
    stdlib_ms_embed.c   # 源码 blob + 注册函数
include/ms/
    stdlib_ms.h         # ms_stdlib_register_ms_modules 声明
```

### CMake 步骤（`cmake/embed_ms.cmake`）

```cmake
# 遍历 stdlib/ms/*.ms，生成 src/generated/stdlib_ms_embed.c
set(MS_STDLIB_SOURCES ${CMAKE_SOURCE_DIR}/stdlib/ms)
file(GLOB MS_FILES "${MS_STDLIB_SOURCES}/*.ms")

# 生成的 .c 包含：
#   static const char <name>_ms_src[] = "...escaped source...";
#   void ms_stdlib_register_ms_modules(MsVM* vm);
# 由 cmake/embed_ms.py（或 cmake -P 脚本）读取每个 .ms 并输出 C 字符串字面量
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/generated/stdlib_ms_embed.c
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_DIR=${MS_STDLIB_SOURCES}
        -DOUTPUT=${CMAKE_SOURCE_DIR}/src/generated/stdlib_ms_embed.c
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_ms.cmake
    DEPENDS ${MS_FILES}
)
```

### 生成的 `stdlib_ms_embed.c` 结构

```c
// AUTO-GENERATED — do not edit

#include "ms/stdlib_ms.h"
#include "ms/module.h"

static void run_embedded(MsVM* vm, MsObjModule* mod,
                          const char* src, const char* name) {
    ms_module_run_embedded(vm, mod, src, name);
}

static const char slices_ms_src[] =
    "fun map(list, fn) {\n"
    "  var out = []\n"
    "  for v in list { out.push(fn(v)) }\n"
    "  return out\n"
    "}\n"
    /* ... */;

static void slices_init(MsVM* vm, MsObjModule* mod) {
    run_embedded(vm, mod, slices_ms_src, "slices");
}

/* ... 其余模块 ... */

void ms_stdlib_register_ms_modules(MsVM* vm) {
    ms_vm_register_builtin_module(vm, "slices",    slices_init);
    ms_vm_register_builtin_module(vm, "maps",      maps_init);
    ms_vm_register_builtin_module(vm, "errors",    errors_init);
    /* ... */
}
```

---

## 运行时加载：`ms_module_run_embedded`

新增 helper，在 `src/module.c` 或 `src/stdlib_ms_loader.c`：

```c
/* 编译嵌入的 .ms 源码并在 mod 上下文中执行，把顶层声明写入 mod->exports。
   source_name 仅用于错误消息（如 "<stdlib:slices>"）。 */
void ms_module_run_embedded(MsVM* vm, MsObjModule* mod,
                             const char* src, const char* module_name) {
    char diag_name[64];
    snprintf(diag_name, sizeof(diag_name), "<stdlib:%s>", module_name);

    MsObjFunction* fn = ms_compile(vm, src, diag_name);
    if (!fn) { mod->state = MS_MOD_FAILED; return; }

    MsInterpretResult r = ms_vm_execute_module(vm, fn, mod);
    if (r != MS_INTERPRET_OK) mod->state = MS_MOD_FAILED;
}
```

`ms_compile(vm, src, name)` 已存在（`tests/unit` 用 `ms_vm_interpret` 走同路径）；需确认或暴露无副作用的编译入口。

---

## C 原语私有模块（`_` 前缀）

| 私有模块 | 服务于 | 内容 |
|---|---|---|
| `_strprim` | `strings` | split/join/replace/index（C 实现，性能关键）|
| `_strconv` | `strconv` | strtod/strtoll/格式化（C，locale 无关）|
| `_fmt` | `fmt` | sprintf 内核（%d/%f/%s/%x 等）|
| `_sort` | `sort` | list 带回调排序（Timsort/quicksort）|
| `_rand` | `random` | PRNG 状态（MT19937）+ OS 熵种子 |
| `_codec` | `base64` | RFC4648 编解码 |
| `_re` | `regexp` | 正则引擎（NFA-based，无外部依赖）|

私有模块与 Tier 0/1 C 模块结构相同（`src/stdlib/_strprim.c` 等），
但名称以 `_` 开头，文档注明「内部使用，不稳定」。
`ms_stdlib_register_all` 中一并注册。

---

## 注册总入口（更新 `src/stdlib_register.c`）

```c
void ms_stdlib_register_all(MsVM* vm) {
    /* Tier 0：已有 C 模块 */
    ms_vm_register_builtin_module(vm, "math",   ms_module_math_init);
    /* ... 其余 9 个 ... */

    /* 私有 C 原语 */
    ms_vm_register_builtin_module(vm, "_strprim",  ms_module_strprim_init);
    ms_vm_register_builtin_module(vm, "_strconv",  ms_module_strconv_init);
    ms_vm_register_builtin_module(vm, "_fmt",      ms_module_fmt_init);
    ms_vm_register_builtin_module(vm, "_sort",     ms_module_sort_prim_init);
    ms_vm_register_builtin_module(vm, "_rand",     ms_module_rand_init);
    ms_vm_register_builtin_module(vm, "_codec",    ms_module_codec_init);
    ms_vm_register_builtin_module(vm, "_re",       ms_module_re_init);

    /* 纯 C 新模块（Tier 1/2）*/
    ms_vm_register_builtin_module(vm, "json",     ms_module_json_init);
    ms_vm_register_builtin_module(vm, "base64",   ms_module_base64_init);
    ms_vm_register_builtin_module(vm, "regexp",   ms_module_regexp_init);
    ms_vm_register_builtin_module(vm, "unicode",  ms_module_unicode_init);
    ms_vm_register_builtin_module(vm, "binary",   ms_module_binary_init);
    ms_vm_register_builtin_module(vm, "bits",     ms_module_bits_init);

    /* .ms 层（codegen 生成，在 src/generated/stdlib_ms_embed.c）*/
    ms_stdlib_register_ms_modules(vm);
}
```

---

## 开发期磁盘回退（`MSLANG_STDLIB`）

```
MSLANG_STDLIB=/path/to/stdlib/ms ./build/mslang-c script.ms
```

加载顺序变为：module_cache → builtin_registry（嵌入源）→ **`MSLANG_STDLIB/<name>.ms`**（磁盘，优先于嵌入）→ 文件系统 → 动态库。

实现：在 `ms_vm_init` 读取 `MSLANG_STDLIB` 环境变量，调用 `ms_vm_prepend_search_path`，
但此路径仅在 `_` 开头以外的模块名用于 `.ms` 查找时生效（即绕过 builtin_registry 不可行，
需改 `ms_module_load` 在 builtin 命中前先检查 MSLANG_STDLIB 磁盘文件，仅在 debug/dev 构建启用）。

简化方案（推荐）：提供 `--stdlib-path` CLI 选项，用 `ms_vm_prepend_search_path` 注入，
模块名在 builtin_registry 命中即停止；磁盘回退仅对未注册名生效。
即：开发期把模块改名为 `slices_dev` 测试，正式嵌入后换回 `slices`。

---

## .ms 模块编写约定

```ms
// stdlib/ms/slices.ms
// 依赖声明写在文件顶部（可选，仅供文档）
// Requires: list builtins, sort module

fun map(list, fn) {
    var out = []
    for v in list { out.push(fn(v)) }
    return out
}

fun filter(list, fn) {
    var out = []
    for v in list { if fn(v) { out.push(v) } }
    return out
}
// ... 所有函数均为顶层 fun，自动成为 exports
```

- **所有顶层 `fun`/`var`/`class` 声明自动导出**（`ms_vm_execute_module` 机制）。
- 不需要显式 `export` 语句。
- 内部辅助函数用 `_` 前缀命名，但注意用户可以 `import` 后访问——真正私有化需语言层支持（v2 特性）。
- import 其他模块：`import "math"` / `import "_strprim"`。

---

## .ms 模块测试约定

```
tests/unit/test_stdlib_<name>.c    # 通过 ms_vm_interpret 运行 .ms 断言
tests/fixtures/stdlib_<name>_basic.ms   # 可独立运行的集成脚本
```

```c
// test_stdlib_slices.c
static void test_slices_map(void) {
    MsVM vm; ms_vm_init(&vm);
    const char* src =
        "import \"slices\"\n"
        "var r = slices.map([1,2,3], fun(x){ return x*2 })\n"
        "assert(r[0] == 2 and r[1] == 4 and r[2] == 6)";
    TEST_ASSERT_EQ(ms_vm_interpret(&vm, src, "<test>"), MS_INTERPRET_OK);
    ms_vm_free(&vm);
}
```

---

## 语言前置缺口

实施 .ms 层标准库前，下列语言/运行时能力**尚不具备**，须在编码前决策处置方式：

| 缺口 | 影响模块 | 处置 |
|---|---|---|
| `instanceof` 运算符 | `errors`（`errors.is` 类型链检查）| 降级为引用相等（`==`）沿 cause 链遍历；v2 补 `instanceof` |
| 解构赋值（`var [a,b]=...`）| `context`（`with_cancel` 等返回值）| 改返回单一 Context 对象，取消句柄挂为 `ctx.cancel()` 方法 |
| map 反射方法（`hasattr`/`getattr`/`setattr`）| `maps`/`set` | 用 `m[key]`（下标）/`m.has(key)`/`m.remove(key)` 替代 |
| 内建 `replace` 无 n 参 / `trim` 无 chars 参 | `strings` | 在 `_strprim` 中实现 `replace_n`/`trim_chars` 等变体 |

---

## 依赖

- CAPI-01（注册表）
- CAPI-02（NativeDef）
- CAPI-05（MsModuleApi，供 `_` 私有模块使用稳定 ABI）
- `ms_compile` / `ms_vm_execute_module`（模块加载路径）
- CMake ≥ 3.20 + Python 3（codegen 脚本）
