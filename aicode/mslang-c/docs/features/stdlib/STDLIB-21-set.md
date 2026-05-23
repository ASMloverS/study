# STDLIB-21: set 模块

## 职责

集合数据结构：基于 map（用 key 存在性表示成员）。
支持常规集合运算（并/交/差/子集）。Go 无原生 set，这是 mslang 扩展。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/set.ms`）。内部用 `map` 存储，key=元素值，value=`true`。

---

## 类型设计

```ms
class Set {
    init(iterable=nil) {
        this._data = {}
        if iterable != nil {
            for v in iterable { this._data[v] = true }
        }
    }
}
```

> ObjMap 使用 `MsValueTable`（Value 键），int/num/str/tuple 等可哈希值均可直接作键，无需 `str()` 转换。

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `set.new(iterable=nil)` | list? | Set | 创建集合（可选初始元素）|
| `set.from_list(list)` | list | Set | 同 `set.new(list)` |

### 实例方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `s.add(value)` | any | nil | 添加元素 |
| `s.remove(value)` | any | nil | 删除元素（不存在则静默）|
| `s.discard(value)` | any | nil | `remove` 别名 |
| `s.contains(value)` | any | bool | 是否含该元素 |
| `s.size()` | — | int | 元素数量 |
| `s.is_empty()` | — | bool | 是否为空 |
| `s.to_list()` | — | list | 转为列表（顺序不保证）|
| `s.clear()` | — | nil | 清空 |

### 集合运算（返回新 Set）

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `s.union(other)` | Set | Set | 并集 |
| `s.intersection(other)` | Set | Set | 交集 |
| `s.difference(other)` | Set | Set | 差集（s 有但 other 没有）|
| `s.symmetric_difference(other)` | Set | Set | 对称差（只在其中一个中）|
| `s.copy()` | — | Set | 浅复制 |

### 关系判断

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `s.is_subset(other)` | Set | bool | s ⊆ other |
| `s.is_superset(other)` | Set | bool | s ⊇ other |
| `s.is_disjoint(other)` | Set | bool | 交集为空 |
| `s.equals(other)` | Set | bool | 元素相同 |

---

## 依赖

- map 内置（`m[key]` 赋值、`m.has(key)`、`m.remove(key)`、`len(m)`、`for k in m` 迭代）

---

## 示例

```ms
import "set"

var s1 = set.new([1, 2, 3, 4])
var s2 = set.new([3, 4, 5, 6])

print(s1.contains(3))         // true
s1.remove(4)
print(s1.size())               // 3

print(s1.union(s2).to_list())          // [1,2,3,5,6] (order varies)
print(s1.intersection(s2).to_list())   // [3]
print(s1.difference(s2).to_list())     // [1,2]
print(s1.is_subset(set.new([1,2,3,99])))  // true
```

---

## 测试

```
tests/unit/test_stdlib_set.c
tests/fixtures/stdlib_set_basic.ms
```
