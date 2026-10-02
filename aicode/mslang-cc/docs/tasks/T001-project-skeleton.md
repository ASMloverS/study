# T001 — 项目骨架：构建体系与平台层

- 阶段: P0 骨架（roadmap 期 0）
- 依赖: 无
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/29-roadmap.md` §1-§2、`20-architecture.md` §4 §7、`28-style-c.md`

## 1. 目标与范围

建立可三平台构建的空壳工程：CMake 顶层、`common` 基础设施、`plat` 平台薄封装、`ms` 主命令（仅 `version` 子命令）、CI 工作流与格式化配置。

本任务不包含任何语言功能代码；`src/scanner` 等目录仅建占位（空编译单元），由后续任务按目录填入。

## 2. 实现要点

- 顶层 `CMakeLists.txt`：`cmake_minimum_required(3.16)`、C11 严格（`-std=c11`，禁编译器扩展）、`-Wall -Wextra -Werror` + `-Wshadow`（MSVC `/W4 /WX`）；预设 Debug / Release / ASan+UBSan（Sanitizer 仅 GCC/Clang）。
- 唯一可执行目标 `ms`（`tools/` 链接 `src/` 各模块静态库）；构建输出仅 `build/`。
- 探测 computed goto 可用性，定义 `MS_USE_COMPUTED_GOTO`（MSVC 恒否），供 T011 dispatch 双路径使用。
- `src/common/common.h`：定宽别名（`ms_u8/ms_i32/ms_u32/ms_i64/ms_f64`）、动态数组宏 `MS_ARRAY_PUSH(arr, count, cap, elem)`、`MS_ASSERT`（Release 剔除）、FNV-1a（32/64 位）声明。
- `src/plat/`：
  - `plat_thread.h/.c`：线程/互斥/条件变量的创建、加锁、等待、销毁（`<threads.h>` 不依赖，封装 OS API）
  - `plat_atomic.h`：`<stdatomic.h>` 薄封装，memory order 显式
  - `plat_time.h`：单调/墙钟时间（毫秒/纳秒）
  - `plat_io.h`：仅接口占位声明，reactor 实现归 T032
- `.clang-format`（2 空格缩进、120 建议行宽，按 28-style-c.md）、`.editorconfig`、`.gitignore`（`build/`、`__mscache__/`、`ms_packages/`）。
- CI：`.github/workflows/ci.yml` 三平台矩阵（windows/ubuntu/macos）× Debug/Release；门禁 = 构建 + ctest + clang-format `--check`。

关键接口（plat 线程封装示例）：

```c
typedef struct PlatThread PlatThread;
typedef void (*PlatThreadFn)(void* arg);
bool platThreadCreate(PlatThread** t, PlatThreadFn fn, void* arg);
void platThreadJoin(PlatThread* t);
void platThreadDestroy(PlatThread* t);

typedef struct PlatMutex PlatMutex;
void platMutexLock(PlatMutex* m);
bool platMutexTryLock(PlatMutex* m);
void platMutexUnlock(PlatMutex* m);
```

## 3. 涉及文件

新增: `CMakeLists.txt`、`cmake/CompilerWarnings.cmake`、`src/common/common.h`、`src/common/common.c`、`src/plat/plat_thread.h`、`src/plat/plat_thread.c`、`src/plat/plat_atomic.h`、`src/plat/plat_time.h`、`src/plat/plat_time.c`、`src/plat/plat_io.h`、`tools/ms.c`、`.clang-format`、`.editorconfig`、`.gitignore`、`.github/workflows/ci.yml`
修改: 无（空仓库起步）

## 4. 验收标准（DoD）

- [ ] Windows（MSVC 2022）本地 Debug/Release 构建通过、零警告
- [ ] `build/ms version` 打印版本串（如 `mslang 0.0.1 (dev)`）退出码 0
- [ ] `build/` 外无任何构建产物
- [ ] CI 三平台矩阵全绿（构建 + 空 ctest + 格式检查）
- [ ] `clang-format --dry-run --Werror` 对全部 `.c/.h` 通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_common.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 定宽别名宽度 | `sizeof` 静态断言 | ms_i64==8、ms_u32==4 等 |
| MS_ARRAY_PUSH | 追加 100 个 int | count==100、cap 单调不减、值正确 |
| FNV-1a-64 向量 | `""` / `"a"` / `"foobar"` | 0xcbf29ce484222325 / 0xaf63dc4c8601ec8c / 0x85944171f73967e8 |
| platThread 冒烟 | 起线程自增共享计数并 join | 计数正确、无泄漏（ASan 验证） |

### 5.2 ms 脚本用例

无（尚无语言实现）。冒烟门禁：`ms version` 退出码 0。

## 6. 风险与备注

- MSVC 无 computed goto：本任务起在编译脚本预留探测开关，避免 T011 dispatch 返工。
- `plat/` 保持最小面：只封装线程/原子/时间/IO 多路复用四类，不引入其他抽象。
- AGENTS.md 约束：构建产物仅 `build/`、唯一可执行 `ms`、每次构建原地覆盖。
