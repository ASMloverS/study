# 02 类型系统

mslang 动态类型：变量无类型约束，值有类型。`type(x)` 返回类型名字符串。

## 1. 类型总表

| 类型 | type() 名 | 字面量示例 | 可变 | hashable |
|------|-----------|------------|------|----------|
| nil | `nil` | `nil` | - | 是 |
| bool | `bool` | `true / false` | 否 | 是 |
| int | `int` | `42 / 0x1F / 'a'` | 否 | 是 |
| bigint | `bigint` | `123n` | 否 | 是 |
| float | `float` | `3.14` | 否 | 是 |
| string | `string` | `"a${b}"` | 否（不可变） | 是 |
| list | `list` | `[1, 2, 3]` | 是 | 否 |
| map | `map` | `{"a": 1}` | 是 | 否 |
| tuple | `tuple` | `(1, "a")` | 否 | 是（元素需 hashable） |
| set | `set` | `{1, 2, 3}` | 是 | 否 |
| function | `function` | `func(x){}` | 否 | 是 |
| class | `class` | — | 是（方法表） | 是 |
| instance | 类名 | — | 是（字段） | 默认否 |
| channel | `chan` | `make(chan)` | — | 否 |
| Future | `future` | async fn 调用结果 | - | 否 |
| iterator | `iterator` | `iter(xs)` | - | 否 |
| WeakRef | `weakref` | `WeakRef(o)` | - | 是 |

## 2. nil 与 bool

- `nil` 表示"无值"。未初始化的变量、缺失的 map 键（未用 `.get`）、函数无 return 的返回值均为 `nil`。
- 真值规则（Go/JS 立场，刻意区别于 Python）：**仅 `false` 与 `nil` 为假**；`0 / "" / [] / {}` 均为真。
- bool 与 int **不隐式互转**：`if 1 {}` 合法（1 为真），但 `1 == true` 为 `false`。

## 3. 数值

### 3.1 int（64 位有符号）

- 运算溢出时**自动提升为 bigint**，不报错：`9223372036854775807 + 1` → bigint。
- 位运算 `& | ^ << >> ~` 仅作用于 int / bigint，操作数为 float 报 TypeError。
- `int(x)` 支持 base：`int("ff", 16)`。

### 3.2 bigint（任意精度）

- 字面量后缀 `n`，或 int 溢出自动产生。
- 与 int 运算结果为 bigint；`5n / 2n` 截断除法得 `2n`。

### 3.3 float（IEEE 754 双精度）

- bigint 与 float 混算转 float（可能丢精度，不报错）。
- 整数溢出后不丢精度路径：需要精确时显式用 `n` 后缀。

### 3.4 混算规则表

| 左 \ 右 | int | bigint | float |
|---------|-----|--------|-------|
| int | int（溢出升 bigint） | bigint | float |
| bigint | bigint | bigint | float |
| float | float | float | float |

### 3.5 除法与取模

- `/`：int/bigint 截断除法（`-7 / 2 == -3`，Go 语义）；float 真除。
- `%`：符号随被除数（Go 语义）；`0` 除数 / 取模抛 ArithmeticError。

## 4. string

- **不可变**；内部 UTF-8 字节存储。
- `s[i]`：返回 **rune**（int 型码点），越界抛 IndexError。`s[-1]` 负索引。
- `len(s)`：返回**字节数**；字符数用 `s.charLen()`，字符迭代用 `s.chars()`。
- 切片 `s[a:b:c]`：按 **rune 索引**切片，步进可负；返回新 string。省略端点取全长。
- 拼接 `+`；重复 `"ab" * 3 == "ababab"`。
- 实现细节（intern、ASCII 快路径、惰性 rune 索引表）见 `22-object-model.md`。

## 5. list / map / tuple / set

### 5.1 list

有序可变动态数组，异构。`xs[i]` 读写、`xs[i] = v`；切片 `xs[a:b:c]` 返回新 list；`+` 拼接；`*` 重复。

### 5.2 map

键值对，键须 hashable（nil/bool/int/bigint/float/string/tuple；list/map/set/普通实例作键抛 TypeError）。
`m[k]` 缺键抛 KeyError；`m.get(k, default)` 不抛。插入序保留（遍历顺序 = 插入顺序）。

### 5.3 tuple

不可变序列，元素 hashable 时 tuple 整体 hashable。单元素 tuple 必须带逗号：`(1,)`。`()` 空 tuple。括号可省略的场合：解构赋值、return 多值；字面量场合必须带括号。

### 5.4 set / frozenset

无序去重集合（set 可变，frozenset 不可变且 hashable）。`set` 字面量 `{1, 2}` 与空 map 字面量 `{}` 冲突：**`{}` 为空 map**，空 set 用 `set()`。运算：`| & - ^`（并交差对称差）。

## 6. 相等与同一

- `==` 深值相等：int/bigint 数值比较（`1 == 1n` 为 true）；float 与 int 数值比较；string 字节比较；list/tuple/set 逐元素；map 逐键值；instance 默认引用比较，定义 `__eq__` 后覆盖。
- `!=` 恒为 `==` 取反。
- `is`：同一性（同一对象）。`x is nil` 是判空推荐写法。
- `1 == 1.0` 为 true（数值塔内比较）；`"1" == 1` 为 false（无跨类型字符串转换）。
- NaN：`NaN == NaN` 为 false（IEEE 语义），`is` 为 true。

## 7. hash 一致性

- 自定义 `__eq__` 的类若需入 map/set，必须同时定义 `__hash__`；只定义 `__eq__` 则实例不可 hash（TypeError），明确防坑。
- 数值 hash：`hash(1) == hash(1.0) == hash(1n)`（数值塔统一）。

## 8. 隐式转换

**无隐式字符串转换**：`"a" + 1` 抛 TypeError。显式转换：`str(x)` / `repr(x)` / `int(x)` / `float(x)` / `bool(x)`。

## 9. 类型查询

- `type(x)` → 类型名字符串（instance 返回类名）。
- `isinstance(x, T)` → bool；T 可为 class 或 class 的 tuple/list。
- dunder 协议（`__len__` 等）见 `06-classes.md`。
