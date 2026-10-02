# T021 — dunder 全协议

- 阶段: P5 函数/类（roadmap 期 5）
- 依赖: T020
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/06-classes.md` §5 §7、`03-expressions.md` §6、`02-types.md` §6 §7 §8

## 1. 目标与范围

实现全部 dunder 协议语义：字符串（__str__/__repr__ 与插值形态）、等价序（__eq__/__hash__/比较族与自动推导）、算术/位/一元重载映射、容器协议（__len__/__getitem__/__setitem__/__delitem__/__contains__）、迭代（__iter__/__next__）、调用（__call__）、可哈希实例规则。出口：roadmap 期 5 完成——06 文档用例全过。

不做：反射运算符（v1 不做，03 §6）、property/元类/装饰器（06 §8）。

## 2. 实现要点

- 运算符慢路径：BINARY_*/COMPARE_*/UNARY_* 快路径（内置类型）miss 后查左操作数类 MRO 的对应 dunder（03 §6 映射表）；左无 dunder 且右不匹配 → 直接 TypeError（无反射，03 §6）。
- 数值 × 实例混算：数值侧永不调实例 dunder，直接 TypeError（03 §6 规则）。
- `!=` 自动 = `__eq__` 取反；`> >= <=` 在仅有 `__lt__/__eq__` 时自动推导（06 §5：`a > b ≡ b < a`、`a >= b ≡ !(a < b)`）。
- `==` 深值相等（02 §6）：int/bigint/float 数值比较；容器递归；实例默认引用比较，`__eq__` 覆盖；NaN IEEE 语义。
- hash 一致性（02 §7）：定义 `__eq__` 未定义 `__hash__` → 实例不可 hash（TypeError）；两者齐备才可入 map/set；`__hash__` 生命周期内稳定（文档化风险自负）。
- 字符串形态：`str()` 走 `__str__` 未定义回落 `__repr__`；都未定义 `<ClassName instance at 0x...>`；插值 `${x}` 同 str 规则（instance 走 __str__ 否则 __repr__，03 §9）。
- 容器协议：`__getitem__` 支持 int 负索引与 slice（以 `(start, stop, step)` tuple 传入，步进默认 1，06 §5）；`__delitem__` 对接 del 语义（`del` 语法 v1 范围内仅 map.remove/xs.remove 形态——`__delitem__` 供 C API 与容器子类，文档标注）。
- 迭代协议（06 §5）：`__iter__` 通常 `return this` 且实现 `__next__`；`__next__` return nil 表耗尽；自定义可迭代接入 for/iter()/推导式（03 §2、05 §4）。
- `__call__`（06 §5）：`instance(...)` 触发；callable() 对 `__call__` 实例返回 true（T017 桩激活）。
- 内置类型亦走同一协议表（int/string/list 等的"虚拟 dunder"以原生实现挂表）——统一 instance 与内置的语义路径（如自定义类切片转发）。

## 3. 涉及文件

新增: `src/vm/dunder.c/.h`（协议分派表）、`tests/unit/test_dunder.c`、`tests/fixtures/dunder/*.ms`
修改: `src/vm/vm.c`（运算/比较/IN/GET_INDEX 慢路径接入）、`src/obj/obj_map.c`（hash/eq 协议钩子）、`src/vm/builtins.c`（str/repr/hash/callable/isinstance 增强）

## 4. 验收标准（DoD）

- [ ] 06 §5 表逐行 dunder 用例通过
- [ ] 03 §6 映射表全运算符（含位/一元/`in`）重载正确；无反射 TypeError 用例通过
- [ ] Vector（__add__ + __eq__ + __repr__）标准样例通过
- [ ] 自定义可迭代类（__iter__/__next__）接入 for 与推导式
- [ ] `__getitem__` 切片 tuple 传参正确；负索引自处理
- [ ] hash/eq 规则：只 eq 报 TypeError；齐备可作键

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_dunder.c`

| 用例 | 源码 | 期望 |
|------|------|------|
| __add__ | Vector(1,2)+Vector(3,4) | Vector(4,6) |
| 数值×实例 | `1 + v` | TypeError（不走 v 的 __radd__——不存在） |
| __eq__/hash | 入 map | 齐备可用/只 eq 拒绝 |
| 推导比较 | 仅 __lt__/__eq__ | > >= <= 正确 |
| __contains__ | `x in obj` | 自定义包含 |
| __len__ | `len(obj)` | 自定义 |
| __getitem__ 切片 | `obj[1:3]` | 收 tuple(1,3,1) |
| __iter__/__next__ | for 推进 | 自定义序列后 nil 停 |
| __call__ | `obj(5)` | 调用协议 |
| __str__ 回落 | 只有 __repr__ | str() 用 __repr__ |
| __neg__/__invert__ | 一元 | 正确触发 |

### 5.2 ms fixtures `tests/fixtures/dunder/`

- `vector.ms`：

```ms
class Vector {
    func __init__(x, y) { this.x = x; this.y = y }
    func __repr__() -> "Vector(${this.x}, ${this.y})"
    func __add__(o) -> Vector(this.x + o.x, this.y + o.y)
    func __eq__(o) -> this.x == o.x && this.y == o.y
    func __hash__() -> this.x * 31 + this.y
}
var m = {}
m[Vector(1, 2)] = "a"
print(Vector(1, 2) + Vector(3, 4))     // Vector(4, 6)
print(m[Vector(1, 2)])                 // a
```

- `iterable.ms`：`class Countdown { func __init__(n){…} func __iter__(){return this} func __next__(){…} }` + for + 推导式
- `container_cls.ms`：__getitem__/__setitem__/__len__/__contains__ 组合（栈类样例）
- `call_callable.ms`：`__call__` + callable() 判定
- `no_reflect.ms`：`1 + v`、`"a" + v` TypeError（stderr + 退出码 1）

## 6. 风险与备注

- 慢路径性能：本任务不引入缓存（T022 统一做 IC）；快路径/慢路径分派开销以 `--trace` 指令计数观测，基准门禁 T044 兜底。
- 内置类型"虚拟 dunder"表是 stdlib（T034+）复用点：原生类（如 math 返回类型）经同一机制获得运算符行为，设计为公开内部接口（非 ms.h）。
