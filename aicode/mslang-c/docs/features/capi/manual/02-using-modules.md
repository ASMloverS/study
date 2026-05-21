# 02 — 使用模块

本章面向 `.ms` 脚本用户，说明如何导入模块、配置查找路径，以及排查导入失败。

---

## 最小示例

```ms
import io
io.open("hello.txt", "r").read()
```

---

## import 三段查找顺序

执行 `import name` 时，VM 按顺序尝试三个来源，找到即停：

| 步骤 | 来源 | 说明 |
|:---:|---|---|
| 1 | **内置注册表** | 启动时静态注册的模块（`io`、`net`、`math` 等）|
| 2 | **文件系统 `.ms`** | 在搜索路径中查找 `name.ms` |
| 3 | **动态库** | 在搜索路径中查找 `libname.so` / `name.dll` / `libname.dylib` |

**搜索路径**由以下两处合并（先 CLI 后环境变量）：

```
--module-path=<dir>  （可多次指定，依次追加）
MSLANG_PATH          （Unix 用 : 分隔，Windows 用 ; 分隔）
```

---

## 配置搜索路径

### 环境变量 MSLANG_PATH

```bash
# Unix / macOS
export MSLANG_PATH=/usr/local/lib/ms:/home/alice/mylibs

# Windows
set MSLANG_PATH=C:\Users\Alice\mslibs;D:\shared\ms
```

### CLI 参数 --module-path

```bash
./mslang-c --module-path=/tmp/myext script.ms
./mslang-c --module-path=a --module-path=b script.ms   # 多目录
```

CLI 参数优先级高于 `MSLANG_PATH`，会被插入搜索路径的最前面。

---

## 命名冲突规则

**内置裸名优先**：`import io` 始终加载内置 `io` 模块，即使搜索路径中存在同名 `io.ms` 或 `libio.so`。

**显式路径绕过内置**：在 `.ms` 文件中使用相对或绝对路径可跳过内置注册表：

```ms
import "./io.ms"    # 强制加载当前目录下的 io.ms，而非内置 io
```

---

## 导入失败诊断

三段查找全部失败时，VM 向 `.ms` 端抛出：

```
ImportError: module 'mylib' not found
  tried: [builtin]  not registered
  tried: /usr/local/lib/ms/mylib.ms  No such file or directory
  tried: /usr/local/lib/ms/libmylib.so  No such file or directory
  dlopen last error: /usr/local/lib/ms/libmylib.so: cannot open shared object file
```

### 启用逐步追踪

设置 `MSLANG_TRACE_IMPORT=1` 可在每个查找步骤打印细节：

```bash
MSLANG_TRACE_IMPORT=1 ./mslang-c script.ms
```

输出示例：

```
[import] step 1: lookup builtin 'mylib' -> not found
[import] step 2: search path /usr/local/lib/ms/mylib.ms -> not found
[import] step 3: search path /usr/local/lib/ms/libmylib.so -> not found
ImportError: module 'mylib' not found
```

---

## 进一步阅读

- **搜索路径规格**：[CAPI-03-search-path.md](../CAPI-03-search-path.md)
- **动态加载 ABI**：[CAPI-04-dynamic-loading.md](../CAPI-04-dynamic-loading.md)
- **编写第一个扩展**：[03-hello-extension.md](03-hello-extension.md)
