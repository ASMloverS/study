# STDLIB-35: cmp 模块

## 职责

比较原语与比较函数组合器：标准三路比较、比较器的反转/链式/投影。
对应 Go 的 `cmp` 包。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/cmp.ms`）。

---

## 函数清单

### 基础比较

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `cmp.compare(a, b)` | any, any | int | -1/0/1（三路比较）|
| `cmp.less(a, b)` | any, any | bool | `a < b` |
| `cmp.greater(a, b)` | any, any | bool | `a > b` |
| `cmp.equal(a, b)` | any, any | bool | `a == b` |
| `cmp.min(a, b)` | any, any | any | 较小值 |
| `cmp.max(a, b)` | any, any | any | 较大值 |
| `cmp.clamp(value, lo, hi)` | any,any,any | any | 钳制到 [lo, hi] |

### 比较器组合（返回 `fn(a,b)→int` 形式的比较函数）

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `cmp.natural()` | — | cmp_fn | 默认自然序（`compare` 本身）|
| `cmp.reversed(fn)` | cmp_fn | cmp_fn | 反转比较器 |
| `cmp.by_key(key_fn)` | fn(v)→k | cmp_fn | 按 key_fn 提取键后比较 |
| `cmp.chain(fn1, fn2, ...)` | cmp_fn... | cmp_fn | 多键字典序（前者相等时用后者）|
| `cmp.null_last(fn)` | cmp_fn | cmp_fn | nil 排到末尾 |
| `cmp.null_first(fn)` | cmp_fn | cmp_fn | nil 排到开头 |

---

## 依赖

- 无（仅语言内置操作符）

---

## 示例

```ms
import "cmp"
import "sort"

var data = [{"name":"Bob","age":30}, {"name":"Alice","age":25}, {"name":"Carol","age":30}]

// 先按 age 升序，age 相等时按 name 升序
var by_age_then_name = cmp.chain(
    cmp.by_key(fun(p){ return p["age"] }),
    cmp.by_key(fun(p){ return p["name"] })
)
sort.sort(data, cmp=by_age_then_name)
// Alice(25), Bob(30), Carol(30)

print(cmp.clamp(15, 0, 10))  // 10
```

---

## 测试

```
tests/unit/test_stdlib_cmp.c
tests/fixtures/stdlib_cmp_basic.ms
```
