# STDLIB-39: flag 模块

## 职责

命令行参数解析：定义标志（`--name value`/`--flag`），解析 `os.argv`，
返回解析结果和剩余位置参数。对应 Go 的 `flag` 包。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/flag.ms`）。从 `os.argv()` 读取参数（`os.argv` 是函数，须调用）。

---

## 函数清单

### 定义标志

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `flag.string(name, default, usage)` | str,str,str | Ref | 字符串标志 |
| `flag.int(name, default, usage)` | str,int,str | Ref | 整数标志 |
| `flag.float(name, default, usage)` | str,num,str | Ref | 浮点标志 |
| `flag.bool(name, default, usage)` | str,bool,str | Ref | 布尔标志（`--flag` 设为 true）|

### 解析

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `flag.parse()` | — | nil | 解析 `os.argv()[1:]`（失败打印用法并退出）|
| `flag.parse_args(args)` | list[str] | nil | 解析给定参数列表 |
| `flag.args()` | — | list | 解析后剩余的位置参数 |
| `flag.usage()` | — | str | 所有标志的帮助文本 |

### Ref 对象

| 字段/方法 | 描述 |
|---|---|
| `ref.value` | 解析后的值（parse 前为 default）|
| `ref.is_set()` | bool，是否被命令行显式设置 |

---

## 支持的格式

```
--name value
--name=value
-name value       (单字母标志)
--bool_flag       (不需要值，设为 true)
--no-bool_flag    (设为 false)
--                (停止标志解析，剩余为位置参数)
```

---

## 依赖

- `os` 模块（STDLIB-02 ✅，`os.argv()`/`os.exit()`）
- `strconv` 模块（STDLIB-14，整数/浮点解析）

---

## 示例

```ms
import "flag"

var host = flag.string("host", "localhost", "服务器地址")
var port = flag.int("port", 8080, "监听端口")
var verbose = flag.bool("verbose", false, "详细输出")
flag.parse()

print(host.value)    // 命令行 --host=127.0.0.1 时输出 127.0.0.1
print(port.value)    // 默认 8080
print(flag.args())   // 剩余位置参数列表
```

---

## 测试

```
tests/unit/test_stdlib_flag.c
tests/fixtures/stdlib_flag_basic.ms
```
