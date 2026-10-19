# conformance 用例约定

- 命名：`<文档号>-<主题>-<序号>-<slug>.ms`，如 `01-lex-07-asi-return.ms`，与 `docs/language/` 设计文档条目一一对应，是各阶段出口金集。
- 契约：同目录同名 `.out`（期望 stdout，UTF-8、LF）；可选 `.code`（期望退出码，缺省 0）、`.err`（期望 stderr 子串：取 `.err` 首行，在 stderr 任意位置命中即通过）、`.timeout`（秒，缺省 10）、`.args`（空白分隔参数，插在 ms 与脚本之间，如 `--dump-tokens`）。比对前实际与期望输出统一 LF 归一化。
- 快照（T009）：同目录同名 `.ast` 存在时跑器自动以 `--dump-ast` 执行并比对 stdout（`.out` 可缺省，退出码须 0）。基线只能由 `tools/gen_ast_snapshots.py` 生成/再生成，禁止手改（任务书 §6）。
- 快照（T010）：同目录同名 `.bc` 存在时（且无 `.ast`）跑器自动以 `ms dump` 子命令执行并比对 stdout（`.out` 可缺省，退出码须 0）。基线只能由 `tools/gen_bc_snapshots.py` 生成/再生成，禁止手改。
- 注册：CMake 扫描 `tests/conformance/*.ms` 自动注册（CTest 标签 `conformance`，见 `cmake/Tests.cmake`），新增用例无需改构建脚本，reconfigure 自动生效。
