# STDLIB-40: url 模块

## 职责

URL 解析、构造、百分号编码/解码。对应 Go 的 `net/url` 包。

---

## C/.ms 分层

| 层 | 内容 |
|---|---|
| C 原语（`_strprim` 或 `_codec`）| 百分号编码/解码（性能+正确性）|
| .ms 组合层（`url`）| URL 对象解析/构造、query 参数处理 |

---

## 类型设计

```ms
class URL {
    // 字段：scheme host port path query fragment userinfo
}
```

---

## 函数清单

### 解析

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `url.parse(s)` | str | URL | 解析完整 URL，失败抛错 |
| `url.try_parse(s)` | str | URL\|nil | 解析失败返回 nil |

### URL 对象字段与方法

| 字段/方法 | 类型 | 描述 |
|---|---|---|
| `u.scheme` | str | 协议（`http`/`https`…）|
| `u.host` | str | 主机名（不含端口）|
| `u.port` | str | 端口字符串（无端口为 `""`）|
| `u.path` | str | 路径 |
| `u.raw_query` | str | 原始 query 字符串 |
| `u.fragment` | str | 锚点（`#` 后）|
| `u.userinfo` | str | `user:pass`（如有）|
| `u.query_params()` | → map | 解析 query 为 map |
| `u.to_str()` | → str | 重建 URL 字符串 |
| `u.join(ref)` | str → str | 基于当前 URL 解析相对引用 |

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `url.build(scheme, host, path, query=nil, fragment=nil)` | str... | str | 构造 URL 字符串 |
| `url.build_query(params)` | map | str | map → `k=v&k=v` |

### 编解码

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `url.encode(s)` | str | str | 百分号编码（所有非 unreserved 字符）|
| `url.encode_path(s)` | str | str | 路径编码（保留 `/`）|
| `url.encode_query(s)` | str | str | query 值编码（` `→`+`）|
| `url.decode(s)` | str | str | 百分号解码 |

---

## 依赖

- `strings` 模块（STDLIB-13）
- `_strprim` 或 `buffer` 模块（编码实现）

---

## 示例

```ms
import "url"

var u = url.parse("https://user:pass@example.com:8080/path?a=1&b=2#frag")
print(u.scheme)    // https
print(u.host)      // example.com
print(u.port)      // 8080
print(u.path)      // /path
print(u.query_params())  // {a:"1", b:"2"}
print(u.fragment)  // frag

print(url.encode("hello world!"))  // hello%20world%21
print(url.decode("hello%20world")) // hello world
```

---

## 测试

```
tests/unit/test_stdlib_url.c
tests/fixtures/stdlib_url_basic.ms
```
