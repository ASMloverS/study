# T024 — 模块系统

- 阶段: P6 异常/模块（roadmap 期 6）
- 依赖: T018
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/09-modules.md` 全文

## 1. 目标与范围

实现文件模块加载：五级解析顺序、import 四形式、`_` 私有可见性、模块对象（只读命名空间 + `__name__`/`__file__`）、首次执行缓存、循环导入（未完成命名空间）、`__name__ == "__main__"` 守卫、标准库占位路径（内置标准库注册表先行，C 实现模块归 T034+）。

不做：.msc 缓存（T025）、`importlib.load`（v1 后期）、msext 动态扩展（T033）。

## 2. 实现要点

- MsObjModule（22 §2）：命名空间 map + `__name__`/`__file__`；模块对象在 GC 永久代（23 §7：运行期不卸载）。
- 解析顺序（09 §4）：相对（`./x` `../x` 相对 import 所在文件目录）→ 入口脚本目录 → `MS_PATH` 各目录（分隔符平台自适应）→ `ms_packages/` → 标准库（内置注册表 + 编译内置路径）；先中先得；仅拼 `.ms`；找不到 `ImportError("module not found: xxx")`。
- import 语义（09 §1 §2）：模块体首次 import 执行一次，结果缓存（模块注册表：解析路径 → module）；`import "m" as x` 别名绑定；`from "m" import {a, b as c}` 具名导入（不存在 → ImportError）；`from "m" import *` 导入非 `_` 开头符号——脚本中由 fmt/linter 报错（T038），运行时放行（REPL 允许）；import 可出现在任何作用域（函数内 import 局部绑定）。
- 可见性（09 §3）：`_name` 模块私有，from-import 具名取 `_` 名 → ImportError；`import "m"` 整体访问 `m._x` 亦不可见（AttributeError）。
- 循环导入（09 §5）：加载中模块再次被 import → 返回当前未完成命名空间（已执行符号）；不抛错；使用未定义符号运行时 AttributeError。
- 只读命名空间（09 §7）：外部 `su.newVar = 1` 抛 AttributeError（模块自身体内写自身全局合法）。
- `__name__`（09 §8）：入口 `"__main__"`、被导入为模块名；守卫样例落地。
- 标准库位（09 §9）：内置模块注册表（名字 → C 构造钩子）；用户模块与标准库同名冲突：标准库优先 + stderr 告警。
- import 失败的编译错与运行错边界：语法错照常编译错（退出码 3）；未找到/具名缺失运行时 ImportError。

## 3. 涉及文件

新增: `src/module/module.c/.h`（解析/注册表/加载器）、`src/module/builtin_libs.c`（标准库注册表占位）、`tests/unit/test_module.c`、`tests/fixtures/modules/**/*.ms`（多文件 fixture 支持——跑器以入口文件为目录根）
修改: `src/compiler/compiler.c`（import 编译为模块加载调用）、`src/vm/vm.c`（模块执行帧）、`tools/test_runner.c`（多文件 fixture：目录内相对 import 生效）

## 4. 验收标准（DoD）

- [ ] 09 §2/§7/§8 全部示例运行正确
- [ ] 五级解析顺序各有命中用例（MS_PATH/ms_packages 用临时目录注入）
- [ ] 循环导入 a⇄b：部分符号可用、未定义符号 AttributeError
- [ ] `_` 私有：具名导入拒绝、整体访问 AttributeError
- [ ] 模块只读：外部写命名空间 AttributeError
- [ ] `__name__` 守卫：直接运行执行 / 被导入不执行

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_module.c`

| 用例 | 布局 | 期望 |
|------|------|------|
| 首次执行一次 | main 导入 m 两次 | m 体内计数 1 |
| 别名 | `import "net/addr" as na` | na.__name__ == "net/addr" |
| 具名+改名 | `{join, split as sp}` | 绑定正确 |
| 未找到 | `import "nope"` | ImportError 含模块名 |
| 相对路径 | `./sub/x`、`../y` | 命中相对文件 |
| MS_PATH | 注入临时目录 | 第三级命中 |
| 循环 | a imports b imports a | 未完成命名空间可读已定义符号 |
| 私有 | from import `_x` | ImportError |
| 只读 | `su.v = 1` | AttributeError |
| 同名标准库 | 自定义 "math" | 标准库优先 + stderr 告警 |

### 5.2 ms fixtures `tests/fixtures/modules/`

- `basic/main.ms`：

```ms
import "strutil" as su
from "strutil" import { join }
print(su.__name__)          // strutil
print(join(["a", "b"], "-"))
if __name__ == "__main__" { print("main guard") }
```

- `cycle/a.ms` + `cycle/b.ms`：互引 + 部分符号
- `privacy/main.ms`：`_hidden` 访问两形式失败（stderr）
- `nested/net/addr.ms` + 相对引用上跳 `../base.ms`

## 6. 风险与备注

- 多文件 fixture 需要跑器隔离：每个 fixture 目录复制到临时目录运行（防 `__mscache__` 与相对路径互染）——T002 跑器本任务扩展"目录型用例"。
- import 编译期 vs 运行期：v1 采用运行期加载（import 语句执行到才加载），与 09 §1"首次被 import 时执行"一致；编译器不预解析依赖。
