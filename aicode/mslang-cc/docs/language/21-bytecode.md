# 21 字节码与指令集

栈式 VM，CPython 风格变长指令；一个编译单元 = 一个 chunk。

## 1. 指令编码

```
+--------+------------------+
| opcode | operand (0..2 B) |
+--------+------------------+
```

- opcode 1 字节；操作数 0/1/2 字节（u8 或 u16），按指令定义。
- `EXTENDED_ARG <u8>`：前置扩展，操作数左移 8 位拼接——支持 16/24/32 位常量池索引与跳转目标。
- 跳转目标：绝对偏移（模块内），经 EXTENDED_ARG 支持 >255。
- 每个 chunk 附行号表（指令偏移 → 源行，RLE 压缩）供 traceback。

## 2. chunk 结构

```c
typedef struct Chunk {
    uint8_t* code;        /* 指令流 */
    int32_t  codeLen;
    MsValue* consts;      /* 常量池：字面量、函数原型、类原型 */
    int32_t  constsLen;
    SourceRun* lines;     /* RLE 行号 */
    int32_t  nUpvalues;   /* 闭包捕获描述（函数 chunk） */
    uint16_t nIcSlots;    /* 内联缓存槽数 */
    ...
} Chunk;
```

## 3. 指令目录（约 90 条，分组）

### 3.1 装载/存储

```
LOAD_NIL LOAD_TRUE LOAD_FALSE        LOAD_CONST <k>
LOAD_LOCAL <slot>  STORE_LOCAL <slot>
LOAD_GLOBAL <name> STORE_GLOBAL <name>  DEF_GLOBAL <name>
LOAD_UPVAL <idx>   STORE_UPVAL <idx>
LOAD_GLOBAL_IC <name> <icSlot>       （内联缓存变体，见 §5）
MOVE（经栈：DUP ROT POP SWAP 等栈操作）
```

### 3.2 运算

```
BINARY_ADD SUB MUL DIV MOD POW
BINARY_AND OR XOR SHL SHR
UNARY_NEG NOT INVERT
COMPARE_EQ NE LT LE GT GE
IS     IN
GET_INDEX SET_INDEX          BUILD_SLICE <argc=3>
```

- ADD 内建快路径：int+int / float+float / str+str；否则走 dunder 慢路径。
- int 溢出在快路径内直接升 bigint。

### 3.3 函数与调用

```
MAKE_FUNCTION <proto> <flags>   （默认参数个数、rest 标志在 flags）
CLOSURE <proto>                 （捕获 upvalue 描述表在 proto）
CALL <argc>    CALL_METHOD <nameIdx> <icSlot> <argc>
RETURN
```

### 3.4 类

```
NEW_CLASS <nameIdx> <baseIdx?>
DEFINE_METHOD <nameIdx> <flags(method/static/dunder)>
INHERIT_INIT   （构建 MRO/方法表）
GET_ATTR <nameIdx> <icSlot>   SET_ATTR <nameIdx>
```

### 3.5 控制流

```
JMP <t>   JMP_IF_FALSE <t>   JMP_IF_TRUE <t>
JMP_IF_FALSE_OR_POP <t>   JMP_IF_TRUE_OR_POP <t>   （短路求值）
POP_JUMP_IF_NIL <t>
SWITCH_TABLE <n> <defaultT>   （值 switch 跳转表：常量键 → 目标）
TYPE_SWITCH_TABLE              （类型 switch：类常量 → 目标）
CHECK_TYPE <classIdx>          （case as 绑定校验）
```

### 3.6 迭代与生成器

```
GET_ITER   FOR_ITER <exitT>     （迭代协议 + nil 耗尽跳出）
YIELD      RESUME               （协程挂起/恢复共用）
```

### 3.7 异常 / defer

```
PUSH_TRY <catchT> <finallyT>    POP_TRY
THROW                           （抛出栈顶值）
MATCH_CATCH <classIdx>          （instanceof 匹配判定）
PUSH_DEFER <fnIdx>              RUN_DEFERS <n>
RERAISE                         （裸 throw 重抛）
```

### 3.8 并发

```
MAKE_CHAN <cap>   SEND   RECV   CLOSE_CHAN
SELECT_SETUP <n>  SELECT_ARM <i>  SELECT_GO <defaultT?>  SELECT_COMMIT
SPAWN            （go 语句：新 G）
AWAIT            （挂起直至 Future 完成）
```

### 3.9 杂项

```
BUILD_LIST <n>   BUILD_MAP <n*2>   BUILD_TUPLE <n>   BUILD_SET <n>
UNPACK_SEQ <n>   UNPACK_STAR <slot>          （解构赋值）
FORMAT_STRING <k-interp>       （插值：常量段 + 表达式段交错）
CHECK_SAFEPOINT                （回边预算：抢占 + GC safepoint，见 §6）
ASSERT <msgIdx?>               （断言失败构造 AssertionError）
NOP                            （peephole 占位后压实）
```

## 4. 示例

```ms
func add(a, b) { return a + b }
print(add(1, 2))
```

```text
; func add
0000 LOAD_LOCAL     0        ; a
0002 LOAD_LOCAL     1        ; b
0004 BINARY_ADD
0005 RETURN
; main
0006 LOAD_GLOBAL_IC "print"  <ic0>
0009 LOAD_CONST      0       ; <func add>
0011 MAKE_FUNCTION   0
0012 LOAD_CONST      1       ; 1
0014 LOAD_CONST      2       ; 2
0016 CALL            2
0018 CALL            1
0020 POP
0021 LOAD_NIL
0022 RETURN
```

## 5. 内联缓存（IC）

- IC 槽是 chunk 内数组，每槽 `{ shape/class + 缓存目标 }`。
- `GET_ATTR / CALL_METHOD / LOAD_GLOBAL_IC` 命中走缓存路径；miss 填充。
- 方法缓存 4 路 PIC，满 4 路转 megamorphic（退化为每次查表）。
- 语义透明：IC 只缓存"查什么"，不缓存"值"，不影响正确性（`22-object-model.md` §6）。

## 6. safepoint 与抢占

- `CHECK_SAFEPOINT` 编译器插入于：函数入口、回边（循环尾）、CALL 之前。
- 递减预算；预算尽 → 检查抢占请求（调度器）与 GC 握手（`23-gc.md`）。
- 开销恒定 2 指令（递减 + 判零跳过），基准回归监控中。

## 7. 序列化（.msc）

```
magic "MSC1" | 编译器版本 u32 | 源指纹 u64(FNV-1a) | flags u16
| 常量池（类型标记 + 递归编码；函数原型/类原型/字符串/数字）
| code 段 | 行号表 RLE | IC 槽数 | upvalue 描述
```

- 加载校验版本与指纹，不符即弃用重编译。
- 字符串常量加载后重新 intern（跨进程地址无关）。
