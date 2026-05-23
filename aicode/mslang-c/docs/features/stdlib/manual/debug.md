# debug 模块

运行时自省：调用栈、帧信息、局部变量、字节码反汇编、类型检测、VM 统计。

```ms
import "debug"
```

> 实现规格：[STDLIB-09-debug.md](../STDLIB-09-debug.md)

---

## 函数速查表

| 函数 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `debug.typeof(val)` | any | str | 值的类型名称 |
| `debug.is_native(fn)` | any | bool | 是否为内置 Native 函数 |
| `debug.is_closure(fn)` | any | bool | 是否为脚本定义的闭包 |
| `debug.traceback([depth])` | int? | str | 从 depth 帧起的调用栈字符串 |
| `debug.frame_info([depth])` | int? | map | depth 帧的信息 `{name, file, line}` |
| `debug.locals([depth])` | int? | map | depth 帧的局部变量快照 |
| `debug.upvalues(fn)` | Closure | map | 闭包捕获的上值快照 |
| `debug.disasm(fn)` | Closure | str | 函数字节码反汇编文本 |
| `debug.vm_stats()` | — | map | VM / GC 统计信息 |
| `debug.gc_trace(b)` | bool | nil | 开关 GC 跟踪输出 |

`depth` 参数：0 = 当前帧，1 = 上一帧，以此类推。

---

## 分组详解

### typeof 类型名称

```ms
import "debug"
print(debug.typeof(nil))      // nil
print(debug.typeof(true))     // bool
print(debug.typeof(42))       // int
print(debug.typeof(3.14))     // number（注意：不是 "num"）
print(debug.typeof("hi"))     // string
print(debug.typeof([1,2]))    // list
print(debug.typeof({"a":1}))  // 不能直接传 map 字面量，先赋变量
var m = {"a": 1}
print(debug.typeof(m))        // map
```

### is_native / is_closure

```ms
import "debug"
fun greet() { return "hello" }
print(debug.is_native(len))       // true  (内置 len)
print(debug.is_closure(greet))    // true  (脚本定义的函数)
print(debug.is_native(greet))     // false
```

> `print` 是关键字，不能作为参数传入，请用 `len`、`type`、`str` 等普通 native 函数。

### frame_info：当前帧 / 调用者帧

```ms
import "debug"
fun outer() {
    fun inner() {
        var fi = debug.frame_info()    // depth=0: inner 自身
        print(fi["name"])   // inner
        print(fi["line"])   // 当前行号

        var caller = debug.frame_info(1)  // depth=1: outer
        print(caller["name"])  // outer
    }
    inner()
}
outer()
```

### traceback：调用栈字符串

```ms
import "debug"
fun b() {
    var tb = debug.traceback()
    print(len(tb) > 0)   // true
    // 典型格式：
    //   at b (script.ms:3)
    //   at a (script.ms:6)
    //   at <script> (script.ms:8)
}
fun a() { b() }
a()
```

### locals：帧局部变量

```ms
import "debug"
fun compute(x, y) {
    var sum = x + y
    var locs = debug.locals()
    print(locs["slot_0"])   // x 的值（slot 编号从 0 起）
    return sum
}
compute(10, 20)
```

### disasm：字节码反汇编

```ms
import "debug"
fun fib(n) {
    if (n <= 1) return n
    return fib(n-1) + fib(n-2)
}
var asm_str = debug.disasm(fib)
print(asm_str)
```

### vm_stats：VM / GC 统计

```ms
import "debug"
var s = debug.vm_stats()
print(s["gc_bytes"])      // 当前已分配字节数
print(s["gc_threshold"])  // 触发 GC 的阈值
print(s["stack_used"])    // 当前栈深度（slot 数）
print(s["frame_used"])    // 当前帧数
```

---

## 完整示例

文件：[`examples/debug.ms`](examples/debug.ms)

运行：

```
$ ./build/Debug/mslang-c.exe docs/features/stdlib/manual/examples/debug.ms
nil
bool
int
number
string
list
map
true
true
false
show_frame
true
true
true
true
true
true
```

---

## 实现/性能注解

- `traceback` / `frame_info` / `locals` 以当前执行帧为参照，**不包括** `debug.XXX` 函数自身的帧（已内部跳过）。
- `locals` 的键是 `"slot_0"`, `"slot_1"` 等数字编号，编号与寄存器分配有关，不保证等于变量声明顺序。
- `disasm` 输出格式与版本相关，仅供调试目的，不保证稳定性。

## 常见陷阱

```ms
import "debug"
// ❌ print 是关键字，不能作为参数
// debug.is_native(print)   → 语法错误

// ✅ 用其他 native 函数
debug.is_native(len)    // true

// ❌ map 字面量不能直接作为 debug.typeof 的参数
// debug.typeof({"a": 1})  → 解析错误

// ✅ 先赋给变量
var m = {"a": 1}
debug.typeof(m)    // "map"
```
