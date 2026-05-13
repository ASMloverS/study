# OPT-01: ObjectPool 接入 upvalue / bound method

## 背景

`MsObjectPool` 已完整实现（`include/ms/memory.h:23-38`, `src/memory.c:54-97`），  
`vm->upvalue_pool` 与 `vm->bound_pool` 也已在 `src/vm.c:159-160` 初始化。  
但 `ms_pool_alloc` 的调用方为零——所有 upvalue (48B) 和 bound method (40B) 都走 `ms_allocate_object` → `realloc`，每次都有系统调用开销。

这是成本最低、收益最确定的单点优化：基础设施已就绪，只需接通调用点。

## 影响对象

| 类型 | 大小 | 当前分配路径 | 高频场景 |
|---|---|---|---|
| `MsObjUpvalue` | 48B | `ms_obj_upvalue_new` → `MS_ALLOC_OBJ` → realloc | 每次 `capture_upvalue` |
| `MsObjBoundMethod` | 40B | `ms_obj_bound_method_new` → `MS_ALLOC_OBJ` → realloc | 每次方法绑定 |

## 修改点

### 分配路径

**`src/object.c:116-122`** — `ms_obj_upvalue_new`

```c
// 改前
MsObjUpvalue* ms_obj_upvalue_new(MsVM* vm, MsValue* slot) {
    MsObjUpvalue* uv = (MsObjUpvalue*)ms_allocate_object(vm, sizeof(MsObjUpvalue), MS_OBJ_UPVALUE);
    ...
}

// 改后
MsObjUpvalue* ms_obj_upvalue_new(MsVM* vm, MsValue* slot) {
    MsObjUpvalue* uv = (MsObjUpvalue*)ms_pool_alloc(&vm->upvalue_pool);
    uv->obj.type             = MS_OBJ_UPVALUE;
    uv->obj.is_marked        = false;
    uv->obj.in_remembered_set = false;
    uv->obj.generation       = 0;
    uv->obj.age              = 0;
    uv->obj.next             = (MsObject*)vm->open_upvalues; /* 接入 GC 链由调用方完成 */
    ...
}
```

**`src/object.c:158-165`** — `ms_obj_bound_method_new`（行号近似，grep `MS_OBJ_BOUND_METHOD` 确认）

```c
// 改后：走 bound_pool
MsObjBoundMethod* ms_obj_bound_method_new(MsVM* vm, MsValue receiver, MsObjClosure* method) {
    MsObjBoundMethod* bm = (MsObjBoundMethod*)ms_pool_alloc(&vm->bound_pool);
    bm->obj.type             = MS_OBJ_BOUND_METHOD;
    bm->obj.is_marked        = false;
    bm->obj.in_remembered_set = false;
    bm->obj.generation       = 0;
    bm->obj.age              = 0;
    bm->obj.next             = NULL; /* 挂入 GC 链由 ms_alloc_object 之后的逻辑负责 */
    ...
}
```

### 释放路径

**`src/vm_gc.c`** — `sweep_generation` 或 `sweep_all` 中对应分支  
当 `obj->type == MS_OBJ_UPVALUE` 且 sweep 决定回收时，改调 `ms_pool_free_obj`：

```c
case MS_OBJ_UPVALUE:
    ms_pool_free_obj(&vm->upvalue_pool, obj);
    break;
case MS_OBJ_BOUND_METHOD:
    ms_pool_free_obj(&vm->bound_pool, obj);
    break;
```

> **`bytes_allocated` 协议**：`ms_pool_alloc`（`memory.c:60`）内部使用 `malloc` 直接分配 slab，
> **不经过** `ms_reallocate`，因此不会更新 `vm->bytes_allocated`。  
> 这意味着 pool 对象不计入 GC 阈值触发计算，也不影响 `bytes_allocated_peak` 统计。  
> `ms_pool_free_obj` 仅把槽挂回 freelist（`memory.c:83`），同样不更新计数，无需对称减。  
> 若需精确内存统计，可在首次创建 slab 时手动调用一次 `ms_reallocate` 模拟入账（可选）。

### MsObject 头初始化责任

`ms_pool_alloc` 只返回裸内存——**不初始化任何字段**（与 `ms_alloc_object` 不同，后者在内部设好 `obj->type`、`obj->next`、`obj->generation` 等并将对象挂入 GC 链表 `vm->objects`）。

因此，改用 pool 路径后，调用方必须手动完成：

```
obj->type              ← 对应枚举值
obj->is_marked         ← false
obj->in_remembered_set ← false
obj->generation        ← 0
obj->age               ← 0
obj->next              ← vm->objects（将新对象挂入 GC 链头）
vm->objects            ← (MsObject*)obj
```

> GC sweep 依赖 `vm->objects` 链遍历所有对象，漏挂会导致内存泄漏。

### Pool 大小参数

`MS_POOL_SLAB_SIZE = 64`（`memory.h`），对 upvalue/bound 来说足够大，无需调整。

## 验证

```bash
cmake --build build && cd build && ctest --output-on-failure
python ../benchmarks/run_all.py --compare baseline.json --runs 3
```

重点关注 benchmark：
- `closure_counter.ms` — upvalue 密集，预期时间 -5~15%
- `method_dispatch.ms` — bound method 密集，预期 alloc 峰值下降

使用 `MSLANG_VM_STATS` 编译开关（`memory.c:14`）对比 `bytes_allocated_peak` 和 `minor_gc_count`。
