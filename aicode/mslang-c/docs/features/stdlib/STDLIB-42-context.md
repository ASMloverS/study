# STDLIB-42: context 模块

## 职责

请求上下文：取消信号、截止时间、跨协程传递键值。
基于 mslang async 模型实现，对应 Go 的 `context` 包。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/context.ms`）。取消/超时依赖 `time` 模块 Future 机制。

---

## 类型设计

```ms
class Context {
    init() {
        this._cancelled = false
        this._deadline  = nil   // ms 时间戳或 nil
        this._values    = {}
        this._cancel_future = nil
        this._parent    = nil
    }
    // with_cancel/with_timeout/with_deadline 创建的 ctx 均挂有 cancel 方法
    fun cancel() { this._cancelled = true }
}
```

---

## 函数清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `context.background()` | — | Context | 根上下文，永不取消 |
| `context.with_cancel(parent)` | Context | Context | 返回可取消子 ctx；调用 `ctx.cancel()` 取消 |
| `context.with_timeout(parent, ms)` | Context, int | Context | ms 毫秒后自动取消；同样有 `ctx.cancel()` |
| `context.with_deadline(parent, ts)` | Context, int | Context | 指定时间戳（毫秒）截止；同样有 `ctx.cancel()` |
| `context.with_value(parent, key, val)` | Context,any,any | Context | 携带键值（不可取消）|

### 实例方法

| 方法 | 返回 | 描述 |
|---|---|---|
| `ctx.cancel()` | nil | 取消此 ctx（含所有子 ctx）；可多次调用，幂等 |
| `ctx.is_cancelled()` | bool | 是否已取消（含父级取消）|
| `ctx.deadline()` | int\|nil | 截止时间戳（nil=无）|
| `ctx.value(key)` | any\|nil | 查找键值（沿父链查找）|
| `ctx.done` | Future | 取消时 resolve 的 Future |

---

## 依赖

- `time` 模块（STDLIB-03 ✅，Future/sleep/现在时间）

---

## 示例

```ms
import "context"
import "time"

async fun fetch(ctx, url) {
    // 每次循环检查是否被取消
    if ctx.is_cancelled() {
        throw "cancelled"
    }
    await time.sleep(100)
    return "data from " + url
}

async fun main() {
    var ctx = context.with_timeout(context.background(), 50)
    try {
        var result = await fetch(ctx, "http://example.com")
        print(result)
    } catch (e) {
        print("error: " + e)  // error: cancelled
    }
    ctx.cancel()  // 清理资源（可多次调用，幂等）
}
time.run_until_complete(main())
```

---

## 测试

```
tests/unit/test_stdlib_context.c
tests/fixtures/stdlib_context_basic.ms
```
