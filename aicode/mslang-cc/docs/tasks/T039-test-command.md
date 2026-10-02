# T039 — ms test 与 testing 模块

- 阶段: P11 工具链（roadmap 期 11）
- 依赖: T037
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/27-toolchain.md` §4、`26-stdlib.md` §3（testing 模块）

## 1. 目标与范围

实现 `testing` 模块（expect 断言句柄 + bench）与 `ms test` 命令：`*_test.ms` 发现、用例函数 `testXxx(t)`/`benchXxx(t)` 识别、并行运行（每用例独立 VM 隔离崩溃）、输出（进度点 + 摘要 + 失败详单）、`--filter/-v/--json`、退出码（0/1/2）。

不做：覆盖率、随机化顺序（v1 固定字母序）、`ms test` 基准回归阈值门禁（数据落盘在 T044 接管）。

## 2. 实现要点

- testing 模块（27 §4、26 §3）：`t.expect(actual)` 返回断言句柄——`.toBe(expected)`（同一性 ==）/`.toEqual(expected)`（深比较 == 语义）/`.toThrow(fn, Type)`；`t.bench(fn)`（计时 + 循环自适应）；`t.skip()`/`t.fail(msg)` 辅助。
- 发现（27 §4）：`ms test [dirs]` 递归找 `*_test.ms`；文件内 `func testXxx(t)` / `func benchXxx(t)` 按名字约定识别（无注册宏）。
- 运行（27 §4）：并行度 = CPU 核；**每用例独立 VM**（隔离崩溃：编译错/崩溃仅标记该文件失败）；working dir = 用例文件目录（相对 import 友好）。
- 输出：进度点（`.` 过 `F` 败）+ 摘要（文件数/用例数/耗时）+ 失败详单（文件:行 + expect diff——expected/got 对比打印）；`--filter=Xxx` 子串过滤；`-v` 逐用例名；`--json` CI 机器格式。
- 退出码（27 §4）：0 全过 / 1 失败 / 2 基准回归超阈值（本任务保留 2 的产生条件骨架：bench 结果超 `bench.threshold` 文件时置 2，阈值文件格式随 T044 对齐）。
- 断言失败异常经专用 AssertionError 子类型携带 expected/got 文本。

## 3. 涉及文件

新增: `src/stdlib/testing.c`（模块，注册于标准库位）、`tools/test_cmd.c`（发现/调度/报告）、`tests/ms_test_self/*.ms`（自测套件：用 ms test 测 testing 自身）、`examples/strutil_test.ms`（27 §4 示例入库）
修改: `tools/ms.c`、`tools/test_runner.c`（复用子进程执行框架）、`CMakeLists.txt`（ctest 增 `ms-test-self` 项）

## 4. 验收标准（DoD）

- [ ] 27 §4 strutil_test.ms 示例原样运行通过
- [ ] 断言三式（toBe/toEqual/toThrow）成功/失败路径输出正确
- [ ] 失败用例 diff 输出（expected vs got）+ 文件:行 定位
- [ ] 崩溃隔离：某用例 panic/死循环（超时）不影响其他用例与总流程
- [ ] `--filter/-v/--json` 行为正确（json 可被脚本解析）
- [ ] 退出码 0/1 语义正确；并行运行输出稳定（汇总不受调度序影响）

## 5. 测试计划

### 5.1 自测套件 `tests/ms_test_self/`

- `basic_test.ms`：

```ms
import "testing"

func testPass(t) {
    t.expect(1 + 1).toBe(2)
    t.expect([1, 2]).toEqual([1, 2])
}
func testFail(t) {
    t.expect("a").toBe("b")     // 预期失败：验证 diff 输出
}
func testThrow(t) {
    t.expect(func() { throw ValueError("x") }).toThrow(ValueError)
}
```

- `bench_test.ms`：`func benchJoin(t) { t.bench(func() { … }) }`（耗时行格式断言）
- `crash_sibling/a_test.ms`（故意运行时错误）+ `crash_sibling/b_test.ms`（正常）：验证隔离与汇总

### 5.2 C 侧验证

`tests/unit/test_ms_test_cmd.c`：目录扫描/过滤/退出码映射（进程级断言）。

## 6. 风险与备注

- 每用例独立 VM 的启动开销：v1 接受（roadmap 29 §3 期 11 无性能门）；并行度上限 8 防大核机抖动。
- `--json` 输出 schema 一经交付冻结（T044 CI 消费 + 未来工具兼容）。
