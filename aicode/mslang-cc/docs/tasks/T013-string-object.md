# T013 — string 对象：UTF-8 与方法族

- 阶段: P4 对象系统（roadmap 期 4）
- 依赖: T012
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/02-types.md` §4 §6、`10-builtins.md` §2、`22-object-model.md` §3

## 1. 目标与范围

实现 MsObjString：UTF-8 不可变存储、FNV-1a hash、ASCII 快路径、惰性 rune 索引表、intern 表；`s[i]` rune 索引、`s[a:b:c]` 切片、`+` 拼接、`*` 重复；10-builtins §2 全部 string 方法；GET_INDEX/SET_INDEX（string 只读，SET_INDEX 报错）与 FORMAT_STRING 插值求值前置（str 形态转换）。

不做：Unicode 大小写映射全表之外的高级算法（toUpper/toLower 用 Unicode 简单大小写映射表，生成脚本复用 T004 的 tools/gen_unicode_table.py 扩展）。

## 2. 实现要点

- 布局（22 §3）：`head/hash/byteLen/charLen/isAscii/runeIndex(FAM 前置字段)` + FAM 字节；创建时一次扫描定 isAscii 与 charLen（ASCII 直等，非 ASCII 惰性 runeIndex——首次按需 O(n) 构建，此后二分）。
- intern 表：模块常量、属性名、全局名 intern；运行时拼接产物不强制（`strings.intern` 显式，归 T034）。intern 串挂永久代。
- 相等与 hash：hash/len 短路 → memcmp（22 §3）；FNV-1a-32 与 64（map 键用 64 位，T015 对接）。
- 索引/切片（02 §4）：`s[i]` 返回 rune（int 码点），负索引，越界 IndexError（错误形态以"错误槽"最小路径）；切片按 rune 索引、步进可负、端点可省略，返回新 string。
- 方法族（10 §2）：charLen/chars/bytes/toUpper/toLower/split/splitLines/join/contains/startsWith/endsWith/indexOf/lastIndexOf/replace/trim/trimLeft/trimRight/repeat/count/padLeft/padRight/slice/isDigit/isSpace/isAlpha/compareTo —— 以"实例方法表"机制挂接（原生方法对象 NATIVE + 绑定接收者，机制本任务实现，后续容器复用）。
- 无隐式转换：`"a" + 1` 抛 TypeError（02 §8）；`str(x)`/`repr(x)` 内置先支持 nil/bool/int/float/string（插值 FORMAT_STRING 消费同一形态函数）。
- GC：markChildren 为空（string 无子引用）；runeIndex 属对象内分配，对象释放时一并 free。

## 3. 涉及文件

新增: `src/obj/obj_string.c`、`src/obj/obj_string.h`、`src/obj/intern.c`（intern 表）、`src/vm/str_methods.c`（方法表）、`tests/unit/test_obj_string.c`、`tests/fixtures/string/*.ms`
修改: `src/gc/gc.c`（mark 挂接）、`src/vm/builtins_bridge.c`（str/repr）、`src/compiler/compiler.c`（FORMAT_STRING 字符串段编译——表达式段求值后走 str()）

## 4. 验收标准（DoD）

- [ ] 02 §4 与 10 §2 全部行为用例通过
- [ ] `s[i]`/切片在纯 ASCII 与中文混合串上 rune 语义正确
- [ ] `len(s)` 字节、`s.charLen()` 字符、`s.chars()` 迭代三者自洽
- [ ] intern：同一字面量两处出现指针相等（单测断言）
- [ ] 插值 `"a = ${a}"` 输出正确（含嵌套与 instance 前 str 形态占位——`__str__` 归 T021 后增强）
- [ ] GC 压力：字符串循环分配下无泄漏（ASan）

## 5. 测试计划

### 5.1 C 单测 `tests/unit/test_obj_string.c`

| 用例 | 输入 | 期望 |
|------|------|------|
| hash/相等 | "abc" vs 运行时拼接 "ab"+"c" | hash 相等、值相等、指针不同 |
| ASCII 快路径 | isAscii=true | s[1] O(1)（无 runeIndex 构建） |
| rune 索引 | `"中文ab"[1]` | 20025 ('文') |
| 负索引 | `"abc"[-1]` | 99 |
| 切片 | `"a中b"[0:2]` / `[::2]` / `[::-1]` | `"a中"` / `"ab"` / `"b中a"` |
| 重复 | `"ab" * 3` | `"ababab"` |
| join | `",".join(["a","b"])` | `"a,b"` |
| pad | `"5".padLeft(3, '0')` | `"005"` |
| trim | `" x ".trim()` | `"x"` |
| isDigit | `"123".isDigit()` / `"12a"` | true / false |

### 5.2 ms fixtures `tests/fixtures/string/`

- `index_slice.ms`：

```ms
var s = "Hello, 世界"
print(s[0])            // 72
print(s[7])            // 19990
print(s[0:5])          // Hello
print(s[::-1])         // 界世 ,olleH
print(len(s))          // 13
print(s.charLen())     // 9
```

- `methods.ms`：split/replace/indexOf/contains/toUpper/count/padLeft 组合样例（golden 输出）
- `interp.ms`：`"a = ${1 + 2}, s = ${"in${"n"}"}"` → `a = 3, s = inn`
- `type_err.ms`：`"a" + 1` → stderr TypeError，退出码 1

## 6. 风险与备注

- runeIndex 惰性构建的线程安全：本任务单线程访问（调度器 T028 后仅 worker 自对象访问，跨 G 共享 string 的构建需在 T028 复核——设计上仅读缓存 + 原子发布）。
- toUpper/toLower 的 Unicode 简单映射表体积控制：仅 SimpleCaseMapping，生成脚本统计行数入报告。
