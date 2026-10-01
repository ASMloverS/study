# 22 对象模型与内存布局

## 1. MsValue（值表示）

默认 tagged union（可调试性优先）；`MS_NAN_BOXING=1` 编译期可切 NaN-boxing（§7）。

```c
typedef enum MsValueType {
  MS_VAL_NIL, MS_VAL_BOOL, MS_VAL_I64, MS_VAL_F64,
  MS_VAL_OBJ  // 其余皆为堆对象指针
} MsValueType;

typedef struct MsValue {
  MsValueType type;
  union {
    bool     b;
    int64_t  i;
    double   f;
    MsObj*   obj;
  } as;
} MsValue;
```

- int 快路径 i64 内联；bigint（MsObjBigInt*）为对象。
- 传值语义：MsValue 48 位 + tag，栈/寄存器槽直接存值。

## 2. 对象头与类型清单

```c
typedef struct MsObj {
  uint8_t       type;   // MsObjType
  uint8_t       flags;  // mark 颜色位 | 世代位 | 杂项
  struct MsObj* next;   // 全堆链表（GC 枚举）
} MsObj;
```

| MsObjType | 对象 | 要点 |
|-----------|------|------|
| STRING | MsObjString | 见 §3 |
| BIGINT | MsObjBigInt | 变长 limbs（32 位 limb 数组），符号位 |
| FUNCTION | MsObjFunction | 指向 chunk + 原型元数据 |
| NATIVE | MsObjNative | C 函数指针 + 名字 |
| CLOSURE | MsObjClosure | MsObjFunction + upvalue 数组（FAM） |
| UPVALUE | MsObjUpvalue | 开放（栈槽指针）/ 闭合（值） |
| CLASS | MsObjClass | 方法表、MRO、基类、Shape 种子 |
| INSTANCE | MsObjInstance | Shape + 内联字段(≤8) + 溢出数组 |
| BOUND_METHOD | MsObjBound | receiver + method |
| LIST | MsObjList | 动态数组（倍增，初始 8） |
| MAP | MsObjMap | 开放寻址线性探测（§4） |
| SET | MsObjSet | 复用 MsObjMap（值占位） |
| TUPLE | MsObjTuple | FAM 定长，hash 惰性缓存 |
| MODULE | MsObjModule | 命名空间 map + 元信息 |
| COROUTINE | MsObjCoroutine | goroutine/async 协程体（24） |
| CHANNEL | MsObjChannel | 环形缓冲 + 等待队列（24） |
| FUTURE | MsObjFuture | 状态机 + 结果/异常（24） |
| ITERATOR | MsObjIter | 迭代器壳（目标对象 + 游标） |
| WEAKREF | MsObjWeak | 弱表登记项 |

## 3. 字符串

```c
typedef struct MsObjString {
  MsObj    head;
  uint32_t hash;      // FNV-1a，创建时计算
  uint32_t byteLen;
  uint32_t charLen;   // ASCII 时 = byteLen；否则惰性计算
  bool     isAscii;   // 创建时一次扫描判定
  int32_t* runeIndex; // 非 ASCII 且首次需要 rune 索引时构建（字节偏移表）
  // FAM: char bytes[]
} MsObjString;
```

- intern 表：模块常量、属性名、全局名一律 intern；运行时拼接产物不强制 intern（`strings.intern` 可显式）。
- 快路径：`isAscii == true` 时 `s[i]`、`charLen` 全 O(1)；非 ASCII 首次 rune 索引构建 O(n) 偏移表，此后 O(log n) 二分 + 缓存。
- 比较：先 hash/len 短路，再 memcmp。

## 4. list / map / set / tuple

- list：`MsValue* data` 倍增；`push`/`pop` 均摊 O(1)；迭代器游标防并发修改检查（结构版本号，修改即失效迭代器，继续 next 抛 IteratorError）。
- map：容量 2^n，负载 0.75 触发再哈希；开放寻址线性探测；键 hash 缓存在 entry（int64）；entry 三态（空/活/墓碑）；插入序由独立链（seq 链表）维护，遍历按插入序。
- set：map 特例（占位值），同结构。
- tuple：不可变；首次入 map/set 时缓存 hash。

## 5. 数值对象

- MsObjBigInt：limb 数组（uint32×n）+ 符号；运算朴素实现（v1），Karatsuba 乘法（bigint 位宽 > 4096 时）。
- int→bigint 提升：快路径检测 `__builtin_add_overflow`，溢出转对象路径。
- float 全内联（union），无对象。

## 6. Shape 与内联缓存

```
Shape: { 父指针, 字段名→槽位 map, 实例字段数, 迁移表(属性名→新Shape) }
```

- 实例添加字段 → Shape 转移链共享（同构对象共享布局）。
- 内联字段 8 个（SBO），超出溢出到堆数组。
- IC（GET_ATTR/CALL_METHOD）：槽 = `{Shape指针 或 Class指针, 缓存的槽位/方法}`；4 路 PIC 环形替换；megamorphic 后直查。
- class 方法表独立于 Shape（方法存 MsObjClass，字段存 Shape）。

## 7. NaN-boxing 选项（MS_NAN_BOXING）

```
64 位：sign+exponent 全 1 的 NaN 作 tag 空间
  [nan48|tag16]
  - i48 直接内联 int（覆盖 ±2^47，超范围升 bigint——溢出阈值提前，语义不变）
  - f64 原样位型
  - obj* 48 位指针（需地址空间 < 2^48，三平台均满足；Windows 需高 16 位符号扩展处理）
  - nil/true/false 保留 tag
```

- 仅 Release 基准评估启用与否的依据：≥5% 综合提升才默认开启；Debug 构建恒 tagged union。

## 8. 弱引用与 handle

- WeakRef：全局弱表 `对象 → weak 节点链`；对象回收时节点置 nil（`23-gc.md` §7）。
- C API handle：独立注册表数组，GC 扫描为根；见 `25-capi.md` §4。

## 9. 对象池（小对象 slab）

- Upvalue、BoundMethod、Iterator 等高频短命对象走 per-worker slab 池（free-list），降低分配压力；池对象同样入全堆链表（GC 兼容）。
