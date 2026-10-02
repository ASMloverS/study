# T036 — Tier1 组 C：json/log/runtime

- 阶段: P10 C API + Tier1（roadmap 期 10）
- 依赖: T027、T029
- 规模: M
- 状态: 未开始
- 设计文档: `docs/language/26-stdlib.md` §2（对应行）、`23-gc.md` §9（gcStats）、`24-scheduler.md` §9（goroutineStats）、`09-modules.md` §5（importGraph）、`08-concurrency.md` §7（cancel）

## 1. 目标与范围

实现 Tier1 收尾三模块：`json`（自研解析/序列化）、`log`、`runtime`。完成后 Tier1 17 模块齐备（time 在 T032、组 A 五模块 T034、组 B 八模块 T035）。

不做：JsonBuilder 流式构造的高级特性（最小可用：链式 API 落地）、log 的自定义 handler 插件（setOutput 句柄重定向即止）。

## 2. 实现要点

- json（26 §2）：marshal(v[, indent])（map/list/string/number/bool/nil ↔ JSON；bigint 经选项）、unmarshal(s)（严格递归下降解析，错误位置诊断；number 解析：整型 int、超界升 bigint、含小数 float）、isValid、JsonBuilder（beginObject/key/endObject/beginArray/… 流式构造，输出 string）。
  - 语义映射：map 键须 string（其他键 marshal 抛 TypeError）；重复键后者覆盖；缩进 indent=N 美化。
- log（26 §2）：info/warn/error/debug(msg[, fields])（fields 为 map 附加键值）、setLevel、setFormat(text|json)、setOutput(句柄重定向 io 句柄/标准流)、with(fields)（返回携带上下文的子 logger）——默认 text 格式 `时间 LEVEL msg k=v`；json 格式单行对象。
- runtime（26 §2）：version、gcStats（暂停分位数/次数/存活字节/assist——T027 埋点消费）、memStats、goroutineStats（T029 埋点消费）、cancel(fut)、backgroundWaitMs（属性读写）、importGraph（T025 记账消费）、stackTrace()（当前栈文本）。
- `runtime.gc()`：强制 full（23 §2 强制路径——T027 已备 gcCollectAll）。

## 3. 涉及文件

新增: `src/stdlib/json.c`、`src/stdlib/log.c`、`src/stdlib/runtime.c`、`tests/unit/test_stdlib_json.c`、`tests/fixtures/stdlib_json/*.ms`、`tests/fixtures/stdlib_log/*.ms`、`tests/fixtures/stdlib_runtime/*.ms`
修改: `src/module/builtin_libs.c`、`src/sched/sched.c`（stats 出口函数化）、`src/gc/gc_stats.c`（对外快照 API）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] json：往返 marshal(unmarshal(s)) == 结构等价；嵌套/unicode/转义/大整数全过
- [ ] unmarshal 非法输入报错含位置（"offset N"）
- [ ] JsonBuilder 产物与 marshal 等价（同构数据）
- [ ] log 四级别过滤 + json/text 双格式 + fields 附加
- [ ] runtime.gcStats/goroutineStats/memStats 返回结构字段完整（stats 跑器断言）
- [ ] runtime.cancel/backgroundWaitMs/importGraph/stackTrace 行为正确
- [ ] Tier1 17 模块注册表完整性断言（单测枚举清单比对 26 §2 目录）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_stdlib_json.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| 解析基础 | 对象/数组/字符串/数字/bool/null | 类型正确映射 |
| 精度 | 大整数 2^70 | bigint |
| 缩进 | indent=2 | 换行缩进格式 |
| 非法 | `{a:1}` / `{"a"` | 位置报错 |
| marshal 错键 | int 键 map | TypeError |
| 转义 | `"\u4e2d"` / 控制字符 | 解析与再序列化一致 |
| Builder | 构造与字面量等价 | marshal 相等 |

### 5.2 ms fixtures（节选）

- `stdlib_json/roundtrip.ms`：

```ms
import "json"
var v = json.unmarshal("{\"a\": [1, 2.5, true, null], \"b\": {\"c\": \"中\"}}")
print(json.marshal(v))                    // 顺序保持
print(json.marshal(v, 2) is nil)          // false
print(json.isValid("{bad"))               // false
```

- `stdlib_log/levels.ms`：setLevel(DEBUG) + 四级输出 + fields（stderr 行匹配）
- `stdlib_runtime/stats.ms`：gc() 后 gcStats() 字段输出 + goroutineStats 计数（`go` 后差异断言）
- `stdlib_runtime/graph.ms`：多模块 import 后 importGraph 输出边集

## 6. 风险与备注

- json number 的 int/float/bigint 判定：无小数/指数 → int（超 i64 升 bigint）；否则 float（26 §2 bigint 经选项：`json.marshal(v, {bigint: "string"})` 处理形态——选项 map 第二参数扩展，默认 bigint 原样输出为整数）。
- runtime 属性（backgroundWaitMs）以模块级 getter/setter 函数对实现（v1 无属性语法，06 §8）。
