# STDLIB-24: random 模块

## 职责

伪随机数生成：均匀分布整数/浮点、正态分布、列表操作（洗牌/抽样/选择）。
使用高质量 PRNG（MT19937 或 Xoshiro256**），支持从 OS 熵自动种子。
不适用于密码学——请用 `crypto` 模块（Tier 3）。

---

## C/.ms 分层

| 层 | 内容 | 文件 |
|---|---|---|
| C 原语（`_rand`）| PRNG 状态（MT19937/Xoshiro256**）、OS 熵种子、`rand_int`/`rand_float` | `src/stdlib/_rand.c` |
| .ms 组合层（`random`）| `gauss`（Box-Muller）、`choices`（加权）、`sample`（无重复抽样）、`shuffle` | `stdlib/ms/random.ms` |

---

## 函数清单

### 种子

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `random.seed(n=nil)` | int? | nil | nil=从 OS 熵（/dev/urandom 或 BCryptGenRandom）种子；否则用 n |

### 基础

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `random.float()` | — | num | `[0.0, 1.0)` 均匀分布 |
| `random.uniform(lo, hi)` | num, num | num | `[lo, hi)` 均匀分布 |
| `random.int(lo=0, hi=nil)` | int, int? | int | `[lo, hi]` 整数均匀分布，**两端闭区间**（hi=nil→max int64；注：异于 Go `IntN` 的半开区间 `[0,n)`）|
| `random.gauss(mu=0.0, sigma=1.0)` | num, num | num | 正态分布（Box-Muller）|

### 列表操作

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `random.choice(list)` | list | any | 随机选一个元素 |
| `random.choices(list, k=1, weights=nil)` | list, int, list? | list | 有放回抽取 k 个（可加权）|
| `random.sample(list, k)` | list, int | list | 无放回抽取 k 个（Fisher-Yates 子集）|
| `random.shuffle(list)` | list | nil | 原地随机打乱（Fisher-Yates）|

---

## C 原语 `_rand` 接口

```c
// 导出 native 函数：
// _rand.seed(n)           → nil  (n=nil 时读 OS 熵)
// _rand.rand_int(lo, hi)  → int  ([lo, hi] 拒绝采样无偏)
// _rand.rand_float()      → num  ([0.0, 1.0))
```

MT19937 状态（624 个 uint32_t）存在 `MsObjUserdata` 中，全局一个实例。

---

## 依赖

- `_rand`（C 原语）
- list 内置（len/push/索引）

---

## 示例

```ms
import "random"

random.seed(42)

print(random.int(1, 6))       // 骰子 [1,6]
print(random.float())          // [0.0, 1.0)
print(random.gauss(0, 1))      // 标准正态

var deck = [1,2,3,4,5,6,7,8,9,10]
random.shuffle(deck)
print(deck)

print(random.sample(deck, 3))  // 无放回取3个
print(random.choices(["a","b","c"], 5, [0.6, 0.3, 0.1]))  // 加权抽取
```

---

## 测试

```
tests/unit/test_stdlib_random.c
tests/fixtures/stdlib_random_basic.ms
```

关键测试点：
- 相同 seed 产出相同序列（可重现性）
- `int(lo,hi)` 分布无偏（拒绝采样）
- `sample` 无重复
- `shuffle` 所有排列均等概率（理论验证）
