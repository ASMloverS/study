# STDLIB-17: errors 模块

## 职责

结构化错误值：创建、包装、展开、链式检查。在 mslang 的异常模型上叠加
「可检查错误类型链」语义（对应 Go 的 `errors.Is`/`errors.As`/`errors.Unwrap`）。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/errors.ms`）。
在 .ms 中定义 `Error` 基类，通过语言类继承机制实现链式检查。

---

## 类型设计

```ms
// 内置于 errors.ms

class Error {
    init(msg, cause=nil) {
        this.message = msg
        this.cause   = cause   // nil 或另一个 Error
    }
    fun to_str() { return "Error: " + this.message }
}
```

`errors.new` 返回 `Error` 实例；
`errors.wrap` 返回以 `cause` 字段链接的新 `Error`；
`errors.is` 沿 cause 链向上遍历，用引用相等（`==`）与 target 比较。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `errors.new(msg)` | str | Error | 创建新错误 |
| `errors.wrap(cause, msg)` | Error, str | Error | 包装已有错误，保留 cause 链 |
| `errors.unwrap(err)` | Error | Error\|nil | 返回 `err.cause`（nil 若无）|
| `errors.message(err)` | Error | str | 返回 `err.message` |
| `errors.is(err, target)` | Error, Error | bool | 沿 cause 链查找与 target 相同的错误 |
| `errors.chain(err)` | Error | list | 展开整条 cause 链为列表 |
| `errors.format(err)` | Error | str | 拼接完整错误链描述 |

---

## 依赖

- 语言特性：class / 异常 try/catch

---

## 示例

```ms
import "errors"

var ErrNotFound = errors.new("not found")
var ErrPermission = errors.new("permission denied")

fun read_file(path) {
    if path == "" {
        throw errors.wrap(ErrNotFound, "read_file: empty path")
    }
}

try {
    read_file("")
} catch (e) {
    print(errors.message(e))          // read_file: empty path
    print(errors.is(e, ErrNotFound))  // true
    print(errors.format(e))           // read_file: empty path: not found

    var chain = errors.chain(e)
    print(len(chain))  // 2
}
```

---

## 测试

```
tests/unit/test_stdlib_errors.c
tests/fixtures/stdlib_errors_basic.ms
```
