# 06 类

Python 语义的 class，Go 拼写的外观（`class Name : Base { }`、`this`）。

## 1. 声明与实例化

```ms
class Animal {
    var name, age                       // 类字段声明（可省略，字段可动态添加）

    func __init__(name, age = 1) {
        this.name = name
        this.age = age
    }

    func speak() -> "..."
    func describe() -> "${this.name} (${this.age})"
}

var a = Animal("Cat", 3)                // 直接调用类名实例化（无 new 关键字）
print(a.describe())
```

- 实例化流程：分配实例 → 字段置默认 → 调 `__init__`（若有）；`__init__` 返回值忽略。
- 字段声明 `var name, age` 仅作文档与初始化顺序提示，不强制；未声明字段首次赋值即创建。

## 2. 继承与 mixin

```ms
class Base {
    func who() -> "base"
    func call() -> "Base.call"
}

trait Logger {                          // mixin：用 trait 声明（class 亦可作 mixin）
    func log(msg) { print("[log] ${msg}") }
}

class Impl : Base, Logger {             // 第一个为基类，其余为 mixin
    func who() -> "impl"
    func call() -> "${super.call()} + Impl.call"
}
```

- **单继承链**：`:` 后第一个是基类；后续为 mixin（按声明序混入）。
- **线性化（MRO）**：`[本类] + [mixin 逆序] + [基类 MRO]`；C3 冲突（菱形重复且顺序矛盾）编译期报 SyntaxError。
- `super` 指代 MRO 中本类的下一个类：仅支持 `super.method(args)` 形式（显式转发用 `Base.method(this, args)`）。
- 基类构造不自动调用；惯例在 `__init__` 首行显式 `super.__init__(...)`。
- `instanceof(x, T)` 沿 MRO 判定。

## 3. this

- 方法体内 `this` 指接收者实例；绑定方法提取（`var m = obj.method`）后调用仍绑原实例。
- 类体内非方法位置（字段初始化）`this` 非法。
- 显式传递：`Class.method(instance, args)` 合法。

## 4. 类成员与 static

```ms
class Counter {
    static var total = 0                // 类级共享变量
    static func describe() -> "counters, total=${Counter.total}"
    const VERSION = "1.0"               // 类常量（首字母大写惯例，仍是类属性）
}
Counter.total += 1
```

- `static func` 无 `this`；通过 `Class.name` 访问，子类可见可遮蔽。
- 实例查找顺序：实例字段 → 本类方法/类属性 → MRO 逐级（Python 语义）。

## 5. dunder 协议总表

| 类别 | dunder | 触发点 |
|------|--------|--------|
| 构造 | `__init__` | 实例化 |
| 字符串 | `__str__` / `__repr__` | `str()` / `repr()`、插值 |
| 等价序 | `__eq__` `__hash__` `__lt__` `__le__` `__gt__` `__ge__` | 比较运算、入 map/set |
| 算术 | `__add__ __sub__ __mul__ __truediv__ __mod__ __pow__` | 运算符（映射表见 `03-expressions.md` §6） |
| 位 | `__and__ __or__ __xor__ __shl__ __shr__ __invert__` | 位运算 |
| 一元 | `__neg__` | `-x` |
| 容器 | `__len__` `__getitem__` `__setitem__` `__delitem__` `__contains__` | `len()/索引/del/in` |
| 迭代 | `__iter__` `__next__` | `for in`、`iter()/next()` |
| 调用 | `__call__` | `instance(...)` |

- `__str__` 未定义回落 `__repr__`；都未定义输出 `<ClassName instance at 0x...>`。
- `__iter__` 返回迭代器（通常 `return this` 且实现 `__next__`）；`__next__` 无值时 `return nil` 表耗尽。
- `__getitem__` 支持 int 负索引与 slice 参数（slice 以 `(start, stop, step)` tuple 传入，步进默认 1）。

## 6. 属性访问内联缓存（实现注记）

实例字段访问与方法调用由 Shape（隐藏类）+ 4 路 PIC 内联缓存加速，megamorphic 回退字典查找——语义不变，纯性能机制，详见 `22-object-model.md` §6。

## 7. 可哈希实例

默认实例不可哈希（入 map/set 抛 TypeError）；同时定义 `__eq__` 与 `__hash__` 后可哈希。`__hash__` 在对象生命周期内须稳定（可变性风险自负，同 Python）。

## 8. v1 不做

- property 读写器（用方法 + dunder 覆盖）
- 运算符反射（`__radd__` 族）
- 元类 / 装饰器语法糖（高阶函数直接调用可替代：`f = logged(f)`）
- `__del__`（见 `07-errors.md` 末尾与 `23-gc.md`）
