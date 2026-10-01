# 11 ms 编码规范

命名约定对齐 Google Java Style Guide §5（https://google.github.io/styleguide/javaguide.html），结合 mslang 语言特性裁剪；显式偏离见 §7 清单。`ms fmt` 机械化执行其中格式部分（`27-toolchain.md`）。

## 1. 命名

| 实体 | 风格 | 示例 |
|------|------|------|
| 类 | UpperCamelCase，名词/名词短语 | `HttpClient`、`Greeter` |
| 异常类 | UpperCamelCase，`Error` 结尾 | `AppError`、`ConfigError` |
| trait（mixin） | UpperCamelCase，名词/形容词短语 | `Logger` |
| 函数 / 方法 | lowerCamelCase，动词/动词短语 | `fetchPage`、`sendMessage` |
| 参数 / 局部变量 | lowerCamelCase；公共 API 避免单字符参数 | `totalCount`、`prefix` |
| 常量（深不可变） | SCREAMING_SNAKE_CASE | `MAX_RETRIES`、`GREETING` |
| const 可变容器 | lowerCamelCase（const 不深冻结，不算常量，`04-statements.md` §1） | `const routes = {}` |
| 模块（文件名） | 全小写单词，允许小写下划线 | `strutil.ms`、`net_addr.ms` |
| dunder | 语言保留 | `__init__` |
| 私有符号 | `_` 前缀（语言可见性特性，唯一允许的前缀装饰） | `_internalCache` |
| 测试 | `*_test.ms` 文件；用例 `testXxx`，下划线分场景可选（`27-toolchain.md` §4） | `testJoin`、`testJoin_emptySep` |

- 驼峰定义（Java Guide §5.3）：缩略词作普通词处理——`XmlParser`、`newCustomerId`；禁 `XMLParser`、`newCustomerID`。仅数字邻接例外允许下划线（极少用）。
- 标识符仅 ASCII（语言允许 Unicode 标识符，规范强制 ASCII）；禁装饰性前后缀：`mName`、`s_name`、`kName` 均禁止（`_` 私有前缀除外）。
- 布尔命名推荐 `is/has/can` 前缀：`isValid`、`hasNext`。
- 类名避免 `Manager/Processor/Info` 尾缀滥用；取具体名。

## 2. 格式

- **缩进 4 空格**，禁 Tab。
- 行宽 100（硬限 120）。
- 左大括号不换行（K&R）：`if cond {`、`func f() {`、`class A {`。
- 无分号；一行一语句。
- 二元运算符两侧空格；逗号后空格；`(` `[` 内侧无填充空格。
- 关键字 `if/for/while/switch/catch` 后一个空格；函数名/方法名后不加（`f(x)`）。
- 空行：函数间 1 行，类内方法间 1 行，文件末尾 1 个换行；禁连续 2+ 空行。
- import 组与组之间 1 空行，组内按路径字母序：标准库 → 第三方（ms_packages）→ 本地相对路径。

## 3. 文件布局

```ms
/// 模块文档注释：一句话职责 + 版权/作者（可选）

import "strings"                    // 标准库
import "third/party"                // 第三方
import "./local_util"               // 本地

const MAX_SIZE = 1024               // 常量

var globalCache = {}                // 模块级状态（慎用，注明理由）

class Thing { ... }                 // 类型声明

func helper() { ... }               // 函数
```

- 一文件至多一个公共 class（可伴小型私有 class）。
- 模块体副作用代码应克制；入口逻辑放 `__main__` 守卫。

## 4. 注释与文档

```ms
/// 计算两点的曼哈顿距离。
/// @param a 第一个点
/// @param b 第二个点
/// @return 曼哈顿距离，非负 int
func manhattan(a, b) {
    return abs(a.x - b.x) + abs(a.y - b.y)   // 行尾注释与代码隔 2 空格
}
```

- 语言仅有 `//` 行注释（`01-lexical.md` §5）；`/* */` 非法，无块注释语法。
- 文档注释 = 紧贴声明（无空行分隔）的连续 `///` 行；标签顺序 `@param` → `@return`（ms doc 抽取，`27-toolchain.md` §7）。
- 摘要句（对齐 Java Javadoc 惯例）：首行为名词/动词短语摘要，大写开头、句号结尾；禁"此函数返回…"式开头。
- 公共 API（导出的 func/class）写 `///`；实现细节用普通 `//`。
- 注释解释 **why**，不复述 what。

## 5. 语言惯用法

- 判空用 `x is nil`；真值判断直接 `if xs {`（记住 0/""/[] 为真）。
- 资源清理一律 `defer`，不写 try/finally。
- 集合构造小规模用字面量；推导式优先于 map/filter 链（可读性）。
- 字符串拼接 3+ 段用插值 `"${a}-${b}"`，不用 `+` 链。
- 并发：长计算/IO 用 `go` 或 async fn；数据交接用 channel，避免共享内存 + 锁除非必要。
- 异常消息含上下文值：`throw ValueError("port out of range: ${port}")`。
- 魔法数字提常量；`_` 作丢弃名（`for _ in range(3)`）。

## 6. 禁止事项

- 源码出现分号、Tab 缩进、行尾空白（ms fmt 报错并修复）。
- 块注释 `/* */`（语言非法，见 `01-lexical.md` §5）。
- `import *`（仅 REPL 允许）。
- 与标准库模块同名的自有模块（`09-modules.md` §9）。
- 单函数 > 80 行、嵌套 > 4 层（fmt 提示告警级别）。
- 定义与内置同名的全局函数覆盖 print/len 等（REPL 试验除外）。

## 7. 对 Java Guide 的偏离

- 模块文件名允许小写下划线（Java 包名纯小写拼接）。
- `_` 前缀私有符号（语言可见性特性；Java 无此前缀）。
- 异常类 `Error` 结尾（Java 习惯 `Exception` 结尾；对齐 mslang 内置异常命名）。
- 格式不对齐 Java：缩进 4 空格（Java 2 空格）、行宽 100 建议/120 硬限（Java 100 硬限）。
