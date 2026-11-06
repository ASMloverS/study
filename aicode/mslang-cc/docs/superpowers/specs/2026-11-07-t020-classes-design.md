# T020 class 与继承 mixin — 设计规格

- 日期: 2026-11-07
- 任务: docs/tasks/T020-classes-inheritance.md（P5，依赖 T018）
- 分支: t020-classes-inheritance（worktree .worktrees/t020-classes）
- 状态: 已评审通过（用户确认两项决策：实例字段布局、字段初始化器语义）
- 设计文档: docs/language/06-classes.md §1-§4、22-object-model.md §2 §6、21-bytecode.md §3.4

## 1. 决策记录

| 决策点 | 选择 | 备选（否决原因） |
|--------|------|------------------|
| 实例字段布局 | 每实例一个 MsObjMap（intern 名 → 值）单层直查；shape.c/.h 承载字段表 API，T022 升级内部为 Shape 转移链 + 内联槽 | 内联 8 槽 + 溢出数组：T020 即引入双结构与双 GC 面，语义无增量（性能机制归 T022） |
| 类体非 static `var x = e` 语义 | e 在 NEW_CLASS 时求值一次存 fieldDefaults；实例化时拷贝进实例字段表（无 e 者置 nil），__init__ 可覆盖 | 当作类属性（Python 式）：与任务书「字段置默认（声明字段 nil）」矛盾——声明字段预置 nil 会遮蔽类属性 |
| C3 冲突报错路径 | INHERIT_INIT 运行时经错误槽报 "SyntaxError: ..."；ms.c 对该前缀路由退出码 3 | 新增 VM 错误类别枚举：改所有错误置位点，侵入面大 |
| MRO 算法 | `seq = [本类] + reverse(mixins) + 基类MRO` 拼接 → 去重保首次 → 验证每个 base 自身 MRO 是结果子序列（矛盾即冲突） | 完整 C3 merge：任务公式已定死拼接形态，merge 属多余机制 |
| super 编码 | 编译器识别 `IDENT "super"` + MEMBER（方法体内直系位置）→ `LOAD_LOCAL 0` + 新 opcode OP_GET_SUPER；函数对象持 ownerClass（DEFINE_METHOD 回填） | 帧内隐藏"当前类"槽：帧布局再加隐藏槽，调用方装配负担大 |
| 方法帧 this | this 占 slot 0，paramCount 含 this；chunk 置 MS_CHUNK_METHOD 位；binder 方法作用域预声明占位 "this" | this 作隐式尾参：与文档「接收者为帧槽 0」相悖 |
| NEW_CLASS 编码 | `<nameIdx> <u8 baseCount>`，bases 对象依序压栈（含首基类） | 文档 `<nameIdx> <baseIdx?>` 单基类形态：mixin 清单无处安放；opcode 此前未使用，注释随实现更新 |

## 2. 组件设计

### 2.1 对象（新增 src/obj/obj_class.c/.h、src/obj/shape.c/.h）

```c
typedef struct MsObjClass {
  MsObj head;
  MsObjString* name;          // intern 类名
  MsObjList*   bases;         // 声明清单（首基类 + mixins；空 = 无基类）
  MsObjList*   mro;           // 线性化结果（自身在首位）
  MsObjMap*    methods;       // intern 名 → 函数值（普通方法）
  MsObjMap*    statics;       // intern 名 → 类属性（static var/const 值与 static func 函数值）
  MsObjList*   fieldNames;    // 声明字段名清单（intern 串）
  MsObjMap*    fieldDefaults; // 声明字段名 → 默认值（var x = e 的 e）
} MsObjClass;

typedef struct MsObjInstance {
  MsObj head;
  MsObjClass* klass;
  MsObjMap*   fields;         // 字段表（Shape 基础版：intern 名 → 值）
} MsObjInstance;
```

- shape.h API：字段表创建/取/置（内部即 MsObjMap）；T022 仅换内部实现。
- MsObjFunction 增 `MsObjClass* owner`（初始 NULL；DEFINE_METHOD 回填，super 消费）。
- MsObjBoundMethod.method 由 `MsObjNative*` 泛化为 `MsValue`（native/函数/闭包皆可）；boundMark 与 OP_CALL bound 分派兼容两种。
- GC：CLASS 标记 name/bases/mro/methods/statics/fieldNames/fieldDefaults；INSTANCE 标记 klass/fields；均无独立分配数据（子结构皆堆对象自管理），不需 freeFn。

### 2.2 指令与编译（src/compiler/compiler.c、opcodes.h、chunk.h）

- `NEW_CLASS <nameIdx> <u8 baseCount>`：栈 [b1..bn] → 空类（记录 name/bases；mro=INHERIT_INIT 补）。
- `DEFINE_METHOD <nameIdx> <u8 flags>`：栈 [class, value] → [class]。flags 位：
  - bit0 `MS_DEFM_STATIC`：value 入 statics（static var/const 值、static func 函数值）
  - bit1 `MS_DEFM_DUNDER`：dunder 标记（T021 消费；T020 仅随方法登记）
  - bit2 `MS_DEFM_FIELD`：登记 fieldNames
  - bit3 `MS_DEFM_FIELD_DEF`：value 入 fieldDefaults
  - 普通方法 flags=0：value（函数）入 methods 且回填 fn->owner
  - 字段无初始化器压 LOAD_NIL 统一栈形；FIELD|FIELD_DEF 组合用于带值字段
- `INHERIT_INIT`：栈 [class] → [class]，构建 MRO（§2.3）。
- 类声明编译序：bases 求值压栈 → NEW_CLASS → 逐成员（方法经 compileFunctionValue 物化、字段/static 求值）→ DEFINE_METHOD → INHERIT_INIT → finishDeclStore 落全局（模块级声明）。
- chunk.h 新增 `#define MS_CHUNK_METHOD 4u`；方法 chunk paramCount 含 this。
- AST_THIS：沿 FuncState 链最近函数边界为方法 → LOAD_LOCAL 0；否则 compErr "this outside method"。捕获扫描把方法体内 AST_THIS 当定义帧 slot 0（嵌套闭包捕获 this 走既有 upvalue 机制）。
- binder：类体成员方法（非 static func）的函数作用域预声明 "this" 占 slot 0（参数自 slot 1 起）；AST_THIS 不查符号表。
- MEMBER 赋值目标：compileAssign 增 MEMBER 分支（obj + value → SET_ATTR nameIdx）；复合赋值（Class.total += 1）展开 GET_ATTR → BINARY_* → SET_ATTR。
- super 编码：AST_MEMBER.obj 为 AST_IDENT 名 "super" 且无该名局部绑定且直系方法体内（非 static、非嵌套函数）→ `LOAD_LOCAL 0` + `OP_GET_SUPER <nameIdx> <icSlot>`；static 方法内/嵌套函数内 compErr；有 "super" 局部绑定则按普通名解析（super 非保留字）。

### 2.3 MRO（src/vm/class_ops.c）

1. bases 清单内部重复 → 冲突。
2. seq = [本类] + reverse(mixins) + 基类.mro；baseCount=0 时 mro=[本类]。
3. 去重保首次得 mro。
4. 每个 base 自身 mro 须为 mro 的子序列（保序），否则冲突。

冲突经错误槽报 `SyntaxError: cannot create a consistent MRO for class '<name>'`；ms.c 对 errMsg 前缀 "SyntaxError:" 路由退出码 3（其余运行时错误 1 不变）。

### 2.4 VM（src/vm/vm.c、src/vm/class_ops.c）

- **OP_CALL 遇 CLASS callee**（栈 [class, a1..an]）：
  分配实例（GC root：callee 槽改写 instance）→ 按 fieldNames 置 nil、fieldDefaults 拷贝默认 → 沿 MRO 查 methods["__init__"]：
  - 命中：实参区上移一格（memmove），栈 [fn, instance, a1..an]，setupCall(argc+1)（instance 即 slot 0 = this），返回值忽略，callee 槽终写 instance。
  - 未命中：argc≠0 → ArgumentError；否则 instance。
- **CALL_METHOD 遇 INSTANCE**：查找序 = 实例字段 → MRO methods → MRO statics。
  - 普通方法：实参区含接收者整体上移一格，recv 槽写 fn → [fn, recv, a1..an]，setupCall(argc+1)。
  - static 函数/字段值：recv 槽改写为值，setupCall(argc)（不可调用由 OP_CALL 兜底 TypeError）。
- **OP_GET_ATTR**：INSTANCE → 字段 → MRO methods（物化 bound{receiver, fn}）→ MRO statics（原值）→ AttributeError。
  CLASS → 本类及 MRO 各级 statics → methods（普通方法返回未绑定函数值，支持 `Base.method(this, args)`）→ AttributeError。
  现有函数内省/string/native 方法路径前置不变。
- **OP_SET_ATTR**：INSTANCE → 字段表写（首赋创建）；CLASS → statics 写（首赋创建类属性）；其余 TypeError。
- **OP_GET_SUPER <nameIdx> <icSlot>**：栈顶 this；取当前帧执行函数 fn（frame->closure ? closure->fn : function）→ fn->owner → owner->mro 中 owner 之后首个 methods 命中类 → 物化 bound（statics 命中返回原值；未命中 AttributeError）。

### 2.5 内置（src/vm/builtins.c）

- `instanceof(x, T)`：T 为 CLASS → x 为 INSTANCE 且 x.klass->mro 含 T → true；x 非 INSTANCE → false；T 非 CLASS → TypeError。新注册。
- `isinstance(x, T)`：T 为 CLASS → 同上 class 语义；T 为字符串/字符串集合 → 现有类型名匹配不变。
- `type(x)`：INSTANCE → klass->name（intern 串，即"返回类名"）；其余不变。
- `msValueStrForm`/`msValueReprForm`：INSTANCE → `<ClassName instance>`（__str__/__repr__ 归 T021）。

## 3. 错误处理

- 属性未命中：`AttributeError: '<TypeName>' has no attribute '<name>'`（TypeName 对 INSTANCE 取类名）。
- MRO 冲突：SyntaxError 前缀 + 退出码 3（§2.3）。
- this/super 非法位置：编译期诊断（compErr，退出码 3 常规路径）。
- OOM：沿现有 "RuntimeError: out of memory" 错误槽纪律；类对象/实例/bound 分配期相关源值均在栈 root。

## 4. 测试策略

- C 单测 tests/unit/test_class.c（编译+运行+输出断言，模式同 test_func_call.c）：任务书 §5.1 全部 11 用例（实例化/默认参构造/动态字段/继承+super/mixin 逆序/C3 冲突/static 共享/绑定方法/显式传递/查找遮蔽/instanceof）。
- fixtures tests/fixtures/class/：animal.ms、mixin.ms、static_counter.ms、bound_method.ms（.out golden）；mro_conflict.ms（.code=3、.err 含 SyntaxError）。
- GC：单测中低阈值压力实例化 + 引用丢弃触发回收（沿 test_gc_basic 模式）；ASan 门禁在 CI（GCC/Clang preset）。

## 5. 不做（越界即拒）

- dunder 协议语义（T021；本任务仅登记方法名与 DUNDER 标记位）。
- Shape 转移链/内联缓存（T022）。
- 多重继承基类（首基类外皆 mixin 线性化）、类冻结、property/运算符反射/元类（06 §8）。
