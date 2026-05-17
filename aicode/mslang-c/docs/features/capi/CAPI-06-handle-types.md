# CAPI-06: 句柄类型（ObjFile / ObjBuffer / MsObjUserdata）

## 目标

`io.open()` 返回 `File` 对象，`buffer.new()` 返回 `Buffer` 对象，用户可在其上调用方法（`f.read()`、`b.append()`）。实现路径与 `ObjSocket` 完全对称：

1. 新增 `MS_OBJ_FILE` / `MS_OBJ_BUFFER` 枚举值
2. 定义 `MsObjFile` / `MsObjBuffer` 结构
3. 在 `ms_builtin_invoke` 中增加对应 `case`，分发到各自的 `*_invoke` 函数
4. GC：print / free / trace 各加一个 case

---

## ObjFile（`include/ms/stdlib/objfile.h`）

```c
#ifndef MS_OBJFILE_H
#define MS_OBJFILE_H

#include "ms/object.h"
#include <stdio.h>

typedef enum {
    MS_FILE_TEXT   = 0,
    MS_FILE_BINARY = 1,
} MsFileMode;

typedef struct {
    MsObject   obj;      /* type = MS_OBJ_FILE */
    FILE*      fp;       /* NULL 表示已关闭 */
    MsFileMode mode;
    bool       eof;
} MsObjFile;

MsObjFile*  ms_obj_file_new(MsVM* vm, FILE* fp, MsFileMode mode);
MsObjFile*  ms_obj_file_from_fd(MsVM* vm, int fd, const char* open_mode);
/* 方法分发入口（被 ms_builtin_invoke 调用）*/
bool        ms_objfile_invoke(MsVM* vm, MsObjFile* f,
                              const char* method,
                              int argc, MsValue* argv,
                              MsValue* out);

#endif
```

### 方法实现（`src/stdlib/io.c`）

| 方法 | 实现 |
|---|---|
| `read(n)` | n < 0 → `fread` 到 EOF；否则读 n 字节；文本模式返 ObjString，二进制返 ObjBuffer |
| `readline()` | `fgets` 循环；EOF 返 `MS_NIL_VAL()` |
| `write(data)` | str → `fwrite`；Buffer → `fwrite(buf->data, 1, buf->len, fp)` |
| `flush()` | `fflush(fp)` |
| `seek(off, whence)` | `fseek` |
| `tell()` | `ftell` |
| `close()` | `fclose(fp); f->fp = NULL` |
| `eof()` | `MS_BOOL_VAL(f->eof || feof(f->fp))` |
| `fd()` | `fileno(f->fp)` |

### GC（`src/vm_gc.c`）

```c
case MS_OBJ_FILE: {
    MsObjFile* f = (MsObjFile*)obj;
    if (f->fp) { fclose(f->fp); f->fp = NULL; }
    break;
}
```

File 不持有其他 GC 对象，trace 为空。

---

## ObjBuffer（`include/ms/stdlib/objbuffer.h`）

```c
#ifndef MS_OBJBUFFER_H
#define MS_OBJBUFFER_H

#include "ms/object.h"

typedef struct {
    MsObject obj;      /* type = MS_OBJ_BUFFER */
    uint8_t* data;
    int      len;
    int      cap;
} MsObjBuffer;

MsObjBuffer* ms_obj_buffer_new(MsVM* vm, int initial_cap);
MsObjBuffer* ms_obj_buffer_from_bytes(MsVM* vm, const uint8_t* bytes, int len);
bool         ms_objbuffer_invoke(MsVM* vm, MsObjBuffer* b,
                                 const char* method,
                                 int argc, MsValue* argv,
                                 MsValue* out);

#endif
```

### 内存管理

```c
/* 确保 cap >= needed，采用 2x 增长 */
static void buf_ensure(MsVM* vm, MsObjBuffer* b, int needed) {
    if (b->cap >= needed) return;
    int new_cap = b->cap < 8 ? 8 : b->cap;
    while (new_cap < needed) new_cap *= 2;
    b->data = (uint8_t*)ms_reallocate(vm, b->data,
                                       (size_t)b->cap,
                                       (size_t)new_cap);
    b->cap = new_cap;
}
```

`ms_reallocate` 在 `memory.h` 中已有，复用即可。GC free：`ms_reallocate(vm, b->data, b->cap, 0)`（置 0 容量触发 free）。

### 方法实现（`src/stdlib/buffer.c`）

| 方法 | 描述 |
|---|---|
| `len()` | `MS_INT_VAL(b->len)` |
| `get(i)` | 边界检查后 `MS_INT_VAL(b->data[i])` |
| `set(i, v)` | `b->data[i] = (uint8_t)MS_AS_INT(v)` |
| `slice(s,e)` | 新建 ObjBuffer，`memcpy` |
| `append(x)` | in-place：`buf_ensure` + `memcpy` |
| `concat(x)` | 新建 ObjBuffer，`memcpy` 两段 |
| `to_str()` | `ms_obj_string_copy(vm, (char*)b->data, b->len)` |
| `to_hex()` | 逐字节 sprintf `%02x`，返回 ObjString |
| `find(sub)` | memmem / 手写 naive search |
| `equals(x)` | `MS_BOOL_VAL(b->len==x->len && memcmp(...)==0)` |
| `copy()` | `ms_obj_buffer_from_bytes(vm, b->data, b->len)` |

---

## `ms_builtin_invoke` 修改（`src/vm_builtins.c`）

```c
bool ms_builtin_invoke(MsVM* vm, MsValue receiver, const char* method,
                       int argc, MsValue* argv, MsValue* out) {
    if (!MS_IS_OBJ(receiver)) return false;
    switch (MS_OBJ_TYPE(receiver)) {
        case MS_OBJ_STRING:  return ms_string_invoke(vm, receiver, method, argc, argv, out);
        case MS_OBJ_LIST:    return ms_list_invoke(vm, receiver, method, argc, argv, out);
        case MS_OBJ_MAP:     return ms_map_invoke(vm, receiver, method, argc, argv, out);
        case MS_OBJ_TUPLE:   return ms_tuple_invoke(vm, receiver, method, argc, argv, out);
        case MS_OBJ_SOCKET:  return ms_socket_invoke(vm, receiver, method, argc, argv, out);
        /* ★ 新增 */
        case MS_OBJ_FILE:    return ms_objfile_invoke(vm, MS_AS_FILE(receiver), method, argc, argv, out);
        case MS_OBJ_BUFFER:  return ms_objbuffer_invoke(vm, MS_AS_BUFFER(receiver), method, argc, argv, out);
        case MS_OBJ_USERDATA: return ms_userdata_invoke(vm, receiver, method, argc, argv, out);
        default: return false;
    }
}
```

`ms_userdata_invoke`：通过 `type_tag` 查注册的方法表（v1 简化：每种 userdata 必须在 `type_tag` 上 dispatch，或不支持方法调用，直接返回 false）。

---

## `object.h` 修改

```c
typedef enum {
    // ... 已有 ...
    MS_OBJ_SOCKET,
    MS_OBJ_FILE,     /* ★ */
    MS_OBJ_BUFFER,   /* ★ */
    MS_OBJ_USERDATA, /* ★ */
} MsObjectType;

/* 访问宏 */
#define MS_IS_FILE(v)   (MS_IS_OBJ(v) && MS_OBJ_TYPE(v) == MS_OBJ_FILE)
#define MS_AS_FILE(v)   ((MsObjFile*)MS_AS_OBJ(v))
#define MS_IS_BUFFER(v) (MS_IS_OBJ(v) && MS_OBJ_TYPE(v) == MS_OBJ_BUFFER)
#define MS_AS_BUFFER(v) ((MsObjBuffer*)MS_AS_OBJ(v))
```

---

## 文件修改清单

| 文件 | 操作 |
|---|---|
| `include/ms/object.h` | 增 `MS_OBJ_FILE/BUFFER/USERDATA`、宏、`MsObjUserdata` 定义 |
| `include/ms/stdlib/objfile.h` | 新建 |
| `include/ms/stdlib/objbuffer.h` | 新建 |
| `src/object.c` | `ms_obj_print`/`ms_obj_free` 增加 3 个 case |
| `src/vm_gc.c` | `ms_mark_object`/free 增加 3 个 case |
| `src/vm_builtins.c` | `ms_builtin_invoke` 增加 3 个 case |
| `src/stdlib/io.c` | `MsObjFile` 构造、方法实现 |
| `src/stdlib/buffer.c` | `MsObjBuffer` 构造、方法实现 |

---

## 测试要点

```ms
// tests/fixtures/stdlib_io_file.ms
import io
var f = io.open("tests/fixtures/hello.txt", "r")
print(f.read())
f.close()
```

```ms
// tests/fixtures/stdlib_buffer_basic.ms
import buffer
var b = buffer.from_str("hello")
print(b.len())     // 5
print(b.to_hex())  // 68656c6c6f
b.append(" world")
print(b.to_str())  // hello world
```
