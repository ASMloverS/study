# T017 — Tier0 内置函数与迭代协议

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T013、T014、T015、T016
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/10-builtins.md` 全文、`03-expressions.md` §2（推导式）、`05-functions.md` §4（迭代协议语义）

## 1. 目标与范围

实现 Tier0 内置函数全集（10 §1 表）、iterator 对象与 `iter()/next()` 迭代协议（容器默认游标迭代器）、range 惰性迭代器、iterator 方法族（10 §4）、FORMAT_STRING 插值完整接通、三式推导式 + 生成器表达式的编译与执行（生成器表达式经迭代协议惰性求值——先以"立即求值的序列 + 惰性 iterator 语义"落地：genexp 语义闭包归 T019 完整化，本任务推导式全量）。

出口：roadmap 期 4 达成（容器 fixtures 全过）。

## 2. 实现要点

- 内置注册表：`msRegisterBuiltin(vm, name, MsNativeFn)`；print/len/cap/range/type/repr/str/int/bigint/float/bool/hash/abs/min/max/sum/sorted/reversed/enumerate/zip/iter/next/isinstance/callable/make(占位 T030)/id/input(占位 T035 后真实现)/assert/all(序列义)/race(占位 T031)/WeakRef(占位 T027) —— 占位项注册即报"未实现运行时错误"，避免静默。
- 迭代协议（05 §4）：`iter(x)` 返回 ITERATOR 壳；list/map(键)/tuple/set/string(chars 游标) 提供默认迭代器；`next(it)` 耗尽返回 nil（不抛）；`for x in it` 遇 nil 停止——GET_ITER/FOR_ITER 指令完整化。
- range：惰性 int 迭代器（start/stop/step；step 0 报错）；`range(10)`/`range(1,6)`/`range(10,0,-2)`。
- iterator 方法（10 §4）：next/toList/take/drop/map/filter/each（惰性链接，终结于 toList/for）。
- sorted：timsort 稳定，key/reverse 参数；min/max 单容器形态；sum 带 init；zip strict=true 长度不等 ValueError。
- isinstance：内置类型名匹配（class 沿 MRO 归 T020 后增强）；callable 先支持 function/native（`__call__` 归 T021）。
- 推导式编译（03 §2）：脱糖为嵌套循环 + 条件 + BUILD 追加；迭代变量子作用域（T009 标注）保证不泄漏；多 for 多 if 直译循环嵌套。
- 生成器表达式：调用实参/赋值右值位置合法（T007 解析）；本任务编译为"立即展开的惰性 iterator 包装"（语义等价实现：物化 list 包迭代器），T019 生成器到位后替换为真实惰性管道（DoD 标注复核）。
- 真值/打印：print 对容器递归；`__name__`/`__version__` 内置变量（10 §5）。

## 3. 涉及文件

新增: `src/vm/builtins.c`（Tier0 注册与实现）、`src/obj/obj_iter.c/.h`、`tests/unit/test_builtins.c`、`tests/fixtures/builtin/*.ms`
修改: `src/compiler/compiler.c`（推导式脱糖、FORMAT_STRING）、`src/vm/vm.c`（FOR_ITER 通用化）、`src/vm/builtins_bridge.c`（合并）

## 4. 验收标准（DoD）

- [ ] 10 §1 表可现函数全过（占位项报错信息明确）
- [ ] `for x in <list/string/map/tuple/set/range>` 六源全过
- [ ] 推导式三式 + 多 for/if 与嵌套推导式输出正确，变量不泄漏
- [ ] `sorted(range(5).toList(), reverse=true)`、zip strict、sum init 等组合正确
- [ ] `next` 耗尽 nil、for 停止；it.take/drop/map/filter 惰性链正确
- [ ] 00-overview §3 示例片段 `[x * x for x in range(1, 6)]` → `[1, 4, 9, 16, 25]`

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_builtins.c`

| 用例 | 调用 | 期望 |
|------|------|------|
| len 全类型 | string/list/map/set/tuple | 字节数/元素数 |
| type 全类型 | 02 §1 表逐项 | 名字串正确 |
| range 负步 | `range(10,0,-2)` | 10,8,6,4,2 |
| enumerate | start=1 | (1,a) (2,b) |
| zip strict | 不等长 + true | ValueError |
| sorted key | len 键 | 稳定序 |
| min/max 容器形态 | `[3,1,2]` | 1 / 3 |
| iter/next | list 耗尽 | 最后 nil |
| hash 数值塔 | 三态一致 | 相等 |
| assert 假 | `assert(false, "m")` | AssertionError |

### 5.2 ms fixtures `tests/fixtures/builtin/`

- `comprehension.ms`：

```ms
var xs = [1, 2, 3, 4, 5, 6]
print([x * x for x in xs if x % 2 == 0])     // [4, 16, 36]
print({x % 3 for x in xs})                    // {0, 1, 2}（toList 排序输出）
var m = {"a": [1], "b": [2, 3]}
print({k: len(v) for k, v in m.items()})      // {"a": 1, "b": 2}
var flat = [x + y for x in [1, 2] for y in [10, 20]]
print(flat)                                   // [11, 21, 12, 22]
```

- `iter_protocol.ms`：iter/next 手动推进、take/drop/map/filter 链、`for v in it`
- `conv.ms`：int/float/bool/str/repr 组合（golden）
- `genexp_pre.ms`：`sum(x * x for x in range(5))` → `30`（T019 后复跑同文件）

## 6. 风险与备注

- map 迭代 = 键迭代、items() 返回快照 list（10 §4）；快照在迭代中修改原 map 不受版本号影响——与 list 迭代器差异需用例分别锁定。
- `all(xs)` 本任务为"迭代器全真"义；`all(futures)` 并发义在 T031 重载启用——重载依据参数为 Future 列表。
