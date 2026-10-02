# T005 — Scanner 字符串与 rune 字面量

- 阶段: P1 词法（roadmap 期 1）
- 依赖: T004
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/01-lexical.md` §3.3 §3.4 §7（细则 2）

## 1. 目标与范围

实现普通字符串（转义 + `${}` 插值切分）、原始字符串（反引号）、rune 字面量。完成后词法阶段出口达成：01 文档全部词法用例覆盖。

不做：插值表达式的求值（编译期仅切分为段，求值归 T010/T017）。

## 2. 实现要点

- 普通字符串 `"..."`：扫描器将一个字符串字面量切分为交替的"常量段 + 表达式段"token 序列，用专用 token 包装：

```c
// token 流形态: STR_BEGIN (STR_PART|INTERP_BEGIN ... INTERP_END)* STR_END
// 嵌套: "${ "a${b}" }" —— INTERP 内部递归扫描普通 token，内层字符串同规则
```

  - 转义：`\n \r \t \\ \" \' \0 \xHH \u{XXXXXX}`；非法转义发 MSE0xxx。
  - 相邻字符串字面量不拼接（两个 STR_END 相邻即两个独立字面量）。
  - 插值段内部换行不产生 ASI（细则 2）；括号深度计数在 INTERP 内延续。
- 原始字符串 `` `...` ``：无转义无插值，跨行保留原样（含换行）；行内 `` ` `` 不允许（未闭合到行尾发诊断；跨行合法）。
- rune `'a'`：值为 int 码点；`'\n' '\x41' '\u{4E2D}' '中'`；空 `''` 或多字符 `'ab'` 非法。
- `\u{XXXXXX}` 上界 0x10FFFF，非法码点/代理区段发诊断。
- 扫描器需处理字符串内的多字节字符对列号的影响（rune 列）。

## 3. 涉及文件

新增: `tests/unit/test_scanner_string.c`
修改: `src/scanner/scanner.c`（字符串/rune 状态机）、`src/scanner/token.h`（STR_PART/INTERP_BEGIN/INTERP_END/RAW_STR/RUNE token）

## 4. 验收标准（DoD）

- [ ] 01 §3.3/§3.4 全部示例通过
- [ ] 插值嵌套（`"${"inner"}"` 与三层嵌套）token 流正确
- [ ] 原始字符串跨行内容字节级保留
- [ ] rune 各形态值正确（`'a'`→97、`'\u{4E2D}'`→20013）
- [ ] 非法转义/未闭合/空 rune 均有 MSE0xxx 诊断
- [ ] conformance `01-lex-*` 全绿（roadmap 期 1 出口：词法单测全过）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_scanner_string.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 转义全表 | `"\n\r\t\\\"\'\0\x41\u{4E2D}"` | 常量段字节序列正确 |
| 插值切分 | `"a = ${a}, sum = ${a + b}"` | STR_BEGIN, STR_PART("a = "), INTERP(a), STR_PART(", sum = "), INTERP(a+b), STR_END |
| 嵌套插值 | `"${"in${x}"}"` | INTERP 内含完整内层字符串序列 |
| 原始串 | `` `a\t${x}\n` `` 反引号包裹 | 单 RAW_STR，内容原样 `\t${x}` |
| 跨行原始串 | `` `a\nb` ``（真实换行） | RAW_STR 含 `\n` |
| rune | `'a' '\n' '\x41' '\u{4E2D}' '中'` | INT token 值 97/10/65/20013/20013 |
| 非法 | `''` `'ab'` `"\q"` `"abc` | 各发 MSE0xxx |
| 插值换行 | `"${1 +\n2}"` | INTERP 内换行无 `;` |
| 相邻不拼接 | `"a" "b"` | 两组独立字符串 token |

### 5.2 ms conformance 用例 `tests/conformance/`

- `01-lex-03-string-escapes.ms`：token dump 校验转义段
- `01-lex-03b-string-interp.ms`：token dump 校验插值切分（含嵌套）
- `01-lex-05-raw-string.ms`：token dump 校验原始串
- `01-lex-06-rune.ms`：token dump 校验 rune 值
- `01-lex-err-*.ms`（3 例）：非法输入，stderr 匹配 `error[MSE0`，退出码 3 预期（编译错误码，27 §10 —— 词法阶段即编译错）

## 6. 风险与备注

- 插值表达式内注释、ASI 抑制的组合是易错点：INTERP 域内按"括号深度 + 换行"常规规则，但整个字符串 token 化期间不发 `;`，仅在 INTERP 内部子表达式正常处理。
- 转义 `\u{}` 大括号内允许多位十六进制（1-6 位），按 rune 计 1 列。
- 词法完成后 `--dump-tokens` 调试开关保留，供 T038 fmt 与排障复用。
