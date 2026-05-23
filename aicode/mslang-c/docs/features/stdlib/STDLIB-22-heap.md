# STDLIB-22: heap 模块

## 职责

优先队列（堆）：最小堆（默认）或自定义比较器的堆。
对应 Go 的 `container/heap`，但提供面向对象接口而非接口实现模式。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/heap.ms`）。底层用 list 存储。

---

## 类型设计

```ms
class Heap {
    // cmp(a, b) → int：<0 表示 a 优先级更高（最小堆时 a<b）
    init(cmp=nil) {
        this._data = []
        this._cmp  = cmp == nil ? fun(a,b){ return a < b ? -1 : (a>b?1:0) } : cmp
    }
}
```

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `heap.new(cmp=nil)` | fn? | Heap | 创建空堆（nil=最小堆）|
| `heap.heapify(list, cmp=nil)` | list, fn? | Heap | 从已有列表建堆（O(n)）|
| `heap.nsmallest(n, list)` | int, list | list | 列表中最小的 n 个元素 |
| `heap.nlargest(n, list)` | int, list | list | 列表中最大的 n 个元素 |

### 实例方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `h.push(value)` | any | nil | 插入元素（O(log n)）|
| `h.pop()` | — | any | 弹出并返回优先级最高的元素（O(log n)）|
| `h.peek()` | — | any | 查看顶部元素不弹出（O(1)）|
| `h.push_pop(value)` | any | any | 先 push 再 pop（效率更高）|
| `h.size()` | — | int | 元素数量 |
| `h.is_empty()` | — | bool | 是否为空 |
| `h.to_list()` | — | list | 复制为普通列表（堆序）|
| `h.to_sorted_list()` | — | list | 按优先级顺序弹出（消耗性）|

---

## 依赖

- list 内置（push/len/索引）

---

## 示例

```ms
import "heap"

// 最小堆
var h = heap.new()
h.push(5)
h.push(1)
h.push(3)
print(h.pop())   // 1
print(h.pop())   // 3

// 最大堆（反转比较器）
var maxh = heap.new(fun(a,b){ return b < a ? -1 : (b>a?1:0) })
maxh.push(5)
maxh.push(1)
maxh.push(9)
print(maxh.pop())  // 9

// 按字段排序的对象堆
var pq = heap.new(fun(a,b){ return a["pri"] < b["pri"] ? -1 : 1 })
pq.push({"task":"low","pri":10})
pq.push({"task":"high","pri":1})
print(pq.pop()["task"])  // high

print(heap.nsmallest(3, [5,2,8,1,9,3]))  // [1,2,3]
```

---

## 测试

```
tests/unit/test_stdlib_heap.c
tests/fixtures/stdlib_heap_basic.ms
```
