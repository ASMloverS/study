# STDLIB-23: itertools 模块

## 职责

惰性迭代器工具：基于 mslang 生成器（`yield`/coroutine）实现无限序列、组合器和惰性流水线。
对应 Go 的 `iter` 包 + Python `itertools` 的常用子集。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/itertools.ms`）。
依赖语言的 generator（`fun*`/`yield`）特性（Phase 12 已实现协程）。

---

## 函数清单

### 无限生成器

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `itertools.count(start=0, step=1)` | int, int | generator | 从 start 开始无限递增 |
| `itertools.cycle(iterable)` | list\|gen | generator | 循环重复 iterable |
| `itertools.repeat(value, n=nil)` | any, int? | generator | 重复 value n 次（nil=无限）|

### 有限组合器

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `itertools.chain(iter1, iter2, ...)` | gen... | generator | 顺序连接多个迭代器 |
| `itertools.zip_iter(iter1, iter2, ...)` | gen... | generator | 对应元素配对（以最短者为准）|
| `itertools.zip_longest(iter1, iter2, fill=nil)` | gen..., any | generator | 配对（以最长者为准，不足用 fill 补）|
| `itertools.enumerate(iterable, start=0)` | gen, int | generator | 产出 `[i, v]` 对 |
| `itertools.flatten(iterable_of_iterables)` | gen | generator | 展平一层 |
| `itertools.windowed(iterable, n)` | gen, int | generator | 滑动窗口（产出长度 n 的 tuple）|
| `itertools.batch(iterable, n)` | gen, int | generator | 分批（每批 n 个，最后一批可能不足）|
| `itertools.accumulate(iterable, fn=nil)` | gen, fn? | generator | 累计值（nil=加法）|
| `itertools.pairwise(iterable)` | gen | generator | 产出相邻对 `(a,b)`（等同 windowed n=2）|

### 函数式变换

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `itertools.map(fn, iterable)` | fn, gen | generator | 惰性 map |
| `itertools.filter(fn, iterable)` | fn, gen | generator | 惰性 filter |
| `itertools.starmap(fn, iterable)` | fn, gen[[...]] | generator | 对每个 list 元素解包调用 fn |
| `itertools.take(n, iterable)` | int, gen | list | 取前 n 个（物化）|
| `itertools.drop(n, iterable)` | int, gen | generator | 跳过前 n 个 |
| `itertools.take_while(fn, iterable)` | fn, gen | generator | 取前缀直到 fn 返回 false |
| `itertools.drop_while(fn, iterable)` | fn, gen | generator | 跳过前缀直到 fn 返回 true |
| `itertools.reduce(fn, iterable, init)` | fn, gen, any | any | 折叠（物化）|
| `itertools.groupby(iterable, key)` | gen, fn | generator | 产出 `[key, sub_gen]`（连续分组）|

### 组合生成

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `itertools.product(iter1, iter2)` | gen, gen | generator | 笛卡尔积（物化两个）|
| `itertools.permutations(list, r=nil)` | list, int? | generator | r 排列 |
| `itertools.combinations(list, r)` | list, int | generator | r 组合（无重复）|
| `itertools.combinations_with_replacement(list, r)` | list, int | generator | r 组合（允许重复）|

---

## 与 list 的互转

- `itertools.take(n, gen)` 物化为 list。
- `list` 可直接作为 iterable 传入所有接受 gen 的函数（`for v in list` 已支持）。

---

## 依赖

- 语言 generator 特性（`fun*`/`yield`，Phase 12）
- list 内置（`for` 迭代）

---

## 示例

```ms
import "itertools"

// 无限序列取前 5
print(itertools.take(5, itertools.count(10, 2)))  // [10,12,14,16,18]

// 循环配对
var colors = itertools.cycle(["red","green","blue"])
print(itertools.take(5, colors))  // [red,green,blue,red,green]

// 滑动窗口
print(itertools.take(4, itertools.windowed([1,2,3,4,5], 3)))
// [[1,2,3],[2,3,4],[3,4,5]]

// 惰性 filter + take
var evens = itertools.filter(fun(x){ return x%2==0 }, itertools.count())
print(itertools.take(5, evens))  // [0,2,4,6,8]

// 排列
for p in itertools.permutations([1,2,3], 2) {
    print(p)  // [1,2],[1,3],[2,1],[2,3],[3,1],[3,2]
}
```

---

## 测试

```
tests/unit/test_stdlib_itertools.c
tests/fixtures/stdlib_itertools_basic.ms
```
