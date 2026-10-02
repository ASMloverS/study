# T003 — Token 定义与诊断基础设施

- 阶段: P1 词法（roadmap 期 1）
- 依赖: T002
- 规模: S
- 状态: 未开始
- 设计文档: `docs/language/01-lexical.md` §2 §4 §8、`20-architecture.md` §1（诊断）

## 1. 目标与范围

定义 Token 类型与关键字表、诊断结构 `MsDiagnostic` 与诊断收集器，为 Scanner/Parser/Binder 共用的前端基座。本任务不切分任何源码，仅交付数据结构与格式化。

不做：诊断的语义判断（各阶段自行产生诊断）。

## 2. 实现要点

- `MsTokenType` 枚举：37 保留字单列（`func var const if else for while switch case default break continue return class trait this true false nil and or not in is try catch finally throw defer go chan async await import from as yield`）+ 标识符 + 字面量（INT/FLOAT/BIGINT/STRING/RUNE，插值字符串在 T005 细分为段）+ 全部运算符标点（01 §4 表，含 `:= -> <- ** **=` 等）+ `EOF`。
- `struct/instance/package/type` 等不是关键字，按普通标识符处理。
- Token 结构（01 §8）：

```c
typedef struct MsToken {
  MsTokenType type;
  const char* start;  // 源内指针（源缓冲由调用方持有）
  int32_t    len;     // 字节长
  const char* file;   // 文件路径（intern）
  int32_t line, col;  // 1-based
  int32_t offset;     // 字节偏移
} MsToken;
```

- 诊断（20 §1）：

```c
typedef struct MsDiagnostic {
  MsDiagLevel level;  // ERROR/WARNING
  uint32_t    code;   // MSExxxx
  const char* file;
  int32_t line, col, len;
  const char* message;
} MsDiagnostic;

typedef struct MsDiagSink {
  MsDiagnostic items[MS_DIAG_MAX];  // 上限 20，超出仅计数
  int32_t count, overflow;
} MsDiagSink;
```

- 格式化：`file:line:col: error[MSExxxx]: message` + 源码行 + `^` 指示（`^` 长度 = len，最小 1）。
- 错误码段规划：01 词法 MSE0xxx、03/04 语法 MSE1xxx、02 类型/运行时 MSE2xxx，后续任务沿用。

## 3. 涉及文件

新增: `src/scanner/token.h`、`src/scanner/token.c`（关键字查找表 + 诊断格式化）、`tests/unit/test_token_diag.c`
修改: `CMakeLists.txt`（scanner 模块纳入构建）

## 4. 验收标准（DoD）

- [ ] 关键字表恰好 37 项，大小写敏感（`Func` 是标识符）
- [ ] `msDiagFormat` 输出格式与设计文档一致（含 `^` 指示对齐）
- [ ] 诊断超 20 条后停止收集但 overflow 计数正确
- [ ] 单测全绿；无内存分配依赖（结构均为借用指针，便于前端零分配）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_token_diag.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 关键字查找 | "func"/"trait"/"await" | 命中对应类型 |
| 非关键字 | "type"/"struct"/"Func"/"变量" | 均为 TK_ID |
| 诊断格式 | line=2,col=5,len=3 | `f.ms:2:5: error[MSE0001]: msg` + 源行 + `^^^` |
| 诊断上限 | 灌 25 条 | count==20、overflow==5 |
| 多字节列号 | `中` 占 3 字节 | col 按 rune 计 1 列（文档以 1-based rune 列） |

### 5.2 ms 用例

无独立 `.ms` 用例；本任务产物由 T004/T005 的词法 conformance 覆盖（错误格式用例落在 `tests/conformance/01-lex-err-*.ms`，期望 stderr 前缀匹配）。

## 6. 风险与备注

- 列号语义需定死：按 rune 列（非字节列），诊断对齐 UTF-8 安全；本任务测试先行锁定。
- Token 借用源缓冲，扫描器保证源缓冲生命周期覆盖整个编译期（`.msc` 命中路径不经过前端，无冲突）。
