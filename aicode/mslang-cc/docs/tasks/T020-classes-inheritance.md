# T020 — class 与继承 mixin

- 阶段: P5 函数/类（roadmap 期 5）
- 依赖: T018
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/06-classes.md` §1-§4、`22-object-model.md` §2 §6（CLASS/INSTANCE 结构）、`21-bytecode.md` §3.4

## 1. 目标与范围

实现 MsObjClass（方法表/基类/MRO/Shape 种子）、MsObjInstance（Shape + 内联 8 字段 + 溢出数组，本任务 Shape 先以"字段名→槽位 map"直查实现，转移链优化归 T022）、MsObjBound；NEW_CLASS/DEFINE_METHOD/INHERIT_INIT/GET_ATTR/SET_ATTR；实例化流程、this、绑定方法、单继承 + mixin 线性化（C3）、super、static、instanceof；类属性查找顺序。出口：roadmap 期 5 主体——06 文档 §1-§4 用例全过。

不做：dunder 协议语义（T021，本任务仅注册方法名进表）、IC（T022）。

## 2. 实现要点

- 类对象（22 §2）：`name/bases(声明清单)/mro(线性化结果)/methods(map)/statics/类字段默认`；类创建时机 = 模块体执行到 class 声明（NEW_CLASS + DEFINE_METHOD 序列，21 §3.4）。
- MRO（06 §2）：`[本类] + [mixin 逆序] + [基类 MRO]`；C3 冲突（菱形重复且顺序矛盾）编译期 SyntaxError——实现在 INHERIT_INIT 时计算，冲突经错误槽报"编译期等价"错误（模块体执行即报）。
- 实例化（06 §1）：分配 → 字段置默认（声明字段 nil）→ 调 `__init__`（若有，返回值忽略）；`Point(1, 2)` 直接调用类名。
- 属性查找（06 §4）：实例字段 → 本类方法/类属性 → MRO 逐级；未找到 AttributeError。SET_ATTR：实例字段首赋即建（动态字段）；类体 `static var`/`const` 归类属性，`Class.total += 1` 走类属性读写。
- this 与绑定方法（06 §3）：方法调用以接收者为帧槽 0；`var m = obj.method` 提取 BOUND_METHOD（receiver + method），调用仍绑原实例；类体内非方法位置 this 非法（Binder 已拒）；`Class.method(instance, args)` 显式传递合法。
- super：仅 `super.method(args)` 形式；实现为帧内"当前类"（编译期确定：方法所属类）沿其 MRO 下一类查找；基类构造不自动调用（惯例首行 `super.__init__(...)`）。
- static func 无 this；子类可见可遮蔽。
- `instanceof(x, T)` 沿 MRO 判定（06 §2 用 `instanceof` 拼写——注意与内置 `isinstance` 并存：isinstance 支持类集合，T017 已有内置名匹配，本任务接 class 语义）。
- GC：INSTANCE 标记内联字段 + 溢出数组；CLASS 标记方法表/基类。

## 3. 涉及文件

新增: `src/obj/obj_class.c/.h`、`src/obj/shape.c/.h`（基础版字段表）、`src/vm/class_ops.c`（实例化/查找）、`tests/unit/test_class.c`、`tests/fixtures/class/*.ms`
修改: `src/compiler/compiler.c`（类声明编译、super 编码）、`src/vm/vm.c`（GET_ATTR/SET_ATTR 类路径）、`src/gc/gc.c`

## 4. 验收标准（DoD）

- [ ] 06 §1-§4 全部示例运行正确（Animal/Impl/Base/Counter）
- [ ] 菱形 C3 冲突报 SyntaxError（编译期等价路径）
- [ ] 实例查找顺序：实例字段遮蔽方法/类属性正确；`Counter.total` 跨实例共享
- [ ] super 沿 MRO 调用（mixin 逆序链）；`Base.method(this, args)` 显式形式可用
- [ ] instanceof 沿 MRO；`type(instance)` 返回类名
- [ ] GC：实例/类/绑定方法可达性 ASan 通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_class.c`

| 用例 | 源码 | 期望 |
|------|------|------|
| 实例化 | Animal("Cat", 3) | 字段初始化、describe 输出 |
| 默认参构造 | `Animal("Dog")` | age=1 |
| 动态字段 | 未声明字段首赋 | 创建并可读 |
| 继承 + super | 06 §2 Impl.call | `Base.call + Impl.call` |
| mixin 逆序 | 双 mixin 同名方法 | 后声明者近 |
| C3 冲突 | 菱形矛盾序 | SyntaxError |
| static | Counter.total 跨实例 | 累加共享 |
| 绑定方法 | `var m = obj.method; m()` | this 保持 |
| 显式传递 | `Base.method(this)` | 等价 |
| 查找遮蔽 | 同名实例字段遮蔽方法 | 字段优先 |
| instanceof | 子类实例 vs 基类/mixin | 均真；无关类假 |

### 5.2 ms fixtures `tests/fixtures/class/`

- `animal.ms`（06 §1 原例）、`mixin.ms`（06 §2 原例）、`static_counter.ms`（06 §4 原例）
- `mro_conflict.ms`：C3 冲突 → stderr SyntaxError 退出码 3
- `bound_method.ms`：提取绑定方法、显式传递组合

## 6. 风险与备注

- super 的"当前类"取编译期方法所属类而非运行时 this 类——与 Python 显式 super(当前类, self) 对齐，静态可定。
- 类是可变对象（方法表可变，02 §1 表 class 可变）：`Class.total += 1` 即类属性写；不做类冻结。
