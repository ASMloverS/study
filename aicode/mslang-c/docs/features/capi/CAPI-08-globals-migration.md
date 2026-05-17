# CAPI-08: 全局原生迁入模块

## 目标

将 `vm_natives.c` 中已注册的全局名字迁入对应标准库模块，使 `import net; net.connect(...)` 成为唯一入口，同时清理全局命名空间。

---

## 迁移映射表

| 原全局名 | 新位置 | 实现文件 | 备注 |
|---|---|---|---|
| `print` | 保留全局 | `vm_natives.c` | 语言核心 |
| `input` | 保留全局 | `vm_natives.c` | 语言核心 |
| `assert` | 保留全局 | `vm_natives.c` | 语言核心 |
| `type` | 保留全局 | `vm_natives.c` | 类型反射 |
| `str`/`tostring` | 保留全局 | `vm_natives.c` | 类型转换 |
| `num`/`int`/`float`/`toint`/`tofloat` | 保留全局 | `vm_natives.c` | 类型转换 |
| `len` | 保留全局 | `vm_natives.c` | 通用 |
| `hasattr`/`getattr`/`setattr` | 保留全局 | `vm_natives.c` | 反射 |
| `clock` | `time.clock` | `src/stdlib/time.c` | 移除全局 |
| `sleep` | `time.sleep` | `src/stdlib/time.c` | 移除全局 |
| `run_until_complete` | `time.run_until_complete` | `src/stdlib/time.c` | 移除全局 |
| `gather` | `time.gather` | `src/stdlib/time.c` | 移除全局 |
| `resume` | `time.resume` | `src/stdlib/time.c` | 移除全局 |
| `tcp_listen` | `net.listen` | `src/stdlib/net.c` | 移除全局 |
| `tcp_connect` | `net.connect` | `src/stdlib/net.c` | 移除全局 |
| `_socket_accept` | `net.accept`（+ ObjSocket 方法 `s.accept()`）| `src/stdlib/net.c` | 移出全局；方法保留 |
| `_socket_read` | `net.read`（+ `s.read()`）| `src/stdlib/net.c` | 同上 |
| `_socket_write` | `net.write`（+ `s.write()`）| `src/stdlib/net.c` | 同上 |
| `_socket_close` | `net.close`（+ `s.close()`）| `src/stdlib/net.c` | 同上 |

**保留全局的原则**：无模块前缀仍有语义（`print`、`len`）或是类型系统基础（`type`、`str`）。

---

## `vm_natives.c` 重构步骤

**迁移前**（当前状态）：
```c
void ms_vm_register_natives(MsVM* vm) {
    ms_vm_define_native(vm, "clock", native_clock, 0);
    ms_vm_define_native(vm, "print", native_print, -1);
    // ... 全部 ~22 个 ...
    ms_vm_define_native(vm, "tcp_listen",     native_tcp_listen,    2);
    ms_vm_define_native(vm, "tcp_connect",    native_tcp_connect,   2);
    ms_vm_define_native(vm, "_socket_accept", native_socket_accept, 1);
    ms_vm_define_native(vm, "_socket_read",   native_socket_read,   2);
    ms_vm_define_native(vm, "_socket_write",  native_socket_write,  2);
    ms_vm_define_native(vm, "_socket_close",  native_socket_close,  1);
    ms_vm_define_native(vm, "sleep",              native_sleep,     1);
    ms_vm_define_native(vm, "run_until_complete", native_run_until_complete, 1);
    ms_vm_define_native(vm, "gather",             native_gather,    -1);
    ms_vm_define_native(vm, "resume",             native_resume,    2);
}
```

**迁移后**（目标状态）：
```c
void ms_vm_register_natives(MsVM* vm) {
    /* 语言核心全局（保留）*/
    ms_vm_define_native(vm, "print",    native_print,    -1);
    ms_vm_define_native(vm, "input",    native_input,     0);
    ms_vm_define_native(vm, "assert",   native_assert,    2);
    ms_vm_define_native(vm, "type",     native_type,      1);
    ms_vm_define_native(vm, "str",      native_str,       1);
    ms_vm_define_native(vm, "tostring", native_str,       1);
    ms_vm_define_native(vm, "num",      native_num,       1);
    ms_vm_define_native(vm, "int",      native_num_int,   1);
    ms_vm_define_native(vm, "float",    native_num_float, 1);
    ms_vm_define_native(vm, "toint",    native_num_int,   1);
    ms_vm_define_native(vm, "tofloat",  native_num_float, 1);
    ms_vm_define_native(vm, "len",      native_len,       1);
    ms_vm_define_native(vm, "hasattr",  native_hasattr,   2);
    ms_vm_define_native(vm, "getattr",  native_getattr,   2);
    ms_vm_define_native(vm, "setattr",  native_setattr,   3);
    /* clock/sleep/tcp_*/resume/gather 已迁入 stdlib 模块，不在此注册 */
}
```

被迁出的原生函数实现（`native_tcp_listen` 等）**移动**到对应 `src/stdlib/net.c` / `src/stdlib/time.c`，改为 `static` 作用域。不删除实现，只改位置和可见性。

---

## Fixtures 更新清单

需将以下文件中的旧全局调用改为 `import` + 模块调用：

| 文件 | 改动 |
|---|---|
| `tests/fixtures/async_tcp_server.ms` | `tcp_listen` → `import net; net.listen` |
| `tests/fixtures/async_tcp_client.ms` | `tcp_connect` → `net.connect` |
| `tests/fixtures/async_sleep.ms` | `sleep` → `import time; time.sleep` |
| `tests/fixtures/async_gather.ms` | `gather` → `time.gather` |
| `tests/fixtures/async_run.ms` | `run_until_complete` → `time.run_until_complete` |
| `tests/fixtures/native_clock.ms` | `clock()` → `import time; time.clock()` |
| （其他引用 `_socket_*` 的 fixture）| 改为方法调用 `s.read()` 或 `net.read(s, n)` |

批量替换命令参考：
```bash
sed -i 's/tcp_listen(/import net\nnet.listen(/g' tests/fixtures/async_*.ms
```

实际操作建议逐文件手动确认，sed 替换后运行 ctest 验证。

---

## 验证步骤

```bash
cmake --build build && cd build && ctest --output-on-failure
```

重点验证：
1. `test_async_frontend`、`test_tcp_socket` 仍然通过（net 功能未损坏）
2. `test_event_loop`、`test_future` 仍通过（async 基础不受影响）
3. 新增 `import net; print(type(net.listen))` → `"native"` 而非报错

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `src/vm_natives.c` | 删除已迁出的注册行；移动 `native_tcp_*/sleep/...` 函数实现到对应 stdlib 文件 |
| `src/stdlib/net.c` | 接收 `native_tcp_listen/connect/_socket_*` 实现（改 static） |
| `src/stdlib/time.c` | 接收 `native_clock/sleep/run_until_complete/gather/resume`（改 static） |
| `tests/fixtures/async_*.ms` + `native_*.ms` | 更新为模块调用方式 |
