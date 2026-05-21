# CAPI 用户手册 — 设计规格

> **状态**：设计完成，待实现
>
> 本文件描述手册的结构、每章内容要点、示例工程布局和编写约束。
> 实际手册章节（`01-overview.md` 等）和 `examples/` 示例工程（位于 `manual/examples/`）在此规格评审通过后再写入。

---

## 背景与目标

`docs/features/capi/CAPI-01..08-*.md` 全部 ✅ 完成，面向**实现者**描述内部数据结构和文件修改清单。它们不是"上手用 CAPI"的入口文档。

随着 CAPI-04（动态加载）、CAPI-05（MsModuleApi 函数表）、CAPI-06（File/Buffer/Userdata）、CAPI-07（异步线程池）落地，需要一份面向**使用者**的手册，覆盖三类读者：

| 读者 | 需要知道什么 |
|---|---|
| `.ms` 脚本用户 | `import`、`MSLANG_PATH`、`--module-path` |
| 第三方 C 扩展作者 | `ms_module_init` 入口、`MsModuleApi` 用法、Userdata GC 协议、异步模式 |
| 内置 stdlib 作者 | `MsNativeDef`、`ms_module_register_natives`、静态注册约定 |

---

## 产物结构

```
docs/features/capi/
├── index.md                              # 已存在；新增 manual 入口链接
├── CAPI-01..08-*.md                      # 已存在；保留作为"设计规格"
└── manual/                               # ★ 新增
    ├── README.md                         #   本文件（设计规格）
    ├── 01-overview.md
    ├── 02-using-modules.md
    ├── 03-hello-extension.md
    ├── 04-values-and-errors.md
    ├── 05-userdata-handles.md
    ├── 06-async-extensions.md
    ├── 07-bytes-interop.md               # v1 字节流互操（非 File/Buffer 直接操作）
    ├── 08-builtin-module-author.md
    ├── 09-packaging-and-distribution.md
    ├── 10-api-reference.md
    └── examples/                         #   ★ 可独立编译的示例工程
        ├── README.md                     #   仅含：子工程索引 + 构建命令 + v1 字段依赖清单
        ├── CMakeLists.txt
        ├── 00-hello/          {CMakeLists.txt, hello.c, run.ms}
        ├── 01-values-errors/  {CMakeLists.txt, greet.c, run.ms}
        ├── 02-userdata-hash/  {CMakeLists.txt, hash_ext.c, run.ms}
        ├── 03-async-fileread/ {CMakeLists.txt, fileread.c, run.ms}
        └── 04-bytes-interop/  {CMakeLists.txt, bufops.c, run.ms}
```

---

## 手册风格

- **前半部分**：Tutorial（循序渐进写一个扩展）
- **后半部分**：Reference（按 MsModuleApi 字段分组的函数参考表）
- 代码片段：C 和 `.ms` 混排；关键行加行注释说明
- 每章末尾：「进一步阅读」链接到对应 CAPI-XX 设计规格

---

## 各章内容要点

### 01 — 概述（~80 行）

- CAPI 是什么 / 不是什么
- **读者地图表**：标注每类读者应读哪些章节
- 词汇表：Module、Native、Userdata、Future、ObjFile/Buffer、MsModuleApi

### 02 — 使用模块（~100 行）

- `import name` 三段查找顺序（内置注册表 → 文件系统 → 动态库）
- `MSLANG_PATH`（Unix `:` / Win `;`）与 `--module-path=<dir>` CLI
- 命名冲突规则：内置裸名优先；`./x.ms` 显式路径绕过内置
- 最小 `.ms` 示例：`import io; io.open("f.txt", "r").read()`
- 失败诊断格式：三段查找全部失败时输出 `ImportError: module <name> not found`，附尝试路径列表与 dlopen 最后错误；`MSLANG_TRACE_IMPORT=1` 输出每步细节

### 03 — Hello 扩展教程（~150 行）— **核心 Tutorial**

走过 `examples/00-hello/` 全流程：

1. `MS_EXPORT void ms_module_init(const MsModuleApi*, MsVM*, MsObjModule*)` 签名；`MS_EXPORT` 定义于 `ms/module.h`（Linux/macOS → `__attribute__((visibility("default")))`，Windows → `__declspec(dllexport)`）
2. `CMakeLists.txt` 模板（`add_library(... SHARED ...)`，引用 `ms/module.h` 所在头文件路径）
3. 扩展**不**链接主程序符号，只通过 `api->*` 调用 VM 功能
4. 编译 → 放进 `MSLANG_PATH` → 跑 `run.ms`
5. 跨平台产物名：`lib*.so` / `*.dll` / `lib*.dylib`

**全局符号与 export_value（CAPI-08）**：`ms_module_init` 内通过 `api->export_value(module, "name", val)` 将任意 `MsValue` 挂到模块作用域，即 `.ms` 侧 `import mod; mod.name` 的查找入口；与 `def_native` 的区别是前者可挂任意值（常量、表、Userdata）。详见 CAPI-08-globals-migration.md。

### 04 — 值与错误处理（~150 行）

- 构造：`api->make_int/number/string/list/map/nil/bool`
- 解包：`is_*` + `val_to_*`；必须先 `is_` 检查再解包
- argv 校验范式：`if (argc < N) return api->raise(vm, "expected ...")`
- **raise 协议**：`api->raise` 后必须**立即** `return`；禁止继续访问 MsValue/MsObject
- **内存所有权**：`val_to_cstring` 返回指针有效直到「下一次调用任何触发分配的 `api->*` 函数」或「native 返回」（以先发生者为准）；触发分配的 api 包括 `make_string/make_list/make_map/list_push/map_set/userdata_new`；跨此边界访问为未定义行为，需长期持有时须拷贝到私有缓冲

### 05 — Userdata 句柄（~180 行）

- 为什么需要 Userdata（包装 C 句柄，不新增 `MS_OBJ_*` 枚举）
- 完整签名：`userdata_new(vm, bytes, finalize, mark, type_tag)`
- `finalize`：GC 标记-清除阶段由 VM 主线程调用；禁止在 finalize 内调用任何 `api->*`（GC 临界区）；适合释放 fd / 私有内存
- `mark`：仅当 userdata 内部持有 MsValue 引用时需实现，调用 `api->mark_value(v)` 将引用标活；否则传 `NULL`
- `type_tag`：要求静态生存期字符串字面量；`userdata_is` 内部走指针相等（fastpath）+ `strcmp`（fallback）；跨 DLL 边界时 ptr 不保证相等，依赖 `strcmp` 兜底
- 访问模式：v1 通过模块函数 `hash.update(h, data)`（非方法语法）

  案例：`manual/examples/02-userdata-hash/hash_ext.c`

### 06 — 异步扩展（~200 行）

- Future 模型：`make_future` → pin → worker 完成 → `future_resolve/reject` → unpin
- **关键约束**：worker 线程不操作 MsValue/MsObject，只处理原始 C 数据
- **v1 异步约束（重要）**：`MsModuleApi v1` 不导出 `vm->threadpool`/eventloop；第三方扩展在 v1 下无法接入 VM 异步运行时。`examples/03-async-fileread/` 提供同步等价实现，演示 `make_future/future_resolve/future_reject` 调用形态
- **v2 路线（不承诺）**：计划在 `MsModuleApi v2` 暴露 `threadpool_submit`；届时示例切换为真异步。扩展应在 `init` 时检测 `api->version` 选择实现路径

### 07 — 字节流互操（v1）（~120 行）

- **v1 限制声明**：`MsModuleApi` 函数表无 `is_file/is_buffer`，第三方扩展不能直接操作 `ObjFile/ObjBuffer`；本章以「ObjString 字节流为契约」演示互操方式
- 可行路径：接收 ObjString 字节流（`is_string` + `val_to_cstring` + `string_len`）
- `manual/examples/04-bytes-interop/bufops.c`：演示「接收 ObjString、操作字节、返回新 ObjString」
- 需要直接构造 `ObjFile*/ObjBuffer*` 的场景见第 08 章（内置模块作者）

### 08 — 内置模块作者指南（~120 行）

- 与第三方扩展的差异：可 `#include` 内部头（`ms/object.h`、`ms/stdlib/objbuffer.h`）
- `MsNativeDef[]` + 哨兵 → `ms_module_register_natives`（表式，引 CAPI-02）
- `ms_module_*_init` 注册到 `ms_stdlib_register_all`（懒加载，引 CAPI-01）
- 直接构造 `ObjFile*/ObjBuffer*` 并 `MS_OBJ_VAL()` 返回
- 何时选内置模块 vs 动态扩展：许可证、体积、依赖树

### 09 — 打包与分发（~100 行）

- CMake 模板：头文件获取路径（同仓库 `../../../../include/ms/` 或 `-DMSLANG_INCLUDE_DIR=...`）
- 仅需的头文件：`ms/module.h`、`ms/value.h`、`ms/common.h`（无需完整源码）
- ABI 版本协商模板（raise 拒绝版）：
  ```c
  if (api->version < 1) {
      api->raise(vm, "requires MsModuleApi v1, got v%d", api->version);
      return;  // raise 后 VM 把 import 标为失败，向 .ms 端抛错
  }
  // v2 字段访问前再次检查
  if (api->version >= 2 && api->threadpool_submit) { /* 走异步路径 */ }
  ```
  不 raise 直接 return 会得到「空模块加载成功」，调试困难，避免使用
- 跨平台打包：Linux `.so` rpath、macOS `.dylib` install_name、Windows `.dll` 旁挂 PDB
- 校验清单（编译 → 复制到 MSLANG_PATH → `import` 成功 → 跑 `run.ms`）

### 10 — MsModuleApi 参考（~200 行）

按五个分组各一节，每个函数格式：

```
#### api->make_string
签名、一行说明、最小示例（≤5 行）、失败/边界行为
```

分组（与 CAPI-05-module-api.md 分组一一对应；术语以 CAPI-05 为准，本章与 CAPI-05 不一致时以 CAPI-05 为权威）：
1. 注册（`def_native`、`register_natives`、`export_value`）
2. 值构造（`make_nil/bool/int/number/string/list/map`、`list_push`、`map_set`）
3. 错误与异步（`raise`、`make_future`、`future_resolve`、`future_reject`）
4. Userdata（`userdata_new`、`userdata_data`、`userdata_tag`、`userdata_is`）
5. 值解包与容器（`is_*`、`val_to_*`、`list_len`、`list_get`、`map_get`）

---

## 示例工程约束

- `manual/examples/CMakeLists.txt` **不**被主项目 `CMakeLists.txt` 引用，单独构建：
  ```bash
  cmake -S docs/features/capi/manual/examples -B docs/features/capi/manual/examples/build
  cmake --build docs/features/capi/manual/examples/build
  ```
- 每个示例扩展只引用 `MsModuleApi` v1 字段，`scripts/lint-manual-examples.sh` 自动 grep 校验
- `examples/README.md` 必须包含且仅包含：子工程索引表（编号/目录/关联章节）、顶层构建命令、示例所依赖的 v1 字段最小集合（脚本自动生成）；不得复述 `manual/README.md` 章节结构
- `run.ms` 验证命令格式：
  ```bash
  MSLANG_PATH=docs/features/capi/manual/examples/build/00-hello \
    ./build/mslang-c docs/features/capi/manual/examples/00-hello/run.ms
  ```

---

## 关键设计决策

| 决策 | 结论 | 理由 |
|---|---|---|
| 章节文件 vs 单文件 README | 分章 | 5 类主题差异大；利于跳读和增量提交 |
| 示例位置 | `docs/features/capi/manual/examples/` | 独立教学产物，与 `tests/fixtures/` 约束不同 |
| v1 异步示例 | 写同步版 + v2 路线独立子节 | v1 MsModuleApi 不暴露 threadpool；两条路均不可用不应并列 |
| §07 章节名 | 「字节流互操（v1）」 | v1 ABI 无 `is_file/is_buffer`；名实一致，File/Buffer 内容下沉 §08 |
| 示例 04 目录名 | `04-bytes-interop/` | 与 §07 重命名对齐 |
| CAPI-08 在手册归属 | §03 末段说明 `export_value` | globals 机制影响扩展作者最小知识集 |
| 是否修改 CAPI-01..08 | 否 | 规格已 ✅ 完成；手册只引用它们 |

---

## 实施顺序

| # | 状态 | 产物 |
|---|---|---|
| 1 | ⬜ | `01-overview.md` 占位（读者地图 + 词汇表）+ `index.md` 加 manual 入口链接 |
| 2 | ⬜ | `examples/00-hello/` + `03-hello-extension.md`（跑通最小路径） |
| 3 | ⬜ | `examples/01-values-errors/` + `04-values-and-errors.md` |
| 4 | ⬜ | `examples/02-userdata-hash/` + `05-userdata-handles.md` |
| 5 | ⬜ | `examples/03-async-fileread/`（同步版）+ `06-async-extensions.md` |
| 6 | ⬜ | `examples/04-bytes-interop/` + `07-bytes-interop.md` |
| 7 | ⬜ | `08-builtin-module-author.md` + `09-packaging-and-distribution.md` |
| 8 | ⬜ | `10-api-reference.md` |
| 9 | ⬜ | `02-using-modules.md` + 回填 `01-overview.md` 终稿 |

> ⬜ 待实现 · 🚧 进行中 · ✅ 完成

---

## 验证方法

1. **文档自洽**：`manual/README.md` → 按章节顺序通读，确认链接有效、术语与规格一致
2. **示例可构建**：`cmake -S manual/examples -B manual/examples/build && cmake --build manual/examples/build`（全部 5 个子工程）
3. **示例可运行**：每个 `run.ms` 输出符合预期
4. **主构建不受影响**：`cmake --build build && ctest --output-on-failure` 仍全绿
5. **ABI 真实性（自动化）**：`scripts/lint-manual-examples.sh`（⬜ 待补）提取 v1 字段清单（解析 `include/ms/module.h` `MsModuleApi` struct），grep `api->[a-z_]+` 全部示例 `.c`，diff 发现非 v1 字段调用则 CI 失败
