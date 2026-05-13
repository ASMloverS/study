# OPT-02: INVOKE / CALL_METHOD 接入 Inline Cache

## 背景

`MS_OP_INVOKE`（`src/vm.c:1413`）目前每次都走 shape 查找，  
没有利用已有的 PIC（多态内联缓存）基础设施。

`include/ms/shape.h:50-53` 已定义：

```c
typedef enum {
    MS_IC_FIELD,   // 只有这个被实际使用
    MS_IC_METHOD,  // 闲置
    MS_IC_GETTER,  // 闲置
    MS_IC_SETTER,  // 闲置
} MsICKind;
```

IC 的分配（`vm.c:52-65` `ensure_ic`）、查表（`vm.c:68-77` `ic_get_field_slot`）、  
megamorphic 短路（`vm.c:86-88`）已完整，只需在 INVOKE 处补写入和查表路径。

## 目标

- `MS_OP_INVOKE` 命中缓存时：shape 匹配 → 直接取 closure 指针 → 不走 `ms_shape_find_slot`
- cache miss：走现有 shape 查找 → 填缓存 → 执行
- megamorphic：直接走 shape 查找，不再写缓存

## 数据结构扩展

`MsICEntry`（`include/ms/shape.h:57-66`）现有字段：

```c
typedef struct {
    MsICKind kind;
    uint32_t shape_id;
    int slot_index;      // 字段偏移
    bool megamorphic;
} MsICEntry;
```

方法缓存不需要 `slot_index`，需要 `closure` 指针。  
两种方式：

**方案 A（推荐）**：union 复用

```c
typedef struct {
    MsICKind kind;
    uint32_t shape_id;
    union {
        int slot_index;               // MS_IC_FIELD
        MsObjClosure* method_closure; // MS_IC_METHOD
    };
    bool megamorphic;
} MsICEntry;
```

**方案 B**：单独 `MsMethodICEntry` 数组（侵入更少，但要改 `ensure_ic` 分配逻辑）。

推荐方案 A，改动最小，union 不增加结构体大小。

## VM 修改（src/vm.c）

### INVOKE 处理逻辑（vm.c:1413 起）

```c
case MS_OP_INVOKE: {
    // A = 接收者寄存器, B = 方法名常量索引, C = 参数数量
    MsValue receiver = frame->slots[A];
    if (!MS_IS_INSTANCE(receiver)) { /* 报错 */ break; }
    MsObjInstance* inst = MS_AS_INSTANCE(receiver);
    ObjString* name = MS_AS_STRING(K(B));
    int argc = C;

    // 1. 查 IC
    MsIC* ic = ensure_ic(vm, frame->closure->function, (int)(ip - frame->closure->function->chunk.code.data - 1));
    MsObjClosure* cached = NULL;
    if (!ic->entries[0].megamorphic) {
        for (int i = 0; i < ic->count; i++) {
            MsICEntry* e = &ic->entries[i];
            if (e->kind == MS_IC_METHOD && e->shape_id == inst->shape->id) {
                cached = e->method_closure;
                break;
            }
        }
    }

    if (cached) {
        // 2a. 命中：直接调用
        call_closure(vm, cached, receiver, argc);
    } else {
        // 2b. Miss：shape 查找
        int slot = ms_shape_find_slot(inst->shape, name);
        MsValue method_val = /* 从 slot 读值 */;
        if (!MS_IS_CLOSURE(method_val)) { /* 报错 */ break; }
        MsObjClosure* closure = MS_AS_CLOSURE(method_val);

        // 3. 填缓存（非 megamorphic）
        if (ic->count < MS_IC_PIC_SIZE) {
            MsICEntry* e = &ic->entries[ic->count++];
            e->kind = MS_IC_METHOD;
            e->shape_id = inst->shape->id;
            e->method_closure = closure;
        } else {
            ic->entries[0].megamorphic = true;
        }

        call_closure(vm, closure, receiver, argc);
    }
    break;
}
```

> `call_closure` 复用现有的 `call_value` 入口，传入 closure 直接进入已知路径，
> 跳过外层 `MS_IS_CLOSURE` 检查以减少 1 次 branch。

## 关键路径改动文件

| 文件 | 修改 |
|---|---|
| `include/ms/shape.h:57-66` | `MsICEntry` 加 union |
| `src/vm.c:1413` 起 | `MS_OP_INVOKE` 处理逻辑 |
| `src/vm.c:68-77` `ic_get_field_slot` | 可提取为通用 `ic_lookup`，被 GETPROP + INVOKE 共用 |

## 验证

```bash
cmake --build build && cd build && ctest --output-on-failure
python ../benchmarks/run_all.py --compare baseline.json --runs 3
```

重点 benchmark：
- `method_dispatch.ms`（单态方法）：预期 -20~35%
- `shape_mono_field.ms`：验证 GETPROP IC 未被破坏
- `shape_poly_field.ms`：验证 megamorphic 短路正常

一致性测试：`tests/conformance/` 100% 通过。
