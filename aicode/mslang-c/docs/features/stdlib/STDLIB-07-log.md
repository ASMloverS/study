# STDLIB-07: log 模块

## 职责

分级日志：6 个日志级别、可替换 sink（输出目标）、可自定义格式模板、支持 tag 子 logger。

---

## 日志级别

```
TRACE(0) < DEBUG(1) < INFO(2) < WARN(3) < ERROR(4) < FATAL(5)
```

默认级别：`INFO`。低于当前级别的消息静默丢弃。

---

## 函数清单

### 全局 logger（模块函数）

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `log.trace(msg, ...)` | str,... | nil | 最低级别日志 |
| `log.debug(msg, ...)` | str,... | nil | 调试 |
| `log.info(msg, ...)` | str,... | nil | 信息（默认可见）|
| `log.warn(msg, ...)` | str,... | nil | 警告 |
| `log.error(msg, ...)` | str,... | nil | 错误 |
| `log.fatal(msg, ...)` | str,... | nil | 致命，默认 `exit(1)` |

`msg` 中 `%s`、`%d`、`%f`、`%v`（任意值 str 化）均被替换，其余参数按顺序填充。

### 配置

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `log.set_level(level)` | str\|int | nil | 设置全局最低级别（"trace".."fatal" 或 0..5）|
| `log.get_level()` | – | str | 返回当前级别字符串 |
| `log.set_sink(file_or_nil)` | File\|nil | nil | nil = stderr；传入 `io.open(...)` 的 File |
| `log.set_format(fmt)` | str | nil | 格式模板，见下 |
| `log.set_fatal_exits(flag)` | bool | nil | `fatal` 后是否调 `exit(1)`，默认 true |

**格式模板占位符**：

| 占位符 | 含义 |
|---|---|
| `{time}` | ISO-8601 本地时间 |
| `{level}` | 级别字符串（大写，5 字符对齐）|
| `{tag}` | logger tag（根 logger 为空）|
| `{msg}` | 格式化后的消息体 |

默认格式：`"{time} [{level}] {msg}"`

### tag 子 logger

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `log.with(tag)` | str | Logger | 返回一个带 tag 的子 logger 句柄 |
| `lg.trace(msg, ...)` / `lg.info(...)` / ... | – | – | 与全局同，但输出携带 tag |
| `lg.set_level(level)` | str\|int | nil | 覆盖此子 logger 的级别（不影响全局）|

---

## Logger 句柄（`MsObjUserdata`）

```c
typedef struct {
    char*   tag;       /* strdup，finalize 时 free */
    int     level;     /* 本 logger 的最低级别；-1 = 继承全局 */
} MsLoggerData;

static void logger_finalize(void* data) {
    MsLoggerData* ld = (MsLoggerData*)data;
    free(ld->tag);
}
```

子 logger 方法通过 `ms_userdata_invoke`（`type_tag == "MsLogger"`）分发。

---

## 全局状态（`src/stdlib/log.c`）

```c
static struct {
    int      level;           /* 全局最低级别 */
    FILE*    sink;            /* 输出目标，默认 stderr */
    char     format[256];     /* 格式模板 */
    bool     fatal_exits;
} g_log = {
    .level       = 2,         /* INFO */
    .sink        = NULL,      /* = stderr（懒初始化）*/
    .format      = "{time} [{level}] {msg}",
    .fatal_exits = true,
};
```

> `g_log.sink == NULL` 时等价于 `stderr`，避免与 io 模块初始化顺序产生循环依赖。

### 并发模型

`log.*` 函数（`log.info`、`log.warn`、`log.error` 等）只能在**主线程**调用。

- `g_log` 全局结构无互斥锁，多线程并发写会产生数据竞争。
- Worker 线程（CAPI-07 §线程池）禁止直接调用 `log.*`；如需记录，须将日志请求投递回主线程（通过完成回调）。
- `g_log.sink == FILE*` 路径下的 `fprintf(stderr, ...)` 同样不加锁；若未来引入多 worker 直接写文件，须在此加 `pthread_mutex_t` 或等价机制。

---

## 格式化输出（核心逻辑）

```c
static void emit(int level, const char* tag,
                 const char* msg_fmt, int argc, MsValue* argv) {
    if (level < g_log.level) return;

    /* 消息体格式化（支持 %s %d %f %v）*/
    char msg[1024];
    format_msg(msg, sizeof(msg), msg_fmt, argc, argv);

    /* 模板替换 */
    char line[2048];
    format_line(line, sizeof(line), g_log.format, level, tag, msg);

    FILE* out = g_log.sink ? g_log.sink : stderr;
    fprintf(out, "%s\n", line);

    if (level == 5 && g_log.fatal_exits) exit(1);
}
```

### 缓冲策略

`emit` 改用 `MsObjStringBuilder`，无固定字节上限：

1. 调 `ms_obj_sb_new(vm)` 创建动态缓冲。
2. `%v` 占位符调用 `ms_value_to_string(vm, val)` 字符串化任意值；对 List/Map 限制递归深度为 **4**，超出部分输出 `<...>`。
3. 格式化完成后调 `ms_obj_sb_to_string(vm, sb)` 得到 `ObjString*`，再写入 sink。

旧代码的 `char line[2048]` / `char msg[1024]` 在超长消息时会被 `snprintf` 静默截断，改用 StringBuilder 后消除此问题。

---

## 实现

```c
static MsValue ms_log_info(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1) { ms_vm_runtime_error(vm, "log.info: need message"); return MS_NIL_VAL(); }
    const char* fmt = MS_AS_CSTRING(argv[0]);
    emit(2, "", fmt, argc - 1, argv + 1);
    return MS_NIL_VAL();
}

static const MsNativeDef ms_log_defs[] = {
    {"trace",          ms_log_trace,          -1},
    {"debug",          ms_log_debug,          -1},
    {"info",           ms_log_info,           -1},
    {"warn",           ms_log_warn,           -1},
    {"error",          ms_log_error,          -1},
    {"fatal",          ms_log_fatal,          -1},
    {"set_level",      ms_log_set_level,       1},
    {"get_level",      ms_log_get_level,       0},
    {"set_sink",       ms_log_set_sink,        1},
    {"set_format",     ms_log_set_format,      1},
    {"set_fatal_exits",ms_log_set_fatal_exits, 1},
    {"with",           ms_log_with,            1},
    {NULL, NULL, 0}
};

void ms_module_log_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_log_defs);
}
```

---

## 依赖

- CAPI-01/02（注册）
- CAPI-05（MsObjUserdata，Logger 句柄）
- STDLIB-04（io.File — `set_sink` 接受 File 句柄）

---

## 测试

```ms
// tests/fixtures/stdlib_log_basic.ms
import log

log.set_level("debug")
log.debug("debug message")
log.info("hello %s", "world")
log.warn("count=%d", 42)

var lg = log.with("myapp")
lg.info("tagged message")
```

```c
// tests/unit/test_stdlib_log.c
// 1. 低于 level 的消息不输出（重定向 sink 到临时文件，断言文件内容）
// 2. set_format 改格式后输出包含新占位符内容
// 3. fatal 默认不 exit（测试中 set_fatal_exits(false)）
// 4. tag 子 logger 输出包含 tag 字符串
```
