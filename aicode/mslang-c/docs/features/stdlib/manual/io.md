# io 模块

文件 IO：整文件读写（同步 + 异步）、流式 `File` 句柄、标准流。

```ms
import "io"
```

> 实现规格：[STDLIB-04-io.md](../STDLIB-04-io.md)

---

## 函数速查表

### 整文件（一次性）

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `io.read_file(path)` | str | str | 读整文件为 UTF-8 字符串 |
| `io.read_bytes(path)` | str | Buffer | 读整文件为 Buffer |
| `io.write_file(path, text)` | str, str | nil | 覆盖写入字符串 |
| `io.write_bytes(path, buf)` | str, Buffer | nil | 覆盖写入字节 |
| `io.append_file(path, text)` | str, str | nil | 追加写入字符串 |
| `io.lines(path)` | str | list[str] | 按行读，每行**不含**尾部换行符 |
| `io.read_file_async(path)` | str | Future\<str\> | 异步整文件读 |
| `io.write_file_async(path, text)` | str, str | Future\<nil\> | 异步整文件写 |

### 流式句柄

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `io.open(path, mode)` | str, str | File | 打开文件，返回 File 句柄 |

支持的模式：`"r"`（读）、`"w"`（写/截断）、`"a"`（追加）、`"rb"`、`"wb"`、`"ab"`（二进制）、`"r+"`、`"w+"`（读写）。

### 标准流（常量）

| 名称 | 说明 |
|---|---|
| `io.stdin` | 标准输入 File 句柄 |
| `io.stdout` | 标准输出 File 句柄 |
| `io.stderr` | 标准错误 File 句柄 |

---

## File 句柄方法

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `f.read([n])` | int? | str \| Buffer | n < 0 或省略：读到 EOF；二进制模式返回 Buffer |
| `f.readline()` | — | str \| nil | 读一行（**含尾部 `\n`**）；EOF 返回 nil |
| `f.readlines()` | — | list[str] | 读所有行（各行**含尾部 `\n`**）|
| `f.write(data)` | str \| Buffer | int | 写入，返回写入字节数 |
| `f.flush()` | — | nil | 刷新到磁盘 |
| `f.seek(offset[, whence])` | int, int? | int | 移动读写位置；whence 0/1/2 = SET/CUR/END |
| `f.tell()` | — | int | 当前读写位置 |
| `f.close()` | — | nil | 关闭文件，之后操作报错 |
| `f.eof()` | — | bool | 是否到 EOF |
| `f.fd()` | — | int | 底层文件描述符 |
| `f.mode()` | — | str | 打开时的 mode 字符串 |

---

## 分组详解

### 整文件读写

```ms
import "io"

// 写文件
io.write_file("out.txt", "hello\nworld\n")

// 读整文件为字符串
var text = io.read_file("out.txt")
print(text)        // hello\nworld\n

// 按行读（不含换行符）
var lines = io.lines("out.txt")
print(lines[0])    // hello
print(lines[1])    // world
print(len(lines))  // 2

// 追加
io.append_file("out.txt", "line3\n")
```

### 流式 File 句柄

```ms
import "io"

var f = io.open("out.txt", "r")
print(f.mode())           // r

var line = f.readline()   // 返回 "hello\n"（含换行符）
print(f.tell())           // 6（读取了 6 字节）

f.seek(0)                 // 回到文件开头
var all = f.readlines()   // 读所有行
print(len(all))           // 2

f.close()
```

### 二进制 IO

```ms
import "io"
import "buffer"

var buf = buffer.from_hex("cafebabe")
io.write_bytes("data.bin", buf)

var rbuf = io.read_bytes("data.bin")
print(rbuf.to_hex())   // cafebabe
```

### 标准流

```ms
import "io"
io.stdout.write("hello from stdout\n")
io.stderr.write("error message\n")
```

### 异步文件 IO

```ms
import "io"
import "time"

async fun read_config() {
    var content = await io.read_file_async("config.txt")
    return len(content)
}

var n = time.run_until_complete(read_config())
print(n)   // 文件字节数
```

---

## 完整示例

文件：[`examples/io.ms`](examples/io.ms)

```ms
import "io"

var tmp = "docs/features/stdlib/manual/examples/_tmp_io_test.txt"

io.write_file(tmp, "Hello, mslang!\nLine 2\nLine 3")

var content = io.read_file(tmp)
print(content)

var lines = io.lines(tmp)
print(len(lines))
print(lines[0])
print(lines[2])

io.append_file(tmp, "\nLine 4")
var lines2 = io.lines(tmp)
print(len(lines2))

var f = io.open(tmp, "r")
var first = f.readline()
print(first)
print(f.eof())
f.close()

import "buffer"
var btmp = "docs/features/stdlib/manual/examples/_tmp_io_bytes.bin"
var buf = buffer.from_hex("cafebabe")
io.write_bytes(btmp, buf)
var rbuf = io.read_bytes(btmp)
print(rbuf.to_hex())
print(rbuf.len())

io.stdout.write("written via io.stdout\n")

import "os"
os.remove(tmp)
os.remove(btmp)
print(os.exists(tmp))
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/io.ms
Hello, mslang!
Line 2
Line 3
3
Hello, mslang!
Line 3
4
Hello, mslang!
              ← readline 保留了尾部 \n，print 又加了一个 \n
false
cafebabe
4
written via io.stdout
false
```

---

## 实现/性能注解

- `io.lines(path)` 返回的每行**不含**换行符，而 `f.readline()` / `f.readlines()` **保留**尾部 `\n`。
- 使用 `"r+"` / `"w+"` 读写混合模式时，在读写方向切换前须调用 `f.flush()` 或 `f.seek(f.tell())`，否则行为未定义（C89 标准限制）。
- `io.stdin` / `io.stdout` / `io.stderr` 的 `close()` 会报错（GC 不会释放标准流）。

## 常见陷阱

```ms
import "io"
// ❌ readline 含尾部 \n，直接拼接会多一个换行
var f = io.open("file.txt", "r")
var line = f.readline()     // "hello\n"
// print(">" + line)  → ">hello\n\n"（多余空行）

// ✅ 手动去掉尾部换行（字符串 trim 尚未内置，可先用 io.lines）
var lines = io.lines("file.txt")   // 不含换行符

// ❌ 文件关闭后继续操作
f.close()
// f.readline()  → 运行时错误
```
