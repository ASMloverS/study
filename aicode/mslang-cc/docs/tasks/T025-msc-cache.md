# T025 — .msc 字节码缓存

- 阶段: P6 异常/模块（roadmap 期 6）
- 依赖: T024
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/21-bytecode.md` §7、`09-modules.md` §6、`20-architecture.md` §1

## 1. 目标与范围

实现 chunk 序列化与 `__mscache__/*.msc` 缓存：格式（magic "MSC1" + 编译器版本 + 源 FNV-1a 指纹 + 常量池递归编码 + code + 行号表 RLE + IC 槽数 + upvalue 描述）、命中跳过前端、指纹不符静默重编译、MS_CACHE_DIR 覆盖。出口：roadmap 期 6 后半——二次运行缓存命中率 > 90%。

不做：跨版本兼容（版本不符即弃用）、bundle 内缓存（T041）。

## 2. 实现要点

- 序列化（21 §7）：常量池按类型标记递归编码（nil/bool/i64/f64/bigint(进制串)/string(UTF-8)/函数原型(嵌套 chunk：code+行号+upvalue 描述+IC 槽数+参数元数据)/类原型）；行号表 RLE；端序固定小端。
- 指纹：源内容 FNV-1a-64 + 编译器版本 u32（21 §7）；写入 header。
- 加载：校验 magic/版本/指纹，不符弃用重编译（无诊断噪音）；字符串常量加载后重新 intern（跨进程地址无关，21 §7）。
- 写入时机：模块编译成功后写 `__mscache__/<模块指纹名>.msc`（入口脚本同目录；MS_CACHE_DIR 覆盖）；写失败（只读目录等）静默降级。
- 命中路径：`module` 加载器编译前查缓存（09 §6：.msc 是缓存替身，查找仍只拼 .ms，缓存命中前必须源文件存在且指纹一致）。
- IC 槽数：反序列化仅重建空槽表（IC 是运行时态，不缓存内容，21 §5 语义透明）。
- `runtime.importGraph()`（09 §5）依赖本任务的模块加载记账——本任务在加载器埋点，runtime 模块归 T036 消费。

## 3. 涉及文件

新增: `src/compiler/serialize.c/.h`（编码/解码）、`tests/unit/test_msc_serialize.c`、`tests/fixtures/cache/*.ms`
修改: `src/module/module.c`（缓存查询/写入）、`tools/test_runner.c`（fixture 临时目录的缓存行为控制：默认关闭缓存跑 + 专项缓存用例开启）

## 4. 验收标准（DoD）

- [ ] 序列化往返：任意 chunk roundtrip 后反汇编 diff 为空（`.bc` 对照）
- [ ] 命中路径：二次运行跳过前端（`--trace` 无编译期输出/加载计数断言）
- [ ] 指纹不符（改源一字节）：静默重编译，行为正确
- [ ] 版本不符：弃用重编译
- [ ] 删除缓存目录行为不变（09 §6：缓存零语义影响）
- [ ] 多模块程序二次运行命中率 > 90%（≥10 模块 fixture 统计）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_msc_serialize.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| roundtrip 基础 | 含字面量/嵌套函数 chunk | code/consts/行号全等 |
| bigint 常量 | `123n` | 精度保持 |
| 嵌套原型 | 三层闭包 | upvalue 描述保持 |
| 损坏输入 | 截断/坏 magic/坏指纹 | 拒绝并报"弃用重编译"路径 |
| RLE 行号 | 跨多行 chunk | 表往返一致 |
| intern 重挂 | 命中后常量 string | 与 intern 表指针相等 |

### 5.2 ms fixtures `tests/fixtures/cache/`

- `hit_rate/main.ms` + `m1..m10.ms`（各含函数与类）：跑器模式 = 两次运行，第二次统计 `__mscache__` 命中数（`MS_VM_STATS` 或专用计数开关输出），断言 ≥10 模块中 ≥9 命中
- `stale.ms`：首跑后修改源文件时间戳+内容（跑器钩子），二跑输出新结果
- `readonly_dir.ms`：缓存目录只读 → 程序照常输出（`.code`=0）

## 6. 风险与备注

- 序列化版本字段即编译器版本（29 §7：编译器版本独立，`.msc` 指纹组成部分）——每次改动指令编码必须 bump，CI 加断言（单测比对 `MS_COMPILER_VERSION` 与序列化产物 header）。
- fixture 默认禁用缓存（确定性），缓存专项用例以环境变量显式开启——避免 T024 多文件用例受缓存污染。
