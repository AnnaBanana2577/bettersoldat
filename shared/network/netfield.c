// A struct on the wire, field by field from its table, whole or as a delta.

#include <string.h>

#include "network/network.h"

static uint64_t load_uint(const void *at, int size)
{
    uint64_t v = 0;
    memcpy(&v, at, (size_t)size); // little-endian: the low bytes are the value
    return v;
}

static void store_uint(void *at, int size, uint64_t v) { memcpy(at, &v, (size_t)size); }

// One field, written or read, at `at` in the struct.
static void field_serialize(NetBuf *b, const NetField *f, void *at)
{
    switch (f->kind) {
    case NET_U: {
        if (f->size == 8) {
            uint64_t v = load_uint(at, 8);
            net_u64(b, &v);
            store_uint(at, 8, v);
            break;
        }
        uint32_t v = (uint32_t)load_uint(at, f->size);
        net_bits(b, &v, f->bits);
        if (f->max && v > f->max) b->bad = true;
        store_uint(at, f->size, v);
        break;
    }
    case NET_I: {
        // sign-extend from the memory width, then the wire's
        uint64_t raw = load_uint(at, f->size);
        int shift = 64 - f->size * 8;
        int32_t v = (int32_t)((int64_t)(raw << shift) >> shift);
        net_signed(b, &v, f->bits);
        store_uint(at, f->size, (uint64_t)(int64_t)v);
        break;
    }
    case NET_BOOL: {
        bool v = *(bool *)at;
        net_bool(b, &v);
        *(bool *)at = v;
        break;
    }
    case NET_F32: net_f32(b, (float *)at); break;
    case NET_VEC2: net_vec2(b, (Vec2 *)at); break;
    case NET_RGBA: {
        Rgba *c = at;
        net_u8(b, &c->r);
        net_u8(b, &c->g);
        net_u8(b, &c->b);
        net_u8(b, &c->a);
        break;
    }
    }
}

void netfields_serialize(NetBuf *b, const NetField *fields, int count, void *state, const void *base)
{
    for (int i = 0; i < count; i++) {
        const NetField *f = &fields[i];
        void *at = (uint8_t *)state + f->offset;
        const void *from = base ? (const uint8_t *)base + f->offset : NULL;
        if (from) {
            bool changed = b->mode == NET_WRITE && memcmp(at, from, f->size) != 0;
            net_bool(b, &changed);
            if (!changed) {
                if (b->mode == NET_READ) memcpy(at, from, f->size);
                continue;
            }
        }
        field_serialize(b, f, at);
    }
}

void netfields_copy(const NetField *fields, int count, void *dst, const void *src)
{
    for (int i = 0; i < count; i++) {
        const NetField *f = &fields[i];
        memcpy((uint8_t *)dst + f->offset, (const uint8_t *)src + f->offset, f->size);
    }
}

bool netfields_equal(const NetField *fields, int count, const void *a, const void *b)
{
    for (int i = 0; i < count; i++) {
        const NetField *f = &fields[i];
        if (memcmp((const uint8_t *)a + f->offset, (const uint8_t *)b + f->offset, f->size) != 0) return false;
    }
    return true;
}
