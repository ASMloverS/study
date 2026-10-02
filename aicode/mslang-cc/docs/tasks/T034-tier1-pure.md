# T034 — Tier1 组 A：math/strings/errors/sort/random

- 阶段: P10 C API + Tier1（roadmap 期 10）
- 依赖: T033
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/26-stdlib.md` §2（对应行）、§5（融合原则）、`10-builtins.md` §2 §3（实例方法与模块函数等价原则）

## 1. 目标与范围

以 C 实现并注册 5 个纯计算 Tier1 模块（26 §2 逐函数）：`math`、`strings`、`errors`、`sort`、`random`。均无 IO/调度依赖。

不做：`strings.Builder`（归组 B，涉对象语义）、`random` 加密用途（crypto/rand 归 T043）。

## 2. 实现要点

- 通用骨架：`src/stdlib/<mod>.c` 经 ms.h 注册面挂接（T033）；每个模块配套 `tests/fixtures/stdlib_<mod>/` 用例；融合原则按 26 §5（Go error 风格→异常、实例方法优先、迭代器优先、秒 float/毫秒 int）。
- math（26 §2）：pi/e 常量；sqrt/pow/exp/log/exp2/log2/floor/ceil/round/trunc/abs/max/min/modf/hypot/isqrt/gcd/lcm/deg/rad；三角双曲族；inf/nan 判定（isNaN/isInf）。整数族（isqrt/gcd/lcm）接受 int 返回 int；其余 float。
- strings（26 §2）：contains/index/lastIndex/join/repeat/upper/lower/title/trim 系列/split 系列/fields/compare/Format(fmt, args)（Go Sprintf 子集：%v %d %s %f %.2f %x %%）/tryInt/tryFloat/parseInt/parseFloat/formatInt/formatFloat/toSnake/toCamel/intern/Builder（字符串构建器：write/toString）。
- errors（26 §2）：new(msg)/wrap(e, msg)/unwrap(e)/is(e, target)/str；define(name, parent) 子类工厂（运行时创建 Error 子类）。
- sort（26 §2）：sort(xs[, cmp])/stable/areSorted/search(二分)/reverse(xs)/byKey/byField(x, "name")/compare（多键 cmp 构造）。
- random（26 §2）：xorshift128+ 种子态；seed/rnd int(min,max)/float()/choice/shuffle/sample/bytes(n)/bool()。
- 实例方法↔模块函数等价（26 §5-3）：`"a".toUpper()` 已有（T013），模块函数补齐同语义——共享 C 实现体。

## 3. 涉及文件

新增: `src/stdlib/math.c`、`src/stdlib/strings.c`、`src/stdlib/errors.c`、`src/stdlib/sort.c`、`src/stdlib/random.c`、`tests/unit/test_stdlib_pure.c`、`tests/fixtures/stdlib_math/*.ms`、`tests/fixtures/stdlib_strings/*.ms`、`tests/fixtures/stdlib_errors/*.ms`、`tests/fixtures/stdlib_sort/*.ms`、`tests/fixtures/stdlib_random/*.ms`
修改: `src/module/builtin_libs.c`（注册 5 模块）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 26 §2 五模块行内列出的全部函数可 import 调用且行为正确
- [ ] `strings.Format` 支持子集全部动词 + 精度
- [ ] `errors.define` 运行时子类可抛可捕、is/unwrap 链正确
- [ ] sort 多键 compare/byField 稳定正确；search 二分边界正确
- [ ] random 种子可复现（同种子同序列）；shuffle 保持元素多重集
- [ ] math 整数族溢出/负数边界报错语义明确（isqrt 负数 ValueError）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_stdlib_pure.c`

| 用例 | 调用 | 期望 |
|------|------|------|
| math.gcd/lcm | (12,18)/(4,6) | 6/12 |
| math.isqrt | 17 | 4 |
| strings.toSnake/toCamel | "HTTPServer"/"my_field" | "http_server"/"myField" |
| strings.tryInt | "ff" base16 | ("ff",16)→(255,true) 形态 |
| strings.Format | "%d-%s=%.2f" | 数值格式正确 |
| sort.byField | 对象列表按名 | 稳定序 |
| sort.search | 有序数组 | 插入点正确 |
| random 复现 | 同种子两次 | 序列相等 |
| errors.wrap/is | wrap 链 | is 命中根因 |

### 5.2 ms fixtures（每模块一组，节选）

- `stdlib_math/math_basic.ms`：常量与常用函数 golden（含三角/双曲采样值）
- `stdlib_strings/format.ms`：

```ms
import "strings"
print(strings.Format("%d-%s=%.2f", 7, "ms", 3.14159))   // 7-ms=3.14
print(strings.toCamel("my_field"))                       // myField
print(strings.parseInt("ff", 16))                        // 255
```

- `stdlib_errors/wrap.ms`：new/wrap/unwrap/is/str 链 + define 自定义
- `stdlib_sort/multi_key.ms`：compare 多键 + byField
- `stdlib_random/reproducible.ms`：seed(42) 两轮序列一致

## 6. 风险与备注

- 函数面广但单点简单：以"每函数至少 1 断言"的表格化 fixtures 保证覆盖率，避免漏实现（DoD 清单逐函数勾选）。
- `random.shuffle/sample` 用时间复杂度 O(n) 的 Fisher-Yates；choice 空容器 ArgumentError。
