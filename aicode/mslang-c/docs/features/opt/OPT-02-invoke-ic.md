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

IC 的分配（`ensure_ic`，见 `vm.c`）、查表（`ic_get_field_slot`）、  
megamorphic 短路（`ic->megamorphic` 标志，位于 `MsInlineCache` 顶层）已完整，只需在 INVOKE 处补写入和查表路径。

## 目标

- `MS_OP_INVOKE` 命中缓存时：shape 匹配 → 直接取 closure 指针 → 不走 `ms_shape_find_slot`
- cache miss：走现有 shape 查找 → 填缓存 → 执行
- megamorphic：直接走 shape 查找，不再写缓存

## 数据结构确认（无需修改）

`MsICEntry`（`include/ms/shape.h:55-60`）实际定义：

```c
typedef struct {
    uint32_t  shape_id;
    uint32_t  slot_index;
    MsICKind  kind;
    MsValue   cached;   /* cached method value (for MS_IC_METHOD) */
} MsICEntry;
```

`cached` 字段**已存在**，专为 `MS_IC_METHOD` 设计，类型为 `MsValue`（可直接存 closure 的 `MsValue`）。
**无需改动 `MsICEntry` 结构**，只需在 INVOKE 路径中启用 `cached` 的写入和读取。

`megamorphic` 标志位于 `MsInlineCache` 顶层（`include/ms/shape.h:62-66`）：

```c
typedef struct MsInlineCache {
    MsICEntry entries[MS_IC_PIC_SIZE];
    uint8_t   count;
    bool      megamorphic;   /* ← 顶层，不在 MsICEntry 内 */
} MsInlineCache;
```

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
    MsValue cached_val = MS_NIL_VAL();
    if (!ic->megamorphic) {                             /* ← ic->megamorphic，不是 entries[0].megamorphic */
        for (int i = 0; i < ic->count; i++) {
            MsICEntry* e = &ic->entries[i];
            if (e->kind == MS_IC_METHOD && e->shape_id == inst->shape->id) {
                cached_val = e->cached;                 /* ← 使用已有的 cached 字段（MsValue） */
                break;
            }
        }
    }

    if (MS_IS_CLOSURE(cached_val)) {
        // 2a. 命中：直接调用（跳过外层 MS_IS_CLOSURE 检查）
        MsInterpretResult cr = call_value(vm, cached_val, argc, A); /* call_value 是实际 API */
        if (cr != MS_INTERPRET_OK) return cr;
        frame = &vm->frames[vm->frame_count - 1];
    } else {
        // 2b. Miss：shape 查找（字段优先，然后类方法表）
        int slot = ms_shape_find_slot(inst->shape, name);
        MsValue method_val = (slot >= 0) ? inst_get_by_slot(inst, slot) : MS_NIL_VAL();
        if (!MS_IS_CLOSURE(method_val)) {
            if (!ms_table_get(&inst->klass->methods, name, &method_val) ||
                !MS_IS_CLOSURE(method_val)) { /* 报错 */ break; }
        }

        // 3. 填缓存（非 megamorphic）
        if (ic->count < MS_IC_PIC_SIZE) {
            MsICEntry* e = &ic->entries[ic->count++];
            e->kind     = MS_IC_METHOD;
            e->shape_id = inst->shape->id;
            e->cached   = method_val;                   /* ← 写入 cached 字段 */
        } else {
            ic->megamorphic = true;                     /* ← ic->megamorphic */
        }

        MsInterpretResult cr = call_value(vm, method_val, argc, A);
        if (cr != MS_INTERPRET_OK) return cr;
        frame = &vm->frames[vm->frame_count - 1];
    }
    break;
}
```

> `call_value`（`src/vm.c:485`）是实际 API，直接传入 closure 的 `MsValue` 即可。
> 不存在 `call_closure` 函数，无需新增。

## 关键路径改动文件

| 文件 | 修改 |
|---|---|
| `include/ms/shape.h` | **无需改动**；`MsICEntry.cached` 和 `MsInlineCache.megamorphic` 已就位 |
| `src/vm.c` (`MS_OP_INVOKE`) | 补写 IC 查表 + 写缓存路径（见上伪码） |
| `src/vm.c` `ic_get_field_slot` | 可提取为通用 `ic_lookup`，被 GETPROP + INVOKE 共用（可选重构） |

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
