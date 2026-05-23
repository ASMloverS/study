import "math"

// 基础取整与绝对值
print(math.abs(-3.7))
print(math.floor(2.9))
print(math.ceil(2.1))
print(math.round(2.5))
print(math.round(3.5))
print(math.trunc(-2.9))
print(math.sign(-5))

// 幂与对数
print(math.sqrt(16))
print(math.pow(2, 10))
print(math.log(math.E))
print(math.log2(8))
print(math.log10(1000))

// 角度转换
print(math.degrees(math.PI))
print(math.radians(180) == math.PI)

// 聚合
print(math.min(3, 1, 4, 1, 5))
print(math.max(3, 1, 4, 1, 5))
print(math.clamp(10, 0, 5))
print(math.sum([1, 2, 3, 4, 5]))

// 整数工具
print(math.gcd(12, 8))
print(math.lcm(4, 6))

// 特殊值检测
print(math.is_nan(math.NAN))
print(math.is_inf(math.INF))
print(math.is_finite(1.0))

// 常量
print(math.PI)
print(math.TAU)
print(math.E)

// 随机（固定种子）
math.seed(42)
print(math.randint(1, 6))
print(math.randint(1, 6))
