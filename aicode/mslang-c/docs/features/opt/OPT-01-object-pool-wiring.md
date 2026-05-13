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
    MsObjUpvalue* uv = (MsObjUpvalue*)ms_pool_alloc(&vm->upvalue_pool, vm, MS_OBJ_UPVALUE);
    ...
}
```

**`src/object.c:158-165`** — `ms_obj_bound_method_new`（行号近似，grep `MS_OBJ_BOUND_METHOD` 确认）

```c
// 改后：走 bound_pool
MsObjBoundMethod* ms_obj_bound_method_new(MsVM* vm, MsValue receiver, MsObjClosure* method) {
    MsObjBoundMethod* bm = (MsObjBoundMethod*)ms_pool_alloc(&vm->bound_pool, vm, MS_OBJ_BOUND_METHOD);
    ...
}
```

### 释放路径

**`src/vm_gc.c`** — `sweep_generation` 或 `sweep_all` 中对应分支  
当 `obj->type == MS_OBJ_UPVALUE` 且 sweep 决定回收时，改调 `ms_pool_free`：

```c
case MS_OBJ_UPVALUE:
    ms_pool_free(&vm->upvalue_pool, obj);
    break;
case MS_OBJ_BOUND_METHOD:
    ms_pool_free(&vm->bound_pool, obj);
    break;
```

> **注意**：`ms_pool_free` 把 slab 块挂回 freelist，不调 `free`；  
> GC 的 `sweep_all` 对 pool 对象只需操作 freelist，不减 `vm->bytes_allocated`（pool 的整体 slab 由 pool 自身管理）。  
> 确认 `ms_pool_alloc`（`memory.c:60`）内部是否已调 `vm->bytes_allocated += size`，若已调则 `ms_pool_free` 需对称减。

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
