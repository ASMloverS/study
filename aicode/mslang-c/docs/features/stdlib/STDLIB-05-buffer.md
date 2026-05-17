# STDLIB-05: buffer 模块

## 职责

提供可变字节缓冲 `ObjBuffer`。与 `ObjString` 的区别：Buffer 内容可原地修改、不被字符串池 intern、以字节为单位操作。是 io/net/hash 的底层数据容器。

---

## 函数清单

### 构造

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `buffer.new(size=0, fill=0)` | int,int | Buffer | 分配 size 字节，全部填充为 fill（0..255）|
| `buffer.from_str(s)` | str | Buffer | 字符串字节复制为 Buffer（不终止 NUL）|
| `buffer.from_hex(hex)` | str | Buffer | 十六进制字符串解码（忽略空格，奇数长报错）|
| `buffer.concat(a, b)` | Buffer,Buffer | Buffer | 两个 Buffer 拼接为新 Buffer |

### 方法（`ObjBuffer.invoke`）

| 方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `b.len()` | – | int | 当前字节数 |
| `b.cap()` | – | int | 当前容量 |
| `b.get(i)` | int | int | 第 i 字节（0..255）；负下标从尾部算 |
| `b.set(i, v)` | int,int | nil | 设置第 i 字节（v 钳制到 0..255）|
| `b.slice(start, end=-1)` | int,int | Buffer | 子切片（副本），end=-1 表示到尾 |
| `b.append(x)` | Buffer\|str | nil | 原地追加；str 按 UTF-8 字节追加 |
| `b.prepend(x)` | Buffer\|str | nil | 原地前插（memmove）|
| `b.fill(v, start=0, end=-1)` | int,int,int | nil | 区间填充 |
| `b.copy()` | – | Buffer | 深拷贝 |
| `b.to_str()` | – | str | 按字节转字符串（不做 UTF-8 验证）|
| `b.to_hex()` | – | str | 逐字节 `%02x`，小写 |
| `b.find(sub, start=0)` | Buffer\|str,int | int | 返回首次出现的字节偏移；-1 未找到 |
| `b.replace(old, new, count=-1)` | Buffer,Buffer,int | Buffer | 新副本，替换 count 次（-1=全部）|
| `b.equals(x)` | Buffer | bool | 等长且 memcmp==0 |
| `b.clear()` | – | nil | len 置 0（不释放 cap）|
| `b.resize(n, fill=0)` | int,int | nil | 调整 len；增大时用 fill 填充 |

---

## 数据结构（`include/ms/stdlib/objbuffer.h`）

```c
typedef struct {
    MsObject obj;    /* type = MS_OBJ_BUFFER */
    uint8_t* data;   /* GC 管理：ms_reallocate */
    int      len;    /* 有效字节数 */
    int      cap;    /* 分配容量 */
} MsObjBuffer;
```

容量增长策略：`new_cap = max(8, next_power_of_2(needed))`。

---

## 实现要点（`src/stdlib/buffer.c`）

```c
MsObjBuffer* ms_obj_buffer_new(MsVM* vm, int initial_cap) {
    MsObjBuffer* b = (MsObjBuffer*)ms_allocate_object(
        vm, sizeof(MsObjBuffer), MS_OBJ_BUFFER);
    b->len = 0;
    b->cap = 0;
    b->data = NULL;
    if (initial_cap > 0) buf_ensure(vm, b, initial_cap);
    return b;
}

static void buf_ensure(MsVM* vm, MsObjBuffer* b, int needed) {
    if (b->cap >= needed) return;
    int new_cap = b->cap < 8 ? 8 : b->cap;
    while (new_cap < needed) new_cap <<= 1;
    b->data = (uint8_t*)ms_reallocate(vm, b->data,
                                       (size_t)b->cap,
                                       (size_t)new_cap);
    b->cap = new_cap;
}

/* append */
static MsValue ms_buf_append(MsVM* vm, MsObjBuffer* self, int argc, MsValue* argv) {
    (void)argc;
    if (MS_IS_BUFFER(argv[0])) {
        MsObjBuffer* other = MS_AS_BUFFER(argv[0]);
        buf_ensure(vm, self, self->len + other->len);
        memcpy(self->data + self->len, other->data, (size_t)other->len);
        self->len += other->len;
    } else if (MS_IS_STRING(argv[0])) {
        MsObjString* s = MS_AS_STRING(argv[0]);
        buf_ensure(vm, self, self->len + s->length);
        memcpy(self->data + self->len, s->chars, (size_t)s->length);
        self->len += s->length;
    } else {
        ms_vm_runtime_error(vm, "buffer.append: expected Buffer or str");
    }
    return MS_NIL_VAL();
}
```

### GC free

```c
case MS_OBJ_BUFFER: {
    MsObjBuffer* b = (MsObjBuffer*)obj;
    ms_reallocate(vm, b->data, (size_t)b->cap, 0);
    break;
}
```

Buffer 不持有其他 GC 对象，trace 为空。

---

## 依赖

- CAPI-01/02（注册）
- CAPI-06（MS_OBJ_BUFFER 枚举值、ms_builtin_invoke 分支）

---

## 测试

```ms
// tests/fixtures/stdlib_buffer_basic.ms
import buffer

var b = buffer.from_str("hello")
assert(b.len() == 5)
assert(b.get(0) == 104)        // 'h'
assert(b.to_hex() == "68656c6c6f")

b.append(" world")
assert(b.len() == 11)
assert(b.to_str() == "hello world")

var c = b.slice(0, 5)
assert(c.to_str() == "hello")

var d = buffer.from_hex("deadbeef")
assert(d.len() == 4)
assert(d.get(0) == 0xde)
```

```c
// tests/unit/test_stdlib_buffer.c
// 1. from_hex 奇数长度抛错
// 2. get/set 负下标（-1 = 最后一字节）
// 3. resize 增大时 fill 生效
// 4. find 未找到返 -1
// 5. GC 后 buffer 数据不 use-after-free（valgrind/asan 下跑）
```
