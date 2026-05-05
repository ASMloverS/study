# `__mscache__` 字节码缓存设计方案

> 日期：2026-05-05  
> 状态：设计完成，待实现

---

## 背景与目标

mslang-c 在 T30 已初步实现 `ms_serialize` / `ms_deserialize` / `ms_compile_cached`（`src/serializer.c`），但与"用字节码缓存提升脚本执行性能"这一目标之间存在以下关键差距：

| 问题 | 现状 | 目标 |
|---|---|---|
| 缓存位置 | `<src>c` 与源文件同目录平铺（如 `foo.msc` 紧邻 `foo.ms`） | 集中到 `__mscache__/` 子目录 |
| 校验性能 | 每次启动都完整读源文件算 FNV-1a hash | mtime+size 模式：**不读源文件**直接命中 |
| 模块缓存 | `ms_module_load` 直接调 `ms_compile`，import 链每次重编译 | 模块也走缓存 |
| 默认行为 | `--with-cache` opt-in | 默认开启，`--no-cache` 显式关闭 |
| 写入安全 | `fopen("wb")` 直接写，崩溃/并发会产生截断文件 | 原子 rename（tmp → final） |
| fs 工具 | 无 `mkdir` / `stat` / `rename` 跨平台 helper | 新增 `fs_util` 模块 |

**核心收益**：第二次及以后执行同一脚本（且源未变）时，不打开源文件，直接走 deserialize 路径，最大化启动性能。

---

## 设计决策（Q&A 汇总）

| 决策点 | 选择 |
|---|---|
| `.msc` vs `.mso` | 只用 `.msc`，不区分 `.mso` |
| 校验模式 | 混合：默认 `mtime+size`，可选 `hash`，header 自描述 |
| 默认开关 | 默认开，`--no-cache` 关；移除 `--with-cache` |
| 模块 import | 模块也走缓存，规则与顶层脚本一致 |
| 文件名格式 | 简洁：`__mscache__/<basename>.msc`，不带版本 tag |
| header 扩展 | 32 字节，新增 `src_size / src_mtime_ns` 字段 |
| 写入安全 | 原子 rename（POSIX `rename()`，Windows `MoveFileExA`） |
| 写失败处理 | Silently 忽略（缓存是性能优化，非语义，不污染 stderr） |

---

## 缓存目录布局

```
project/
  app.ms
  __mscache__/
    app.msc          ← app.ms 的字节码缓存
  utils/
    helper.ms
    __mscache__/
      helper.msc     ← import 的模块缓存
```

- 每个含 `.ms` 源的目录中，首次写入时自动 `mkdir __mscache__`（已存在则忽略）。
- 不在文件名中编码字节码版本；版本不匹配由 `MsMscHeader.version` 在 header 阶段检测并触发重生。

---

## `MsMscHeader` 扩展（v1 → v2，32 字节）

```c
typedef struct {
    char     magic[4];      // "MSC\0"
    uint32_t version;       // 字节码格式版本（v1 → v2，旧缓存自动失效）
    uint32_t flags;         // bit0: 0=mtime+size 模式, 1=hash 模式；其余 reserved
    uint64_t src_size;      // 源文件字节数（mtime 模式校验）
    int64_t  src_mtime_ns;  // 源文件最后修改时间（纳秒，Y2038-safe）
    uint32_t src_hash;      // FNV-1a（hash 模式校验；mtime 模式置 0）
    uint32_t reserved;      // 8 字节对齐
} MsMscHeader;  // 32 字节
```

`MS_MSC_VERSION` 从 1 bump 到 2；旧 v1 文件被加载时 version 不匹配 → 触发重编译 + 覆盖。

---

## 校验模式

### mtime+size 模式（默认，`flags & 1 == 0`）

加载流程（**不打开源文件**）：

```
1. ms_fs_stat(src_path) → (size, mtime_ns)
2. open(__mscache__/<basename>.msc), read 32B header
3. 校验 magic / version / flags
4. 比对 (header.src_size == size) && (header.src_mtime_ns == mtime_ns)
5. 命中 → deserialize，返回 MsObjFunction*
6. 任一步失败 → fall through 到 compile + 写入路径
```

### hash 模式（`--cache-mode=hash`，`flags & 1 == 1`）

```
1. ms_fs_stat(src_path)（可选，仅用于快速排除 size 不同的情况）
2. 读源文件，计算 FNV-1a hash
3. open header，比对 header.src_hash
4. 命中 → deserialize
5. Miss → compile + 写入路径
```

**模式自描述**：写入时由 CLI `--cache-mode` 决定 flags，读取时按 header 自描述模式校验——无需要求加载时使用与写入时相同的 CLI 参数。

---

## 原子写入

```
1. ms_fs_mkdir(__mscache__)  // EEXIST 忽略
2. 打开临时文件 <cache>.tmp.<pid>
3. ms_serialize(fn, tmp_path, src_hash)
4. fflush + fclose
5. ms_fs_atomic_rename(tmp_path, final_path)
   // POSIX:   rename()
   // Windows: MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)
6. 任一步失败：ms_fs_unlink(tmp_path)（best-effort），silently 放弃写缓存
```

---

## CLI 行为

| 参数 | 说明 |
|---|---|
| （无参数）| 默认开启缓存，mtime 模式 |
| `--no-cache` | 完全不读不写缓存 |
| `--cache-mode=mtime` | 写出 mtime 模式缓存（默认，可省略） |
| `--cache-mode=hash` | 写出 hash 模式缓存 |
| `--with-cache` | **移除**（功能已是默认，不再需要） |

---

## 模块（import）缓存

`src/module.c::ms_module_load` 将

```c
fn = ms_compile(vm, source, path, diags, ...);
```

改为

```c
fn = ms_compile_cached(vm, source, path, cache_flags);
```

- 每个被 import 的 `.ms` 文件在其所在目录生成 `__mscache__/<modname>.msc`。
- 循环依赖语义不变：`MS_MOD_INITIALIZING` 标记仍在，缓存只替代"读源 + 编译"步骤，模块顶层执行阶段不变。

---

## 新增模块：`fs_util`

**`include/ms/fs_util.h` / `src/fs_util.c`**（新建，内部使用）

```c
typedef struct { uint64_t size; int64_t mtime_ns; } MsFileMeta;

bool ms_fs_stat(const char* path, MsFileMeta* out);         // stat / GetFileAttributesEx
bool ms_fs_mkdir(const char* path);                          // mkdir/_mkdir，EEXIST = OK
bool ms_fs_atomic_rename(const char* tmp, const char* dst); // rename / MoveFileExA
bool ms_fs_unlink(const char* path);                         // unlink / _unlink
```

- Windows mtime：`GetFileTime` → `FILETIME` → 100ns 单位 → 纳秒。
- POSIX mtime：`stat::st_mtim.tv_nsec`（Linux）或 `st_mtimespec.tv_nsec`（BSD/macOS）。

---

## 缓存路径解析

```c
// "/path/to/script.ms"  → "/path/to/__mscache__/script.msc"
// "/path/to/script"     → "/path/to/__mscache__/script.msc"
bool ms_cache_path_for(const char* src_path, char* out, size_t cap);
```

路径处理参考 `src/module.c::dir_end`（已支持 `\` 和 `/`），避免重复逻辑。

---

## 需修改的文件

| 文件 | 改动 |
|---|---|
| `include/ms/serializer.h` | 扩展 `MsMscHeader`；调整 `ms_compile_cached` 签名；bump `MS_MSC_VERSION` |
| `src/serializer.c` | mtime 快速路径；原子写入；`__mscache__` 路径解析；hash/mtime 模式分支 |
| `include/ms/fs_util.h` | **新建** |
| `src/fs_util.c` | **新建** |
| `src/main.c` | 移除 `--with-cache`；新增 `--no-cache` / `--cache-mode=`；默认走缓存 |
| `src/module.c` | `ms_module_load` 改调 `ms_compile_cached` |
| `CMakeLists.txt` | 注册 `ms_fs_util` static lib |
| `tests/unit/test_serializer.c` | 扩展测试用例（见下） |

### 可复用的现有代码

- `src/module.c::ms_read_file` — 缓存 miss 路径仍需，不重复造轮子。
- `src/module.c::dir_end` — `ms_cache_path_for` 实现参考。
- `src/serializer.c::collect_fns` 等嵌套函数 DFS 序列化逻辑 — 保持原样。

---

## 验证方案

### 单元测试扩展（`tests/unit/test_serializer.c`）

| # | 用例 | 期望 |
|---|---|---|
| 1 | mtime 写入 + 再次命中 | 第二次加载走 deserialize 路径 |
| 2 | mtime 失效（mtime 改变） | 触发重编译 + 覆盖缓存 |
| 3 | mtime 失效（size 改变） | 触发重编译 + 覆盖缓存 |
| 4 | hash 模式写入 + 命中 | hash 比对正确 |
| 5 | hash 模式：mtime 变但内容未变 | 仍命中（header 自描述为 hash 模式） |
| 6 | v1 header（旧格式） | version 不匹配 → 重编译，覆盖为 v2 |
| 7 | 截断 .msc 文件 | 加载失败 → 重编译 |
| 8 | `__mscache__/` 不存在 | 执行后自动创建目录 |
| 9 | `__mscache__/` 父目录只读 | 脚本正常执行，无 stderr 输出 |
| 10 | 原子写入（进程中断模拟） | 目标 .msc 为完整文件或不存在，不存在截断文件 |

### 端到端冒烟

```bash
cmake --build build && cd build && ctest --output-on-failure -R test_serializer
```

```bash
# 首次执行（编译 + 写缓存）
./build/mslang-c tests/fixtures/foo.ms
ls tests/fixtures/__mscache__/          # 应有 foo.msc

# 第二次执行（fast path）
./build/mslang-c tests/fixtures/foo.ms

# mtime 变化 → 重写
touch tests/fixtures/foo.ms
./build/mslang-c tests/fixtures/foo.ms

# 禁用缓存
./build/mslang-c --no-cache tests/fixtures/foo.ms

# hash 模式
./build/mslang-c --cache-mode=hash tests/fixtures/foo.ms

# import 链（每个被 import 的 .ms 旁都应有 __mscache__/<name>.msc）
./build/mslang-c tests/fixtures/import_chain.ms
```

### 性能验证（可选）

```bash
time ./build/mslang-c <large-script.ms>    # 首次
time ./build/mslang-c <large-script.ms>    # 第二次，应显著低于首次
```

差值 ≈ scan + parse + compile 阶段的时间。

---

## 不在本次范围内

- `--clean-cache` / `--compile-only` 等工具命令（YAGNI）
- 文件名带字节码版本 tag（`foo.mslang-2.msc`）
- 多解释器版本并存
- T30 余下功能（dunder 运算符重载、enum 声明、ternary、incremental GC）——独立任务
- 将 `__mscache__/` 加入 `.gitignore`（用户决策）
- 序列化 STRING / FUNCTION 之外的常量池对象类型（CLASS / LIST / MAP / TUPLE 等当前静默丢弃，需独立讨论）
