# T004 — Scanner 核心：标识符/数字/运算符/ASI

- 阶段: P1 词法（roadmap 期 1）
- 依赖: T003
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/01-lexical.md` §1 §3.1 §3.2 §4 §6 §7 §9

## 1. 目标与范围

实现 Scanner 主循环：标识符、37 关键字、整数（十进制/十六/八/二进制/下划线分组/`n` 后缀）、浮点、运算符与标点、注释、换行归一化与 ASI 规则。字符串/rune/插值归 T005。

出口：01 文档中非字符串类词法用例全覆盖（roadmap 期 1 门禁的主体）。

## 2. 实现要点

- 扫描器结构（逐 token 产出，供 parser 拉取）：

```c
typedef struct Scanner {
  const char* src;    // 已 LF 归一化的 UTF-8 源
  int32_t len, pos;
  int32_t line, col;  // rune 列
  int32_t parenDepth; // ()[]{} 合计深度，>0 抑制 ASI
  const char* file;
  MsDiagSink* diags;
} Scanner;
bool scannerNext(Scanner* s, MsToken* out);
```

- 标识符：`ID_START = Unicode 字母 | '_'`，`ID_CONTINUE = ID_START | Unicode 数字`；内部维护简易 Unicode 类别判定（ASCII 快路径 + 查表，字符属性表生成脚本另附 `tools/gen_unicode_table.py`，仅覆盖 L* 与 N* 类）。
- 整数：`123`、`1_000_000`、`0x/0X`、`0o/0O`、`0b`、后缀 `n`；64 位范围暂存 `int64 + isBig` 标志（溢出精确值由 T016 接管，扫描阶段记录溢出诊断豁免：溢出 token 仍产出，标记 bigint）。`123abc`、`0x1F` 后跟标识符字符 → MSE0xxx。
- 浮点：必须整数部分与前导数字（`.5`、`5.` 非法）；`1e9 1E-3 2.5e+10 1_000.5`；`1e`/`1e+` 非法。
- 运算符最长匹配：`**=` > `**` > `<<=` `>>=` `<<` `>>` `<=` `>=` `==` `!=` `&&` `||` `:=` `->` `<-` `+=` 等；`~` 一元位反。
- 注释：`//` 至行尾，产出注释内文档标记（`///` 开头注释记 flag 供 `ms doc`，本任务仅透出行信息）。
- ASI（01 §7）：换行前最后 token 属于 { 标识符 | 字面量 | `break continue return throw defer yield` | `true false nil this` | `) ] }` } 时自动补 `;` token。细则：
  1. `parenDepth > 0`（三种括号合计）抑制
  2. 插值字符串内部换行不产生（T005 落实）
  3. 行尾是 `<-` 抑制（等待下一行补全）
  4. `return`/`throw` 后换行立即断句
- 换行归一化：构造时 `\r\n`/`\r` → `\n`（读取层完成，扫描器只见 `\n`）。

## 3. 涉及文件

新增: `src/scanner/scanner.h`、`src/scanner/scanner.c`、`src/scanner/unicode_table.h`（生成物）、`tools/gen_unicode_table.py`、`tests/unit/test_scanner_core.c`
修改: `CMakeLists.txt`（生成脚本纳入自定义目标）

## 4. 验收标准（DoD）

- [ ] 01 §3.1/§3.2/§4/§7 全部示例 token 化正确（conformance 全绿）
- [ ] 37 关键字与全部运算符的最大匹配无歧义
- [ ] ASI 四细则各有正反用例通过
- [ ] 非法数字（`123abc`、`1e`、`.5`、`5.`）产生 MSE0xxx 诊断
- [ ] `parenDepth` 对混合嵌套 `([{ }])` 抑制正确

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_scanner_core.c`

| 用例 | 输入 | 期望 token 序列（摘要） |
|------|------|------|
| 下划线整数 | `1_000_000` | INT 1000000 |
| 进制 | `0x1F 0o17 0b101` | INT 31/15/5 |
| bigint 后缀 | `123n 0xFFn` | BIGINT |
| 浮点形态 | `3.14 1e9 1E-3 2.5e+10 1_000.5` | FLOAT ×5 |
| 最大匹配 | `a **= b` | ID `**=` ID |
| 多符号 | `x := y <- ch` | ID `:=` ID `<-` ID |
| ASI 基础 | `a\nb` | ID `;` ID |
| ASI 抑制(1) | `f(\na\n)` | 无 `;` |
| ASI 抑制(3) | `ch <-\n1` | 无 `;`（`<-` 行尾抑制） |
| ASI(4) | `return\nx` | `return` `;` ID `;` |
| 注释 | `// c\n1` | INT（注释跳过） |
| Unicode ID | `变量 = 1` | ID `=` INT |

### 5.2 ms conformance 用例 `tests/conformance/01-lex-*`

以"源码 + 期望 token 流 dump"形式（临时 `ms` 调试开关 `--dump-tokens` 输出；T004 实现）：`01-lex-01-int-forms.ms`、`01-lex-02-float-forms.ms`、`01-lex-04-operators.ms`、`01-lex-07-asi-rules.ms`、`01-lex-09-numeric-ambiguity.ms`（本用例含非法输入，期望 stderr 匹配 `error[MSE0`）。

## 6. 风险与备注

- `<-` 抑制 ASI 需前瞻：行尾 token 为 `<-` 时不发 `;`，下一行继续（与细则 3 对应：`ch <-` 跨行发送 / `<-ch` 跨行接收均合法）。
- 溢出 int 字面量（如 `9223372036854775808`）：本任务产 BIGINT token 但值存不下 —— 先发 MSE0xxx 暂用近似值占位，T016 bigint 落地后改为精确解析（测试同步更新，已在 DoD 标注）。
