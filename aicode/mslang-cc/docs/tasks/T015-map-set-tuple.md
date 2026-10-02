# T015 — map / set / tuple

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T013
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/02-types.md` §5.2 §5.3 §5.4 §7、`10-builtins.md` §4、`22-object-model.md` §4

## 1. 目标与范围

实现 MsObjMap（开放寻址线性探测 + 插入序 seq 链）、MsObjSet（map 特例）、MsObjTuple（FAM 定长 + 惰性 hash 缓存）；BUILD_MAP/BUILD_TUPLE/BUILD_SET 字面量与 `{}` 空 map 歧义；多返回值铺垫（BUILD_TUPLE 语义）；键 hashable 规则与数值塔 hash 一致性。

不做：实例作键的 `__eq__/__hash__`（T021 后生效）、chan 的 len/cap（T030）。

## 2. 实现要点

- map（22 §4）：容量 2^n，负载 0.75 再哈希；entry 三态（空/活/墓碑）；键 hash（int64）缓存在 entry；插入序独立 seq 链表维护，遍历按插入序（02 §5.2）。
- set：占位值复用 map 结构；`| & - ^` 运算符（并交差对称差，02 §5.4）；`set()` 构造空 set、`{}` 为空 map（parser 前瞻已定，本任务接通构造）。
- 键规则（02 §7）：可 hash 类型 = nil/bool/int/bigint/float/string/tuple（元素全 hashable）；list/map/set/普通实例作键 TypeError；数值塔 hash 统一：`hash(1) == hash(1.0) == hash(1n)`（实现：数值键统一规范化为 i64 域 hash + 类型域消歧——`1 == 1.0` 键等价，文档语义 02 §6 `1 == 1.0` true）。
- 错误：`m[k]` 缺键 KeyError；`m.get(k, default)` 不抛（10 §4）。
- tuple（22 §4）：FAM 定长；hash 首次入 map/set 时计算缓存（元素 hash 复合）；`(1,)` 单元素逗号 parser 已管；多返回值 = return tuple（05 §5 语义本任务固化 BUILD_TUPLE/UNPACK 复用解构路径）。
- 方法族（10 §4）：map get/has/remove/clear/keys/values/items/update/copy；set add/remove/has/union/intersect/diff/symmetricDiff/clear/copy/toList；tuple count/indexOf/contains/toList。
- GC：markChildren 遍历 entry 键值/seq 链与 FAM 元素。

## 3. 涉及文件

新增: `src/obj/obj_map.c/.h`、`src/obj/obj_set.c/.h`、`src/obj/obj_tuple.c/.h`、`src/vm/map_methods.c`、`src/vm/set_methods.c`、`src/vm/tuple_methods.c`、`tests/unit/test_obj_map.c`、`tests/unit/test_obj_tuple.c`、`tests/fixtures/container/*.ms`
修改: `src/compiler/compiler.c`（BUILD_MAP/TUPLE/SET）、`src/vm/vm.c`（GET_INDEX map 路径、IN 指令容器路径）、`src/gc/gc.c`、`src/vm/vm.c`（globals 换 map 承载）

## 4. 验收标准（DoD）

- [ ] 02 §5.2-§5.4、§7 与 10 §4 全部行为用例通过
- [ ] 插入序遍历稳定（含 remove 后再插入）
- [ ] 数值塔键等价：`m[1]` 与 `m[1.0]` 同槽（`1 == 1.0` 语义）
- [ ] 墓碑与再哈希：删除密集场景查找正确（构造 1000 增删）
- [ ] tuple 缓存 hash：两次 hash 调用值一致且仅计算一次（计数断言）
- [ ] GC 扩展 + ASan 压力通过

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_obj_map.c`、`tests/unit/test_obj_tuple.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 插入序 | put b,a,c 删 a 再 put a | 遍历 b,c,a |
| 缺键 | `m["x"]` | KeyError |
| get 默认 | `m.get("x", 7)` | 7 |
| 数值塔 | `m[1]=1; m[1.0]=2` | 单键，值 2 |
| 不可 hash | list 作键 | TypeError |
| 再哈希 | 容量跨越 | 全键值保留、序不变 |
| set 运算 | `{1,2} \| {2,3}` 等 | `{1,2,3}`、`{2}`、`{1}`、`{1,3}` |
| 空 literal | `{}` / `set()` | type 为 map / set |
| tuple hash 缓存 | 计数器 | 首次计算后缓存 |
| tuple 相等 | `(1,"a") == (1,"a")` | true |

### 5.2 ms fixtures `tests/fixtures/container/`

- `map_basic.ms`：

```ms
var m = {"a": 1, "b": 2}
m["c"] = 3
print(m)                     // {"a": 1, "b": 2, "c": 3}
print(m.keys())              // ["a", "b", "c"]
for k, v in m.items() { print("${k}=${v}") }
```

- `set_ops.ms`：四种运算 + toList 排序输出（golden 稳定化：toList 后 sort）
- `tuple.ms`：`(1,)`、`()`、count/indexOf、嵌套 tuple 作 map 键
- `key_err.ms`：缺键 KeyError；list 键 TypeError

## 6. 风险与备注

- 全局表（VM globals）本任务切换为 MsObjMap 承载：LOAD_GLOBAL/STORE_GLOBAL 走 intern 名查，性能路径的 LOAD_GLOBAL_IC 归 T022。
- float 键的 hash：i64 域统一采用"整数值 float 归一为 int 键"策略（与 02 §6 数值相等一致）；NaN 键：`NaN != NaN` → 不等价，每次插入新键（IEEE 语义），单测锁定。
