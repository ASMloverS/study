# STDLIB-04: io 模块

## 职责

文件 IO：整文件读写（同步 + 异步）、流式 `File` 句柄（`io.open`）、标准流（stdin/stdout/stderr）。异步版本 Unix 走线程池（CAPI-07），Windows 走 IOCP。

---

## 函数清单

### 整文件（一次性）

| 函数 | 参数 | 返回 | async | 描述 |
|---|---|---|---|---|
| `io.read_file(path)` | str | str | – | 整文件读为 UTF-8 字符串 |
| `io.read_bytes(path)` | str | Buffer | – | 整文件读为 ObjBuffer |
| `io.write_file(path, text)` | str,str | nil | – | 覆盖写入字符串 |
| `io.write_bytes(path, buf)` | str,Buffer | nil | – | 覆盖写入字节 |
| `io.append_file(path, text)` | str,str | nil | – | 追加写入 |
| `io.lines(path)` | str | list\[str\] | – | 按行读，每行不含尾部换行符 |
| `io.read_file_async(path)` | str | Future\<str\> | ✓ | 线程池/IOCP 异步读 |
| `io.write_file_async(path, text)` | str,str | Future\<nil\> | ✓ | 异步写 |

### 流式句柄

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `io.open(path, mode)` | str,str | File | 模式：`"r"` `"w"` `"a"` `"rb"` `"wb"` `"ab"` `"r+"` `"w+"` |

> **注**：使用 `r+`/`w+` 模式时，read 与 write 操作切换前须显式调用 `f.flush()` 或 `f.seek(current_pos)`（等价于 `f.seek(0, 1)`）。C89 标准要求在读写方向切换时执行一次定位操作，否则行为未定义（UB）。v2 计划在底层自动插入 `fseek` 隔离操作，目前为 known-issue。

### 补充 API（v2）

| 函数 | 描述 |
|---|---|
| `io.copy(src_path, dst_path)` | 复制文件内容，返回写入字节数 |
| `io.tempfile() → (File, str)` | 创建临时文件，返回 (文件句柄, 临时路径) |
| `io.tempdir() → str`          | 创建临时目录，返回目录路径 |
| `io.exists(path) → bool`      | 判断路径是否存在（与 `os.exists` 择一保留，建议统一到 io）|

> `io.exists` 与 `os.exists`（如有）语义相同，最终实现时择一保留，避免重复。

### 标准流（export_value，在 init 时构造）

| 名称 | 描述 |
|---|---|
| `io.stdin` | `ObjFile(stdin, TEXT)` |
| `io.stdout` | `ObjFile(stdout, TEXT)` |
| `io.stderr` | `ObjFile(stderr, TEXT)` |

---

## File 句柄方法（`ObjFile`，见 CAPI-06）

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `f.read(n=-1)` | int | str\|Buffer | n<0 读到 EOF；二进制模式返 Buffer |
| `f.readline()` | – | str\|nil | nil 表示 EOF |
| `f.readlines()` | – | list\[str\] | 全部行 |
| `f.write(data)` | str\|Buffer | int | 实际写入字节数 |
| `f.flush()` | – | nil | `fflush` |
| `f.seek(offset, whence=0)` | int,int | int | whence 0/1/2 = SEEK_SET/CUR/END |
| `f.tell()` | – | int | `ftell` |
| `f.close()` | – | nil | `fclose`；后续操作报错 |
| `f.eof()` | – | bool | `feof` |
| `f.fd()` | – | int | `fileno`（Windows `_fileno`）|
| `f.mode()` | – | str | 返回打开时的 mode 字符串 |

---

## 实现要点（`src/stdlib/io.c`）

```c
#include "ms/module.h"
#include "ms/stdlib/objfile.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/threadpool.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* 整文件同步读 */
static MsValue ms_io_read_file(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    const char* path = MS_AS_CSTRING(argv[0]);
    char* buf = ms_read_file(path);  /* 复用 module.c 中已有的实现 */
    if (!buf) {
        ms_vm_runtime_error(vm, "io.read_file: cannot open '%s'", path);
        return MS_NIL_VAL();
    }
    MsValue v = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)strlen(buf)));
    free(buf);
    return v;
}

/* io.open */
static MsValue ms_io_open(MsVM* vm, int argc, MsValue* argv) {
    const char* path = MS_AS_CSTRING(argv[0]);
    const char* mode = MS_AS_CSTRING(argv[1]);
    MsFileMode fm = (strchr(mode, 'b') != NULL) ? MS_FILE_BINARY : MS_FILE_TEXT;
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, mode);
#else
    fp = fopen(path, mode);
#endif
    if (!fp) {
        ms_vm_runtime_error(vm, "io.open: cannot open '%s': %s", path, strerror(errno));
        return MS_NIL_VAL();
    }
    return MS_OBJ_VAL(ms_obj_file_new(vm, fp, fm, true));
}

static const MsNativeDef ms_io_defs[] = {
    {"read_file",        ms_io_read_file,        1},
    {"read_bytes",       ms_io_read_bytes,       1},
    {"write_file",       ms_io_write_file,       2},
    {"write_bytes",      ms_io_write_bytes,      2},
    {"append_file",      ms_io_append_file,      2},
    {"lines",            ms_io_lines,            1},
    {"read_file_async",  ms_io_read_file_async,  1},
    {"write_file_async", ms_io_write_file_async, 2},
    {"open",             ms_io_open,             2},
    {NULL, NULL, 0}
};

void ms_module_io_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_io_defs);
    /* 标准流句柄（owns_fp=false，GC 回收时不关闭）*/
    ms_module_export_value(vm, mod, "stdin",
        MS_OBJ_VAL(ms_obj_file_new(vm, stdin,  MS_FILE_TEXT, false)));
    ms_module_export_value(vm, mod, "stdout",
        MS_OBJ_VAL(ms_obj_file_new(vm, stdout, MS_FILE_TEXT, false)));
    ms_module_export_value(vm, mod, "stderr",
        MS_OBJ_VAL(ms_obj_file_new(vm, stderr, MS_FILE_TEXT, false)));
}
```

### 标准流保护

`ObjFile` 结构新增字段 `bool owns_fp;`：

```c
ms_obj_file_new(MsVM* vm, FILE* fp, MsFileMode mode, bool owns_fp)
```

GC free 时：
```c
if (f->owns_fp && f->fp) { fclose(f->fp); f->fp = NULL; }
```

标准流以 `owns_fp = false` 构造，GC 回收时不关闭：
```c
vm->io_stdin  = ms_obj_file_new(vm, stdin,  MS_FILE_TEXT, false);
vm->io_stdout = ms_obj_file_new(vm, stdout, MS_FILE_TEXT, false);
vm->io_stderr = ms_obj_file_new(vm, stderr, MS_FILE_TEXT, false);
```

> **注**：`CAPI-06 §MsObjFile` 需同步加入 `owns_fp` 字段与更新后的构造函数签名（本模块为前置变更需求方）。

---

## 异步读文件（Unix 线程池）

见 CAPI-07 `ms_io_read_file_async` 实现，此处不重复。

---

## 依赖

- CAPI-01/02（注册）
- CAPI-06（ObjFile，ObjBuffer）
- CAPI-07（线程池，Unix 异步）
- STDLIB-05（Buffer，`read_bytes` 返回 ObjBuffer）

---

## 测试

```ms
// tests/fixtures/stdlib_io_file.ms
import io, buffer
var f = io.open("tests/fixtures/hello.txt", "r")
var line = f.readline()
assert(line != nil)
f.close()
```

```ms
// tests/fixtures/stdlib_io_async.ms
import io, time
async fun main() {
    var content = await io.read_file_async("tests/fixtures/hello.txt")
    assert(len(content) > 0)
}
time.run_until_complete(main())
```

```c
// tests/unit/test_stdlib_io.c
// 1. io.read_file + io.write_file 往返
// 2. io.open("wb") + f.write(buf) + f.close + io.read_bytes 一致
// 3. io.lines 行数正确
// 4. 打开不存在文件抛运行时错误
```

> **待补充 fixture（错误路径）**：
> - 连接被拒绝（连接不存在的端口，断言 Future 状态为 error）
> - 读到 EOF（对端 close，断言返回空 Buffer 或 nil）
> - 部分写入触发 EAGAIN（大数据写入，断言正确重试或回调）
> - 超时（若 EventLoop 支持 deadline，断言 Future 被 kill 后状态）
