# T016 — bigint 与数值塔

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T012
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/02-types.md` §3、`22-object-model.md` §5、`10-builtins.md` §1（int/bigint/float 转换）

## 1. 目标与范围

实现 MsObjBigInt（uint32 limb 数组 + 符号）、朴素四则/比较/幂、Karatsuba 乘法（位宽 > 4096）、toString(base)/解析、int64 溢出自动提升（BINARY_* 快路径接通 `__builtin_add_overflow` / MSVC `_add_overflow_i64` 等价物）、混算规则表（02 §3.4）、除法取模 Go 语义、位运算（int/bigint 域）。

不做：bigint 位运算的完整语义优化（实现正确即可，性能基准归 T044）。

## 2. 实现要点

- 表示（22 §5）：`limbs[]` 小端序、`sign ∈ {-1, 0, +1}`、规范化（去高位零 limb）。
- 运算：加减（带符号加减）、乘（朴素 O(n²) + Karatsuba 阈值 4096 bit）、除/模（长除法，Knuth D）；`5n / 2n == 2n` 截断（02 §3.2）；`-7 / 2 == -3`、模符随被除数（02 §3.5 Go 语义，int 与 bigint 一致）。
- int↔bigint：`__builtin_add_overflow`（MSVC 用 `_AddOverflow`/内联检测）快路径在 BINARY_ADD/SUB/MUL 内联；溢出转 bigint 对象路径（21 §3.2）。
- 混算（02 §3.4 表）：int⊗bigint→bigint；含 float→float（bigint 转 double 可能丢精度不报错，02 §3.3）；位运算仅 int/bigint，float 报 TypeError。
- 比较：数值塔 `1 == 1n == 1.0`；hash 统一（与 T015 数值塔键一致——本任务提供 `msHashNumeric` 公共实现回填）。
- 转换（10 §1）：`int(x[, base])`（float 截断、string 按 base、rune 原值、失败 ValueError）、`bigint(x)`（float 须整）、`float(x)`、`abs` 保类型、`i.toString([base])`、`b.pow(n)`、`b.toString([base])`（base 2/8/10/16，10-builtins §4）。
- 词法衔接：T004 的 BIGINT token 溢出占位改为精确解析（扫描器调用 bigint 解析器）。

## 3. 涉及文件

新增: `src/obj/obj_bigint.c/.h`、`tests/unit/test_obj_bigint.c`、`tests/fixtures/numeric/*.ms`
修改: `src/vm/vm.c`（BINARY_*/COMPARE_* 数值路径统一入口）、`src/scanner/scanner.c`（精确 BIGINT 解析）、`src/gc/gc.c`（limbs 子分配释放）、`tests/fixtures/vm-basic/arith.ms`（激活原"溢出临时报错"注释用例）

## 4. 验收标准（DoD）

- [ ] 02 §3 全部规则用例通过（混算表、除模符号、位运算域）
- [ ] `9223372036854775807 + 1` → bigint（`print` 输出 `9223372036854775808`）
- [ ] 阶乘 100、斐波那契 500 精确值正确
- [ ] Karatsuba 与朴素乘在阈值两侧结果一致（随机 200 组对照）
- [ ] `int("ff", 16)`/`bigint("1e3"?)`（非法）/`float("1.5")` 转换族全过
- [ ] toString base 2/8/16 正确回解析

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_obj_bigint.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 解析/打印 | "0"/"1"/"-1"/大数 | 往返一致 |
| 加减进位 | 随机 2048 位 ± | 与十进制参考（测试内软参考实现）一致 |
| 乘法 | 阈值上下对照 | 朴素与 Karatsuba 结果相等 |
| 除/模符号 | `-7n / 2n`、`-7n % 2n` | `-3n`、`-1n` |
| 混算 | `1 + 1n`、`1n + 0.5` | bigint 2n、float 1.5 |
| 位运算 float | `1.5 & 1` | TypeError |
| 溢出提升 | i64 max +1、min -1、× | bigint |
| toString base | 255 → 2/8/16 | 11111111/377/ff |
| hash 一致 | hash(1)/hash(1n)/hash(1.0) | 三者相等 |

### 5.2 ms fixtures `tests/fixtures/numeric/`

- `overflow_promote.ms`：

```ms
var big = 9223372036854775807 + 1
print(type(big))            // bigint
print(big)                  // 9223372036854775808
print(9223372036854775807n * 2n + 1)
```

- `factorial.ms`：`func fact(n)` 循环乘到 100，输出 `93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000`
- `mixed.ms`：混算表逐格输出（golden）
- `divmod.ms`：`-7 / 2` → `-3`、`-7 % 2` → `-1`、`7 / -2` → `-3`（Go 语义表）

## 6. 风险与备注

- 测试参考实现：单测内嵌第二套独立朴素实现做 oracle（开发期防同源错误），保留于代码库供回归。
- MSVC 溢出内建：`_add_overflow_i64` 等 MSVC 2022 提供（`<intrin.h>`），plat 层封装 `msAddOverflowI64` 等三件套。
