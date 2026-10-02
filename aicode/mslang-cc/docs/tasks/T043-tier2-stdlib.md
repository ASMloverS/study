# T043 — Tier2 标准库

- 阶段: P12 发布（roadmap 期 12）
- 依赖: T032、T035
- 规模: L
- 状态: 未开始
- 设计文档: `docs/language/26-stdlib.md` §3、`24-scheduler.md` §6（async IO 语义）、`29-roadmap.md` §3 期 9 出口（echo server）

## 1. 目标与范围

实现 12 个 Tier2 模块（26 §3）：`net`、`http`、`url`、`crypto`、`encoding`、`re`、`slices`、`maps`、`template`、`testing`（已在 T039 交付，此处复核）、`flag`、`unicode`。按内部清单逐模块交付，网络组（net/http）优先。

不做：Tier3（compress/exec/signal/database…）、re 的反向引用与前后顾断言（RE2 语法约束，26 §3 显著标注）。

## 2. 实现要点

- 内部交付顺序（依赖链）：`url/unicode/slices/maps`（无依赖）→ `encoding`（base64/hex/csv）→ `crypto`（md5/sha1/sha256/sha512 + hmac + aes CBC/GCM + 安全 rand）→ `re`（RE2 子集：NFA 回溯-free 引擎）→ `flag` → `template`（`{{.field}}` + `{{range}}` 最小集）→ `net` → `http`。
- net（26 §3）：dial/listen TCP 返回 Future（async 优先，走 T032 reactor）；resolve 域名→IP 列表（bpool）；Conn read/write/close/setDeadline；PacketConn(UDP)。
- http（26 §3）：get/post/request（client 返回 Future：status/headers/body）；serve(addr, handler)（v1 最小服务端：路由回调 + 响应写回）；statusText/header 常量/cookie 解析。
- re（26 §3）：compile/mustCompile/match/find/findAll/replace/replaceCount/split/sub 风格命名取 Py（search/match/findall/sub）+ 分组捕获；RE2 语法（无 backref/lookaround），compile 阶段拒绝并报错。
- crypto：摘要自研（标准测试向量）；aes 自研（FIPS-197）；安全 rand 走 OS 熵（CryptoAPI/getrandom）。
- encoding：base64（标准/url 变体）/hex/csv(parse/生成)。
- slices/maps（26 §3）：indexOf/contains/reverse(新)/clone/concat/max/min；keys/values/items(快照)/clone/copyInto/merge(策略参数)。
- template：apply(tpl, data)（`{{.field}}` 路径取值 + `{{range .xs}}` + `{{end}}`）/delimiters 自定义/escape html 选项。
- flag：string/int/float/bool 定义/parse()/args() 剩余（与 ms test/ms run 共用规范）。
- unicode：isDigit/isLetter/isSpace/isUpper/isLower/toUpper/toLower(rune)/category(rune)（复用 T004 生成表扩展）。
- echo server 里程碑（29 §3 期 9 出口的最终验证）：`examples/echo/echo_server.ms` 千并发连接压测脚本入库（net+reactor 集成）。

## 3. 涉及文件

新增: `src/stdlib/net.c`、`src/stdlib/http.c`、`src/stdlib/url.c`、`src/stdlib/crypto.c`、`src/stdlib/encoding.c`、`src/stdlib/re.c`、`src/stdlib/slices.c`、`src/stdlib/maps.c`、`src/stdlib/template.c`、`src/stdlib/flag.c`、`src/stdlib/unicode.c`、`tests/unit/test_tier2_*.c`（每模块一文件）、`tests/fixtures/stdlib_<mod>/*.ms`、`examples/echo/*.ms`
修改: `src/module/builtin_libs.c`、`src/reactor/*`（TCP interest 完整化）、`CMakeLists.txt`

## 4. 验收标准（DoD）

- [ ] 12 模块 26 §3 清单逐函数勾选通过（testing 复核）
- [ ] crypto 全算法标准测试向量通过（md5/sha*/hmac/aes NIST 向量）
- [ ] re：RE2 语法约束拒绝 backref/lookaround；match/findall/sub/分组全过；与 Python re 行为对照集（语法交集内）一致
- [ ] net：TCP 回环 dial/listen/echo 数据全等；setDeadline 超时触发
- [ ] http：本地起 serve + get/post 回环用例（状态/头/体断言）
- [ ] echo server：千并发连接 10 分钟稳定（29 §4 口径，CI 以 60s 缩样跑 + 全量夜跑）
- [ ] base64/hex/csv 往返；template apply/range/escape

## 5. 测试计划

### 5.1 C 单测（节选）

| 用例 | 调用 | 期望 |
|------|------|------|
| md5/sha256 向量 | "abc" | 标准摘要值 |
| hmac | RFC 4231 向量 | 匹配 |
| aes-gcm | NIST 向量 | 加解密往返 + 篡改检测 |
| re 引擎 | `(a|b)*abb` 自动机 | 匹配/拒配正确 |
| re 语法拒 | `(?<=x)`/`(a)\1` | compile 报错 |
| url.parse | query/map | 编解码往返 |
| base64 url 变体 | `+/` vs `-_` | 交替字符正确 |

### 5.2 ms fixtures（节选）

- `stdlib_net/echo_pair.ms`：本进程 listen + go dial 回环收发
- `stdlib_http/roundtrip.ms`：serve 后 get/post 断言（动态端口）
- `stdlib_re/`：`search/match/findall/sub/replace` 用例组 + RE2 约束报错
- `stdlib_template/apply.ms`：字段/range/escape golden
- `examples/echo/load_test.ms`：并发压测脚本（连接数/时长参数化）

## 6. 风险与备注

- re 引擎实现量最大：采用 Thompson NFA + 优先级 bfs（RE2 语义子集）， Pike vm 参考文献：Russ Cox 文章系列（实现注释引用，28 §9 算法引用惯例）。
- Windows IOCP 的 TCP 语义差异（connect 完成通知）是 echo 稳定性风险点：reactor 单测先行覆盖 connect/read/write 全路径再上压测。
