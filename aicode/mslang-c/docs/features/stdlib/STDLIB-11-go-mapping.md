# STDLIB-11: Go std → mslang 模块映射目录

> 本文档定义从 Go 标准库（go1.26）到 mslang-c 标准库的映射关系，按 Tier 分级，采用**扁平裸名**（`import "json"`，不用层级路径）。

---

## 已有模块（Tier 0，C 实现，10 个）

| mslang 模块 | Go 来源 | 层 | 状态 |
|---|---|---|---|
| `math` | math, math/rand | C | ✅ |
| `os` | os, os/exec, path/filepath | C | ✅ |
| `time` | time | C | ✅ |
| `io` | io, io/fs | C | ✅ |
| `buffer` | bytes, bufio | C | ✅ |
| `hash` | hash, crypto/md5, crypto/sha1, crypto/sha256, hash/crc32, hash/fnv | C | ✅ |
| `log` | log, log/slog | C | ✅ |
| `net` | net | C | ✅ |
| `debug` | runtime/debug | C | ✅ |
| `gc` | runtime | C | ✅ |

---

## Tier 1 — 核心高频（优先实现）

| mslang 模块 | Go 来源 | 层 | 新增文件 |
|---|---|---|---|
| `strings` | strings | mixed | STDLIB-13 |
| `strconv` | strconv | mixed | STDLIB-14 |
| `fmt` | fmt | mixed | STDLIB-15 |
| `json` | encoding/json | C | STDLIB-16 |
| `errors` | errors | .ms | STDLIB-17 |
| `slices` | slices | .ms | STDLIB-18 |
| `maps` | maps | .ms | STDLIB-19 |
| `sort` | sort | mixed | STDLIB-20 |
| `set` | (扩展，Go 无) | .ms | STDLIB-21 |
| `heap` | container/heap | .ms | STDLIB-22 |
| `itertools` | iter | .ms | STDLIB-23 |
| `random` | math/rand/v2 | mixed | STDLIB-24 |
| `base64` | encoding/base64 | C | STDLIB-25 |
| `hex` | encoding/hex | .ms | STDLIB-26 |
| `bytes` | bytes | .ms | STDLIB-27 |
| `regexp` | regexp | C | STDLIB-28 |
| `path` | path, path/filepath | .ms | STDLIB-29 |
| `bufio` | bufio | .ms | STDLIB-30 |

---

## Tier 2 — 广泛扩展

| mslang 模块 | Go 来源 | 层 | 新增文件 |
|---|---|---|---|
| `unicode` | unicode, unicode/utf8, unicode/utf16 | C | STDLIB-31 |
| `binary` | encoding/binary | C | STDLIB-32 |
| `csv` | encoding/csv | .ms | STDLIB-33 |
| `base32` | encoding/base32 | .ms | STDLIB-34 |
| `cmp` | cmp | .ms | STDLIB-35 |
| `deque` | (扩展，Go 无) | .ms | STDLIB-36 |
| `linkedlist` | container/list | .ms | STDLIB-37 |
| `ring` | container/ring | .ms | STDLIB-38 |
| `flag` | flag | .ms | STDLIB-39 |
| `url` | net/url | mixed | STDLIB-40 |
| `sync` | sync | .ms | STDLIB-41 |
| `context` | context | .ms | STDLIB-42 |
| `bits` | math/bits | C | STDLIB-43 |
| `hmac` | crypto/hmac | mixed | STDLIB-44 |
| `testing` | testing | .ms | STDLIB-45 |
| `template` | text/template | .ms | STDLIB-46 |

> **注**：`linkedlist` 而非 `list`——避免与内置 `list` 类型产生命名混淆。

---

## Tier 3 — 可选/重量级（暂不写单独规格，后续按需）

| mslang 模块 | Go 来源 | 层 | 说明 |
|---|---|---|---|
| `http` | net/http | mixed | 基于 net 实现 HTTP client/server，工程量大 |
| `big` | math/big | C | 任意精度算术，依赖自包含大数实现 |
| `xml` | encoding/xml | .ms | XML 1.0 解析/生成 |
| `pem` | encoding/pem | .ms | 基于 base64 |
| `cmplx` | math/cmplx | .ms | 复数运算 |
| `crypto` | crypto/rand, crypto/hmac | C | 安全随机、恒时比较 |
| `compress` | compress/gzip, compress/zlib | C | 依赖 zlib |

---

## 明确排除（不适合可嵌入脚本 VM）

| Go 包组 | 排除原因 |
|---|---|
| `go/*`（ast/parser/types/…）| 编译器工具链，不适合脚本 VM |
| `reflect` | 已由 `debug` 模块提供内省 |
| `unsafe`, `syscall`, `cgo` | 语言层不暴露底层指针/系统调用 |
| `plugin` | 已由 dynlib 动态库机制（CAPI-04）覆盖 |
| `runtime` 内部 | 已由 `gc`/`debug` 覆盖 |
| `image/*`, `draw`, `gif`, `jpeg`, `png` | 图形不是脚本 VM 核心需求 |
| `archive/tar`, `archive/zip` | 重量级，Tier 3 候选，暂不排期 |
| `database/sql` | 数据库驱动模型不适合嵌入 |
| `crypto` 重型（tls/x509/rsa/ecdsa/ecdh）| 需要 PKI 生态，超出范围 |
| `debug/*` 二进制格式（elf/macho/pe/dwarf）| 调试工具，非脚本需求 |
| `expvar`, `embed`, `simd`, `weak`, `unique`, `structs` | 与 Go 运行时深度耦合 |

---

## C/.ms 分层原则

| 放 C 的条件 | 放 .ms 的条件 |
|---|---|
| 需要 syscall / OS 原语 | 可纯用语言特性（闭包/类/生成器）实现 |
| 性能关键（正则引擎、JSON 解析器、编解码） | 组合已有 C 原语或内置方法 |
| 需要原始字节操作 | 高层 API，逻辑清晰度优于极致性能 |
| 复杂的正确性要求（Unicode 属性表、RFC 合规编解码）| 纯算法，无外部依赖 |

私有 C 原语用 `_` 前缀注册（如 `_strconv`、`_fmt`、`_sort`），仅供同名 `.ms` 模块 import，用户不应直接 import。

---

## 命名一览（扁平名，按字母序）

`base32` `base64` `binary` `bits` `bufio` `bytes` `cmp` `context` `csv` `debug`✅
`deque` `errors` `flag` `fmt` `gc`✅ `hash`✅ `heap` `hex` `hmac` `io`✅
`itertools` `json` `linkedlist` `log`✅ `maps` `math`✅ `net`✅ `os`✅ `path`
`random` `regexp` `ring` `set` `slices` `sort` `strings` `strconv` `sync`
`template` `testing` `time`✅ `unicode` `url`
