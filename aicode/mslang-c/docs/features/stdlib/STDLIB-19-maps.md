# STDLIB-19: maps 模块

## 职责

map 工具函数：键/值提取、浅复制、合并、过滤变换、键值对互转。
全部 `.ms` 实现，对应 Go 的 `maps` 包。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/maps.ms`）。

---

## 函数清单

### 提取

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `maps.keys(m)` | map | list | 所有键（顺序不保证）|
| `maps.values(m)` | map | list | 所有值（与 keys 同序）|
| `maps.items(m)` | map | list | `[[k,v], ...]` 键值对列表 |
| `maps.count(m)` | map | int | 键的数量（`len(m)` 别名）|

### 复制与合并

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `maps.clone(m)` | map | map | 浅复制 |
| `maps.merge(m1, m2, ...)` | map... | map | 合并（后者键覆盖前者）|
| `maps.update(m, key, fn)` | map, any, fn(v)→v | nil | `m[key] = fn(m[key])` 原地更新 |

### 查询

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `maps.get(m, key, default=nil)` | map, any, any | any | 键存在返回值，否则 default |
| `maps.has(m, key)` | map, any | bool | 是否含该键（`m.has(key)` 封装）|
| `maps.pop(m, key, default=nil)` | map, any, any | any | 取出并删除键，不存在返回 default |

### 变换与过滤

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `maps.filter(m, fn)` | map, fn(k,v)→bool | map | 保留满足条件的键值对 |
| `maps.map_values(m, fn)` | map, fn(k,v)→v | map | 变换每个值，保留键 |
| `maps.map_keys(m, fn)` | map, fn(k)→k | map | 变换每个键（值不变，键冲突取最后一个）|
| `maps.invert(m)` | map | map | 键值互换（值需可哈希）|
| `maps.any(m, fn)` | map, fn(k,v)→bool | bool | 存在满足条件的键值对 |
| `maps.all(m, fn)` | map, fn(k,v)→bool | bool | 全部键值对满足条件 |

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `maps.from_pairs(pairs)` | list[[k,v]] | map | 键值对列表 → map |
| `maps.from_keys(keys, default=nil)` | list, any | map | 以 keys 为键，统一赋 default 值 |
| `maps.group_by(list, fn)` | list, fn(v)→key | map | 按 key 函数将 list 分组为 map[key→list] |
| `maps.zip(keys, values)` | list, list | map | 两个列表 zip 为 map |

---

## 依赖

- map 内置方法（`m[key]` 取值/赋值、`m.has(key)`、`m.remove(key)`、`m.keys()`、`m.values()`、`len(m)`、`for k in m` 迭代）

---

## 示例

```ms
import "maps"

var m = {"a": 1, "b": 2, "c": 3}

print(maps.keys(m))              // ["a", "b", "c"]
print(maps.filter(m, fun(k,v){ return v > 1 }))  // {b:2, c:3}
print(maps.map_values(m, fun(k,v){ return v * 10 })) // {a:10, b:20, c:30}
print(maps.invert(m))            // {1:"a", 2:"b", 3:"c"}

var m2 = maps.merge(m, {"c": 99, "d": 4})
print(m2)  // {a:1, b:2, c:99, d:4}

var words = ["apple", "banana", "avocado", "blueberry"]
var grouped = maps.group_by(words, fun(w){ return w[0] })
print(grouped)  // {a:["apple","avocado"], b:["banana","blueberry"]}
```

---

## 测试

```
tests/unit/test_stdlib_maps.c
tests/fixtures/stdlib_maps_basic.ms
```
