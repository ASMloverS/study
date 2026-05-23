# STDLIB-30: bufio 模块

## 职责

缓冲 IO：在 `io.File` 之上提供行级/块级缓冲读写，减少系统调用次数。
对应 Go 的 `bufio` 包。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/bufio.ms`）。底层 IO 委托 `io` 模块；内部缓冲用 `buffer` 模块。

---

## 类型设计

```ms
// BufReader：缓冲读取器
class BufReader {
    init(file, buf_size=4096) {
        this._file    = file
        this._buf     = buffer.new(buf_size)
        this._pos     = 0
        this._filled  = 0
    }
}

// BufWriter：缓冲写入器
class BufWriter {
    init(file, buf_size=4096) {
        this._file   = file
        this._buf    = buffer.new(buf_size)
    }
}
```

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bufio.new_reader(file, buf_size=4096)` | File, int | BufReader | 创建缓冲读取器 |
| `bufio.new_writer(file, buf_size=4096)` | File, int | BufWriter | 创建缓冲写入器 |

### BufReader 方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `br.read_line()` | — | str\|nil | 读取一行（不含 `\n`），EOF 返回 nil |
| `br.read_lines()` | — | list[str] | 读取所有行 |
| `br.read_chunk(n)` | int | buffer | 读取最多 n 字节 |
| `br.read_until(delim)` | str | buffer | 读取直到出现 delim 字符（含）|
| `br.read_all()` | — | buffer | 读取所有剩余内容 |
| `br.peek(n)` | int | buffer | 预读 n 字节但不移动位置 |

### BufWriter 方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bw.write(data)` | str\|buffer | nil | 写入（缓冲中）|
| `bw.write_line(s)` | str | nil | 写入字符串 + `\n` |
| `bw.flush()` | — | nil | 强制写入底层文件 |

### 便捷函数

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `bufio.read_lines(filepath)` | str | list[str] | 打开文件读取所有行（自动关闭）|
| `bufio.write_lines(filepath, lines)` | str, list[str] | nil | 写入所有行（自动关闭）|

---

## 依赖

- `io` 模块（STDLIB-04 ✅，`File` 句柄）
- `buffer` 模块（STDLIB-05 ✅，内部缓冲）

---

## 示例

```ms
import "bufio"
import "io"

// 逐行读取大文件
var f = io.open("large.txt", "r")
var br = bufio.new_reader(f)
var line = br.read_line()
while line != nil {
    print(line)
    line = br.read_line()
}
f.close()

// 缓冲写入
var wf = io.open("output.txt", "w")
var bw = bufio.new_writer(wf)
bw.write_line("第一行")
bw.write_line("第二行")
bw.flush()
wf.close()

// 便捷一行读取
var lines = bufio.read_lines("data.txt")
print(len(lines))
```

---

## 测试

```
tests/unit/test_stdlib_bufio.c
tests/fixtures/stdlib_bufio_basic.ms
```

关键测试点：
- 跨缓冲边界的 read_line（行内容恰好跨两个 buf_size）
- 空文件
- 无 `\n` 结尾的最后一行
- BufWriter flush 前数据不写入底层
