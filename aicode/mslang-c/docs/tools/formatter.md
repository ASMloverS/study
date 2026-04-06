# C Code Formatter

自动将 C 源文件格式化为 Google C Style，并统一编码与换行。

## 功能

| 功能 | 实现手段 |
|------|----------|
| Google C Style | `clang-format`（BasedOnStyle: Google） |
| UTF-8 编码 | `charset_normalizer` 检测并转码 |
| LF 换行 | bytes 层替换 `\r\n` → `\n` |
| 行尾空白 | 逐行 `rstrip()` |

## 文件结构

```
tools/
  formatter.py        # 核心格式化模块 + CLI
  format_watch.py     # watchdog 文件监听守护进程
  requirements.txt    # Python 依赖
.clang-format         # clang-format 配置
.editorconfig         # 编辑器级规范
.gitattributes        # Git 换行强制
.githooks/
  pre-commit          # Git pre-commit hook
```

## 依赖

```
watchdog>=4.0
charset-normalizer>=3.0
clang-format 18+（系统级）
```

安装：`pip install -r tools/requirements.txt`

## `tools/formatter.py`

**公共 API：**

```python
def format_file(path: Path, check: bool = False) -> FormatResult
def format_files(paths: list[Path], check: bool = False) -> list[FormatResult]
```

**CLI：**

```
python tools/formatter.py [OPTIONS] [FILE|DIR ...]

  -c, --check     检查模式：不修改文件，有问题则 exit 1
  -v, --verbose   打印每个处理的文件
  -e, --ext EXTS  文件扩展名，逗号分隔（默认 c,h）
  （无参数）       从 stdin 读取文件路径，每行一个
```

**处理管线（每文件，顺序固定）：**

1. **编码** — `charset_normalizer.from_path()` 检测；非 UTF-8/ASCII 则转码写回
2. **换行 + 行尾空白** — 读 bytes → `replace(b'\r\n', b'\n')` → 逐行 `rstrip()` → 确保末尾 `\n`
3. **代码风格** — `clang-format -i <file>`

**Check 模式：** 各步骤只检测不修改，返回问题列表，`exit 1` 表示有格式问题。

## `tools/format_watch.py`

监听目录下 `.c`/`.h` 文件的保存事件，自动调用 `formatter.format_file()`。

**防抖：** 文件变更后等待 500ms 无新事件再触发，避免编辑器多次写入重复格式化。

**CLI：**

```
python tools/format_watch.py [DIR ...] [--ext c,h]

  （默认监听当前目录）
  Ctrl+C 退出
```

## `.clang-format`

```yaml
BasedOnStyle: Google
Language: Cpp
ColumnLimit: 100
IndentWidth: 4
UseTab: Never
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"ms/'
    Priority: 1
  - Regex: '^"'
    Priority: 2
  - Regex: '^<'
    Priority: 3
AllowShortFunctionsOnASingleLine: Empty
AlignEscapedNewlines: Left
```

## `.editorconfig`

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
trim_trailing_whitespace = true
insert_final_newline = true
indent_style = space
indent_size = 4

[*.md]
trim_trailing_whitespace = false

[{CMakeLists.txt,*.cmake}]
indent_size = 2
```

## `.gitattributes`

```
* text=auto eol=lf
*.c text eol=lf
*.h text eol=lf
*.py text eol=lf
*.sh text eol=lf
*.bat text eol=crlf
*.png binary
*.jpg binary
```

## `.githooks/pre-commit`

Python 脚本，在每次 `git commit` 时自动格式化暂存区的 `.c`/`.h` 文件并重新 stage。

**逻辑：**

1. `git diff --cached --name-only --diff-filter=ACM -- '*.c' '*.h'` 获取暂存文件列表
2. 无 `.c`/`.h` 文件则跳过
3. 调用 `formatter.format_files()` 格式化
4. `git add <files>` 重新 stage

**激活（一次性配置）：**

```bash
git config core.hooksPath .githooks
```

## 验证方法

```bash
# 1. 手动格式化
python tools/formatter.py src/

# 2. 检查模式（CI 可用）
python tools/formatter.py --check src/

# 3. 实时监听
python tools/format_watch.py .

# 4. 测试 pre-commit hook
git add some_file.c
git commit -m "test"   # hook 自动格式化并重新 stage
```
