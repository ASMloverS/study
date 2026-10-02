# T002 — 测试基础设施：单测框架与 golden 跑器

- 阶段: P0 骨架（roadmap 期 0）
- 依赖: T001
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/28-style-c.md` §10、`29-roadmap.md` §2 §3

## 1. 目标与范围

建立全项目测试基座：C 单测框架（CTest + 自写断言宏）、`.ms` golden fixture 跑器、conformance 目录约定。本任务交付跑器与冒烟样例；后续所有任务按约定追加用例，不改框架。

不做：并行执行调度、覆盖率统计、基准框架（基准归 T044）。

## 2. 实现要点

- `tests/unit/test_harness.h` 断言宏族，失败打印 `file:line: <expr> expected X got Y` 并计数；单文件多用例注册，返回非零表示失败：

```c
typedef struct TestCtx { int failures; } TestCtx;
#define TEST(name) static void name(TestCtx* ctx)
#define EXPECT(ctx, cond) /* 记录失败并继续 */
#define EXPECT_EQ(ctx, a, b)     /* 整型/指针比较 */
#define EXPECT_F64_EQ(ctx, a, b) /* 容差 1e-9 */
#define EXPECT_STREQ(ctx, a, b)  /* C 字符串 */
```

- CMake 函数 `ms_add_test(<name>)`：把 `tests/unit/test_<name>.c` 编为独立可执行并 `add_test`；打标签 `unit`。
- golden 跑器 `tools/test_runner.c`（宿主 C 程序）：
  - fixtures 约定：`tests/fixtures/<topic>/<case>.ms` + 同名 `.out`（期望 stdout，UTF-8）；可选 `.code`（期望退出码，缺省 0）、`.err`（期望 stderr 首行前缀）、`.timeout`（秒，缺省 10）。
  - 运行：为每个用例拉起 `ms <case>.ms` 子进程，捕获 stdout/退出码比对；输出 `PASS/FAIL <case>` 与失败 diff，汇总非零退出。
  - 比对前将实际与期望输出统一 LF 归一化（防 CRLF 误报）。
- 目录自动注册：`cmake/Tests.cmake` 扫描 `tests/fixtures` 与 `tests/conformance` 生成 CTest 项（标签 `fixture` / `conformance`），新增用例无需改 CMake，reconfigure 自动生效。
- conformance 命名：`<文档号>-<主题>-<序号>-<slug>.ms`，如 `01-lex-07-asi-return.ms`，与设计文档条目一一对应，是各阶段出口金集。

## 3. 涉及文件

新增: `tests/unit/test_harness.h`、`tests/unit/test_smoke.c`、`tests/unit/test_runner_selftest.c`、`tools/test_runner.c`、`cmake/Tests.cmake`、`tests/fixtures/smoke/hello.ms`（占位）、`tests/fixtures/smoke/hello.out`、`tests/conformance/README.md`（命名约定说明）
修改: `CMakeLists.txt`（引入 Tests.cmake）、`.github/workflows/ci.yml`（ctest 全标签）

## 4. 验收标准（DoD）

- [ ] `ctest -L unit` 通过（smoke + 跑器自测）
- [ ] golden 跑器对"期望不符"能正确报 FAIL 并输出 diff（用临时失败用例验证后删除）
- [ ] 新增 `.ms` + `.out` 用例无需改 CMake 即被注册执行
- [ ] 子进程超时保护生效（临时死循环用例 10s 被杀并判 FAIL）
- [ ] CI 运行 `ctest --output-on-failure` 全绿

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_smoke.c`、`tests/unit/test_runner_selftest.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 断言宏正常路径 | `EXPECT(ctx, 1 == 1)` | failures==0 |
| 断言宏失败路径 | `EXPECT_EQ(ctx, 1, 2)` | failures==1，消息含 file:line |
| LF 归一化 | 期望 `a\nb` vs 实际 `a\r\nb` | 判 PASS |
| diff 输出 | 构造差异串 | 首个差异行被标出 |

### 5.2 golden 冒烟用例 `tests/fixtures/smoke/`

`hello.ms` 本阶段内容为注释占位（期望空输出）；T011 完成后升级为：

```ms
print("hello")
```

期望 `hello.out`：`hello\n`。

## 6. 风险与备注

- 跑器与被测 `ms` 之间以子进程隔离，用例崩溃不拖垮整组（与 27-toolchain §4 的测试隔离哲学一致）。
- Windows 下子进程 stdout 管道编码统一二进制读出后按 UTF-8 处理。
