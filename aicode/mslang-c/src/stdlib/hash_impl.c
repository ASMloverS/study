/* src/stdlib/hash_impl.c
 * Pure C11 hash algorithm implementations (no external deps).
 * MD5 (RFC 1321), SHA-1 (FIPS 180-4), SHA-256 (FIPS 180-4),
 * CRC-32 (IEEE / zlib polynomial), FNV-1a (32 and 64 bit).
 */
#include "ms/stdlib/hash_impl.h"
#include <string.h>

/* ================================================================
 * Portable byte-order helpers
 * ================================================================ */

MS_INLINE uint32_t u32_from_le(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

MS_INLINE uint32_t u32_from_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         |  (uint32_t)p[3];
}

MS_INLINE void u32_to_le(uint32_t v, uint8_t* p) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

MS_INLINE void u32_to_be(uint32_t v, uint8_t* p) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

MS_INLINE void u64_to_be(uint64_t v, uint8_t* p) {
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8); p[7] = (uint8_t)(v);
}

MS_INLINE void u64_to_le(uint64_t v, uint8_t* p) {
    p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >>  8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32); p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48); p[7] = (uint8_t)(v >> 56);
}

#define ROL32(x,n) (((x) << (n)) | ((x) >> (32-(n))))

/* ================================================================
 * MD5  (RFC 1321)
 * ================================================================ */

static const uint32_t MD5_T[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
    0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,
    0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,
    0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
    0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,
    0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
    0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
    0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,
    0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391,
};

static const int MD5_S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
};

void ms_md5_init(MD5_CTX* c) {
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->count    = 0;
    memset(c->buf, 0, 64);
}

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t M[16];
    for (int i = 0; i < 16; i++) M[i] = u32_from_le(block + i*4);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = (b & c) | (~b & d); g = (uint32_t)i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (uint32_t)(5*i+1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;           g = (uint32_t)(3*i+5) % 16; }
        else             { f = c ^ (b | ~d);         g = (uint32_t)(7*i)   % 16; }
        uint32_t tmp = d;
        d = c; c = b;
        b = b + ROL32(a + f + MD5_T[i] + M[g], MD5_S[i]);
        a = tmp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void ms_md5_update(MD5_CTX* c, const uint8_t* data, size_t len) {
    size_t idx = (size_t)(c->count % 64);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buf + idx, data, part);
        md5_transform(c->state, c->buf);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buf + idx, data + i, len - i);
}

void ms_md5_final(MD5_CTX* c, uint8_t digest[16]) {
    uint64_t bit_count = c->count * 8;
    uint8_t pad[64];
    size_t idx = (size_t)(c->count % 64);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    memset(pad, 0, pad_len);
    pad[0] = 0x80;
    ms_md5_update(c, pad, pad_len);
    uint8_t len_bytes[8];
    u64_to_le(bit_count, len_bytes);
    ms_md5_update(c, len_bytes, 8);
    for (int i = 0; i < 4; i++) u32_to_le(c->state[i], digest + i*4);
}

void ms_md5_oneshot(const uint8_t* data, size_t len, uint8_t digest[16]) {
    MD5_CTX c;
    ms_md5_init(&c);
    ms_md5_update(&c, data, len);
    ms_md5_final(&c, digest);
}

/* ================================================================
 * SHA-1  (FIPS 180-4)
 * ================================================================ */

void ms_sha1_init(SHA1_CTX* c) {
    c->state[0] = 0x67452301;
    c->state[1] = 0xEFCDAB89;
    c->state[2] = 0x98BADCFE;
    c->state[3] = 0x10325476;
    c->state[4] = 0xC3D2E1F0;
    c->count    = 0;
    memset(c->buf, 0, 64);
}

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t W[80];
    for (int i = 0; i < 16; i++) W[i] = u32_from_be(block + i*4);
    for (int i = 16; i < 80; i++) W[i] = ROL32(W[i-3]^W[i-8]^W[i-14]^W[i-16], 1);

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f=(b&c)|(~b&d);        k=0x5A827999; }
        else if (i < 40) { f=b^c^d;                k=0x6ED9EBA1; }
        else if (i < 60) { f=(b&c)|(b&d)|(c&d);   k=0x8F1BBCDC; }
        else             { f=b^c^d;                k=0xCA62C1D6; }
        uint32_t t = ROL32(a,5) + f + e + k + W[i];
        e=d; d=c; c=ROL32(b,30); b=a; a=t;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

void ms_sha1_update(SHA1_CTX* c, const uint8_t* data, size_t len) {
    size_t idx = (size_t)(c->count % 64);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buf + idx, data, part);
        sha1_transform(c->state, c->buf);
        for (i = part; i + 63 < len; i += 64)
            sha1_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buf + idx, data + i, len - i);
}

void ms_sha1_final(SHA1_CTX* c, uint8_t digest[20]) {
    uint64_t bit_count = c->count * 8;
    uint8_t pad[64];
    size_t idx = (size_t)(c->count % 64);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    memset(pad, 0, pad_len);
    pad[0] = 0x80;
    ms_sha1_update(c, pad, pad_len);
    uint8_t len_bytes[8];
    u64_to_be(bit_count, len_bytes);
    ms_sha1_update(c, len_bytes, 8);
    for (int i = 0; i < 5; i++) u32_to_be(c->state[i], digest + i*4);
}

void ms_sha1_oneshot(const uint8_t* data, size_t len, uint8_t digest[20]) {
    SHA1_CTX c;
    ms_sha1_init(&c);
    ms_sha1_update(&c, data, len);
    ms_sha1_final(&c, digest);
}

/* ================================================================
 * SHA-256  (FIPS 180-4)
 * ================================================================ */

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROR32(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define SHA256_CH(e,f,g)   (((e)&(f))^(~(e)&(g)))
#define SHA256_MAJ(a,b,c)  (((a)&(b))^((a)&(c))^((b)&(c)))
#define SHA256_EP0(a)      (ROR32(a,2)^ROR32(a,13)^ROR32(a,22))
#define SHA256_EP1(e)      (ROR32(e,6)^ROR32(e,11)^ROR32(e,25))
#define SHA256_SIG0(x)     (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define SHA256_SIG1(x)     (ROR32(x,17)^ROR32(x,19)^((x)>>10))

void ms_sha256_init(SHA256_CTX* c) {
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->count = 0;
    memset(c->buf, 0, 64);
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; i++) W[i] = u32_from_be(block + i*4);
    for (int i = 16; i < 64; i++)
        W[i] = SHA256_SIG1(W[i-2]) + W[i-7] + SHA256_SIG0(W[i-15]) + W[i-16];

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],
             e=state[4],f=state[5],g=state[6],h=state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + SHA256_EP1(e) + SHA256_CH(e,f,g)  + SHA256_K[i] + W[i];
        uint32_t t2 =     SHA256_EP0(a) + SHA256_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1;
        d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void ms_sha256_update(SHA256_CTX* c, const uint8_t* data, size_t len) {
    size_t idx = (size_t)(c->count % 64);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buf + idx, data, part);
        sha256_transform(c->state, c->buf);
        for (i = part; i + 63 < len; i += 64)
            sha256_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buf + idx, data + i, len - i);
}

void ms_sha256_final(SHA256_CTX* c, uint8_t digest[32]) {
    uint64_t bit_count = c->count * 8;
    uint8_t pad[64];
    size_t idx = (size_t)(c->count % 64);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    memset(pad, 0, pad_len);
    pad[0] = 0x80;
    ms_sha256_update(c, pad, pad_len);
    uint8_t len_bytes[8];
    u64_to_be(bit_count, len_bytes);
    ms_sha256_update(c, len_bytes, 8);
    for (int i = 0; i < 8; i++) u32_to_be(c->state[i], digest + i*4);
}

void ms_sha256_oneshot(const uint8_t* data, size_t len, uint8_t digest[32]) {
    SHA256_CTX c;
    ms_sha256_init(&c);
    ms_sha256_update(&c, data, len);
    ms_sha256_final(&c, digest);
}

/* ================================================================
 * CRC-32  (IEEE 802.3 polynomial 0xEDB88320, same as zlib/PKZIP)
 * ================================================================ */

static uint32_t s_crc32_table[256];
static bool     s_crc32_table_ready = false;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc32_table[i] = c;
    }
    s_crc32_table_ready = true;
}

uint32_t ms_crc32(const uint8_t* data, size_t len) {
    if (!s_crc32_table_ready) crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = s_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ================================================================
 * FNV-1a  (32-bit and 64-bit)
 * ================================================================ */

uint32_t ms_fnv1a_32(const uint8_t* data, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ data[i]) * 0x01000193u;
    return h;
}

uint64_t ms_fnv1a_64(const uint8_t* data, size_t len) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < len; i++)
        h = (h ^ data[i]) * UINT64_C(0x100000001b3);
    return h;
}
