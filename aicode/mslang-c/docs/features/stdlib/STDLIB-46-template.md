# STDLIB-46: template 模块

## 职责

数据驱动的文本模板：变量替换、条件、循环。轻量级，不依赖正则（手写解析器）。
对应 Go 的 `text/template` 包（简化子集）。`.ms` 实现。

---

## C/.ms 分层

全部 `.ms`（`stdlib/ms/template.ms`）。模板解析器和渲染器均用 .ms 编写。

---

## 模板语法

| 语法 | 描述 |
|---|---|
| `{{name}}` | 变量替换（从上下文 map 中取值）|
| `{{= expr }}` | 表达式求值（简单算术/字符串拼接）|
| `{{if cond}} ... {{end}}` | 条件块 |
| `{{if cond}} ... {{else}} ... {{end}}` | 条件分支 |
| `{{for item in list}} ... {{end}}` | 循环（item 在块内可用）|
| `{{for k, v in map}} ... {{end}}` | 映射循环 |
| `{{! comment }}` | 注释（不输出）|
| `\{{` | 字面量 `{{`（转义）|

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `template.parse(src)` | str | Template | 解析模板（编译为 AST 节点列表）|
| `template.render(src, ctx)` | str, map | str | 一次性解析+渲染 |
| `t.render(ctx)` | map | str | 渲染已编译模板 |
| `t.render_to(writer, ctx)` | BufWriter, map | nil | 渲染到 BufWriter（大输出流式）|

---

## 依赖

- `strings` 模块（STDLIB-13，字符串查找/切分）
- `fmt` 模块（STDLIB-15，值格式化）
- `bufio` 模块（可选，`render_to`）

---

## 示例

```ms
import "template"

var tmpl = template.parse(
    "Hello, {{name}}!\n" +
    "{{if admin}}You are an admin.{{else}}You are a user.{{end}}\n" +
    "Your scores:\n" +
    "{{for score in scores}}" +
    "  - {{score}}\n" +
    "{{end}}"
)

var out = tmpl.render({
    "name":   "Alice",
    "admin":  true,
    "scores": [95, 87, 92]
})
print(out)
// Hello, Alice!
// You are an admin.
// Your scores:
//   - 95
//   - 87
//   - 92
```

---

## 测试

```
tests/unit/test_stdlib_template.c
tests/fixtures/stdlib_template_basic.ms
```

关键测试点：
- 嵌套 if/for
- 模板中访问 map 嵌套字段
- 转义 `\{{`
- ctx 中 key 不存在时的默认行为（空字符串）
- 大模板渲染正确性
