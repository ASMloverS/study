# STDLIB-28: regexp 模块

## 职责

正则表达式搜索与替换，使用扩展正则（ERE 超集）。
纯 C 实现，无外部依赖（嵌入 NFA-based 引擎，参考 RE2/POSIX ERE 语义）。

---

## C/.ms 分层

全部 C（`src/stdlib/regexp.c` + `src/stdlib/_re_engine.c`）。
使用 `MsObjUserdata` 封装编译后的正则对象（`finalize` 释放内存）。

---

## 支持的语法

| 特性 | 写法 |
|---|---|
| 字符类 | `[abc]` `[^abc]` `[a-z]` |
| 预定义类 | `\d`/`\D`/`\w`/`\W`/`\s`/`\S` |
| 量词 | `*` `+` `?` `{n}` `{n,}` `{n,m}` |
| 贪心/非贪心 | `*?` `+?` `??` |
| 锚点 | `^` `$` |
| 分组 | `(...)` 捕获 / `(?:...)` 非捕获 |
| 交替 | `a\|b` |
| 点 | `.` 匹配除 `\n` 外任意字符 |
| 转义 | `\.` `\*` `\+` `\(` 等 |
| 标志 | `(?i)` 忽略大小写 / `(?m)` 多行模式 |

不支持（不在规划内）：lookahead/lookbehind、回溯引用 `\1`、POSIX 胶着语义。

---

## Match 对象

| 字段/方法 | 描述 |
|---|---|
| `m.matched` | 整体匹配字符串 |
| `m.start` | 匹配起始位置 |
| `m.end` | 匹配结束位置（不含）|
| `m.groups()` | 所有捕获组的字符串列表 |
| `m.group(n)` | 第 n 个捕获组（0 = 整体）|

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `regexp.compile(pattern)` | str | Regex | 编译正则（失败抛错）|
| `regexp.match(pattern, s)` | str, str | Match\|nil | 从头匹配 |
| `regexp.search(pattern, s)` | str, str | Match\|nil | 任意位置匹配 |
| `regexp.find_all(pattern, s)` | str, str | list[Match] | 所有不重叠匹配 |
| `regexp.replace(pattern, s, repl)` | str, str, str\|fn | str | 替换；repl 为 str 时**字面替换**（无 `$1` 等模板变量）；repl 为 fn 时调用 `fn(Match)→str` |
| `regexp.replace_n(pattern, s, repl, n)` | str,str,str\|fn,int | str | 替换前 n 次；repl 语义同 `replace` |
| `regexp.split(pattern, s, n=-1)` | str, str, int | list | 按正则切分（-1=全切）|
| `regexp.is_match(pattern, s)` | str, str | bool | 是否匹配（比 match 更快，不构造 Match）|

### 编译后的 Regex 对象方法（性能场景）

| 方法 | 等同 |
|---|---|
| `re.match(s)` | `regexp.match(pattern, s)` |
| `re.search(s)` | `regexp.search(pattern, s)` |
| `re.find_all(s)` | `regexp.find_all(pattern, s)` |
| `re.replace(s, repl)` | `regexp.replace(pattern, s, repl)` |
| `re.split(s, n=-1)` | `regexp.split(pattern, s, n)` |

---

## 实现要点（`src/stdlib/regexp.c`）

- 引擎：Thompson NFA（O(n·m) 时间，无回溯，避免 ReDoS）。
- 编译结果缓存：`MsObjUserdata{tag="Regex", data=NfaState*, finalize=free_nfa}`。
- 捕获组：NFA 扩展支持有限捕获（submatch tracking）。
- `find_all`：每次从上次结束位置继续，防无限循环（零宽匹配步进 1）。

---

## 依赖

- CAPI-01/02（注册表）
- CAPI-06（MsObjUserdata，封装 NFA 状态）
- `<stdlib.h>` `<string.h>`

---

## 示例

```ms
import "regexp"

var m = regexp.search("(\\d+)-(\\w+)", "order-123-abc")
if m != nil {
    print(m.matched)    // 123-abc
    print(m.group(1))   // 123
    print(m.group(2))   // abc
}

print(regexp.find_all("\\d+", "a1b22c333"))
// [Match("1",...), Match("22",...), Match("333",...)]

var result = regexp.replace("(\\w+)@(\\w+)", "user@host",
    fun(m){ return m.group(1) + " at " + m.group(2) })
print(result)  // user at host

print(regexp.split("\\s+", "  hello   world  "))
// ["", "hello", "world", ""]
```

---

## 测试

```
tests/unit/test_stdlib_regexp.c
tests/fixtures/stdlib_regexp_basic.ms
```

关键测试点：
- 基本锚点/量词/字符类
- 捕获组
- 非贪心量词
- find_all 无限循环防护
- `(?i)` 大小写不敏感
- 函数替换回调
