# 01 词法

源文件编码：UTF-8（无 BOM）。换行统一 `\n`（读取时 `\r\n` 归一化）。

## 1. 标识符

```
ID_START    = Unicode 字母 | '_'
ID_CONTINUE = ID_START | Unicode 数字
```

- 允许非 ASCII 标识符（如 `变量`），但编码规范建议 ASCII（见 `11-style-ms.md`）。
- 大小写敏感。

## 2. 关键字（保留字，共 37）

```
func  var  const  if  else  for  while  switch  case  default
break  continue  return  class  trait  this  true  false  nil
and  or  not  in  is
try  catch  finally  throw  defer
go  chan  async  await
import  from  as  yield
```

说明：

- `and / or / not` 与 `&& / || / !` 是同一运算符的两种拼写，格式化器归一为 `&& / || / !`。
- `trait` 声明 mixin（见 `06-classes.md`），与 `class` 可互作基类清单成员。
- 不设 `new`：实例化直接 `Point(1, 2)`。
- `struct / interface / package / type` 不是关键字，可作标识符（`switch type(v)` 中 type 为普通名）。

## 3. 字面量

### 3.1 整数

```
123        十进制            1_000_000   下划线分组
0x1F  0X1F 十六进制          0o17  0O17   八进制
0b101      二进制
```

- 默认 int（64 位有符号）；溢出**自动升 bigint**（见 `02-types.md`）。
- 后缀 `n` 显式 bigint：`123n`、`0xFFn`。

### 3.2 浮点

```
3.14   1e9   1E-3   2.5e+10   1_000.5
```

IEEE 754 双精度（float）。必须有整数部分与前导数字：`.5`、`5.` 非法。

### 3.3 字符串

| 形式 | 语法 | 特性 |
|------|------|------|
| 普通字符串 | `"..."` | 转义 + `${expr}` 插值 |
| 原始字符串 | `` `...` `` | 无转义无插值，跨行保留 |

转义序列：`\n \r \t \\ \" \' \0 \xHH \u{XXXXXX}`（Unicode 码点）。

插值：`"a = ${a}, sum = ${a + b}"`；`${}` 内可为任意表达式，可嵌套字符串。

相邻字符串字面量**不**自动拼接（避免歧义，用 `+` 或 `strings.join`）。

### 3.4 rune 字面量

```
'a'   '\n'   '\x41'   '\u{4E2D}'   '中'
```

值为 int（Unicode 码点）。

## 4. 运算符与标点

```
+  -  *  /  %  **        算术
==  !=  <  <=  >  >=     比较
&&  ||  !                逻辑（and or not 等价）
&  |  ^  <<  >>  ~       位运算
=  +=  -=  *=  /=  %=  **=  &=  |=  ^=  <<=  >>=   赋值
:=                       短声明
->                       函数表达式体（func f(x) -> expr）
<-                       channel 接收 / 发送（ch <- v）
?  :                     三元
.  ,  ;  (  )  [  ]  {  }   标点（; 仅 ASI 产物，源码不书写）
```

## 5. 注释

```
// 单行注释

/* 块注释，可嵌套 */
```

文档注释：公共 API 使用 `/** ... */`（供 `ms doc` 抽取，见 `11-style-ms.md`）。

## 6. 空白与换行

空格 / 制表符 / 回车仅分隔 token。**换行有语义**：参与 ASI。

## 7. ASI（自动分号插入）规则

与 Go 一致，扫描器在换行符处回补分号，当且仅当换行前最后一个 token 属于：

```
标识符 | 字面量（int/float/string/rune）| 关键字集合：
break continue return throw defer yield
true false nil this
++ --（预留）| ) ] }
```

细则：

1. 括号 / 方括号 / 花括号内的换行**不产生**分号（括号深度 > 0 抑制 ASI）。
2. 插值字符串内部的换行不产生分号。
3. `ch <-` 与 `<-ch` 跨行：若行尾是 `<-`，抑制 ASI，等待下一行补全。
4. `return` / `throw` 后换行立即断句（与 Go/JS 相同）：跨行返回值需括号包裹。

## 8. Token 流附加信息

每个 token 携带：文件路径、起始行列（1-based）、字节偏移。诊断信息（`20-architecture.md`）依赖于此。

## 9. 数值词法歧义处理

- `1..2` 非法（range 不进 v1 语法，用 `range(1, 2)`）。
- `0x1F` 中 `F` 后跟标识符字符非法；`123abc` 非法。
- `1e` / `1e+` 非法。
