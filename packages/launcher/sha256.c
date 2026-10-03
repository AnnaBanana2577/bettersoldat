#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void compress(Sha256 *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3];
    uint32_t e = s->state[4], f = s->state[5], g = s->state[6], h = s->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->state[0] += a; s->state[1] += b; s->state[2] += c; s->state[3] += d;
    s->state[4] += e; s->state[5] += f; s->state[6] += g; s->state[7] += h;
}

void sha256_init(Sha256 *s)
{
    static const uint32_t H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->state, H, sizeof H);
    s->length = 0;
    s->used = 0;
}

void sha256_feed(Sha256 *s, const void *data, size_t size)
{
    const uint8_t *p = data;
    s->length += size;
    if (s->used) {
        size_t take = 64 - s->used < size ? 64 - s->used : size;
        memcpy(s->block + s->used, p, take);
        s->used += take;
        p += take;
        size -= take;
        if (s->used < 64) return;
        compress(s, s->block);
        s->used = 0;
    }
    for (; size >= 64; p += 64, size -= 64) compress(s, p);
    memcpy(s->block, p, size);
    s->used = size;
}

void sha256_finish(Sha256 *s, uint8_t digest[32])
{
    uint64_t bits = s->length * 8;
    uint8_t pad[72] = {0x80};
    size_t padding = (s->used < 56 ? 56 : 120) - s->used;
    for (int i = 0; i < 8; i++) pad[padding + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_feed(s, pad, padding + 8);
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(s->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(s->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(s->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)s->state[i];
    }
}

void sha256_to_hex(const uint8_t digest[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool sha256_from_hex(const char *hex, uint8_t digest[32])
{
    for (int i = 0; i < 32; i++) {
        int hi = nibble(hex[i * 2]), lo = hi < 0 ? -1 : nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        digest[i] = (uint8_t)(hi << 4 | lo);
    }
    return hex[64] == '\0';
}
