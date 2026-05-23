# math 模块

纯数学函数与常量，无 IO、无副作用。

```ms
import "math"
```

> 实现规格：[STDLIB-01-math.md](../STDLIB-01-math.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `math.abs(x)` | num | num | 绝对值 |
| `math.floor(x)` | num | num | 下取整 |
| `math.ceil(x)` | num | num | 上取整 |
| `math.round(x)` | num | num | 四舍五入（half-even） |
| `math.trunc(x)` | num | num | 向零截断 |
| `math.sign(x)` | num | int | -1 / 0 / 1 |
| `math.fmod(x, y)` | num, num | num | 浮点余数 |
| `math.sqrt(x)` | num | num | 平方根 |
| `math.pow(x, y)` | num, num | num | x ^ y |
| `math.exp(x)` | num | num | e ^ x |
| `math.log(x)` | num | num | 自然对数 |
| `math.log2(x)` | num | num | 以 2 为底 |
| `math.log10(x)` | num | num | 以 10 为底 |
| `math.hypot(x, y)` | num, num | num | √(x²+y²) |
| `math.sin(x)` | num (弧度) | num | 正弦 |
| `math.cos(x)` | num (弧度) | num | 余弦 |
| `math.tan(x)` | num (弧度) | num | 正切 |
| `math.asin(x)` | num | num (弧度) | 反正弦 |
| `math.acos(x)` | num | num (弧度) | 反余弦 |
| `math.atan(x)` | num | num (弧度) | 反正切 |
| `math.atan2(y, x)` | num, num | num (弧度) | 四象限反正切 |
| `math.sinh(x)` | num | num | 双曲正弦 |
| `math.cosh(x)` | num | num | 双曲余弦 |
| `math.tanh(x)` | num | num | 双曲正切 |
| `math.degrees(x)` | num (弧度) | num | 弧度 → 角度 |
| `math.radians(x)` | num (角度) | num | 角度 → 弧度 |
| `math.min(a, b, ...)` | num... | num | 最小值（≥1 个参数） |
| `math.max(a, b, ...)` | num... | num | 最大值 |
| `math.clamp(x, lo, hi)` | num, num, num | num | 限制到 [lo, hi] |
| `math.sum(list)` | list[num] | num | 列表求和 |
| `math.gcd(a, b)` | int, int | int | 最大公约数 |
| `math.lcm(a, b)` | int, int | int | 最小公倍数 |
| `math.is_nan(x)` | num | bool | 是否 NaN |
| `math.is_inf(x)` | num | bool | 是否无穷 |
| `math.is_finite(x)` | num | bool | 是否有限数 |
| `math.random()` | — | num | [0.0, 1.0) 均匀随机数 |
| `math.randint(lo, hi)` | int, int | int | [lo, hi] 整数随机数 |
| `math.seed(n)` | int | nil | 设置随机种子 |

### 常量

| 名称 | 值 |
|---|---|
| `math.PI` | π ≈ 3.14159 |
| `math.E` | e ≈ 2.71828 |
| `math.TAU` | 2π ≈ 6.28318 |
| `math.INF` | 正无穷大 |
| `math.NAN` | 非数值（NaN） |

---

## 分组详解

### 基础取整

```ms
import "math"
print(math.floor(2.9))    // 2
print(math.ceil(2.1))     // 3
print(math.round(2.5))    // 2  (half-even：向偶数取整)
print(math.round(3.5))    // 4
print(math.trunc(-2.9))   // -2 (向零截断)
print(math.abs(-5.0))     // 5
print(math.sign(-3))      // -1
```

> `round` 使用 IEEE-754 的 half-even 规则（银行家舍入），而非「四舍五入」。

### 幂与对数

```ms
import "math"
print(math.sqrt(9))         // 3
print(math.pow(2, 8))       // 256
print(math.exp(1))          // e ≈ 2.71828
print(math.log(math.E))     // 1
print(math.log2(1024))      // 10
print(math.log10(100))      // 2
print(math.hypot(3, 4))     // 5
```

### 三角函数

所有三角函数以**弧度**为单位。使用 `degrees` / `radians` 在弧度与角度间转换。

```ms
import "math"
print(math.sin(math.PI / 2))    // 1
print(math.cos(0))               // 1
print(math.degrees(math.PI))     // 180
print(math.radians(90))          // 1.5708（π/2）
print(math.atan2(1, 1))          // 0.7854（π/4）
```

### 聚合

```ms
import "math"
print(math.min(3, 1, 4, 1, 5))     // 1
print(math.max(3, 1, 4, 1, 5))     // 5
print(math.clamp(10, 0, 5))        // 5   (10 超出上界)
print(math.clamp(-1, 0, 5))        // 0   (-1 低于下界)
print(math.sum([1, 2, 3, 4, 5]))   // 15
```

### 整数工具

```ms
import "math"
print(math.gcd(12, 8))   // 4
print(math.lcm(4, 6))    // 12
```

### 特殊值检测

```ms
import "math"
print(math.is_nan(math.NAN))    // true
print(math.is_inf(math.INF))    // true
print(math.is_finite(1.0))      // true
```

### 随机数

```ms
import "math"
math.seed(42)
print(math.random())          // [0.0, 1.0) 随机浮点
print(math.randint(1, 6))     // [1, 6] 随机整数（模拟骰子）
```

---

## 完整示例

文件：[`examples/math.ms`](examples/math.ms)

```ms
import "math"

print(math.abs(-3.7))
print(math.floor(2.9))
print(math.ceil(2.1))
print(math.round(2.5))
print(math.round(3.5))
print(math.trunc(-2.9))
print(math.sign(-5))

print(math.sqrt(16))
print(math.pow(2, 10))
print(math.log(math.E))
print(math.log2(8))
print(math.log10(1000))

print(math.degrees(math.PI))
print(math.radians(180) == math.PI)

print(math.min(3, 1, 4, 1, 5))
print(math.max(3, 1, 4, 1, 5))
print(math.clamp(10, 0, 5))
print(math.sum([1, 2, 3, 4, 5]))

print(math.gcd(12, 8))
print(math.lcm(4, 6))

print(math.is_nan(math.NAN))
print(math.is_inf(math.INF))
print(math.is_finite(1.0))

print(math.PI)
print(math.TAU)
print(math.E)

math.seed(42)
print(math.randint(1, 6))
print(math.randint(1, 6))
```

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/math.ms
3.7
2
3
2
4
-2
-1
4
1024
1
3
3
180
true
1
5
5
15
4
12
true
true
true
3.14159
6.28319
2.71828
2
5
```

---

## 实现/性能注解

- **随机数质量**：底层使用 `rand()/RAND_MAX`，熵约 15–31 bit，**不适用于密码学或安全敏感场景**。
- **浮点精度**：返回值均为 `double`，存在 IEEE-754 浮点误差（如 `log(E)` 可能输出 `0.999...`）。
- `floor` / `ceil` / `round` 返回 `num`（double），结果仍是浮点数。如需整数类型，用 `int(math.floor(x))`。

## 常见陷阱

```ms
// ❌ NaN 比较永远为 false
import "math"
print(math.NAN == math.NAN)   // false

// ✅ 用 is_nan 检测
print(math.is_nan(math.NAN))  // true
```
