# 09 模块系统

Python 式**文件模块** + Go 式 import 拼写。

## 1. 模块即文件

- 一个 `.ms` 文件 = 一个模块；模块名 = 文件名去后缀（`strutil.ms` → 模块 `strutil`）。
- 目录仅是路径命名空间（`import "net/addr"` 加载 `net/addr.ms`），目录本身**不是模块**，无 `__init__.ms` 语义。
- 模块体在**首次被 import 时执行一次**，执行结果（模块命名空间）全局缓存。

## 2. import 形式

```ms
import "strutil"                     // 绑定模块名 strutil
import "net/addr" as netaddr         // 别名绑定
from "strutil" import { join, split as sp, pad }
from "strutil" import *              // 导入全部导出符号（非 _ 开头）
```

- 路径必须是字符串字面量（动态 import 用标准库 `importlib.load(path)`，v1 后期）。
- `import *` 与具名导入不得引入 `_` 开头符号；`import *` 仅 REPL 允许（脚本中 `ms fmt`/linter 报错，`11-style-ms.md` §6）。
- import 语句可出现在任何作用域（函数内 import 合法，局部绑定）。

## 3. 可见性规则

- `_name`（单下划线前缀）：模块私有，import 不可见。
- 其余符号（`var / const / func / class` 声明）均为导出。
- 无 `export` 关键字、无大写导出（Go）约定。

## 4. 解析顺序

import 路径依次在下列位置查找（先中先得）：

```
1. 相对路径（"./x"、"../x"）：相对 import 所在文件目录
2. 入口脚本所在目录（脚本场景便于单树部署）
3. MS_PATH 环境变量各目录（路径分隔符分隔）
4. 依赖目录 ms_packages/（ms pkg 安装产物，见 27-toolchain.md）
5. 标准库（编译内置路径）
```

- 找不到抛 `ImportError("module not found: xxx")`；命中错误文件（语法错）照常报编译错。
- 查找只拼 `.ms` 后缀；不自动加载同名 `.msc`（.msc 是缓存替身，见 §6）。

## 5. 循环导入

- 检测：加载中的模块再次被 import 时返回**未完成**的模块命名空间（已执行到当前行的符号）。
- 行为：不抛错（Python 立场）；若使用到尚未定义的符号，运行时 AttributeError。
- 诊断：`runtime.importGraph()` 输出依赖图辅助排查。

## 6. 字节码缓存（.msc）

- 编译产物按源文件指纹（FNV-1a of 源内容 + 编译器版本）缓存于 `__mscache__/`（入口脚本同目录，可 MS_CACHE_DIR 覆盖）。
- 命中且指纹一致 → 跳过前端直接加载；不一致静默重编译。
- 缓存对语义零影响：删除缓存目录行为不变（`__mscache__` 应进 .gitignore）。

## 7. 模块对象

```ms
import "strutil" as su
print(type(su))          // module
su.__name__              // "strutil"（文件路径相对解析根）
su.__file__              // 绝对路径
```

- 模块命名空间是只读 map 语义（外部 `su.newVar = 1` 抛 AttributeError，防意外污染）。

## 8. 入口与 __name__

- 顶层即入口；`__name__` 内置变量：入口脚本为 `"__main__"`，被导入模块为其模块名。

```ms
if __name__ == "__main__" {
    // 仅直接运行时执行
}
```

## 9. 标准库模块

标准库即内置在 `5. 标准库` 搜索位的模块集（源码随二进制分发，`26-stdlib.md` 目录）；用户不可遮蔽标准库名（`import "math"` 永远命中标准库——自定义模块不得与标准库同名冲突，冲突时标准库优先并告警）。
