# mslang-c: Maple 脚本语言 C11 实现计划

## Context

将 mslang (C++23, ~15K 行, 52 个源文件) 移植为纯 C11 实现。原版架构为 **寄存器式 VM + 单趟编译器 (Pratt parser, 无 AST)**，区别于已有的 mslangx-c (AST + 栈式 VM)。mslang-c 应忠于原版架构。

## 项目结构

```
mslang-c/
  CMakeLists.txt
  include/ms/
    common.h          -- 类型别名, 平台宏, static_assert
    consts.h          -- 栈/帧限制, GC 阈值
    opcode.h          -- OpCode 枚举, 指令编解码, RK 辅助
    token.h           -- TokenType 枚举 (X-macro), Token 结构
    value.h           -- MsValue 标签联合体 (可选 NaN-boxing)
    object.h          -- MsObject 头部, 全部 Obj* 声明
    table.h           -- 哈希表 (ObjString* 键)
    vtable.h          -- ValueTable (Value 键, 用于 ObjMap)
    chunk.h           -- MsChunk: 指令数组 + 常量池 + 行号 RLE
    scanner.h         -- MsScanner 状态和 API
    compiler.h        -- compile() 入口, MsDiagnostic
    memory.h          -- mark_object, write_barrier, ObjectPool
    vm.h              -- MsVM 状态, interpret() API
    debug.h           -- 反汇编器
    module.h          -- 模块加载器
    optimize.h        -- peephole_optimize()
    serializer.h      -- .msc 序列化/反序列化
    shape.h           -- Shape (隐藏类) 和 InlineCache
  src/
    main.c
    common.c
    value.c
    object.c            -- 所有对象的 create/destroy/stringify/trace
    table.c
    vtable.c
    chunk.c
    scanner.c
    compiler.c          -- Pratt parser 核心, 声明, 语句
    compiler_expr.c     -- 表达式解析
    compiler_impl.h     -- 编译器内部结构 (Local, Upvalue, ExprDesc 等)
    vm.c                -- 分派循环, 栈操作, GC 触发
    vm_call.c           -- 函数调用, invoke, 协程 resume
    vm_builtins.c       -- 内置类型方法 (string/list/map/tuple)
    vm_natives.c        -- 原生函数注册
    vm_gc.c             -- 标记, 清扫, 分代, 增量
    vm_import.c         -- 模块加载
    debug.c
    memory.c            -- ObjectPool, write_barrier
    module.c
    optimize.c          -- 窥孔优化
    serializer.c
    shape.c
  tests/
    CMakeLists.txt
    test_assert.h       -- 简易测试宏
    unit/               -- 各模块单元测试
    fixtures/           -- .ms 脚本集成测试
    conformance/        -- golden output 测试
  cmake/
    MslangTesting.cmake
```

## 核心 C++ → C 设计决策

| C++ | C11 替代方案 |
|-----|-------------|
| `std::variant<nil,bool,double,i64,Object*>` | `struct MsValue { MsValueType type; union { ... } as; }` |
| `std::vector<T>` | `struct { T* data; int count; int capacity; }` + 增长宏 |
| `std::unordered_map` | `MsTable` 开放寻址 + 线性探测 |
| `Singleton<VM>` | 显式传 `MsVM*` 参数 (便于嵌入和测试) |
| RAII | `_init()` / `_destroy()` 配对 |
| `enum class` | `enum` + `MS_` 前缀 |
| 成员函数 | 自由函数, 首参为 `self` 指针 |
| 模板 | 宏 (`MS_ARRAY_PUSH`) 或按类型实例化 |
| 虚函数 (无) | 已是 switch-on-type 分派, 直接移植 |

**命名约定**: 类型 `MsValue`, 函数 `ms_value_is_nil()`, 枚举值 `MS_VAL_NIL`, 宏 `MS_ARRAY_PUSH`, 内部函数 `snake_case`

**错误处理**: 编译错误填充 `MsDiagnostic` 数组; 运行时错误设状态返回错误码; try/catch 用异常处理栈 (非 longjmp)

**分派机制**: 默认 `switch`; GCC/Clang 用 computed goto (`&&label`); MSVC 回退 switch

## 实现阶段 (15 个阶段)

### Phase 01: 项目骨架和构建系统
- CMakeLists.txt (C11, 严格警告), 目录结构
- `common.h` (类型别名 `ms_u8`, `ms_u32`, `ms_i64` 等), `consts.h` (kSTACK_MAX=256, kFRAMES_MAX=64)
- `main.c` 打印版本号
- `test_assert.h` 测试宏, `cmake/MslangTesting.cmake`
- **测试**: 构建成功, `mslang-c --version` 冒烟测试

### Phase 02: Value 系统和哈希表
- `MsValue` 标签联合体 (nil/bool/double/i64/Object*)
- 构造/检查/提取宏和函数, 真值性, 相等性, stringify
- `MsTable` 开放寻址哈希表 (ObjString* 键, 75% 负载因子, 2 的幂容量)
- 动态数组增长宏 `MS_ARRAY_PUSH`
- **测试**: value 创建/类型检查/真值性, table 插入/查找/删除/tombstone
- **参考**: `mslang/src/Value.hh`, `mslang/src/Table.hh`

### Phase 03: 对象系统 (字符串和基础对象)
- `MsObject` 头部 (type, is_marked, generation, age, next)
- `MsObjString` 用 C FAM (`char data[]`), FNV-1a 哈希, 字符串驻留
- `MsObjFunction`, `MsObjNative`, `MsObjUpvalue`, `MsObjClosure` (FAM upvalues)
- `ms_allocate_object` 宏, `object_stringify`/`object_destroy`/`object_size` 分派
- **测试**: 字符串创建/驻留去重, FAM 尺寸, 哈希碰撞
- **参考**: `mslang/src/Object.hh`

### Phase 04: Chunk 和指令编码
- `MsInstruction` (uint32_t), `MsOpCode` 枚举 (70+ 操作码)
- iABC/iABx/iAsBx 编解码函数, RK 辅助函数
- `MsChunk`: 指令数组 + 常量池 + SourceRun RLE 行号
- 反汇编器 `ms_disassemble_chunk` / `ms_disassemble_instruction`
- **测试**: 编解码 roundtrip, sBx 偏移正确性, 反汇编输出验证
- **参考**: `mslang/src/Opcode.hh`, `mslang/src/Chunk.hh`

### Phase 05: 扫描器 (词法分析)
- `MsScanner` 产出全部 token 类型
- ASI (自动分号插入), 括号深度抑制
- 字符串插值 `${expr}` 词法化
- 整数 vs 浮点字面量区分
- 关键字识别, 保存/恢复状态
- **测试**: 全操作符/关键字, 字符串转义, 插值嵌套, ASI 边界
- **参考**: `mslang/src/Scanner.hh`, `mslang/src/TokenTypes.hh`

### Phase 06: 编译器核心 (单趟 Pratt Parser)
- `MsCompiler` 结构, 寄存器分配器, `ExprDesc` 系统
- `MsParseRule` 表 (前缀/中缀/优先级), 优先级爬升
- 表达式: 字面量, 一元, 二元算术/比较, 逻辑 and/or, 分组
- 变量: local 声明/解析, global define/get/set
- 语句: `var`, `print`, 表达式语句, 块作用域
- 常量折叠, 字符串常量去重
- **测试**: 编译简单程序 → 反汇编验证, 寄存器分配, ExprDesc 优化
- **参考**: `mslang/src/CompilerImpl.hh`, `mslang/src/Compiler.cc`, `mslang/src/CompilerExpr.cc`

### Phase 07: VM 核心 (基础执行)
- `MsVM` 结构 (栈, 帧, 全局表, 字符串驻留表)
- 分派循环: LOADK/LOADNIL/LOADTRUE/LOADFALSE, MOVE, 算术, 比较, 一元, 位运算
- GETGLOBAL/SETGLOBAL/DEFGLOBAL, JMP/TEST/TESTSET
- CALL/RETURN (简单函数), 字符串拼接 (ADD on strings)
- RK 解码, 栈追踪错误报告
- **测试**: `print 1 + 2` → `3`, 变量, 条件, 函数调用, 类型错误
- **参考**: `mslang/src/VM.hh`, `mslang/src/VM.cc`

### Phase 08: 闭包, 上值, 控制流
- OP_CLOSURE + FAM 上值数组, 上值捕获 (local vs 传递), GETUPVAL/SETUPVAL, CLOSE
- 开放上值链
- `while`, `for` (C 风格), `for-in` (FORITER), `break`/`continue` (LoopContext + patch list)
- `switch`/`case`
- **测试**: 闭包捕获局部变量, 上值关闭, 计数器闭包, 嵌套循环 break/continue
- **参考**: `mslang/src/CompilerStmt.cc`

### Phase 09: 垃圾回收
- 基础标记-清扫: 根标记, 灰色栈追踪, 清扫
- 分代 GC: young/old 链表, nursery 阈值 (256KB), minor/major, 年龄晋升
- 记忆集, 写屏障
- ObjectPool 板块分配器 (Upvalue, BoundMethod)
- **测试**: 分配压力测试, 循环引用收集, 写屏障正确性, 晋升, 池分配器
- **参考**: `mslang/src/VMGC.cc`, `mslang/src/Memory.hh`

### Phase 10: OOP (类, 实例, 继承)
- CLASS/INHERIT/METHOD/STATICMETH/GETTER/SETTER/ABSTMETH
- ObjClass (方法表, 懒初始化 getter/setter/abstract)
- ObjInstance: Shape 布局 (SBO 8 内联字段, 溢出到堆), ObjBoundMethod
- GETPROP/SETPROP + EXTRAARG IC 槽位, GETSUPER, INVOKE, SUPERINV
- Shape 转换, 多态内联缓存 (4 entry PIC, megamorphic 回退)
- **测试**: 类创建, 字段读写, 方法调用, 继承+super, Shape 共享, IC 命中
- **参考**: `mslang/src/Object.hh` (Shape, InlineCache)

### Phase 11: 集合类型和内置方法
- ObjList (动态数组), ObjMap (ValueTable), ObjTuple (不可变, 可哈希)
- NEWLIST/NEWMAP/NEWTUPLE, GETIDX/SETIDX
- 字符串插值编译
- 内置方法: string (len/upper/lower/split/trim/...), list (push/pop/sort/map/filter/...), map (keys/values/has/remove/...), tuple (len/contains)
- ObjStringBuilder, ObjFile, ObjWeakRef
- **测试**: 各集合创建/操作/索引, 各内置方法, 字符串插值
- **参考**: `mslang/src/VMBuiltins.cc`

### Phase 12: 异常处理, defer, 协程
- TRY/ENDTRY/THROW + 异常处理栈, 栈回退
- DEFER + 帧级延迟闭包缓冲区
- 生成器函数 (`fun*`): ObjCoroutine (独立栈/帧), YIELD/RESUME, 状态机
- 默认参数, rest 参数
- **测试**: try/catch 基础/嵌套/跨帧, defer 执行顺序, 生成器 yield/resume/耗尽
- **参考**: `mslang/src/VMCall.cc`

### Phase 13: 模块系统和原生函数
- IMPORT/IMPFROM/IMPALIAS, ObjModule + 导出表
- 模块路径解析, 缓存, 循环依赖检测
- 原生函数: clock, type, str, num, input, len, int, float, assert 等
- ASCII 字符缓存
- **测试**: import/from-import/alias, 模块隔离, 循环检测, 各原生函数
- **参考**: `mslang/src/VMImport.cc`, `mslang/src/VMNatives.cc`

### Phase 14: 窥孔优化和 Quickening
- 5 趟窥孔优化: (1) 冗余 MOVE 消除, (2) LOADNIL 合并, (3) RETURN/THROW 后死代码, (4) LOADK+NEG 折叠, (5) MOVE+RETURN 尾合并
- NOP 压缩 + 跳转修复
- 运行时 Quickening: 算术特化 (ADD→ADD_II/ADD_FF/ADD_SS 等), deopt 计数器 (3 次失败回退)
- Computed goto 分派 (GCC/Clang)
- **测试**: 各优化前后字节码对比, Quickening 特化验证, deopt 回退
- **参考**: `mslang/src/Optimize.cc`

### Phase 15: 序列化, 增量 GC, 收尾
- .msc 二进制序列化/反序列化 (FNV-1a 哈希, DFS 后序, 自动缓存)
- 增量标记 (64 灰色对象/工作片)
- 运算符重载 (`__add`, `__sub` 等), 枚举声明, 三元运算符, 列表推导
- **测试**: 序列化 roundtrip, 缓存命中, 哈希失配重编译, 增量 GC 暂停验证

## 延迟/跳过的特性

| 特性 | 决策 | 原因 |
|------|------|------|
| LSP 服务器 | 延迟 | 范围大, 与核心运行时正交 |
| NaN-boxing | 延迟到 Phase 15 后 | 先用标签联合体, 后加编译选项 |
| 彩色终端输出 | 延迟 | 纯装饰性 |
| JSON 解析 | 跳过 | 仅 LSP 需要 |

## 验证策略

1. **每阶段**: 编写对应单元测试, `cmake --build build && ctest`
2. **Phase 07 起**: 端到端运行 `.ms` 脚本, 对比预期输出
3. **Phase 13 起**: 复用原版 `tests/` 下的 77 个测试脚本作为一致性测试
4. **最终**: 全部测试通过 + valgrind/ASAN 内存检查

## 关键参考文件

- `mslang/src/Opcode.hh` — 操作码定义, 指令编解码
- `mslang/src/CompilerImpl.hh` — 编译器全部内部结构
- `mslang/src/Object.hh` — 16 种对象类型, Shape, InlineCache
- `mslang/src/VM.hh` — VM 完整状态
- `mslang/src/VMGC.cc` — 分代增量 GC
- `mslang/src/Optimize.cc` — 窥孔优化
- `mslangx-c/CMakeLists.txt` — C11 CMake 配置参考
