# `__mscache__` 字节码缓存 — 实现任务索引

> 设计文档：`docs/MSCACHE-DESIGN.md`

## 任务顺序

| 任务 | 内容 | 依赖 | 测试目标 |
|---|---|---|---|
| [T01](T01-fs-util.md) | 跨平台 fs_util 模块 | 无 | `ctest -R test_fs_util` |
| [T02](T02-header-v2-cache-path.md) | MsMscHeader v2 + 缓存路径解析 | T01 | `ctest -R test_serializer` |
| [T03](T03-mtime-fast-path.md) | mtime 快速路径 + 原子写入 | T01, T02 | `ctest -R test_serializer` |
| [T04](T04-hash-mode.md) | hash 校验模式 | T03 | `ctest -R test_serializer` |
| [T05](T05-cli-default-on.md) | CLI 默认开启 + --no-cache / --cache-mode | T03, T04 | smoke test |
| [T06](T06-module-cache.md) | 模块加载器缓存集成 | T03 | `ctest -R test_modules` |

每个任务完成后系统均可编译、`./build/mslang-c --version` 正常执行、对应测试通过。

## 全部完成后的验证

```bash
cmake --build build && cd build && ctest --output-on-failure

# 首次执行（编译 + 写缓存）
./build/mslang-c tests/fixtures/hello.ms
ls tests/fixtures/__mscache__/          # 应有 hello.msc

# 第二次执行（mtime fast path，不读源文件）
./build/mslang-c tests/fixtures/hello.ms

# mtime 变化 → 重写缓存
touch tests/fixtures/hello.ms
./build/mslang-c tests/fixtures/hello.ms

# 禁用缓存
./build/mslang-c --no-cache tests/fixtures/hello.ms

# hash 模式
./build/mslang-c --cache-mode=hash tests/fixtures/hello.ms
```
