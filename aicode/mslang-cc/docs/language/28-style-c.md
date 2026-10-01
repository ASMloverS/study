# 28 C 编码规范

以 Google C++ Style Guide 为基础（继承：2 空格缩进、K&R、头保护宏、自包含头、include 顺序），命名按 Java Guide 映射到 C；显式覆盖项见 §12 偏离清单。`.clang-format` 固化，CI 校验。

## 1. 命名总表

| 实体 | 风格 | 示例 |
|------|------|------|
| 文件 | lower_snake_case | `gc_young.c`、`obj_string.c` |
| 类型（struct/enum/typedef） | UpperCamelCase；公共 API 加 `Ms` 前缀 | `MsValue`、`Chunk`、`SchedWorker` |
| 函数 | lowerCamelCase + 模块前缀；公共 API `ms` 前缀 | `msNewVM`、`gcCollectYoung` |
| 参数 | lowerCamelCase | `heap`、`grayCount` |
| bool 谓词 | `is/has` 开头，模块/`ms` 前缀紧邻其后 | `msIsNil`、`isValueNil`、`hasFlag` |
| 局部变量 | lowerCamelCase | `grayCount` |
| 全局变量（慎用） | `g` 前缀 lowerCamelCase | `gVmCount` |
| 常量/枚举值/宏 | SCREAMING_SNAKE_CASE | `MS_VAL_NIL`、`GC_MAX_AGE`、`MS_ARRAY_PUSH` |
| 头保护 | `INCLUDE_MS_<路径>_H_` | `INCLUDE_MS_VALUE_H_` |
| 静态函数（内部链接） | 同函数风格，可省模块前缀 | `static bool scanFrame(...)` |

## 2. 文件

- `.h` 声明，`.c` 实现；头文件自包含（self-contained：单独 include 可编译）。
- 目录即模块：`src/gc/`、`src/sched/`；内部共享头 `src/<mod>/impl.h`（不装）。
- 一 `.c` 一主要职责；> 1500 行考虑拆分。
- include 顺序：对应头 → C 标准库 → 平台头（`plat/`）→ 项目其他模块 → 公共 `ms.h`（仅在 API/stdlib 桥接文件）。

## 3. 格式

- **缩进 2 空格**（禁 Tab）；行宽建议 120（硬限 150），注释/字符串字面量同限。
- K&R 大括号；单行函数体仍需大括号（`if (x) { return 1; }` 允许单行）。
- 指针星号靠类型：`MsValue* v`。
- 返回类型与函数名同行；长参数列表断行对齐。

## 4. 类型与内存

- C11；`<stdint.h>` 定宽类型（`uint8_t/int64_t`）；别名集中 `common.h`（`ms_u8/ms_i64`）。
- 禁 VLA（变长数组）；FAM（柔性数组成员）允许（字符串/闭包等，`22-object-model.md`）。
- 动态数组统一宏 `MS_ARRAY_PUSH(arr, count, cap, elem)`。
- 资源配对：`xxxInit/xxxDestroy`、`xxxNew/xxxFree`；谁分配谁释放（GC 堆除外）。
- 初始化：声明即初始化或立即赋值；禁读未初始化。

## 5. 函数

- 函数 ≤ 80 行建议；参数 ≤ 6 建议。
- 第一参数为"self"（对象式 C）：`bool isValueNil(MsValue v)`、`void gcCollectYoung(GcHeap* h)`。
- `static` 优先；非 static 需头文件声明。
- 禁 `goto`，**唯一例外**：错误清理跳转 `goto cleanup`（集中单出口模式）。

## 6. 错误处理

- 返回值优先：`MS_OK/MS_ERROR/MS_ABORT`（`ms.h` 公共）；内部 bool/int 状态码。
- 前端诊断：填 `MsDiagnostic`，不打印。
- 运行时错误：置 VM 错误槽（`25-capi.md` §5），不 longjmp（VM 主循环统一 unwind）。
- 断言：`MS_ASSERT` 仅内部不变量（Release 编译剔除）；用户输入校验不用断言。

## 7. 并发

- 共享可变状态必须注明保护（哪个锁 / 无锁算法依据）。
- 原子操作用 `<stdatomic.h>` 封装 `plat/atomic.h`（memory order 显式；缺省 seq_cst，优化处注释）。
- 禁自旋 > 1000 次（必退让）；锁内禁调用可能重入同锁的函数。

## 8. 宏

- 仅用于：编译期开关、断言、代码生成（MS_ARRAY_PUSH）、常量。
- 多语句宏 `do { } while (0)` 包裹；禁小写宏名。

## 9. 注释

- **仅 `//`；`/* */` 一律禁用**（含文件头横幅、数据表、临时注释）。
- **why 而非 what**；公共 API 注释进 `.h`（纯 `//`，格式由 ms doc 管线另行约定）。
- GC/调度器等算法处引用设计文档：`// 见 docs/language/23-gc.md §4`。
- 多行注释逐行 `//`，与代码同缩进；禁尾注释块遮蔽结构（对齐适度）。

## 10. 测试与构建

- 每模块单测 `tests/unit/test_<mod>_<topic>.c`；`.ms` 端到端放 `tests/fixtures/` + conformance。
- 构建：CMake ≥ 3.16；预设 Debug/Release/ASan/UBSan；`-Wall -Wextra -Werror`（MSVC `/W4 /WX`）。
- 基准：`benchmarks/`（`29-roadmap.md` §5），回归门禁 CI 执行。

## 11. 禁止清单

- 变量 shadow（`-Wshadow` 开启）。
- 隐式转换警告放行（`-Wconversion` 目标清零）。
- 手写 memcpy 可重叠（用 memmove）。
- 线程局部存储滥用（仅调度器 per-worker 指针）。
- `#pragma once`（用头保护宏，跨编译器保守）。
- 块注释 `/* */`（仅 `//`，见 §9）。

## 12. 对 Google C++ Guide 的偏离

- 函数/变量 lowerCamelCase（Google C++ 为 lower_snake；采 Java 方法命名）。
- 行宽建议 120/硬限 150（Google 80）。
- 注释仅 `//`（Google 允许 `/* */`）。
- 定宽整数优先 `uint8_t/int64_t`（Google 倾向 `int`，按需定宽）。
