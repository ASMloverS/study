# T010 — 指令集定义与 Compiler 基础

- 阶段: P3 编译 + VM 基础（roadmap 期 3）
- 依赖: T009
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/21-bytecode.md` 全文、`20-architecture.md` §1

## 1. 目标与范围

定义字节码指令集全集（~90 条分组）、Chunk 结构与操作数编码，实现 Compiler 基础路径（字面量/变量/运算/比较/逻辑短路 → 字节码）与 `ms dump` 反汇编雏形。本任务编译产物暂由直接内存对象消费（无执行），出口 = 反汇编输出稳定。

不做：VM 执行（T011）、函数/类/异常/并发指令的生成逻辑（后续任务按指令分组逐步启用，但指令编码全集一次定义）。

## 2. 实现要点

- 指令编码（21 §1）：opcode 1 字节 + 操作数 0/1/2 字节；`EXTENDED_ARG <u8>` 前置拼接支持 16/24/32 位常量池索引与跳转目标；跳转目标为绝对偏移。
- 指令目录全集定义（21 §3 各组）：装载/存储（LOAD_* / STORE_* / MOVE 族）、运算（BINARY_* / UNARY_* / COMPARE_* / IS / IN / GET_INDEX / SET_INDEX / BUILD_SLICE）、函数调用、类、控制流（JMP 族 / SWITCH_TABLE / TYPE_SWITCH_TABLE / CHECK_TYPE）、迭代生成器（GET_ITER / FOR_ITER / YIELD / RESUME）、异常 defer、并发、杂项（BUILD_* / UNPACK_* / FORMAT_STRING / CHECK_SAFEPOINT / ASSERT / NOP）。
- Chunk（21 §2）：`code/consts/lines(RLE)/nUpvalues/nIcSlots`；行号表 RLE（指令偏移→源行）。
- Compiler：AST 标注节点 → 指令流；本任务启用：LOAD_NIL/TRUE/FALSE/CONST、LOAD/STORE_LOCAL/GLOBAL、DEF_GLOBAL、BINARY_*（int/float 语义先数值，dunder 慢路径桩返回运行时错误）、COMPARE_*、JMP_IF_FALSE_OR_POP/JMP_IF_TRUE_OR_POP（短路）、POP/JMP、三元。
- 常量池：字面量去重（int/float/string/nil/bool）；函数原型/类原型留槽（后续任务填充）。
- peephole 占位：本任务实现 NOP 压实与简单跳转链折叠（`JMP → JMP` 直达）。
- 反汇编 `ms dump`（21 §4 样式）：常量池 + 指令流（带偏移与操作数注释）+ 行号表；输出格式即后续 `.ast` 同级的 `.bc` 快照基线。

```c
typedef struct Chunk {
  uint8_t* code; int32_t codeLen, codeCap;
  MsValue* consts; int32_t constsLen, constsCap;  // T012 后为完整值；本任务先字面量记录
  SourceRun* lines; int32_t linesLen;
  int32_t nUpvalues; uint16_t nIcSlots;
  const char* name;  // 函数名/模块名
} Chunk;
```

## 3. 涉及文件

新增: `src/compiler/opcodes.h`、`src/compiler/chunk.h`、`src/compiler/chunk.c`、`src/compiler/compiler.h`、`src/compiler/compiler.c`、`src/compiler/disasm.c`、`tests/unit/test_compiler_basic.c`
修改: `tools/ms.c`（`ms dump` 子命令雏形）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 21 §4 示例（`func add`）反汇编输出与文档样式一致（该示例含函数指令，函数 MAKE_FUNCTION 本任务生成桩指令，T018 完整化——快照基线允许 T018 后更新一次）
- [ ] 基础表达式（四则/比较/短路/三元）生成的指令序列经单测断言
- [ ] EXTENDED_ARG：>255 常量与 >255 跳转距离用例通过（构造大常量池源码）
- [ ] peephole：`JMP` 链折叠与 NOP 压实用例通过
- [ ] `.bc` 反汇编快照稳定（两次编译 diff 为空）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_compiler_basic.c`

| 用例 | 源码 | 期望指令（摘要） |
|------|------|------|
| 常量装载 | `1 + 2.5` | LOAD_CONST ×2, BINARY_ADD |
| 局部读写 | `var x = 1; x = x + 1` | LOAD_CONST, STORE_LOCAL 0, … |
| 全局读写 | 模块层 `x := 1` | DEF_GLOBAL, STORE_GLOBAL |
| 短路 | `a && b` | JMP_IF_FALSE_OR_POP |
| 三元 | `a ? b : c` | JMP/JMP_IF_FALSE 组合 |
| 比较链禁止 | `a < b < c` | 编译期不可达（Binder 已拒） |
| 大常量池 | 300 个字面量 | EXTENDED_ARG 出现且操作数正确 |
| 跳转折叠 | 嵌套恒真条件 | 折叠后无中间 JMP |

### 5.2 ms 快照用例 `tests/conformance/21-bc-*`

- `21-bc-01-arith.ms` → `.bc`：基础运算指令序列
- `21-bc-02-short-circuit.ms`：短路跳转形态
- `21-bc-03-extended-arg.ms`：EXTENDED_ARG 编码
- `21-bc-04-peephole.ms`：折叠效果

## 6. 风险与备注

- 指令集全集一次定义但执行按阶段启用：未启用指令在 VM 中统一 `unimplemented` 陷阱（防静默错执行）。
- 跳转目标在编译期经 patch 表二次回填（向前跳转），EXTENDED_ARG 长度变化触发的重定位在 patch 阶段统一处理。
