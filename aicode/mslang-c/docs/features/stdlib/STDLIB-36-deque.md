# STDLIB-36: deque 模块

## 职责

双端队列（deque）：O(1) 两端入队/出队，O(1) 随机访问（基于循环数组）。
mslang 扩展，Go 无对应标准包。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/deque.ms`）。底层用 list 模拟循环缓冲；高频操作用 push_front O(n) 近似（如需 O(1) push_front，v2 改为 C 实现）。

---

## 类型设计

```ms
class Deque {
    init(capacity=8) {
        this._data = []
        this._head = 0
        this._size = 0
    }
}
```

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `deque.new()` | — | Deque | 空双端队列 |
| `deque.from_list(list)` | list | Deque | 从列表初始化 |

### 实例方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `d.push_back(v)` | any | nil | 队尾入队 |
| `d.push_front(v)` | any | nil | 队头入队 |
| `d.pop_back()` | — | any | 队尾出队（队空抛错）|
| `d.pop_front()` | — | any | 队头出队 |
| `d.peek_back()` | — | any | 查看队尾（不出队）|
| `d.peek_front()` | — | any | 查看队头 |
| `d.get(i)` | int | any | 按索引访问（0=队头）|
| `d.size()` | — | int | 元素数量 |
| `d.is_empty()` | — | bool | 是否为空 |
| `d.clear()` | — | nil | 清空 |
| `d.to_list()` | — | list | 从队头到队尾的列表 |

---

## 依赖

- list 内置

---

## 示例

```ms
import "deque"

var d = deque.new()
d.push_back(1)
d.push_back(2)
d.push_front(0)

print(d.to_list())    // [0, 1, 2]
print(d.pop_front())  // 0
print(d.pop_back())   // 2
print(d.size())       // 1
```

---

## 测试

```
tests/unit/test_stdlib_deque.c
tests/fixtures/stdlib_deque_basic.ms
```
