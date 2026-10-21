# T013 string 对象 — 设计决策

> 依据：docs/tasks/T013-string-object.md、02-types.md §4/§6/§8、10-builtins.md §2、22-object-model.md §3、21-bytecode.md §3

## D1 MsObjString 布局与 runeIndex 内嵌分配

- 布局按 22 §3 增补 `ms_u64 hash64`（map 键用，T015 对接）：`head/hash/hash64/byteLen/charLen/isAscii/runeIndex + FAM bytes`。
- 非 ASCII 串在**创建时一次扫描**定 `isAscii/charLen`，并在分配时于 `bytes + byteLen` 起预留 `(charLen+1) * sizeof(int32_t)` 尾空间；`runeIndex` 惰性构建时写入该保留区（首次 O(n)，此后 O(1) 直查；字节偏移→rune 索引方向走二分）。runeIndex 存 charLen+1 项（含末尾哨兵 byteLen），切片/取段 O(1) 定界。
- **对象释放时 runeIndex 随对象单块 free**（sweep 的 `free(obj)` 天然覆盖），无需 GC 析构钩子——这是 task §2 "runeIndex 属对象内分配" 的落地方式。

## D2 字符串值迁移：MS_VAL_STRING 退役

- `MS_VAL_STRING` kind 与 `as.str` 联合成员删除；字符串统一为 `MS_VAL_OBJ` + `MS_OBJ_STRING`。
- `msValueEqual`：双方 OBJ 且均为 string → hash/len 短路 + memcmp（值相等，因运行时拼接产物不 intern）；list 等其余仍身份比较。
- `msValueOrder`：string/string → memcmp 字典序（compareTo 同源）。
- `msValueTypeName`：OBJ 按 `obj->type` 分派（string/list/function/iterator/object），判型从"不 deref"改为 deref——test_vm_basic valuePredicateFamily 相应改造。

## D3 常量池：转义解码时机与原始串区分

- 新增 `CONST_RAWSTRING`：反引号原始串（内容含 `\` 不可解码），与 `CONST_STRING`（转义原文，运行时解码）分_kind_去重；值层面经 intern 表自然合一。
- `ConstEntry` 增 `strObj/strVm` 缓存字段：LOAD_CONST 首次解码+intern 后缓存；chunk 跨 VM 复跑时按 `strVm != vm` 失效重建。
- `msValueFromConst` 签名增 `MsVM*`（唯一调用方 vm.c LOAD_CONST）。
- 新增 `CONST_INTERP`：插值描述 `{InterpSeg segs[]}`，字面段在**编译期解码**为 chunk 拥有的缓冲（chunkFree 释放），表达式段运行时前置压栈，`OP_FORMAT_STRING` 消费。

## D4 intern 表

- `src/obj/intern.c`：开放寻址线性探测，容量 2^n，负载 0.75 再哈希，键 = MsObjString（hash32 + 字节）。
- 挂 `MsVM.intern`（按值成员，msVmInit/msVmFree 接线）；intern 产物标 `MS_OBJ_PERMANENT`，sweep 免收、gcHeapFree 统一释放。
- API：`msInternBytes(vm, bytes, len) → MsObjString*`（存在即返回既有指针）。

## D5 方法机制（后续容器复用）

- `obj.h` 增 `MsObjNative {fn, name}` 与 `MsObjBoundMethod {receiver, method*}`；MS_OBJ_BOUND_METHOD 注册 markChildren（receiver + method）。
- 编译：`s.m(args)` → obj + args + `OP_CALL_METHOD <name> <ic=0> <argc>`（icSlot 本任务恒 0，IC 归 T022）；`s.m`（取值）→ `OP_GET_ATTR <name> <ic=0>`。
- 执行：CALL_METHOD 查 string 方法表直接以 `args[0]=接收者` 调用（零分配）；GET_ATTR 物化 Native+Bound 对象（方法一等值）。OP_CALL 增 bound 分派：callee 槽改写为 receiver 后原栈调用。

## D6 split/join 的最小 list 桥

- 10 §2 要求 split/splitLines/join 完整可用，而 list 容器归 T014——本任务在 `src/obj/obj_list.c` 落**最小 MsObjList**（data/count/cap + push + markChildren），接通 `OP_BUILD_LIST`、`GET_INDEX(list, int)` 与 list 的 str/repr 形态（元素 repr、string 带引号，格式 `[a, "b"]`）；方法族/切片/迭代器壳/UNPACK 归 T014。
- `join` 仅接受 list（沿 data 直读，不依赖迭代协议）；list 迭代（GET_ITER/FOR_ITER）归 T014，本任务对 list 报"lands in T014"。

## D7 切片值与规范切片算法

- 新增过渡 `MS_VAL_SLICE`（union 成员 `{start, end, step, mask}`，mask 位标记端点显式；仅存活于 BUILD_SLICE→GET_INDEX 相邻指令间，无堆引用，GC 天然安全；T014/T015 复用后归宿同 RANGE_ITER）。
- `msSliceRange(len, hasA, a, hasB, b, hasC, c)` 规范化（Python 语义：步进 0 → false；正/负步进各自钳位），string 与 T014 list 共用（T014 §2 点名的复用重构点）。

## D8 chars()/bytes() 迭代器

- `MsObjStrIter {str*, pos, byteMode}` 挂 `MS_OBJ_ITERATOR` tag（T017 泛化），markChildren 标 str。
- GET_ITER：string → rune 迭代器；range 直通（既有）。FOR_ITER：range（既有）| str 迭代器分派。msVmInit 注册两 markFn（str 迭代器 / bound method / list）；test_gc_basic 的"测试自注册覆盖"纪律不受影响（同 tag 后注册覆盖）。

## D9 Unicode 支持面

- `tools/gen_unicode_table.py` 扩展：另生成 `src/obj/unicode_case.h`（SimpleCaseMapping 上/下映射对表 + 二分 + ASCII 快路径），统计行数入任务报告。
- UTF-8 decode/encode + 转义解码放 `ms_common`（compiler 与 obj 双向共用，避免 lib 环依赖）；scanner 静态副本不动（surgical）。
- isDigit/isSpace/isAlpha：ASCII 精确 + 非 ASCII isAlpha 借 ID_START 表（剔除 `_`）；isDigit/isSpace 非 ASCII 暂 false（任务备注记录，Tier1/T034 复核）。

## D10 值形态与内置

- `msValueStrForm/msValueReprForm(vm, v) → MsObjString*`（str：string 自身/标量 buf/list 递归；repr：string 加引号转义、list 元素一律 repr）。FORMAT_STRING 与 print 共用 str 形态。
- builtins_bridge 增 `len`（string 字节/list 元素）、`str`、`repr`。
