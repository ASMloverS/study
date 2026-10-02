# T042 — ms pkg 与 ms doc

- 阶段: P12 发布（roadmap 期 12）
- 依赖: T009、T041
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/27-toolchain.md` §6 §7、`09-modules.md` §4（ms_packages 解析位）

## 1. 目标与范围

实现 `ms pkg`（install/update/list，基于 `ms.pkg` TOML 子集清单与 `ms.lock` 锁定，git/path 两类依赖源，无中央 registry）与 `ms doc`（`///` 文档注释抽取：终端分页查看 + `--html` 静态生成）。

不做：semver 传递求解（27 §6 ref 固定）、registry、doc 的搜索索引。

## 2. 实现要点

- ms.pkg 解析（27 §6）：TOML 子集——顶层 `name/version` 字符串 + `[deps]` 表（`"name" = { git = URL, ref = "v0.2.1" }` 或 `{ path = "../x" }`）；自研极简 TOML 解析（仅覆盖此形态，语法越界报错）。
- install：git 依赖 clone/checkout 至 `ms_packages/<name>@<ref>/`（git 子进程调用，未装 git 报用法错）；path 依赖记录绝对路径；生成/更新 `ms.lock`（精确 ref + 内容 hash——FNV-1a 目录聚合）。
- update：`ms pkg update <name>` 按 ms.pkg ref 重新拉取；list：清单+锁对照输出。
- 解析位接线（09 §4）：模块查找第 4 位 `ms_packages/` 已在 T024 预留——本任务接 `name → ms_packages/<name>@<ref>/` 映射（读 ms.lock）。
- ms doc（27 §7）：抽取规则——顶层连续 `///` 行紧随其后（无空行）的声明（11 §4）；解析 `@param/@return` 标签。
- 终端形态：`ms doc strutil` 分页输出模块文档（函数签名 + 摘要 + 标签）；`ms doc strutil.join` 符号级。
- `--html -o docs/`：索引页 + 每模块页（godoc 式简洁版式：包名/函数表/详情）；静态无 JS。

## 3. 涉及文件

新增: `tools/pkg.c`、`tools/doc.c`、`src/parser/doc_extract.c/.h`、`tests/unit/test_pkg_manifest.c`、`tests/unit/test_doc_extract.c`、`tests/fixtures/pkgproj/**`（含 ms.pkg 的示例工程）、`tests/fixtures/doclib/*.ms`（含 /// 的文档示例库）
修改: `tools/ms.c`、`src/module/module.c`（ms_packages 映射）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 27 §6 ms.pkg 示例解析正确；非法 TOML 越界语法报错
- [ ] install：git/path 两源落位 ms_packages、ms.lock 精确 ref+hash
- [ ] import 经 ms_packages 命中依赖（端到端 fixture）
- [ ] update 重拉指定包；list 输出清单/锁对照
- [ ] `ms doc doclib` 终端输出含签名/摘要/@param/@return
- [ ] `--html` 生成索引+模块页（文件存在性与关键内容断言）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_pkg_manifest.c`、`test_doc_extract.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 清单解析 | 27 §6 原文 | name/version/deps 结构正确 |
| path 依赖 | `{ path = "../x" }` | 记录解析 |
| TOML 越界 | 数组/嵌套表 | 报错（子集外） |
| lock hash | 目录聚合 | 内容变更 hash 变 |
| /// 抽取 | 紧贴/空行隔开 | 前者抽取后者不抽取 |
| 标签 | @param/@return 顺序 | 解析为结构 |

### 5.2 fixtures `tests/fixtures/pkgproj/`

- `ms.pkg` + `local_dep/`（path 依赖）+ `main.ms`（import 依赖模块）→ `ms pkg install` 后 run 输出验证（跑器序列模式：install → run → 比对）
- `git_dep/`：本地 git 仓库 fixture（file:// 协议），CI 注入 git 环境；离线环境跳过标记
- `tests/fixtures/doclib/strutil.ms`：`ms doc` 输出快照（golden）+ `--html` 产物断言

## 6. 风险与备注

- git 依赖测试的可移植性：fixture 以本地 `git init` 临时仓库 + path URL 注入（不访问网络）；CI Windows git bash 路径差异由 plat 层 shell 调用封装吸收。
- `ms doc` 依赖 T005 词法的 `///` 标记与 T009 AST 的文档注释挂靠——本任务消费侧实现抽取数据结构。
