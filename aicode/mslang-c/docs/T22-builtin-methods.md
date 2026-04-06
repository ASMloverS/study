# Task 22: Built-in Type Methods

> **For agentic workers:** Use superpowers:executing-plans to implement this task.

**Goal:** Implement built-in methods for string, list, map, tuple types, plus ObjStringBuilder.
**Dependencies:** T21
**Produces:** `"hello".len()`, `list.push()`, `map.keys()` 等内置方法可用

## Files

| Action | Path | Purpose |
|--------|------|---------|
| Create | `src/vm_builtins.c` | 内置方法分派 |
| Modify | `include/ms/object.h` | ObjStringBuilder 结构 |
| Modify | `src/object.c` | StringBuilder 创建/销毁 |
| Modify | `src/vm.c` | INVOKE 分派到 builtins |
| Create | `tests/unit/test_builtins.c` | 内置方法测试 |

## Key Data Structures / API

```c
// 内置方法分派入口 (从 INVOKE / GETPROP 调用)
// 返回 true 表示已处理, result 放入 *out
bool ms_builtin_invoke(MsVM* vm, MsValue receiver, MsObjString* method,
                        int argc, MsValue* argv, MsValue* out);

// ObjStringBuilder
typedef struct {
    MsObject obj;
    char* buffer;
    int length;
    int capacity;
} MsObjStringBuilder;

MsObjStringBuilder* ms_obj_sb_new(MsVM* vm);
void ms_obj_sb_append(MsObjStringBuilder* sb, const char* str, int len);
MsObjString* ms_obj_sb_to_string(MsVM* vm, MsObjStringBuilder* sb);
```

## Implementation Notes

### String 方法

| 方法 | 签名 | 说明 |
|------|------|------|
| `len()` | → int | 字符串长度 |
| `upper()` | → string | 转大写 |
| `lower()` | → string | 转小写 |
| `contains(s)` | → bool | 是否包含子串 |
| `starts_with(s)` | → bool | 前缀匹配 |
| `ends_with(s)` | → bool | 后缀匹配 |
| `index_of(s)` | → int | 子串位置, -1=未找到 |
| `split(sep)` | → list | 按分隔符拆分 |
| `trim()` | → string | 去除首尾空白 |
| `replace(old, new)` | → string | 替换子串 |
| `slice(start, end)` | → string | 子串切片 |

### List 方法

| 方法 | 签名 | 说明 |
|------|------|------|
| `len()` | → int | 列表长度 |
| `push(val)` | → nil | 追加元素 |
| `pop()` | → value | 弹出末尾元素 |
| `contains(val)` | → bool | 是否包含 |
| `index_of(val)` | → int | 元素位置 |
| `remove(idx)` | → value | 按索引移除 |
| `sort()` | → list | 原地排序 (数值/字符串) |
| `reverse()` | → list | 原地反转 |
| `map(fn)` | → list | 映射函数 |
| `filter(fn)` | → list | 过滤函数 |
| `join(sep)` | → string | 用分隔符连接 |
| `slice(start, end)` | → list | 切片 |

### Map 方法

| 方法 | 签名 | 说明 |
|------|------|------|
| `len()` | → int | 条目数 |
| `keys()` | → list | 所有键列表 |
| `values()` | → list | 所有值列表 |
| `has(key)` | → bool | 是否存在 |
| `remove(key)` | → bool | 删除条目 |

### Tuple 方法

| 方法 | 签名 | 说明 |
|------|------|------|
| `len()` | → int | 长度 |
| `contains(val)` | → bool | 是否包含 |

### 分派逻辑

在 INVOKE 中, 若 receiver 不是 ObjInstance, 尝试 ms_builtin_invoke:
```c
if (MS_IS_STRING(receiver)) {
    return string_invoke(vm, MS_AS_STRING(receiver), method, argc, argv, out);
} else if (MS_IS_LIST(receiver)) {
    return list_invoke(vm, MS_AS_LIST(receiver), method, argc, argv, out);
}
// ...
```

## C Unit Tests

```c
// tests/unit/test_builtins.c
#include "test_assert.h"
#include "ms/vm.h"

static void test_string_len(void) {
    MsVM vm; ms_vm_init(&vm);
    MsInterpretResult r = ms_vm_interpret(&vm,
        "print(\"hello\".len())", "<test>");
    TEST_ASSERT_EQ(r, MS_INTERPRET_OK);
    ms_vm_free(&vm);
}

int main(void) {
    test_string_len();
    printf("test_builtins: all passed\n");
    return 0;
}
```

## .ms Integration Tests

```ms
// tests/fixtures/string_methods.ms
print("hello".len())
// expect: 5
print("Hello".upper())
// expect: HELLO
print("Hello".lower())
// expect: hello
print("hello world".contains("world"))
// expect: true
print("hello".starts_with("hel"))
// expect: true
print("hello".ends_with("llo"))
// expect: true
print("hello".index_of("ll"))
// expect: 2
print("a,b,c".split(",").len())
// expect: 3
print("  hello  ".trim())
// expect: hello
print("hello".replace("ll", "r"))
// expect: hero
print("hello".slice(1, 3))
// expect: el

// tests/fixtures/list_methods.ms
var a = [3, 1, 2]
print(a.len())
// expect: 3
a.push(4)
print(a.len())
// expect: 4
print(a.pop())
// expect: 4
print(a.contains(2))
// expect: true
print(a.index_of(1))
// expect: 1
a.sort()
print(a[0])
// expect: 1
a.reverse()
print(a[0])
// expect: 3

var doubled = [1,2,3].map(fun(x) { return x * 2 })
print(doubled[0])
// expect: 2
print(doubled[2])
// expect: 6

var evens = [1,2,3,4,5].filter(fun(x) { return x % 2 == 0 })
print(evens.len())
// expect: 2

print([1,2,3].join("-"))
// expect: 1-2-3

// tests/fixtures/map_methods.ms
var m = {"a": 1, "b": 2}
print(m.len())
// expect: 2
print(m.has("a"))
// expect: true
print(m.has("c"))
// expect: false
m.remove("a")
print(m.len())
// expect: 1

// tests/fixtures/tuple_methods.ms
var t = (1, 2, 3)
print(t.len())
// expect: 3
print(t.contains(2))
// expect: true
print(t.contains(5))
// expect: false
```
