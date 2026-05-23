# STDLIB-38: ring 模块

## 职责

固定容量循环缓冲区（ring buffer）：FIFO 语义，满时自动覆盖最旧元素。
对应 Go 的 `container/ring` 包（简化为固定容量模型）。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/ring.ms`）。内部用 list 实现循环数组。

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `ring.new(capacity)` | int | Ring | 创建容量为 capacity 的循环缓冲 |

### 实例方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `r.push(v)` | any | any\|nil | 入队；满时弹出并返回被覆盖的元素，否则返回 nil |
| `r.pop()` | — | any | 出队最旧元素（空时返回 nil）|
| `r.peek()` | — | any | 查看最旧元素 |
| `r.size()` | — | int | 当前元素数量 |
| `r.capacity()` | — | int | 最大容量 |
| `r.is_full()` | — | bool | 是否已满 |
| `r.is_empty()` | — | bool | 是否为空 |
| `r.to_list()` | — | list | 按入队顺序返回（最旧→最新）|
| `r.clear()` | — | nil | 清空 |

---

## 依赖

- list 内置

---

## 示例

```ms
import "ring"

var r = ring.new(3)
r.push(1); r.push(2); r.push(3)
print(r.is_full())    // true

var evicted = r.push(4)  // 容量满，覆盖最旧
print(evicted)           // 1
print(r.to_list())       // [2, 3, 4]
```

---

## 测试

```
tests/unit/test_stdlib_ring.c
```
