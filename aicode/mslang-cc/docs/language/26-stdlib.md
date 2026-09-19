# 26 标准库

长期目标：融合对齐 Go std 与 Python std 的模块规模。命名：模块路径全小写（允许 `/` 层级，如 `encoding/base64`）；函数 lowerCamelCase。

## 1. 分层

| 层 | 内容 | 交付 |
|----|------|------|
| Tier 0 | 内置函数与类型方法（`10-builtins.md`） | 语言核心 |
| Tier 1 | 17 个核心模块 | v1 必备 |
| Tier 2 | 12 个模块 | v1 后期 |
| Tier 3 | compress/exec/signal/database/... | v2+ |

## 2. Tier 1（v1 必备）

| 模块 | 融合来源 | 核心内容 |
|------|----------|----------|
| math | Go math + Py math | `pi e sqrt pow exp log exp2 log2 floor ceil round trunc abs max min modf hypot/isqrt/gcd/lcm/deg rad/常量/三角双曲/inf nan 判定` |
| strings | Go strings+strconv + Py str | `contains/index/lastIndex/join/repeat/upper lower/title/trim 系列/split 系列/fields/compare/Format(fmt, args)`（Go Sprintf 风格 %v %d %s %.2f）`/tryInt/tryFloat/parseInt/parseFloat/formatInt/formatFloat`（吸收 Go strconv）`/toSnake/toCamel/intern/builder` |
| os | Go os + Py os | `getenv setenv unsetenv/environ/args/exit/cwd chdir/readFile writeFile/listdir mkdir mkdirAll remove removeAll/rename/stat(isDir isFile size mtime)/isTerminal` |
| sys | Py sys + Go runtime | `argv/path/executable/platform/version/stdin stdout stderr/exit`（`gc/memStats/goroutineStats` 由 runtime 模块再导出） |
| io | Go io + Py io | `open(path[, mode])/Reader Writer 抽象/copy(src, dst)/readAll/close；文件句柄 read readLine write flush seek tell`（二进制 IO；文本层用 strings） |
| bufio | Go bufio | `newReader/newWriter（缓冲尺寸可选）/scanner（行分割）/readString/readByte/writeString/writeByte/flush` |
| bytes | Go bytes + Py bytes | `Buffer 类型（可变字节缓冲）/fromString/append/extend/read/write/indexOf/contains/replace/toLower/toUpper/toString/len cap/truncate` |
| time | Go time + Py datetime | `now/mono/sleep(sec)（异步感知，返回 Future）/after(sec)（超时 channel）/since/duration 常量（ms s m h）/Time 类型（format parse 按 Go 布局 `2006-01-02` + Py 预设 `iso`）/tick(sec)/date 构造（year/mon/day...）/unix 秒与毫秒` |
| json | Go encoding/json + Py json | `marshal(v[, indent])/unmarshal(s)（→ map/list/string/number/bool/nil；bigint 经选项）/isValid/JsonBuilder（流式构造）` |
| errors | Go errors + Py exceptions | `new(msg)/wrap(e, msg)/unwrap(e)/is(e, target)/str；Error 子类工厂 define(name, parent)` |
| path | Go path/filepath + Py os.path | `join/clean/abs/rel/base/dir/ext/split/exists/isDir isFile/isAbs/expand（~ 与环境变量）/list separator 常量` |
| collections | Py collections + Go container | `deque（双端队列）/heapq（heappush heappop heapify nlargest nsmallest）/Counter/OrderedMap/defaultList(定长预置)/ring（环形缓冲，Go container/ring）` |
| sort | Go sort + Py sorted 内置 | `sort(xs[, cmp])/stable/areSorted/search(二分)/reverse(xs)/byKey/byField(x, "name")（字段比较器便捷）/compare（构造 cmp：数值/字符串/多键）` |
| sync | Go sync + Py threading | `Mutex/RWMutex/WaitGroup/Once/Cond（broadcast signal wait）/Chan 工具（merge fanIn）/deadlockTimeout` |
| random | Go math/rand + Py random | `seed/rnd int(min,max)/float()/choice/shuffle/sample/bytes(n)/bool()；` **v1 伪随机（xorshift128+），加密用途用 crypto/rand（Tier 2）** |
| log | Go log/slog + Py logging | `info/warn/error/debug(msg[, fields])/setLevel/setFormat(text json)/setOutput/with(fields)（返回子 logger）` |
| runtime | Go runtime/debug | `version/gcStats/memStats/goroutineStats/cancel(fut)/backgroundWaitMs/importGraph/stackTrace()（当前栈文本）` |

## 3. Tier 2（v1 后期）

| 模块 | 融合来源 | 核心内容 |
|------|----------|----------|
| net | Go net + Py socket | `dial/listen（TCP；返回 Future，async 优先）/resolve(域名→IP 列表，走 bpool)/Conn（read write close/setDeadline）/PacketConn(UDP)` |
| http | Go net/http + Py urllib | `get/post/request（client，返回 Future：status headers body）/serve(addr, handler)（v1 最小服务端：路由 + 响应写回）/statusText/header 常量/cookie 解析` |
| url | Go net/url + Py urllib.parse | `parse/encode/decode/encodeComponent/decodeComponent/query(→map)/build/joinPath` |
| crypto | Go crypto + Py hashlib/hmac | `md5/sha1/sha256/sha512（hmac 变体）/aes 加解密（CBC/GCM）/rand(安全随机 bytes/int)/hex/base64 转换放 encoding` |
| encoding | Go encoding + Py base64/binascii/csv | `base64（encode decode url 变体）/hex/csv（parse/生成）/json 已独立` |
| re | Go regexp（RE2）+ Py re | `compile/mustCompile/match/find/findAll/replace(全部与计数)/split/sub 风格命名取 Py：search match findall sub/分组捕获；**RE2 语法：无反向引用、无前后顾断言**（文档显著标注）` |
| slices | Go slices | `indexOf/contains/reverse(返回新)/clone/concat/max min(数值切片)/sort 已在 sort` |
| maps | Go maps | `keys/values/items(快照)/clone/copyInto/merge(不覆盖或覆盖策略)` |
| template | Go text/template + Py string.Template | `apply(tpl, data)（{{.field}} 与 {{range}} 最小集）/delimiters 自定义/escape html 选项` |
| testing | Go testing + Py unittest 精简 | `expect(actual).toBe(expected)/toEqual(深比较)/toThrow(fn, Type)/bench(name, fn)/run(入口供 ms test)` |
| flag | Go flag | `string/int/float/bool 定义/parse()/args()剩余/ms test 与 ms run 共用规范` |
| unicode | Go unicode | `isDigit/isLetter/isSpace/isUpper isLower/toUpper toLower(rune)/category(rune)` |

## 4. Tier 3（v2+ 规划）

compress（zlib/gzip/zip）、os/exec、signal、database（sql 抽象 + sqlite 绑定）、math/cmplx、net/rpc、fs（virtual FS）、weakref 已内置、inspect、importlib、marshal（pickle 式二进制序列化）。

## 5. 融合原则

1. 命名冲突取 mslang 风格（lowerCamelCase；如 Python `os.path.join` → `path.join` 模块独立）。
2. Go 的 `error` 返回风格 → 异常；`context.Context` 参数 → 省略（v1 无 context，靠取消 API）。
3. Python 的 `str` 方法与 Go `strings` 函数重复时：**实例方法优先**，模块函数补齐（`"a".upper()` 与 `strings.upper("a")` 等价，前者惯用）。
4. 迭代器优先：Python 迭代协议为准；Go 的 `[]X` 切片参数泛化为 list。
5. 时间/随机等单位统一秒（float）与毫秒（int）双 API，不用 Go 的 Duration 类型字面量。
