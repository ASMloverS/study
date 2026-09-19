# 10 内置（Tier 0）

无需 import 即可用。命名 lowerCamelCase（ms 编码规范）。

## 1. 内置函数

| 函数 | 签名 | 说明 |
|------|------|------|
| print | `print(...xs)` | 空格连接输出 + 换行（Python 语义） |
| len | `len(x)` | string 字节数 / list,tuple,map,set,chan 元素数 / `__len__` |
| cap | `cap(ch)` | channel 容量（v1 唯一用途） |
| range | `range([start,] stop[, step])` | 惰性 int 迭代器 |
| type | `type(x)` | 类型名字符串（instance 为类名） |
| repr | `repr(x)` | 调试形态字符串（`__repr__`） |
| str | `str(x)` | 人读形态字符串（`__str__` 回落 `__repr__`） |
| int | `int(x[, base])` | 转 int：float 截断、字符串按 base 解析（失败抛 ValueError）、rune 原值 |
| bigint | `bigint(x)` | 转 bigint（int/float(须整)/string） |
| float | `float(x)` | 转 float（string 解析失败抛 ValueError） |
| bool | `bool(x)` | 真值规则：仅 nil/false 为 false |
| hash | `hash(x)` | int 哈希；数值塔统一（`hash(1)==hash(1.0)`） |
| abs | `abs(x)` | 绝对值（int/bigint/float 保持类型） |
| min / max | `min(...xs)` | 数值或同型可比较值；单容器参数取容器内最值 |
| sum | `sum(xs[, init])` | 求和（数值；init 改类型如 sum(xs, "")） |
| sorted | `sorted(xs[, key, reverse])` | 返回新排序 list（timsort，稳定） |
| reversed | `reversed(xs)` | 返回反转迭代器 |
| enumerate | `enumerate(xs[, start])` | `(i, v)` 迭代器 |
| zip | `zip(xs, ys[, strict])` | 短板对齐（strict=true 长度不等抛 ValueError） |
| iter | `iter(x)` | 取迭代器（`__iter__`；容器默认返回自身游标迭代器） |
| next | `next(it)` | 推进；耗尽返回 nil |
| isinstance | `isinstance(x, T)` | T 为 class 或 class 集合，沿 MRO |
| callable | `callable(x)` | function / class / `__call__` 实例 |
| make | `make(chan[, capacity])` | 创建 channel（唯一 make 用途 v1） |
| id | `id(x)` | 对象唯一 int 标识（活跃期唯一） |
| input | `input([prompt])` | 读一行（EOF 返回 nil） |
| assert | `assert(cond[, msg])` | 假则抛 AssertionError（Error 子类） |
| all | `all(xs)` / `all(futures)` | 迭代器全真 / 并发等待全部（重载，见 08） |
| race | `race(futures)` | 首个完成的 Future |
| WeakRef | `WeakRef(obj)` | 弱引用构造：`.get()` 返回对象或 nil |

## 2. string 方法

| 方法 | 说明 |
|------|------|
| `s.charLen()` | 字符（rune）数 |
| `s.chars()` | rune 迭代器 |
| `s.bytes()` | 逐字节 int 迭代器 |
| `s.toUpper() / s.toLower()` | 大小写转换（Unicode 感知） |
| `s.split(sep[, n])` | 切分（sep 为空按空白切）；`s.splitLines()` |
| `s.join(xs)` | 以 s 连接字符串序列 |
| `s.contains(sub)` / `s.startsWith(sub)` / `s.endsWith(sub)` | 包含判断 |
| `s.indexOf(sub[, from])` / `s.lastIndexOf(sub)` | rune 索引，未找到 -1 |
| `s.replace(old, new[, n])` | 替换 |
| `s.trim()` / `s.trimLeft()` / `s.trimRight()` | 去空白（或给定字符集） |
| `s.repeat(n)` | 重复 |
| `s.count(sub)` | 计数 |
| `s.padLeft(w[, ch])` / `s.padRight(w[, ch])` | 对齐填充 |
| `s.slice(a[, b[, c]])` | 等价 `s[a:b:c]` |
| `s.isDigit() / s.isSpace() / s.isAlpha()` | 逐字符判定（全真才真） |
| `s.compareTo(t)` | 字典序 int（供排序） |

## 3. list 方法

| 方法 | 说明 |
|------|------|
| `xs.append(x)` / `xs.pop([i])` | 尾加 / 弹出（默认尾） |
| `xs.insert(i, x)` / `xs.remove(x)` | 插入 / 按值删首个（缺失抛 ValueError） |
| `xs.indexOf(x)` / `xs.contains(x)` | 查找 |
| `xs.sort([cmp])` / `xs.reverse()` | 原地排序（cmp(a,b)→int，缺省 `<` 序）/ 反转 |
| `xs.concat(ys)` / `xs.repeat(n)` | 新 list |
| `xs.map(f)` / `xs.filter(f)` / `xs.reduce(f[, init])` | 变换（返回新 list） |
| `xs.each(f)` | 遍历（f(v, i)） |
| `xs.clear()` / `xs.copy()` | 清空 / 浅拷贝 |
| `xs.first() / xs.last()` | 首尾（空抛 IndexError） |

## 4. map / set / tuple 方法

map：`m.get(k[, default])`、`m.has(k)`、`m.remove(k)`、`m.clear()`、`m.keys()` / `m.values()` / `m.items()`（均返回快照 list）、`m.update(other)`、`m.copy()`、`m.len()`（同 len）。
set：`st.add(x)`、`st.remove(x)`、`st.has(x)`、`st.union(t)` 等价 `st \| t`、`st.intersect(t)`、`st.diff(t)`、`st.symmetricDiff(t)`、`st.clear()`、`st.copy()`、`st.toList()`。
tuple：`t.count(x)`、`t.indexOf(x)`、`t.contains(x)`、`t.toList()`。
bigint：`b.pow(n)`、`b.toString([base])`；运算符全支持（`02-types.md`）。
int：`i.toString([base])`、`i.char()`（合法码点转单字符 string）。
function：`f.__name__`、`f.__params__`（见 `05-functions.md`）。
iterator：`it.next()`（同 `next(it)`）、`it.toList()`、`it.take(n)` / `it.drop(n)`、`it.map(f)` / `it.filter(f)` / `it.each(f)`（惰性链接，终结于 toList/for）。

## 5. 顶层内置变量

| 变量 | 值 |
|------|-----|
| `__name__` | `"__main__"` 或模块名（`09-modules.md`） |
| `__version__` | mslang 版本字符串（如 `"0.1.0"`） |

## 6. 保留（Tier 0 暂缓）

`format / open / exit / sleep / dump / gc` 等不属于 Tier 0——分别归 `strings.format / io.open / os.exit / time.sleep / runtime.gc`（`26-stdlib.md`），避免内置表膨胀。
