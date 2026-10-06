# conformance 用例约定

- 命名：`<文档号>-<主题>-<序号>-<slug>.ms`，如 `01-lex-07-asi-return.ms`，与 `docs/language/` 设计文档条目一一对应，是各阶段出口金集。
- 契约：同目录同名 `.out`（期望 stdout，UTF-8、LF）；可选 `.code`（期望退出码，缺省 0）、`.err`（期望 stderr 首行前缀）、`.timeout`（秒，缺省 10）。比对前实际与期望输出统一 LF 归一化。
- 注册：CMake 扫描 `tests/conformance/*.ms` 自动注册（CTest 标签 `conformance`，见 `cmake/Tests.cmake`），新增用例无需改构建脚本，reconfigure 自动生效。
