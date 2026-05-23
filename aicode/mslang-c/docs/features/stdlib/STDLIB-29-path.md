# STDLIB-29: path 模块

## 职责

路径字符串工具：拼接、拆分、扩展名、规范化、相对路径计算。
使用 OS 原生路径分隔符（Unix `/`，Windows `\`），委托 `os` 模块获取 `sep`/`cwd`。
对应 Go 的 `path` + `path/filepath`。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/path.ms`）。OS 相关信息从 `os` 模块取用（`os.sep`、`os.cwd()`）。

---

## 函数清单

### 分解

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `path.dirname(p)` | str | str | 目录部分（去掉最后一段）|
| `path.basename(p)` | str | str | 最后一段（文件名含扩展名）|
| `path.ext(p)` | str | str | 扩展名含点（无扩展名返回 `""`）|
| `path.stem(p)` | str | str | 文件名去掉扩展名 |
| `path.split(p)` | str | [str, str] | `[dirname, basename]` |
| `path.split_ext(p)` | str | [str, str] | `[root, ext]`（如 `["foo/bar", ".txt"]`）|
| `path.parts(p)` | str | list | 路径各组成部分（去掉空组件）|

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `path.join(part1, part2, ...)` | str... | str | 用 OS sep 拼接路径 |
| `path.normalize(p)` | str | str | 解析 `.`/`..`，规范化分隔符 |
| `path.absolute(p)` | str | str | 返回绝对路径（相对于 `os.cwd()`）|
| `path.relative(p, base)` | str, str | str | p 相对于 base 的相对路径 |

### 判断

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `path.is_abs(p)` | str | bool | 是否为绝对路径 |
| `path.is_rel(p)` | str | bool | 是否为相对路径 |

### 常量

| 名称 | 描述 |
|---|---|
| `path.sep` | OS 路径分隔符（Unix `"/"` / Windows `"\\"` — 从 `os.sep` 读取）|

---

## 依赖

- `os` 模块（STDLIB-02 ✅，`os.sep`/`os.cwd()`）
- `strings` 模块（STDLIB-13，`split`/`join`/`has_prefix`/`has_suffix`）

---

## 示例

```ms
import "path"

print(path.join("foo", "bar", "baz.txt"))    // foo/bar/baz.txt
print(path.dirname("foo/bar/baz.txt"))       // foo/bar
print(path.basename("foo/bar/baz.txt"))      // baz.txt
print(path.ext("foo/bar/baz.txt"))           // .txt
print(path.stem("foo/bar/baz.txt"))          // baz
print(path.split_ext("foo/bar/baz.txt"))     // ["foo/bar/baz", ".txt"]

print(path.normalize("foo//bar/../baz"))     // foo/baz
print(path.is_abs("/etc/hosts"))             // true
print(path.is_abs("relative/path"))          // false

print(path.relative("/a/b/c", "/a"))         // b/c
print(path.parts("/usr/local/bin"))          // ["usr", "local", "bin"]
```

---

## 测试

```
tests/unit/test_stdlib_path.c
tests/fixtures/stdlib_path_basic.ms
```

关键测试点：
- Unix vs Windows 分隔符（`path.sep` 判断）
- `normalize` 处理 `//`/`./`/`../`
- `relative` 跨越父目录（如 `/a/b` → `/a/c` 得 `../c`）
- 空路径/根路径边界
