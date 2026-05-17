# STDLIB-09: debug 模块

## 职责

运行时自省工具：调用栈、当前帧信息、局部变量快照、函数反汇编、VM 统计。不对外暴露 GC 内部对象指针，只返回值快照。

---

## 函数清单

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `debug.traceback(depth=0)` | int | str | 格式化调用栈字符串（最多 64 帧）；depth=0 = 当前位置 |
| `debug.frame_info(depth=0)` | int | map | `{name, file, line}`；depth=0 = 直接调用者 |
| `debug.locals(depth=0)` | int | map | 当前帧局部变量名→值快照（值拷贝）|
| `debug.upvalues(closure)` | Closure | list | 每个 upvalue 的 `{name, value}` map |
| `debug.disasm(fn)` | Function\|Closure | str | 复用 `ms_chunk_disassemble`，返回字符串 |
| `debug.is_native(fn)` | any | bool | fn 是否是 ObjNative |
| `debug.is_closure(fn)` | any | bool | fn 是否是 ObjClosure |
| `debug.typeof(v)` | any | str | 与全局 `type` 相同，供调试脚本使用 |
| `debug.vm_stats()` | – | map | `{stack_used, frame_used, gc_bytes, gc_alive, gc_threshold}` |
| `debug.gc_trace(enable)` | bool | nil | 开启/关闭 GC verbose 输出（写 stderr）|

---

## 实现（`src/stdlib/debug.c`）

```c
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/debug.h"
#include "ms/object.h"

/* debug.traceback */
static MsValue ms_debug_traceback(MsVM* vm, int argc, MsValue* argv) {
    int skip = (argc >= 1 && MS_IS_INT(argv[0])) ? (int)MS_AS_INT(argv[0]) : 0;
    /* 遍历 vm->frames[0..vm->frame_count-1-skip]，反向拼接字符串 */
    MsObjStringBuilder sb;
    ms_obj_sb_init(&sb);
    for (int i = vm->frame_count - 1 - skip; i >= 0; i--) {
        MsCallFrame* frame = &vm->frames[i];
        MsObjFunction* fn  = frame->closure->function;
        int line = ms_chunk_get_line(&fn->chunk,
                       (int)(frame->ip - fn->chunk.code - 1));
        char buf[256];
        snprintf(buf, sizeof(buf), "  at %s (%s:%d)\n",
                 fn->name ? fn->name->chars : "<script>",
                 fn->chunk.source ? fn->chunk.source->chars : "?",
                 line);
        ms_obj_sb_append_cstr(&sb, buf);
    }
    MsObjString* s = ms_obj_sb_finish(vm, &sb);
    return MS_OBJ_VAL(s);
}

/* debug.disasm */
static MsValue ms_debug_disasm(MsVM* vm, int argc, MsValue* argv) {
    MsValue fn_val = argv[0];
    MsObjFunction* fn = NULL;
    if (MS_IS_OBJ(fn_val)) {
        if (MS_OBJ_TYPE(fn_val) == MS_OBJ_FUNCTION)
            fn = (MsObjFunction*)MS_AS_OBJ(fn_val);
        else if (MS_OBJ_TYPE(fn_val) == MS_OBJ_CLOSURE)
            fn = ((MsObjClosure*)MS_AS_OBJ(fn_val))->function;
    }
    if (!fn) {
        ms_vm_runtime_error(vm, "debug.disasm: expected Function or Closure");
        return MS_NIL_VAL();
    }
    /* ms_chunk_disassemble 当前写到 stdout；此处改为写到字符串缓冲 */
    /* 方案：临时 redirect stdout 或为 disassembler 增加 FILE* 参数 */
    /* 推荐：为 ms_chunk_disassemble 增加 ms_chunk_disassemble_str(chunk, sb) 变体 */
    MsObjString* result = ms_chunk_disassemble_str(vm, &fn->chunk,
                                                    fn->name ? fn->name->chars : "<fn>");
    return MS_OBJ_VAL(result);
}

/* debug.vm_stats */
static MsValue ms_debug_vm_stats(MsVM* vm, int argc, MsValue* argv) {
    (void)argc; (void)argv;
    MsValue map = MS_OBJ_VAL(ms_obj_map_new(vm));
    ms_vtable_set(vm, MS_AS_MAP(map),
        MS_OBJ_VAL(ms_obj_string_copy(vm, "stack_used", 10)),
        MS_INT_VAL(vm->stack_top - vm->stack));
    ms_vtable_set(vm, MS_AS_MAP(map),
        MS_OBJ_VAL(ms_obj_string_copy(vm, "frame_used", 10)),
        MS_INT_VAL(vm->frame_count));
    ms_vtable_set(vm, MS_AS_MAP(map),
        MS_OBJ_VAL(ms_obj_string_copy(vm, "gc_bytes", 8)),
        MS_INT_VAL((int64_t)vm->bytes_allocated));
    ms_vtable_set(vm, MS_AS_MAP(map),
        MS_OBJ_VAL(ms_obj_string_copy(vm, "gc_threshold", 12)),
        MS_INT_VAL((int64_t)vm->next_gc));
    return map;
}

static const MsNativeDef ms_debug_defs[] = {
    {"traceback", ms_debug_traceback, -1},
    {"frame_info",ms_debug_frame_info,-1},
    {"locals",    ms_debug_locals,   -1},
    {"upvalues",  ms_debug_upvalues,  1},
    {"disasm",    ms_debug_disasm,    1},
    {"is_native", ms_debug_is_native, 1},
    {"is_closure",ms_debug_is_closure,1},
    {"typeof",    ms_debug_typeof,    1},
    {"vm_stats",  ms_debug_vm_stats,  0},
    {"gc_trace",  ms_debug_gc_trace,  1},
    {NULL, NULL, 0}
};

void ms_module_debug_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_debug_defs);
}
```

---

## `ms_chunk_disassemble_str` 变体（`src/debug.c`）

现有 `ms_chunk_disassemble` 输出到 `stdout`，需增加写到字符串的变体：

```c
/* 新增；返回 malloc'd string，调用方负责 free（或封装为 ObjString）*/
MsObjString* ms_chunk_disassemble_str(MsVM* vm,
                                       const MsChunk* chunk,
                                       const char* name);
```

内部用 `ms_obj_sb_*` 拼接，不改变原 `ms_chunk_disassemble` 接口。

---

## 依赖

- CAPI-01/02（注册）
- `include/ms/debug.h`（`ms_chunk_disassemble_str` 变体）
- 访问 `MsVM.frames`、`vm->stack_top`、`vm->bytes_allocated`、`vm->next_gc`

---

## 测试

```ms
// tests/fixtures/stdlib_debug_basic.ms
import debug

fun foo() {
    print(debug.traceback())
    var s = debug.vm_stats()
    assert(s["frame_used"] >= 1)
}

foo()
```

```ms
// tests/fixtures/stdlib_debug_disasm.ms
import debug

fun add(a, b) { return a + b }
var txt = debug.disasm(add)
print(txt)
assert(len(txt) > 0)
```
