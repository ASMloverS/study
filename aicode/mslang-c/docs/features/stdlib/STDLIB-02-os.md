# STDLIB-02: os 模块

## 职责

操作系统接口：环境变量、进程信息、文件系统元数据操作（不含文件读写，那属于 `io`）、子进程执行。

---

## 函数清单

### 进程信息

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `os.name()` | – | str | "windows" / "linux" / "darwin" / "bsd" / "unknown" |
| `os.pid()` | – | int | 当前进程 PID |
| `os.argv()` | – | list\[str\] | 进程参数（去掉 `mslang-c` 本身，从脚本路径开始）|
| `os.exit(code=0)` | int | – | `exit(code)`，不返回 |

### 环境变量

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `os.env(key)` | str | str\|nil | `getenv`，未设置返回 nil |
| `os.setenv(key, val)` | str,str | nil | `setenv`/`_putenv_s` |
| `os.unsetenv(key)` | str | nil | `unsetenv`/`_putenv_s("KEY=")` |
| `os.environ()` | – | map\[str,str\] | 所有环境变量快照 |

### 文件系统元数据

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `os.cwd()` | – | str | `getcwd`/`GetCurrentDirectory` |
| `os.chdir(path)` | str | nil | `chdir`/`SetCurrentDirectory` |
| `os.exists(path)` | str | bool | stat 成功即为 true |
| `os.isfile(path)` | str | bool | `S_ISREG`/`!FILE_ATTRIBUTE_DIRECTORY` |
| `os.isdir(path)` | str | bool | `S_ISDIR`/`FILE_ATTRIBUTE_DIRECTORY` |
| `os.mkdir(path)` | str | nil | `mkdir`（单级）；失败抛运行时错误 |
| `os.makedirs(path)` | str | nil | 递归建目录（类 `mkdir -p`）|
| `os.rmdir(path)` | str | nil | 删除空目录 |
| `os.remove(path)` | str | nil | 删除文件（`unlink`/`DeleteFile`）|
| `os.rename(src, dst)` | str,str | nil | `rename`/`MoveFileEx` |
| `os.listdir(path)` | str | list\[str\] | 目录项（不含 `.` / `..`）|
| `os.stat(path)` | str | map | `{size, mtime, ctime, isdir, isfile}` |
| `os.realpath(path)` | str | str | `realpath`/`GetFullPathName` |
| `os.join(a, b, ...)` | str... | str | 平台无关路径拼接 |
| `os.basename(path)` | str | str | 最后一个路径分量 |
| `os.dirname(path)` | str | str | 目录部分 |
| `os.splitext(path)` | str | (str,str) | 返回 list\[stem, ext\] |

### 子进程

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `os.exec(cmd)` | str | int | `system(cmd)`，返回退出码（v1 不支持重定向）|

### 常量（export_value）

| 名称 | 值 |
|---|---|
| `os.sep` | `"/"` (Unix) / `"\\"` (Windows) |
| `os.linesep` | `"\n"` / `"\r\n"` |
| `os.pathsep` | `":"` / `";"` |
| `os.devnull` | `"/dev/null"` / `"NUL"` |

---

## 实现要点（`src/stdlib/os.c`）

```c
static MsValue ms_os_name(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
#if defined(_WIN32)
    const char* name = "windows";
#elif defined(__linux__)
    const char* name = "linux";
#elif defined(__APPLE__)
    const char* name = "darwin";
#else
    const char* name = "unknown";
#endif
    return MS_OBJ_VAL(ms_obj_string_copy(vm, name, (int)strlen(name)));
}

static MsValue ms_os_listdir(MsVM* vm, int argc, MsValue* argv) {
    const char* path = MS_AS_CSTRING(argv[0]);
    MsValue list = MS_OBJ_VAL(ms_obj_list_new(vm));
#ifdef _WIN32
    /* FindFirstFile / FindNextFile */
#else
    /* opendir / readdir */
    DIR* d = opendir(path);
    if (!d) { ms_vm_runtime_error(vm, "os.listdir: %s", strerror(errno)); return MS_NIL_VAL(); }
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        MsValue s = MS_OBJ_VAL(ms_obj_string_copy(vm, ent->d_name, (int)strlen(ent->d_name)));
        ms_value_array_push(&MS_AS_LIST(list)->items, s);
    }
    closedir(d);
#endif
    return list;
}
```

`os.argv()` 需要 VM 启动时把 `argc/argv` 保存进 `MsVM`（新增字段 `int main_argc; char** main_argv`）或通过全局变量传入。建议在 `src/main.c` 里 `ms_vm_set_argv(vm, argc - script_idx, argv + script_idx)` 后再调 `ms_vm_interpret`。

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `src/stdlib/os.c` | 新建 |
| `include/ms/vm.h` | `MsVM` 增 `main_argc`、`main_argv`（指针，不 strdup）|
| `src/main.c` | 解析脚本 index，写入 `vm.main_argc/argv` |

---

## 依赖

- CAPI-01/02
- Unix：`<dirent.h>` `<sys/stat.h>` `<unistd.h>`
- Windows：`<windows.h>`

---

## 测试

```ms
// tests/fixtures/stdlib_os_basic.ms
import os
print(os.name())
print(os.cwd())
os.mkdir("__test_tmp__")
print(os.isdir("__test_tmp__"))
os.rmdir("__test_tmp__")
print(os.exists("__test_tmp__"))  // false
```
