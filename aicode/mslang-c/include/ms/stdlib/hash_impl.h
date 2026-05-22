#ifndef MS_HASH_IMPL_H
#define MS_HASH_IMPL_H

#include "ms/common.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Context structs ---- */

typedef struct {
    uint32_t state[4];
    uint64_t count;       /* total bytes processed */
    uint8_t  buf[64];
} MD5_CTX;

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buf[64];
} SHA1_CTX;

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} SHA256_CTX;

/* ---- MD5 ---- */
void ms_md5_init   (MD5_CTX* c);
void ms_md5_update (MD5_CTX* c, const uint8_t* data, size_t len);
void ms_md5_final  (MD5_CTX* c, uint8_t digest[16]);
void ms_md5_oneshot(const uint8_t* data, size_t len, uint8_t digest[16]);

/* ---- SHA-1 ---- */
void ms_sha1_init   (SHA1_CTX* c);
void ms_sha1_update (SHA1_CTX* c, const uint8_t* data, size_t len);
void ms_sha1_final  (SHA1_CTX* c, uint8_t digest[20]);
void ms_sha1_oneshot(const uint8_t* data, size_t len, uint8_t digest[20]);

/* ---- SHA-256 ---- */
void ms_sha256_init   (SHA256_CTX* c);
void ms_sha256_update (SHA256_CTX* c, const uint8_t* data, size_t len);
void ms_sha256_final  (SHA256_CTX* c, uint8_t digest[32]);
void ms_sha256_oneshot(const uint8_t* data, size_t len, uint8_t digest[32]);

/* ---- CRC-32 (IEEE / zlib polynomial) ---- */
uint32_t ms_crc32(const uint8_t* data, size_t len);

/* ---- FNV-1a ---- */
uint32_t ms_fnv1a_32(const uint8_t* data, size_t len);
uint64_t ms_fnv1a_64(const uint8_t* data, size_t len);

#endif /* MS_HASH_IMPL_H */
