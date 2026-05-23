# STDLIB-16: json 模块

## 职责

JSON（RFC 7159）的编码与解码。纯 C 实现，保证正确性与性能；不依赖外部库。

---

## C/.ms 分层

全部 C（`src/stdlib/json.c`）。无 .ms 包装层。

---

## 值类型映射

| mslang 类型 | JSON 类型 |
|---|---|
| `nil` | `null` |
| `bool` | `true` / `false` |
| `int` | number（整数）|
| `num` | number（浮点，不产生 `NaN`/`Infinity` — 抛错）|
| `str` | string（UTF-8，含转义）|
| `list` | array |
| `map` | object（键必须为 str）|
| 其他（函数、类…）| 编码时抛错 |

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `json.encode(value)` | any | str | 序列化为 JSON 单行字符串 |
| `json.encode_pretty(value, indent=2)` | any, int | str | 缩进格式化输出 |
| `json.decode(s)` | str | any | 解析 JSON 字符串，失败抛运行时错误 |
| `json.try_decode(s)` | str | any\|nil | 解析失败返回 nil |
| `json.is_valid(s)` | str | bool | 不抛错，仅验证格式 |

---

## 错误行为

- `json.encode`：value 含不可序列化类型（函数/协程等）→ 运行时错误。
- `json.encode`：num 值为 `NaN` 或 `Infinity` → 运行时错误（JSON 规范不允许）。
- `json.decode`：JSON 格式错误 → 运行时错误，错误信息含行号/列号。
- map 中的非字符串键 → 编码时抛错。

---

## 实现要点（`src/stdlib/json.c`）

- 解析器：手写递归下降，无外部依赖。
- 字符串解码：处理 `\uXXXX` 转义（含代理对 `𐀀`）。
- 数值：整数优先——无小数点且值在 int64 范围内 → `MS_INT_VAL`（int）；含小数点或指数 → `strtod` → `MS_NUMBER_VAL`（num）。`1.0` 解码为 num，`1` 解码为 int，encode→decode 类型一致。
- object 重复键：取**最后一个**（符合大多数 JSON 实现惯例，RFC 7159 未规定）。
- 序列化：循环引用检测（维护一个 set 的轻量版本，检测 list/map 循环）。
- `encode_pretty`：BFS 逐层缩进，使用 `MsObjStringBuilder`（`object.h:343`）。

---

## 依赖

- CAPI-01/02（注册表）
- `MsObjStringBuilder`（序列化输出缓冲）
- `<stdlib.h>` `<string.h>`（`strtod`、内存）

---

## 示例

```ms
import "json"

var obj = {"name": "Alice", "age": 30, "scores": [95, 87, 92]}
var s = json.encode(obj)
print(s)  // {"age":30,"name":"Alice","scores":[95,87,92]}

var pretty = json.encode_pretty(obj, 4)
print(pretty)
// {
//     "age": 30,
//     "name": "Alice",
//     "scores": [
//         95,
//         87,
//         92
//     ]
// }

var back = json.decode(s)
print(back["name"])  // Alice
print(json.try_decode("bad json"))  // nil
```

---

## 测试

```
tests/unit/test_stdlib_json.c
tests/fixtures/stdlib_json_basic.ms
```

关键测试点：
- nil/bool/int/num/str/list/map 往返（encode→decode 等值）
- 嵌套结构
- `\uXXXX` 转义
- 大整数（int64 边界）
- 格式错误输入
- NaN/Infinity 编码报错
- 循环引用检测
- `1` 解码为 int，`1.0` 解码为 num（类型往返一致）
- object 重复键取最后一个
