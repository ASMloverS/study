# STDLIB-10: gc 模块

## 职责

让 .ms 脚本控制 GC 行为：手动触发、调整阈值、暂停/恢复自动 GC、查询当前统计。

---

## 函数清单

### 触发与统计

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `gc.collect()` | – | int | 立即触发一次完整 mark-sweep，返回本轮回收的对象数 |
| `gc.minor()` | – | int | 仅年轻代 GC（若已实现分代）；未实现时等价 `collect()` |
| `gc.alive()` | – | int | 当前活跃对象数（`vm->alive_count`，需在 GC 中维护）|
| `gc.bytes_allocated()` | – | int | 当前堆内存字节数（`vm->bytes_allocated`）|
| `gc.threshold()` | – | int | 下次 GC 触发字节数（`vm->next_gc`）|
| `gc.object_counts()` | – | map | 按类型统计对象数量 `{string:N, list:M, ...}`（调试用）|

### 控制

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `gc.set_threshold(n)` | int | nil | 设置 `vm->next_gc`（最小 1KB）|
| `gc.pause()` | – | nil | 暂停自动 GC（`vm->gc_paused = true`）|
| `gc.resume()` | – | nil | 恢复自动 GC |
| `gc.is_paused()` | – | bool | 查询当前状态 |

---

## 先决条件

本模块依赖以下 VM 字段（当前 `MsVM` 中尚不存在）：

```c
bool   gc_paused;      // gc.pause() / gc.resume() 控制
size_t alive_count;    // gc.stats() 返回存活对象数
```

以及 `ms_maybe_collect` 的早返回判定：
```c
if (vm->gc_paused) return;
```

**这些字段须在独立提交中先行引入**（建议命名为 `vm-gc-controls`），不得与本模块功能代码混入同一 commit，以便 bisect 和 review。

**实施顺序**：
1. PR `vm-gc-controls`：仅加字段 + 早返回，无业务逻辑。
2. PR `stdlib-gc`：在字段已存在的基础上实现本模块全部功能。

---

## VM 侧新增字段（`include/ms/vm.h`）

```c
typedef struct MsVM {
    // ...
    bool gc_paused;    /* true 时 ms_maybe_collect 直接返回 */
    int  alive_count;  /* GC mark-sweep 后更新：sweep 前 count - freed */
} MsVM;
```

`ms_maybe_collect`（`src/vm_gc.c`）开头插入：

```c
void ms_maybe_collect(MsVM* vm) {
    if (vm->gc_paused) return;
    if (vm->bytes_allocated < vm->next_gc) return;
    ms_collect_garbage(vm);
}
```

---

## `gc.collect()` 实现

```c
static MsValue ms_gc_collect(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    int before = vm->alive_count;
    ms_collect_garbage(vm);
    int freed = before - vm->alive_count;
    return MS_INT_VAL(freed < 0 ? 0 : freed);
}
```

`ms_collect_garbage` 已有；`alive_count` 在 sweep 阶段统计。

---

## `gc.object_counts()` 实现

遍历对象链表，按 `obj->type` 分类计数：

```c
static MsValue ms_gc_object_counts(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    int counts[MS_OBJ_TYPE_COUNT] = {0};
    MsObject* obj = vm->objects;
    while (obj) { counts[obj->type]++; obj = obj->next; }

    MsValue map = MS_OBJ_VAL(ms_obj_map_new(vm));
    static const char* names[] = {
        "string","function","native","closure","upvalue",
        "class","instance","bound_method","list","map","tuple",
        "module","coroutine","socket","file","buffer","userdata",
    };
    for (int i = 0; i < MS_OBJ_TYPE_COUNT; i++) {
        if (counts[i] == 0) continue;
        MsValue k = MS_OBJ_VAL(ms_obj_string_copy(vm, names[i], (int)strlen(names[i])));
        ms_vtable_set(vm, MS_AS_MAP(map), k, MS_INT_VAL(counts[i]));
    }
    return map;
}
```

---

## 实现（`src/stdlib/gc.c`）

```c
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/memory.h"

static MsValue ms_gc_pause(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    vm->gc_paused = true;
    return MS_NIL_VAL();
}

static MsValue ms_gc_resume(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    vm->gc_paused = false;
    return MS_NIL_VAL();
}

static MsValue ms_gc_set_threshold(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    int64_t n = MS_AS_INT(argv[0]);
    if (n < 1024) n = 1024;
    vm->next_gc = (size_t)n;
    return MS_NIL_VAL();
}

static const MsNativeDef ms_gc_defs[] = {
    {"collect",       ms_gc_collect,       0},
    {"minor",         ms_gc_minor,         0},
    {"alive",         ms_gc_alive,         0},
    {"bytes_allocated",ms_gc_bytes_allocated,0},
    {"threshold",     ms_gc_threshold,     0},
    {"object_counts", ms_gc_object_counts, 0},
    {"set_threshold", ms_gc_set_threshold, 1},
    {"pause",         ms_gc_pause,         0},
    {"resume",        ms_gc_resume,        0},
    {"is_paused",     ms_gc_is_paused,     0},
    {NULL, NULL, 0}
};

void ms_module_gc_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_gc_defs);
}
```

---

## 依赖

- CAPI-01/02（注册）
- `src/vm_gc.c`（`ms_collect_garbage`）
- `include/ms/vm.h`（`gc_paused`、`alive_count`、`bytes_allocated`、`next_gc`）

---

## 测试

```ms
// tests/fixtures/stdlib_gc_basic.ms
import gc

gc.pause()
assert(gc.is_paused())
gc.resume()
assert(!gc.is_paused())

var before = gc.bytes_allocated()
gc.collect()
// 收集后 bytes 减少（不保证，但不报错）

var counts = gc.object_counts()
print(counts)

gc.set_threshold(1024 * 1024)  // 1MB
assert(gc.threshold() == 1024 * 1024)
```

```c
// tests/unit/test_stdlib_gc.c
// 1. collect 返回 >= 0 的整数
// 2. pause 后 ms_maybe_collect 不执行（通过计数器验证）
// 3. set_threshold < 1024 被钳制到 1024
// 4. object_counts 返回非空 map
```
