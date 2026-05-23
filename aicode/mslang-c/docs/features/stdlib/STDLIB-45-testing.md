# STDLIB-45: testing 模块

## 职责

轻量测试框架：注册/运行测试函数、断言原语、skip、benchmark、结果报告。
对应 Go 的 `testing` 包语义（适配脚本语言模型）。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/testing.ms`）。

---

## 函数清单

### 注册与运行

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `testing.run(name, fn)` | str, fn() | nil | 注册一个测试函数 |
| `testing.run_all()` | — | map | 运行所有注册测试，返回结果汇总 |
| `testing.run_matching(pattern)` | str | map | 仅运行名称含 pattern 的测试 |

### 断言（在测试函数内使用，失败立即抛异常）

| 函数 | 参数 | 描述 |
|---|---|---|
| `testing.assert(cond, msg="")` | bool, str | 条件为 false 时失败 |
| `testing.assert_eq(a, b, msg="")` | any, any, str | `a != b` 时失败，打印两值 |
| `testing.assert_ne(a, b, msg="")` | any, any, str | `a == b` 时失败 |
| `testing.assert_lt/le/gt/ge(a, b)` | any, any | 数值大小断言 |
| `testing.assert_raises(fn, exc=nil)` | fn, type? | fn 未抛出期望异常时失败 |
| `testing.assert_no_raise(fn)` | fn | fn 抛出任何异常时失败 |
| `testing.assert_str_eq(a, b)` | str, str | 字符串相等，失败时显示 diff |
| `testing.assert_approx(a, b, eps=1e-9)` | num, num, num | 浮点近似相等 |

### 控制流

| 函数 | 描述 |
|---|---|
| `testing.skip(reason="")` | 跳过当前测试 |
| `testing.fail(msg="")` | 标记当前测试失败但继续执行 |

### Benchmark

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `testing.bench(name, fn, n=1000)` | str, fn, int | map | 运行 fn n 次，返回 {name, n, total_ms, avg_ms} |
| `testing.bench_all()` | — | list | 运行所有已注册 bench |

### 结果汇总（`run_all` 的返回值）

```ms
{
    "passed":  12,
    "failed":  1,
    "skipped": 2,
    "errors":  [{"name": "test_foo", "message": "..."}],
    "total_ms": 45
}
```

---

## 依赖

- `time` 模块（STDLIB-03 ✅，benchmark 计时）
- `fmt` 模块（STDLIB-15，结果格式化）

---

## 示例

```ms
import "testing"

testing.run("加法", fun() {
    testing.assert_eq(1 + 1, 2)
})

testing.run("除法精度", fun() {
    testing.assert_approx(1.0 / 3.0, 0.333333333, 1e-9)
})

testing.run("异常", fun() {
    testing.assert_raises(fun() { throw "oops" })
})

testing.run("跳过", fun() {
    testing.skip("功能未实现")
    testing.assert(false)  // 不会执行
})

var results = testing.run_all()
print("通过: " + str(results["passed"]))
print("失败: " + str(results["failed"]))
```

---

## 测试（自测）

`testing` 模块自身无法用自身测试，使用 `tests/unit/test_stdlib_testing.c` 通过 `ms_vm_interpret` 验证。
