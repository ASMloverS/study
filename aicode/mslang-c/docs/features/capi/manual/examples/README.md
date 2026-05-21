# CAPI 示例工程索引

## 子工程

| # | 目录 | 关联章节 | 状态 |
|---|---|---|---|
| 00 | `00-hello/` | 03 — Hello 扩展教程 | ✅ |
| 01 | `01-values-errors/` | 04 — 值与错误处理 | ⬜ |
| 02 | `02-userdata-hash/` | 05 — Userdata 句柄 | ⬜ |
| 03 | `03-async-fileread/` | 06 — 异步扩展 | ⬜ |
| 04 | `04-bytes-interop/` | 07 — 字节流互操（v1）| ⬜ |

## 构建

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build
cmake --build docs/features/capi/manual/examples/build
```

如需指定 mslang-c 头文件路径：

```bash
cmake -S docs/features/capi/manual/examples \
      -B docs/features/capi/manual/examples/build \
      -DMSLANG_INCLUDE_DIR=/path/to/mslang-c/include
```

## MsModuleApi v1 字段使用清单

下表由各示例 `.c` 文件汇总，仅列出通过 `api->` 调用的字段。

| 字段 | 00-hello | 01-values-errors | 02-userdata-hash | 03-async-fileread | 04-bytes-interop |
|---|:---:|:---:|:---:|:---:|:---:|
| `version` | ✓ | | | | |
| `def_native` | ✓ | | | | |
| `export_value` | ✓ | | | | |
| `make_int` | ✓ | | | | |
| `make_string` | ✓ | | | | |
| `is_string` | ✓ | | | | |
| `val_to_cstring` | ✓ | | | | |
| `raise` | ✓ | | | | |
