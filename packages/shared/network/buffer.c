// The bits, and the one serializer for both directions.

#include <math.h>
#include <string.h>

#include "network/network.h"

NetBuf netbuf_writer(uint8_t *data, size_t size)
{
    memset(data, 0, size);
    return (NetBuf){.data = data, .size = size, .mode = NET_WRITE};
}

NetBuf netbuf_reader(const uint8_t *data, size_t size)
{
    return (NetBuf){.data = (uint8_t *)data, .size = size, .mode = NET_READ};
}

bool netbuf_ok(const NetBuf *b) { return !b->overflow && !b->bad; }

size_t netbuf_bytes(const NetBuf *b) { return (b->bit + 7) / 8; }

bool netbuf_done(const NetBuf *b)
{
    return netbuf_ok(b) && b->mode == NET_READ && b->bit <= b->size * 8 && b->size * 8 - b->bit < 8;
}

// The bits go out least significant first, filling each byte from its low end.
static void put_bits(NetBuf *b, uint32_t v, int bits)
{
    for (int i = 0; i < bits; i++) {
        if (b->bit >= b->size * 8) {
            b->overflow = true;
            return;
        }
        if (v >> i & 1) b->data[b->bit / 8] |= (uint8_t)(1u << (b->bit % 8));
        b->bit++;
    }
}

static uint32_t get_bits(NetBuf *b, int bits)
{
    uint32_t v = 0;
    for (int i = 0; i < bits; i++) {
        if (b->bit >= b->size * 8) {
            b->bad = true;
            return 0;
        }
        if (b->data[b->bit / 8] >> (b->bit % 8) & 1) v |= 1u << i;
        b->bit++;
    }
    return v;
}

void net_bits(NetBuf *b, uint32_t *v, int bits)
{
    if (b->mode == NET_WRITE) {
        if (bits < 32 && *v >> bits) b->bad = true; // doesn't fit its width
        put_bits(b, *v, bits);
    } else {
        *v = get_bits(b, bits);
    }
}

void net_signed(NetBuf *b, int32_t *v, int bits)
{
    if (b->mode == NET_WRITE) {
        int64_t lo = -((int64_t)1 << (bits - 1)), hi = ((int64_t)1 << (bits - 1)) - 1;
        if (*v < lo || *v > hi) b->bad = true;
        put_bits(b, (uint32_t)*v & (bits == 32 ? 0xffffffffu : (1u << bits) - 1), bits);
    } else {
        uint32_t raw = get_bits(b, bits);
        if (bits < 32 && (raw >> (bits - 1) & 1)) raw |= ~((1u << bits) - 1); // sign-extend
        *v = (int32_t)raw;
    }
}

void net_bool(NetBuf *b, bool *v)
{
    uint32_t bit = *v ? 1 : 0;
    net_bits(b, &bit, 1);
    *v = bit != 0;
}

void net_u8(NetBuf *b, uint8_t *v)
{
    uint32_t x = *v;
    net_bits(b, &x, 8);
    *v = (uint8_t)x;
}

void net_u16(NetBuf *b, uint16_t *v)
{
    uint32_t x = *v;
    net_bits(b, &x, 16);
    *v = (uint16_t)x;
}

void net_u32(NetBuf *b, uint32_t *v) { net_bits(b, v, 32); }

void net_u64(NetBuf *b, uint64_t *v)
{
    uint32_t lo = (uint32_t)*v, hi = (uint32_t)(*v >> 32);
    net_bits(b, &lo, 32);
    net_bits(b, &hi, 32);
    *v = (uint64_t)hi << 32 | lo;
}

int net_bits_for(uint32_t max)
{
    int bits = 1;
    while (bits < 32 && (max >> bits)) bits++;
    return bits;
}

void net_range(NetBuf *b, uint32_t *v, uint32_t max)
{
    if (b->mode == NET_WRITE && *v > max) b->bad = true;
    net_bits(b, v, net_bits_for(max));
    if (b->mode == NET_READ && *v > max) b->bad = true;
}

void net_f32(NetBuf *b, float *v)
{
    uint32_t raw;
    memcpy(&raw, v, 4);
    net_bits(b, &raw, 32);
    memcpy(v, &raw, 4);
    if (b->mode == NET_READ && !isfinite(*v)) b->bad = true;
}

void net_vec2(NetBuf *b, Vec2 *v)
{
    net_f32(b, &v->x);
    net_f32(b, &v->y);
}

void net_string(NetBuf *b, char *s, size_t size)
{
    uint32_t len = b->mode == NET_WRITE ? (uint32_t)strnlen(s, size) : 0;
    if (b->mode == NET_WRITE && len >= size) b->bad = true; // not terminated within its room
    net_bits(b, &len, 8);
    if (len >= size || len > 255) {
        b->bad = true;
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t c = (uint8_t)s[i];
        net_bits(b, &c, 8);
        if (b->mode == NET_READ) s[i] = (char)c;
    }
    if (b->mode == NET_READ) s[len] = '\0';
}
