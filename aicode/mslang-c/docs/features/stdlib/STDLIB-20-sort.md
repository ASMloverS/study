# STDLIB-20: sort 模块

## 职责

list 排序原语与辅助：原地排序、生成排序副本、稳定排序、有序搜索。
对应 Go 的 `sort` 包。

---

## C/.ms 分层

| 层 | 内容 | 文件 |
|---|---|---|
| C 原语（`_sort`）| 带比较回调的 list 原地排序（Timsort 或 introsort）| `src/stdlib/_sort.c` |
| .ms 组合层（`sort`）| key 函数适配、排序副本、stable/is_sorted/二分搜索 | `stdlib/ms/sort.ms` |

---

## 函数清单

### 排序

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `sort.sort(list, key=nil, reverse=false, cmp=nil)` | list, fn?, bool, fn? | nil | 原地不稳定排序 |
| `sort.stable_sort(list, key=nil, reverse=false, cmp=nil)` | list, fn?, bool, fn? | nil | 原地稳定排序（相等元素保持原序）|
| `sort.sorted(list, key=nil, reverse=false, cmp=nil)` | list, fn?, bool, fn? | list | 返回排序副本（不修改原列表）|

> `key`：`fn(element) → comparable_value`，nil 表示直接比较元素。
> `cmp`：`fn(a, b) → int`（-1/0/1），与 `cmp` 模块的比较器组合器兼容；`cmp` 非 nil 时忽略 `key`。

### 查询

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `sort.is_sorted(list, key=nil)` | list, fn? | bool | 是否已升序排列 |
| `sort.is_sorted_desc(list, key=nil)` | list, fn? | bool | 是否已降序排列 |

### 二分搜索（仅适用于已排序列表）

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `sort.binary_search(list, value, key=nil)` | list, any, fn? | int | 返回索引，未找到返回 -1（注：异于 Go `sort.Search` 返回插入点）|
| `sort.bisect_left(list, value, key=nil)` | list, any, fn? | int | 插入点（左侧），对应 Go `sort.SearchInts`|
| `sort.bisect_right(list, value, key=nil)` | list, any, fn? | int | 插入点（右侧）|
| `sort.search_range(list, value, key=nil)` | list, any, fn? | [lo, hi] | 等于 value 的索引范围 [lo, hi) |

---

## C 原语 `_sort` 接口

```ms
// _sort 导出：
// sort_inplace(list, cmp_fn)  — cmp_fn(a,b)→int (-1/0/1)
// sort_stable(list, cmp_fn)
```

`.ms` 层把 `key` 函数转换为比较函数：

```ms
fun _make_cmp(key, reverse) {
    return fun(a, b) {
        var ka = key == nil ? a : key(a)
        var kb = key == nil ? b : key(b)
        var r = ka < kb ? -1 : (ka > kb ? 1 : 0)
        return reverse ? -r : r
    }
}
```

---

## 依赖

- `_sort`（C 原语）
- list 内置方法（len/索引）

---

## 示例

```ms
import "sort"

var nums = [3, 1, 4, 1, 5, 9, 2, 6]
sort.sort(nums)
print(nums)  // [1, 1, 2, 3, 4, 5, 6, 9]

var people = [{"name":"Alice","age":30}, {"name":"Bob","age":25}]
var by_age = sort.sorted(people, fun(p){ return p["age"] })
print(by_age[0]["name"])  // Bob

print(sort.binary_search([1,2,3,4,5], 3))  // 2
print(sort.bisect_left([1,3,3,5], 3))       // 1
print(sort.bisect_right([1,3,3,5], 3))      // 3
```

---

## 测试

```
tests/unit/test_stdlib_sort.c
tests/fixtures/stdlib_sort_basic.ms
```

关键测试点：
- 空列表、单元素
- 含重复元素的稳定性验证
- key 函数（按字符串长度、按对象字段）
- reverse=true
- 二分搜索边界情况
