# STDLIB-18: slices 模块

## 职责

list 工具函数集合：函数式操作（map/filter/reduce）、查找、变换、统计。
全部 `.ms` 实现，组合内置 list 方法。对应 Go 的 `slices` 包。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/slices.ms`）。
`sorted` 委托 `sort` 模块（STDLIB-20）。

---

## 函数清单

### 函数式操作

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `slices.map(list, fn)` | list, fn(v)→v | list | 映射每个元素 |
| `slices.filter(list, fn)` | list, fn(v)→bool | list | 保留满足条件的元素 |
| `slices.flat_map(list, fn)` | list, fn(v)→list | list | map 后展平一层 |
| `slices.reduce(list, fn, init)` | list, fn(acc,v)→acc, any | any | 折叠 |
| `slices.any(list, fn)` | list, fn(v)→bool | bool | 存在满足条件的元素 |
| `slices.all(list, fn)` | list, fn(v)→bool | bool | 全部满足条件 |
| `slices.count(list, fn)` | list, fn(v)→bool | int | 满足条件的数量 |
| `slices.group_by(list, fn)` | list, fn(v)→key | map | 按 key 分组 |
| `slices.partition(list, fn)` | list, fn(v)→bool | [list, list] | 分为满足/不满足两组 |

### 查找与索引

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `slices.find(list, fn)` | list, fn(v)→bool | any\|nil | 第一个满足条件的元素 |
| `slices.find_index(list, fn)` | list, fn(v)→bool | int | 第一个满足条件的索引（-1 未找到）|
| `slices.index_of(list, value)` | list, any | int | 第一个相等元素的索引（-1 未找到）|
| `slices.contains(list, value)` | list, any | bool | 是否含该值（`==` 比较）|
| `slices.last_index_of(list, value)` | list, any | int | 最后一个相等元素的索引 |

### 变换

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `slices.reverse(list)` | list | list | 返回翻转副本（不修改原列表）|
| `slices.reverse_in_place(list)` | list | nil | 原地翻转 |
| `slices.flatten(list)` | list | list | 展平一层嵌套 |
| `slices.unique(list)` | list | list | 去重（保留首次出现顺序）|
| `slices.chunk(list, n)` | list, int | list | 切成每组 n 个的子列表 |
| `slices.zip(list1, list2, ...)` | list... | list | 对应元素配对为元组 |
| `slices.enumerate(list)` | list | list | `[[0,v0],[1,v1],...]` |
| `slices.concat(list1, list2, ...)` | list... | list | 拼接多个列表 |
| `slices.take(list, n)` | list, int | list | 前 n 个元素 |
| `slices.drop(list, n)` | list, int | list | 跳过前 n 个元素 |
| `slices.take_while(list, fn)` | list, fn | list | 取前缀直到条件不满足 |
| `slices.drop_while(list, fn)` | list, fn | list | 跳过前缀直到条件不满足 |

### 统计与排序

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `slices.max(list, key=nil)` | list, fn? | any | 最大值（key 函数可选）|
| `slices.min(list, key=nil)` | list, fn? | any | 最小值 |
| `slices.sum(list)` | list[num] | num | 求和 |
| `slices.sorted(list, key=nil, reverse=false)` | list, fn?, bool | list | 排序副本（委托 sort 模块）|
| `slices.is_sorted(list, key=nil)` | list, fn? | bool | 是否已排序 |

---

## 依赖

- list 内置方法（push/len/迭代）
- `sort` 模块（STDLIB-20，仅 `sorted`/`is_sorted` 用到）

---

## 示例

```ms
import "slices"

var nums = [3, 1, 4, 1, 5, 9, 2, 6]

print(slices.filter(nums, fun(x){ return x > 3 }))   // [4, 5, 9, 6]
print(slices.map(nums, fun(x){ return x * x }))       // [9, 1, 16, ...]
print(slices.reduce(nums, fun(a,x){ return a+x }, 0)) // 31
print(slices.unique(nums))    // [3, 1, 4, 5, 9, 2, 6]
print(slices.chunk(nums, 3))  // [[3,1,4],[1,5,9],[2,6]]

var people = [{"name":"Alice","age":30}, {"name":"Bob","age":25}]
print(slices.sorted(people, fun(p){ return p["age"] }))
// [{name:Bob,age:25}, {name:Alice,age:30}]
```

---

## 测试

```
tests/unit/test_stdlib_slices.c
tests/fixtures/stdlib_slices_basic.ms
```
