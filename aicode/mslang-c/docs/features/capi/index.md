# C API 设计索引

| 状态 | 文件 | 说明 |
|---|---|---|
| ✅ | [CAPI-01-registry.md](CAPI-01-registry.md) | 内置模块注册表 + ms_module_load 钩子 |
| ✅ | [CAPI-02-native-def.md](CAPI-02-native-def.md) | MsNativeDef + ms_module_register_natives |
| ✅ | [CAPI-03-search-path.md](CAPI-03-search-path.md) | MSLANG_PATH + --module-path |
| ⬜ | [CAPI-04-dynamic-loading.md](CAPI-04-dynamic-loading.md) | dlopen/LoadLibrary + ms_module_init ABI |
| ⬜ | [CAPI-05-module-api.md](CAPI-05-module-api.md) | MsModuleApi 函数表 |
| ⬜ | [CAPI-06-handle-types.md](CAPI-06-handle-types.md) | ObjFile / ObjBuffer / MsObjUserdata |
| ⬜ | [CAPI-07-threadpool.md](CAPI-07-threadpool.md) | Unix 文件异步线程池 |
| ⬜ | [CAPI-08-globals-migration.md](CAPI-08-globals-migration.md) | 全局原生迁入模块 |

> ⬜ 待实现 · 🚧 进行中 · ✅ 完成

## 实施顺序

```
CAPI-01 (注册表)
    ↓
CAPI-02 (NativeDef API)
    ↓
CAPI-05 (ModuleApi 函数表)
    ↓
CAPI-06 (句柄类型 ObjFile/ObjBuffer/Userdata)
    ↓
CAPI-08 (全局迁移 — 与 STDLIB-08 net 联动)
    ↓
CAPI-07 (线程池 — 与 STDLIB-04 io async 联动)
    ↓
CAPI-03 (搜索路径)
    ↓
CAPI-04 (动态加载 — 依赖前面全部)
```

## 前置依赖

| 依赖 | 说明 |
|---|---|
| ASYNC-04/05/06（EventLoop / Reactor / Socket）| net 模块迁移复用现有实现 |
| T26（模块系统）| `MsObjModule`、`ms_module_load` 已落地 |
