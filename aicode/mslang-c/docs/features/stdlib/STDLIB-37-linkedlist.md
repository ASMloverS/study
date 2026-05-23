# STDLIB-37: linkedlist 模块

## 职责

双向链表：O(1) 首/尾插入删除，支持节点引用插入和删除中间节点。
对应 Go 的 `container/list` 包。`.ms` 实现。

> **命名说明**：使用 `linkedlist` 而非 `list`，避免与内置 `list` 类型产生命名混淆。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/linkedlist.ms`）。

---

## 类型设计

```ms
class Node {
    init(value) { this.value = value; this.next = nil; this.prev = nil }
}

class LinkedList {
    init() { this.head = nil; this.tail = nil; this._size = 0 }
}
```

---

## 函数与方法清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `linkedlist.new()` | — | LinkedList | 空链表 |
| `linkedlist.from_list(list)` | list | LinkedList | 从列表初始化 |

### 实例方法

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `l.push_front(v)` | any | Node | 头部插入，返回新节点 |
| `l.push_back(v)` | any | Node | 尾部插入 |
| `l.pop_front()` | — | any | 删除并返回头部值 |
| `l.pop_back()` | — | any | 删除并返回尾部值 |
| `l.peek_front()` | — | any | 查看头部值 |
| `l.peek_back()` | — | any | 查看尾部值 |
| `l.insert_after(node, v)` | Node, any | Node | 在 node 后插入 |
| `l.insert_before(node, v)` | Node, any | Node | 在 node 前插入 |
| `l.remove(node)` | Node | any | 删除指定节点，返回其值 |
| `l.size()` | — | int | 长度 |
| `l.is_empty()` | — | bool | 是否为空 |
| `l.each(fn)` | fn(v) | nil | 从头遍历 |
| `l.to_list()` | — | list | 转为 list |

---

## 依赖

- 语言 class 特性

---

## 示例

```ms
import "linkedlist"

var l = linkedlist.new()
var n1 = l.push_back(1)
var n2 = l.push_back(3)
l.insert_before(n2, 2)

l.each(fun(v){ print(v) })  // 1, 2, 3
l.remove(n1)
print(l.to_list())          // [2, 3]
```

---

## 测试

```
tests/unit/test_stdlib_linkedlist.c
```
