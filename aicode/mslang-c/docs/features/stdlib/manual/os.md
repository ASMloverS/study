# os 模块

操作系统接口：进程信息、环境变量、文件系统操作、路径工具。

```ms
import "os"
```

> 实现规格：[STDLIB-02-os.md](../STDLIB-02-os.md)

---

## 函数速查表

### 进程

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `os.name()` | — | str | 平台名："windows" / "linux" / "macos" |
| `os.pid()` | — | int | 当前进程 PID |
| `os.argv()` | — | list[str] | 命令行参数列表 |
| `os.exit([code])` | int? | — | 退出进程，code 默认 0 |

### 环境变量

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `os.env(name)` | str | str \| nil | 读取环境变量，不存在返回 nil |
| `os.setenv(name, val)` | str, str | nil | 设置环境变量 |
| `os.unsetenv(name)` | str | nil | 删除环境变量 |
| `os.environ()` | — | map | 所有环境变量为 map |

### 目录与路径

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `os.cwd()` | — | str | 当前工作目录 |
| `os.chdir(path)` | str | nil | 切换工作目录 |
| `os.exists(path)` | str | bool | 路径是否存在 |
| `os.isfile(path)` | str | bool | 是否为文件 |
| `os.isdir(path)` | str | bool | 是否为目录 |
| `os.mkdir(path)` | str | nil | 创建单层目录 |
| `os.makedirs(path)` | str | nil | 递归创建目录（等价 `mkdir -p`） |
| `os.rmdir(path)` | str | nil | 删除空目录 |
| `os.remove(path)` | str | nil | 删除文件 |
| `os.rename(src, dst)` | str, str | nil | 重命名 / 移动 |
| `os.listdir(path)` | str | list[str] | 列出目录条目（仅文件名，不含路径）|
| `os.stat(path)` | str | map | 文件元信息（见下） |
| `os.realpath(path)` | str | str | 解析为绝对路径 |

### 路径拼接与分割

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `os.join(a, b, ...)` | str... | str | 拼接路径（使用平台分隔符）|
| `os.basename(path)` | str | str | 最后一段路径名 |
| `os.dirname(path)` | str | str | 父目录路径 |
| `os.splitext(path)` | str | (str, str) | 拆分扩展名，返回 `(stem, ext)` |

### 进程执行

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `os.exec(cmd)` | str | int | 执行 shell 命令，返回退出码（0 = 成功）|

---

## 分组详解

### 进程信息

```ms
import "os"
print(os.name())   // windows / linux / macos
print(os.pid())    // 当前 PID（每次运行不同）
```

### 环境变量

```ms
import "os"
var p = os.env("PATH")
print(p != nil)    // true

os.setenv("MY_VAR", "hello")
print(os.env("MY_VAR"))    // hello

os.unsetenv("MY_VAR")
print(os.env("MY_VAR"))    // nil
```

### 文件系统操作

```ms
import "os"
// 检测
print(os.exists("CMakeLists.txt"))   // true
print(os.isfile("CMakeLists.txt"))   // true
print(os.isdir("src"))               // true

// 创建 / 删除
os.makedirs("tmp/a/b")
print(os.isdir("tmp/a/b"))   // true
os.rmdir("tmp/a/b")
os.rmdir("tmp/a")
os.rmdir("tmp")
```

### stat 返回结构

`os.stat(path)` 返回 map，包含以下字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `"size"` | int | 字节数 |
| `"mtime"` | int | 最后修改时间（Unix 秒）|
| `"ctime"` | int | 创建时间（Unix 秒，Windows）/ inode 变更时间（Unix）|
| `"isfile"` | bool | 是否为普通文件 |
| `"isdir"` | bool | 是否为目录 |

```ms
import "os"
var s = os.stat("CMakeLists.txt")
print(s["isfile"])    // true
print(s["size"] > 0)  // true
```

### 路径操作

```ms
import "os"
print(os.join("a", "b", "c"))        // a\b\c (Windows) / a/b/c (Unix)
print(os.basename("path/to/file.c")) // file.c
print(os.dirname("path/to/file.c"))  // path/to
var parts = os.splitext("file.tar.gz")
print(parts[0])   // file.tar
print(parts[1])   // .gz
```

### 执行 shell 命令

```ms
import "os"
var code = os.exec("echo hello")
print(code == 0)   // true（退出码 0 表示成功）
```

---

## 完整示例

文件：[`examples/os.ms`](examples/os.ms)

```ms
import "os"

print(os.name())
print(os.pid())

var home = os.env("PATH")
print(home != nil)

var cwd = os.cwd()
print(os.isdir(cwd))

var p = os.join("docs", "features", "stdlib")
print(p)
print(os.basename("docs/features/stdlib"))
print(os.dirname("docs/features/stdlib"))
print(os.splitext("file.tar.gz"))

print(os.exists("docs"))
print(os.isdir("docs"))
print(os.isfile("CMakeLists.txt"))

var entries = os.listdir("docs/features/stdlib")
print(len(entries) > 0)

var s = os.stat("CMakeLists.txt")
print(s["isfile"])
print(s["isdir"])
print(s["size"] > 0)

os.mkdir("docs/features/stdlib/manual/examples/_tmp_os_test")
print(os.isdir("docs/features/stdlib/manual/examples/_tmp_os_test"))
os.rmdir("docs/features/stdlib/manual/examples/_tmp_os_test")
print(os.exists("docs/features/stdlib/manual/examples/_tmp_os_test"))
```

运行（输出中 PID 每次不同）：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/os.ms
windows
15768
true
true
docs\features\stdlib
stdlib
docs/features
(file.tar, .gz)
true
true
true
true
true
false
true
true
false
```

---

## 实现/性能注解

- `os.join` 使用平台原生分隔符（Windows `\`，Unix `/`），最后一个绝对路径组件会覆盖前面的部分。
- `os.exec` 使用 C `system()`，走 shell 解释，有注入风险——不要将外部输入直接传入。
- `os.stat` 在 Windows 下不返回 `nlink` / `mode` 等 POSIX 字段。

## 常见陷阱

```ms
import "os"
// ❌ 不要对未关闭的文件调用 remove（Windows 会报错）

// ❌ os.exit 直接终止进程，不经过 defer：
// os.exit(0)  ← defer 块不会执行

// ✅ splitext 返回的是元组，用 [0]/[1] 取值
var parts = os.splitext("foo.c")
print(parts[0])   // foo
print(parts[1])   // .c
```
