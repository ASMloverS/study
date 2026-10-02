# T014 — list 与解构赋值

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T013
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/02-types.md` §5.1、`10-builtins.md` §3、`22-object-model.md` §4、`04-statements.md` §2（解构）

## 1. 目标与范围

实现 MsObjList（动态数组）、list 字面量 BUILD_LIST、索引读写（负索引）、切片（返回新 list）、`+`/`*`、10-builtins §3 全部方法、迭代器壳（游标 + 版本号防并发修改）、UNPACK_SEQ/UNPACK_STAR 解构赋值。

不做：map/filter/reduce 的函数值参数（语法可传，本任务函数值仅原生 print 类；真实闭包传入归 T018 后复跑）。

## 2. 实现要点

- 布局（22 §4）：`MsValue* data` 倍增（初始 8，22 §2 表）；`count/cap/version`。
- 版本号迭代器（22 §4）：ITERATOR 壳 `{目标 + 游标 + 快照版本}`；修改（append/pop/insert/remove/sort/reverse/clear）使 `version++`；迭代中 next 遇版本失配抛 IteratorError（07 树，本任务错误槽最小路径）。
- 索引：负索引换算；越界 IndexError。SET_INDEX 指令接通。
- 切片 `xs[a:b:c]`：BUILD_SLICE → 与 string 同一规范切片算法（步进可负、端点省略；rune/元素索引按 list 元素）抽公共 `sliceRange(len, a, b, c)`（T013 复用重构点）。
- 方法族（10 §3）：append/pop/insert/remove/indexOf/contains/sort/reverse/concat/repeat/map/filter/reduce/each/clear/copy/first/last；sort 原地 timsort，缺省 `<` 序（cmp 协议：返回 int，归 T021 后接自定义 cmp，本任务内置类型比较）。
- 解构（04 §2）：UNPACK_SEQ 目标数可少于右值（多余丢弃）；`*rest` 至多一个任意位置；不可解构对象 TypeError；`a, b = b, a` 经 BUILD_TUPLE 临时（tuple 归 T015，本任务右值多表达式先存临时栈序列——实现注记：等 T015 后统一走 tuple）。
- GC：markChildren 遍历 data 槽。

## 3. 涉及文件

新增: `src/obj/obj_list.c`、`src/obj/obj_list.h`、`src/vm/list_methods.c`、`tests/unit/test_obj_list.c`、`tests/fixtures/list/*.ms`
修改: `src/compiler/compiler.c`（BUILD_LIST/UNPACK_*）、`src/vm/vm.c`（GET/SET_INDEX list 路径、迭代器 FOR_ITER list 路径）、`src/gc/gc.c`

## 4. 验收标准（DoD）

- [ ] 02 §5.1 与 10 §3 全部行为用例通过
- [ ] 切片/索引/拼接/重复 fixtures 全绿
- [ ] 迭代中修改抛 IteratorError
- [ ] 解构：少收/星号任意位/交换正确；不可解构 TypeError
- [ ] GC 扩展：list 子引用标记单测 + ASan 压力通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_obj_list.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 倍增 | append 1000 | cap 增长序列 8→…、均摊 O(1) |
| 负索引 | `xs[-1]` | 末元素 |
| 越界 | `xs[10]`（len 3） | IndexError |
| 切片 | `[1,2,3,4][1::2]` / `[::-1]` | `[2,4]` / `[4,3,2,1]` |
| 拼接/重复 | `[1]+[2]`、`[0]*3` | `[1,2]`、`[0,0,0]` |
| remove | 按值删首个；缺失 | ValueError |
| 版本号 | 迭代中 append 后 next | IteratorError |
| 解构 | 3 目标收 5 元素 | 多余丢弃 |
| 星号位 | `a, *m, z = [1,2,3,4]` | a=1 m=[2,3] z=4 |

### 5.2 ms fixtures `tests/fixtures/list/`

- `basic.ms`：

```ms
var xs = [1, 2, 3]
xs.append(4)
print(xs)                    // [1, 2, 3, 4]
print(xs[1:3])               // [2, 3]
print(xs[-1])                // 4
xs.reverse()
print(xs)                    // [4, 3, 2, 1]
```

- `methods.ms`：insert/remove/indexOf/contains/sort/concat/first/last/copy 组合（golden）
- `destructure.ms`：`a, b = b, a`、`*rest` 两种位形、for 解构铺垫
- `iter_err.ms`：迭代中修改 → stderr IteratorError 退出码 1

## 6. 风险与备注

- `print(xs)` 需要 list 的 repr 形态（递归 str/repr 元素）——repr 规则：string 元素带引号（repr 形态），其余 str 形态；本任务实现 `reprList/strList` 同构。
- `xs.map(f)` 等 f 为脚本函数时本任务尚无闭包值：C 单测以原生函数桩验证管道，T018 后 fixtures 补闭包用例（在 T018 DoD 中标注复跑）。
