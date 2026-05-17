# STDLIB-06: hash 模块

## 职责

消息摘要与校验和：MD5、SHA-1、SHA-256（内置实现，不依赖 OpenSSL）；CRC32（查表法）；FNV-1a（内联计算）。提供一次性接口和流式 `Hasher` 句柄（用 `MsObjUserdata`）。

---

## 函数清单

### 一次性

| 函数 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `hash.md5(data)` | str\|Buffer | Buffer(16) | MD5 摘要 |
| `hash.sha1(data)` | str\|Buffer | Buffer(20) | SHA-1 摘要 |
| `hash.sha256(data)` | str\|Buffer | Buffer(32) | SHA-256 摘要 |
| `hash.crc32(data)` | str\|Buffer | int | CRC-32（IEEE 多项式，同 zlib）|
| `hash.fnv1a(data, bits=64)` | str\|Buffer,int | int | FNV-1a 32 或 64 位 |

### 流式（Hasher 句柄）

| 函数/方法 | 参数 | 返回 | 描述 |
|---|---|---|---|
| `hash.new(algo)` | str | Hasher | algo ∈ `{"md5","sha1","sha256"}` |
| `h.update(data)` | str\|Buffer | nil | 追加数据 |
| `h.digest()` | – | Buffer | 返回摘要（不 reset；调用后不可再 update）|
| `h.hexdigest()` | – | str | `digest().to_hex()` 快捷方式 |
| `h.reset()` | – | nil | 重置到初始状态（可重新 update）|

---

## Hasher 句柄（`MsObjUserdata`）

```c
typedef struct {
    int   algo;             /* 0=md5, 1=sha1, 2=sha256 */
    bool  finalized;
    union {
        MD5_CTX    md5;
        SHA1_CTX   sha1;
        SHA256_CTX sha256;
    } ctx;
} MsHasherData;

static void hasher_finalize(void* data) {
    /* 无需额外释放，MsHasherData 内嵌在 MsObjUserdata.data 中 */
    (void)data;
}

static MsValue ms_hash_new(MsVM* vm, int argc, MsValue* argv) {
    const char* algo = MS_AS_CSTRING(argv[0]);
    int algo_id = parse_algo(algo); /* "md5"→0, "sha1"→1, "sha256"→2 */
    if (algo_id < 0) {
        ms_vm_runtime_error(vm, "hash.new: unknown algo '%s'", algo);
        return MS_NIL_VAL();
    }
    MsValue ud = ms_obj_userdata_new(vm, sizeof(MsHasherData),
                                     hasher_finalize, NULL, "MsHasher");
    MsHasherData* h = (MsHasherData*)ms_obj_userdata_data(ud);
    h->algo = algo_id;
    h->finalized = false;
    hasher_init(h);
    return ud;
}
```

`Hasher` 方法通过 `ms_builtin_invoke` → `ms_userdata_invoke`（CAPI-06）分发：按 `type_tag == "MsHasher"` 识别，`strcmp(method, "update")` 等分发到对应函数。

---

## 内置加密实现（`src/stdlib/hash.c` 内嵌）

**MD5**：RFC 1321，约 250 行 C。  
**SHA-1**：FIPS 180-4，约 200 行 C。  
**SHA-256**：FIPS 180-4，约 280 行 C。  
**CRC-32**：预计算 256 项查表，约 30 行。  
**FNV-1a**：内联常量运算，< 15 行。

建议将加密实现放于 `src/stdlib/hash_impl.c`（独立编译单元），`hash.c` 只做包装。

---

## 实现（`src/stdlib/hash.c`）

```c
/* 辅助：从 str 或 Buffer 取出 (data, len) */
static bool coerce_bytes(MsValue v, const uint8_t** out_data, int* out_len) {
    if (MS_IS_STRING(v)) {
        MsObjString* s = MS_AS_STRING(v);
        *out_data = (const uint8_t*)s->chars;
        *out_len  = s->length;
        return true;
    }
    if (MS_IS_BUFFER(v)) {
        MsObjBuffer* b = MS_AS_BUFFER(v);
        *out_data = b->data;
        *out_len  = b->len;
        return true;
    }
    return false;
}

static MsValue ms_hash_sha256(MsVM* vm, int argc, MsValue* argv) {
    const uint8_t* data; int len;
    if (!coerce_bytes(argv[0], &data, &len)) {
        ms_vm_runtime_error(vm, "hash.sha256: expected str or Buffer");
        return MS_NIL_VAL();
    }
    uint8_t digest[32];
    sha256_oneshot(data, (size_t)len, digest);
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, digest, 32));
}

static const MsNativeDef ms_hash_defs[] = {
    {"md5",    ms_hash_md5,    1},
    {"sha1",   ms_hash_sha1,   1},
    {"sha256", ms_hash_sha256, 1},
    {"crc32",  ms_hash_crc32,  1},
    {"fnv1a",  ms_hash_fnv1a, -1},
    {"new",    ms_hash_new,    1},
    {NULL, NULL, 0}
};

void ms_module_hash_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, ms_hash_defs);
}
```

---

## 依赖

- CAPI-01/02（注册）
- CAPI-05（MsObjUserdata，Hasher 句柄）
- STDLIB-05（Buffer，输入与输出类型）

---

## 测试

```ms
// tests/fixtures/stdlib_hash_basic.ms
import hash, buffer

var d = hash.sha256("hello")
assert(d.len() == 32)
print(d.to_hex())  // 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824

var h = hash.new("md5")
h.update("hel")
h.update("lo")
print(h.hexdigest())  // 5d41402abc4b2a76b9719d911017c592
h.reset()
h.update("hello")
assert(h.hexdigest() == "5d41402abc4b2a76b9719d911017c592")

assert(hash.crc32("hello") == 0x3610a686)
```
