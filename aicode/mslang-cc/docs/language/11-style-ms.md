# 11 ms 编码规范

以 Java 命名与格式约定为基准（用户确认），结合 mslang 语言特性裁剪。`ms fmt` 机械化执行其中格式部分（`27-toolchain.md`）。

## 1. 命名

| 实体 | 风格 | 示例 |
|------|------|------|
| 类 / 异常 | UpperCamelCase | `HttpClient`、`AppError` |
| 接口性 mixin（trait） | UpperCamelCase | `Logger` |
| 函数 / 方法 / 变量 | lowerCamelCase | `fetchPage`、`totalCount` |
| 常量 | SCREAMING_SNAKE_CASE | `MAX_RETRIES` |
| 模块（文件名） | 全小写单词或小写下划线 | `strutil.ms`、`net_addr.ms` |
| dunder | 语言保留 | `__init__` |
| 私有符号 | `_` 前缀 | `_internalCache` |

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
/** 模块文档注释：一句话职责 + 版权/作者（可选） */

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
/**
 * 计算两点的曼哈顿距离。
 * 参数与返回值用 @param/@return 标签（ms doc 抽取）。
 * @param a 第一个点
 * @param b 第二个点
 * @return 曼哈顿距离，非负 int
 */
func manhattan(a, b) {
    return abs(a.x - b.x) + abs(a.y - b.y)   // 行尾注释与代码隔 2 空格
}
```

- 公共 API（导出的 func/class）写 `/** */`；实现细节用 `//`。
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
- `import *`（仅 REPL 允许）。
- 与标准库模块同名的自有模块（`09-modules.md` §9）。
- 单函数 > 80 行、嵌套 > 4 层（fmt 提示告警级别）。
- 定义与内置同名的全局函数覆盖 print/len 等（REPL 试验除外）。
