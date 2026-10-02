# T041 — ms build 与 .msb bundle

- 阶段: P12 发布（roadmap 期 12）
- 依赖: T025
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/27-toolchain.md` §5、`21-bytecode.md` §7、`09-modules.md` §4

## 1. 目标与范围

实现 `ms build app.ms -o dist/`：依赖闭包编译缓存落盘（`dist/__mscache__/*.msc`）与单文件 bundle `dist/app.msb`（zip：manifest.json + 入口 + 依赖源码 + 可选 .msc）；`ms run app.msb` 直接运行；bundle 内相对 import 重定向。

不做：原生可执行档打包（27 §5 v2 评估 self-contained runner）。

## 2. 实现要点

- 构建流程：从入口静态+动态(import 运行期加载特性 → 以一次"构建执行"收集依赖，import 副作用按 09 §1 正常执行)收集依赖闭包 → 全量编译 → 写 `dist/__mscache__/`。
- bundle（27 §5）：zip 格式（自研最小 zip 读写——stored + deflate 复用 zlib？零第三方约束：实现 stored（无压缩）+ 文档标注，deflate 后续可选）；`manifest.json`（入口名、依赖清单含内容 hash、编译器版本、源指纹）。
- 运行 bundle：`ms run app.msb` → 内存解包（或临时目录）→ 入口执行；**相对 import 重定向**（27 §5）：`./x` `../x` 在 bundle 命名空间内解析；MS_PATH/ms_packages/标准库位照常生效（bundle 不内嵌标准库，仅用户依赖）。
- `.msb` 双形态：含 .msc（快启动）或仅源（构建选项 `--no-cache`）；运行时按 manifest 与本机编译器版本决定用 .msc 或重编译。
- 依赖收集的动态 import 边界：运行期分支未走的 import 不入闭包——构建报告列出"未覆盖路径"告警（尽力而为，文档化）。

## 3. 涉及文件

新增: `tools/build.c`、`src/module/bundle.c/.h`（zip 读写/manifest/bundle 挂载）、`tests/unit/test_bundle.c`、`tests/fixtures/buildapp/**/*.ms`（多模块示例应用）
修改: `tools/ms.c`、`src/module/module.c`（bundle 解析根注册）

## 4. 验收标准（DoD）

- [ ] `ms build tests/fixtures/buildapp/main.ms -o dist/`：产物 `__mscache__/` + `main.msb` 结构符合 manifest
- [ ] `ms run dist/main.msb` 输出与源目录直跑一致
- [ ] bundle 内相对 import（含 `../` 上跳）重定向正确
- [ ] `--no-cache` bundle（纯源）运行正确
- [ ] 含 .msc bundle 在版本不符机器路径：弃 .msc 重编译运行
- [ ] zip 产物可被标准 unzip 工具列出（格式合法）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_bundle.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| zip 写读 | 构造文件集 | 条目与内容往返 |
| manifest | 依赖 hash | 与源内容 FNV 一致 |
| 挂载解析 | bundle 内 `./x` | 命中包内条目 |
| 版本判定 | 篡改版本字段 | 走重编译路径 |

### 5.2 fixtures `tests/fixtures/buildapp/`

- `main.ms` + `util/strutil.ms` + `util/../local.ms` 三层相对引用：build → run bundle → 输出与直跑 diff 为空（跑器双跑比对模式）
- `conditional_import.ms`：分支内 import → 构建告警行 + 实际运行分支路径正确

## 6. 风险与备注

- "构建执行"副作用边界：构建即运行一次入口收集依赖——副作用（写文件等）会真实发生；`--analyze-only` 选项提供纯静态收集（已知未覆盖告警更保守），文档化取舍。
- zip stored 无压缩的体积可接受性：bundle 场景多为小依赖集；deflate 若引入则自研（零第三方约束），v1 明确 stored。
