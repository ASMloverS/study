# T010 指令集与 Compiler 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 定义字节码指令集全集（~93 条）与 Chunk 结构，实现 Compiler 基础路径（字面量/变量/运算/比较/短路/三元/函数桩 → 字节码）与 `ms dump` 反汇编，`.bc` 快照基线稳定。

**Architecture:** 标注 AST（T009 Binder 输出）→ FuncState 逐 chunk 发射最小宽指令（跳转为 u8 占位 + fixup 表）→ patch → peephole（NOP 压实 + JMP 链折叠，偏移重映射）→ 宽度松弛不动点 → 物化（EXTENDED_ARG 前缀 + 行号表 RLE 重建）。反汇编器与测试解码器共用 opcodes 元数据表。

**Tech Stack:** C11（MSVC /W4 /WX + GCC -Wall -Wextra -Werror）、CMake ≥3.16、ctest、Python 3（快照生成）。

**规格来源:** `docs/tasks/T010-instruction-compiler.md`、`docs/language/21-bytecode.md`、`docs/language/28-style-c.md`。

## 关键设计决策（已定）

1. **偏移空间**：每个 chunk 指令偏移独立从 0 起（跳转目标为 chunk 内绝对偏移）。21 §4 示例中 main 从 0006 续接属文档笔误；反汇编按"样式"对齐（`; <name>` 段头、4 位偏移、操作数注释），快照基线为本任务产物。
2. **跳转编码**：编译期最小宽 `JMP + u8 占位`（2 字节），patch 表记录 target；物化阶段按目标宽度加 `EXTENDED_ARG` 前缀（16/24/32 位），宽度变化经松弛不动点统一重定位（任务书 §6）。
3. **常量操作数**：索引发射时已知 → 直接按需前置 EXTENDED_ARG（索引去重后不再变化，无重定位问题）。
4. **T010 启用集**（超出即 `MSE_COMPILE_UNSUPPORTED`=1022 诊断，尽力继续）：LOAD_NIL/TRUE/FALSE/CONST、LOAD/STORE_LOCAL/GLOBAL、DEF_GLOBAL、BINARY_*（含位运算）、COMPARE_*、IS/IN 不启用（只定义）、JMP/JMP_IF_FALSE(_OR_POP)/JMP_IF_TRUE_OR_POP、POP、三元、函数桩（LOAD_CONST proto + MAKE_FUNCTION 0 + DEF/STORE_GLOBAL、CALL argc、RETURN）、字符串单段字面量。一元/索引/成员/切片/容器/推导式/FORMAT_STRING/upvalue 装载均不启用。
5. **函数体 epilogue**：末指令已为 RETURN 则不再补；否则补 `LOAD_NIL; RETURN`（与 21 §4 func add 一致）。模块恒补。
6. **常量池**：ConstEntry 字面量记录（nil/bool/int/float/rune/string/bigint 借用源文本 + proto 持 Chunk*）；int/float/bool/nil/string 按值去重，proto 不去重。
7. **`ms dump`**：`ms dump <file.ms>` → stdout；诊断 stderr、退出码同 `--dump-ast`（有错 1）。`.bc` conformance 模式由 test_runner 注入 `dump` 子命令，基线仅由 `tools/gen_bc_snapshots.py` 生成。
8. **反汇编格式**（即 .bc 基线格式，确定性输出）：

```
; main
consts 3
  0 int 1
  1 float 2.5
  2 string "x"
code 8
0000 LOAD_CONST      0 ; 1
0002 LOAD_CONST      1 ; 2.5
0004 BINARY_ADD
0005 POP
0006 LOAD_NIL
0007 RETURN
upvalues 0
icslots 0
lines 1
  0000 1
; func add
consts 0
code 6
0000 LOAD_LOCAL      0
0002 LOAD_LOCAL      1
0004 BINARY_ADD
0005 RETURN
```

   float 用 `%.17g`（与 ast_dump 一致）；string 转义 `\" \\ \n \t \r` 与 `\xNN`（<0x20 或 >0x7E）；跳转操作数注释 `; -> %04d`；常量注释按值渲染（proto 为 `<func name>`）。模块段头 `; main`，原型 `; func <name>`；输出顺序：模块 chunk 先，随后按常量池序深度优先递归原型。`SELECT_GO`/`SWITCH_TABLE`/`ASSERT` 等 OPL_SPECIAL 不被 T010 发射，解码器遇到即 MS_ASSERT。

## 文件结构

- 新增 `src/compiler/opcodes.h` — MsOpcode 枚举全集 + 布局枚举 + extern 元数据表声明
- 新增 `src/compiler/chunk.h` / `chunk.c` — Chunk/ConstEntry/SourceRun/ChunkJump、emit、常量去重、行号 RLE、msOpNames/msOpLayouts 表、chunkPeephole、chunkLineAt
- 新增 `src/compiler/compiler.h` / `compiler.c` — compilerCompile（FuncState、语句/表达式编译、跳转 fixup、松弛物化）、MSE_COMPILE_UNSUPPORTED
- 新增 `src/compiler/disasm.c` — compilerDisasm
- 新增 `tests/unit/test_compiler_basic.c`
- 修改 `tools/ms.c` — `dump` 子命令
- 修改 `tools/test_runner.c` — `.bc` 模式
- 新增 `tools/gen_bc_snapshots.py`
- 新增 `tests/conformance/21-bc-01..05-*.ms`（+ 生成基线 `.bc`）
- 修改 `CMakeLists.txt`、`tests/CMakeLists.txt`
- 修改 `docs/tasks/T010-instruction-compiler.md`、`docs/tasks/README.md`（状态）

每个任务：先写失败测试（如适用）→ 实现 → ctest 过 → `<gitmoji> <type>(<scope>): <msg>` 提交。构建目录固定 `build/`，验证命令 `cmake --build build --config Debug` + `ctest --test-dir build -C Debug`（Windows 多配置）。

---

### Task 0: 开工状态

- [ ] `docs/tasks/T010-instruction-compiler.md` 头部状态 → `🚧 进行中`；README 总表 T010 行 → 🚧
- [ ] 提交 `🚧 docs(tasks): mark T010 in progress`

### Task 1: opcodes.h + 元数据表（含 chunk 骨架编译通过）

**Files:** Create `src/compiler/opcodes.h`；Modify `src/compiler/chunk.h`、`src/compiler/chunk.c`、`CMakeLists.txt`

- [ ] opcodes.h：完整 MsOpcode 枚举（NOP=0、EXTENDED_ARG、栈操作 POP/DUP/DUP2/ROT/SWAP、装载/存储组、运算组 25 条、函数调用组 5、类组 5、控制流组 9（含 2 个 SPECIAL）、迭代生成器 4、异常 defer 8、并发 10、杂项 10、OP__COUNT）；`MsOpndLayout { OPL_NONE, OPL_U8, OPL_CONST, OPL_JUMP, OPL_U8_U8, OPL_CONST_U8, OPL_CONST_CONST, OPL_JUMP_JUMP, OPL_CONST_U8_U8, OPL_SPECIAL }`；extern 表声明
- [ ] chunk.h：ConstKind/ConstEntry/SourceRun/Chunk/ChunkJump 定义 + API（chunkInit/chunkFree/chunkAddConst*/chunkEmitOp/chunkEmitByte/chunkLineAt/chunkPeephole）
- [ ] chunk.c：表定义（名字 + 布局，MS_ASSERT 索引边界）+ init/free（递归 proto）
- [ ] CMakeLists.txt：`target_sources(ms_compiler PRIVATE src/compiler/chunk.c)`；删除占位 `src/compiler/compiler.c`（由 Task 3 重建真身前先保留占位？——不：Task 1 直接用 chunk.c 替换 compiler.c 占位文件中的 typedef？**决定：保留 compiler.c 占位不动，追加 chunk.c**）
- [ ] 构建 `cmake --build build --config Debug` 过（/W4 /WX）
- [ ] 提交 `✨ feat(compiler): define full opcode set and chunk structures`

### Task 2: chunk 核心 + 单测（RED→GREEN）

**Files:** Create `tests/unit/test_compiler_basic.c`；Modify `tests/CMakeLists.txt`

- [ ] 测试（先写，失败）：`chunkAddConst*` 去重（int/float/bool/string 各返回同索引）、proto 不去重、chunkEmitOp 行号 RLE（同行合并、行变更新 run）、chunkFree 递归
- [ ] `ms_add_test(compiler_basic ms_compiler ms_binder)` 注册
- [ ] 运行 `ctest --test-dir build -C Debug -R compiler_basic` 确认先 RED（编译失败即 RED）
- [ ] 实现 chunk.c 各函数（MS_ARRAY_PUSH；字符串 memcmp 去重）
- [ ] ctest GREEN → 提交 `✅ test(compiler): chunk const dedup and line RLE unit tests`

### Task 3: Compiler 基础路径（字面量/变量/二元/赋值）+ 单测

**Files:** Create `src/compiler/compiler.h`、真身 `src/compiler/compiler.c`（覆盖占位）

- [ ] compiler.h：`bool compilerCompile(AstNode* module, Chunk* out, const char* file, MsDiagSink* diags);`、`MSE_COMPILE_UNSUPPORTED = 1022`、`void compilerDisasm(const Chunk* c, FILE* out);` 声明
- [ ] compiler.c：FuncState{chunk/jumps/diags/file}；emitOp/emitByte/emitConst（EXT 链）；compileStmt（EXPR_STMT→POP、VAR_DECL global/local、ASSIGN 普通/复合、BLOCK）；compileExpr（NIL/BOOL/INT/FLOAT/RUNE/BIGINT/单段 STRING→LOAD_CONST、IDENT local/global、BINARY 全算术位运算比较→op 映射）；unsupported() 诊断（对齐 binder.c bindErr 的 sink 写法）；模块 epilogue；`finishChunk` 此任务先直接返回（无跳转）
- [ ] 测试：常量装载（`1 + 2.5` → LOAD_CONST×2, BINARY_ADD, POP, LOAD_NIL, RETURN）、局部读写（func 体内 STORE/LOAD_LOCAL 槽位）、全局读写（DEF_GLOBAL/STORE_GLOBAL/LOAD_GLOBAL 序列）、复合赋值（`x += 1` 展开）、常量去重集成、不支持语句（`if true {}` → MSE_COMPILE_UNSUPPORTED 且 diags>0）
- [ ] 解码断言助手：walkInstruction(code, len, &off, &op, &operand)（EXT 累积，查 msOpLayouts）
- [ ] RED → GREEN → 提交 `✨ feat(compiler): basic bytecode path for literals, vars, binary ops`

### Task 4: 短路/三元/跳转回填/松弛物化/EXTENDED_ARG + 单测

**Files:** Modify `src/compiler/compiler.c`、`tests/unit/test_compiler_basic.c`

- [ ] emitJump/patchJump（fixup 记录，占位 u8）；`&&`→JMP_IF_FALSE_OR_POP、`||`→JMP_IF_TRUE_OR_POP、三元→JMP_IF_FALSE+JMP 组合
- [ ] finishChunk：①chunkPeephole（Task 6 前为空实现直接过）②松弛：posMap[codeLen] 数组，循环 {按当前宽度走边界填 posMap；逐 fixup 求需宽 bytesFor(posMap[target])-1；变则重走} ③物化：新缓冲按序拷贝，跳转处写 EXT 链（target 高→低）+ op + 低字节，行号表按 chunkLineAt(oldOff) 重建 RLE
- [ ] 测试：短路形态与目标值（`return a && b` → LOAD_LOCAL 0, JMP_IF_FALSE_OR_POP L, LOAD_LOCAL 1, L==当前 codeLen-…断言解码目标）、三元形态、>255 常量（300 个 var decl 生成源码 → 解码见 EXTENDED_ARG 且操作数==256）、>255 跳距（300 decl 后尾三元 → JMP 操作数 >255 解码正确）、快照确定（同源两次编译 memcmp code 相等）
- [ ] RED → GREEN → 提交 `✨ feat(compiler): short-circuit, ternary, jump patching with EXTENDED_ARG relaxation`

### Task 5: 函数原型桩 + 单测

**Files:** Modify `src/compiler/compiler.c`、`tests/unit/test_compiler_basic.c`

- [ ] compileFuncDecl：子 FuncState 编译体（params 槽位来自 Binder 标注；RETURN 语句；epilogue 规则"末指令非 RETURN 才补"）；addConstProto；模块层 `LOAD_CONST k; MAKE_FUNCTION 0; DEF_GLOBAL n; STORE_GLOBAL n`（函数体内 FUNC_DECL 为 local → STORE_LOCAL slot）
- [ ] compileExpr AST_CALL：callee 仅 IDENT(global) → LOAD_GLOBAL，args 依序，`CALL argc`
- [ ] 测试：`func add(a, b) { return a + b }` → proto 命名/指令序列 LOAD_LOCAL 0, LOAD_LOCAL 1, BINARY_ADD, RETURN（无 epilogue）；`func f() { var x = 1 }` → 有 epilogue；`print(1)` → LOAD_GLOBAL "print", LOAD_CONST, CALL 1, POP, LOAD_NIL, RETURN；无值 return → LOAD_NIL, RETURN
- [ ] RED → GREEN → 提交 `✨ feat(compiler): function prototype stubs with MAKE_FUNCTION/CALL`

### Task 6: peephole（NOP 压实 + JMP 链折叠）+ 单测

**Files:** Modify `src/compiler/chunk.c`（chunkPeephole 真身）、`tests/unit/test_compiler_basic.c`

- [ ] chunkPeephole(Chunk, ChunkJump*, n)：①不动点重定向 resolveTarget（NOP 链跳过 → 目标为 OP_JMP 的 fixup 时直达，自环/超 jumpCount 步停）②存在 NOP 则压实（边界 old→new 映射、搬字节、重映射 fixups.at/target 与 lines[].offset）③保持 fixups 按 at 有序
- [ ] finishChunk 接入 chunkPeephole
- [ ] 测试：手造 chunk（NOP;NOP;LOAD_TRUE;JMP→首 NOP 的 fixup）→ 压实后 NOP 消失、目标重定向到 LOAD_TRUE 新偏移；集成：`var r = a ? (b ? 1 : 2) : 3` 折叠后不存在"跳转目标处为 JMP"的跳转
- [ ] RED → GREEN → 提交 `✨ feat(compiler): peephole NOP compaction and jump chain folding`

### Task 7: disasm + ms dump + 单测

**Files:** Create `src/compiler/disasm.c`；Modify `tools/ms.c`、`CMakeLists.txt`

- [ ] compilerDisasm：按"关键设计决策 8"格式；递归 proto；EXT 链解码注释常量/目标
- [ ] ms.c：`dump` 子命令（frontEnd → compilerCompile → compilerDisasm(stdout)；diags→stderr；退出码同 --dump-ast）；usage 行补充
- [ ] CMake：target_sources 追加 disasm.c；ms_compiler 链接 ms_parser
- [ ] 测试：disasm 确定性（同 chunk 两次输出 memcmp 相等）；`1+2.5` 输出含 "LOAD_CONST" 与 "BINARY_ADD" 行（子串断言）；func 例输出含 `; func add` 段头
- [ ] RED → GREEN → 提交 `✨ feat(compiler): bytecode disassembler and ms dump`

### Task 8: conformance `.bc` 用例 + 基线

**Files:** Modify `tools/test_runner.c`；Create `tools/gen_bc_snapshots.py`、`tests/conformance/21-bc-0{1..5}-*.ms`

- [ ] test_runner.c：`.ast` 未命中且 `.bc` 存在 → bcMode，argv 注入 `dump`（extraArgs 之后、case 之前），stdout 期望为 .bc、退出码 0
- [ ] gen_bc_snapshots.py：镜像 gen_ast_snapshots.py，调 `[ms, dump, case]`，--check 模式退出码语义一致
- [ ] 用例：`21-bc-01-arith.ms`（四则/位/比较混合 var 声明）、`21-bc-02-short-circuit.ms`（&&/||/三元）、`21-bc-03-extended-arg.ms`（300 个 var decl + 尾三元，Python 生成一次落盘）、`21-bc-04-peephole.ms`（嵌套三元×2）、`21-bc-05-doc-example.ms`（`func add(a, b) { return a + b }` + `print(add(1, 2))`，21 §4 原文）
- [ ] 生成基线：`python tools/gen_bc_snapshots.py build/Debug/ms.exe tests/conformance/21-bc-*.ms`
- [ ] `ctest --test-dir build -C Debug -R conformance_21` 全绿；`gen --check` 二次校验（DoD：两次编译 diff 为空）
- [ ] 提交 `✅ test(conformance): 21-bc bytecode snapshot baselines (5 cases)`

### Task 9: 全量验证 + 收尾

- [ ] `cmake --build build --config Debug` 零警告；`ctest --test-dir build -C Debug` 全绿（unit+fixture+conformance）
- [ ] clang-format 新增/修改 C 文件（对齐 T009 做法，单独提交 `🎨 style(compiler): clang-format T010 sources`）
- [ ] LF/UTF-8、尾随空白检查
- [ ] T010 任务文档：DoD 逐项勾选 + 状态 ✅ + 备注补充"偏移空间/启用集/基线再生成命令"；README T010 → ✅
- [ ] 提交 `📝 docs(tasks): mark T010 done`
- [ ] 用户确认后合并回 main（`git checkout main; git merge --no-ff feat/t010-instruction-compiler`）

## 自审记录

- 规格覆盖：DoD 5 项 ↔ Task 4（EXT_ARG/跳距）、Task 6（peephole）、Task 8（.bc 稳定 + §4 样例 21-bc-05）、Task 3（基础表达式单测）；§5.1 表 8 行 ↔ Task 2/3/4/6 测试；§5.2 四用例 + doc-example ↔ Task 8。✓
- 类型一致性：compilerCompile/compilerDisasm/chunkPeephole 签名各任务一致；MSE_COMPILE_UNSUPPORTED=1022 续 binder 1021。✓
- 无占位符：每任务含明确文件、行为、测试与命令。✓
