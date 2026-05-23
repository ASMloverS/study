# STDLIB-44: hmac 模块

## 职责

HMAC（基于哈希的消息认证码，RFC 2104）：基于 `hash` 模块的现有算法生成 HMAC。
`.ms` 实现（算法简单，性能非关键）。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/hmac.ms`）。委托 `hash` 模块（STDLIB-06 ✅）。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `hmac.compute(algo, key, data)` | str, str\|buffer, str\|buffer | str | 一次性计算 HMAC 十六进制摘要 |
| `hmac.new(algo, key)` | str, str\|buffer | Hmac | 流式 HMAC 对象 |

> `algo`：`"md5"` / `"sha1"` / `"sha256"` — 与 `hash` 模块一致。

### Hmac 对象方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `h.update(data)` | str\|buffer | nil | 追加数据 |
| `h.hexdigest()` | — | str | 计算并返回十六进制 HMAC（可多次调用）|
| `h.digest()` | — | buffer | 返回原始字节 HMAC |

---

## HMAC 算法

```
HMAC(K, m) = H((K' ⊕ opad) || H((K' ⊕ ipad) || m))
K' = K（若 |K|≤块大小）或 H(K)（若 |K|>块大小）
```

`.ms` 中用 `hash` 的流式 API（`hash.update`/`hash.hexdigest`）实现，无额外 C 代码。

---

## 依赖

- `hash` 模块（STDLIB-06 ✅）
- `bytes`/`buffer` 模块（XOR 操作）

---

## 示例

```ms
import "hmac"

var key = "secret"
var msg = "Hello, World!"

print(hmac.compute("sha256", key, msg))
// 十六进制 HMAC-SHA256

var h = hmac.new("sha256", key)
h.update("Hello, ")
h.update("World!")
print(h.hexdigest())  // 与上同
```

---

## 测试

```
tests/unit/test_stdlib_hmac.c
tests/fixtures/stdlib_hmac_basic.ms
```

关键测试点：使用 RFC 2202/4231 中的标准测试向量验证 HMAC-MD5、HMAC-SHA1、HMAC-SHA256 结果。
