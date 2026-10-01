// The query's bytes (query.h).

#include <string.h>

#include "network/query.h"

static const uint8_t MAGIC[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t REQUEST[4] = {'B', 'S', 'Q', 'i'};
static const uint8_t REPLY[4] = {'B', 'S', 'R', 'i'};

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool query_is_query(const uint8_t *data, size_t size) { return size >= 4 && memcmp(data, MAGIC, 4) == 0; }

size_t query_write_request(uint8_t *out, size_t size, uint32_t nonce)
{
    if (size < QUERY_REQUEST_SIZE) return 0;
    memset(out, 0, QUERY_REQUEST_SIZE);
    memcpy(out, MAGIC, 4);
    memcpy(out + 4, REQUEST, 4);
    put_u32(out + 8, nonce);
    return QUERY_REQUEST_SIZE;
}

bool query_read_request(const uint8_t *data, size_t size, uint32_t *nonce)
{
    if (size < QUERY_REQUEST_SIZE || !query_is_query(data, size) || memcmp(data + 4, REQUEST, 4) != 0) return false;
    *nonce = get_u32(data + 8);
    return true;
}

// A string as its length and its bytes, cut to what `max` holds with a terminator.
static size_t put_string(uint8_t *p, const char *s, size_t max)
{
    size_t n = strnlen(s, max - 1);
    p[0] = (uint8_t)n;
    memcpy(p + 1, s, n);
    return 1 + n;
}

size_t query_write_reply(uint8_t *out, size_t size, uint32_t nonce, const ServerInfo *info)
{
    if (size < QUERY_REPLY_MAX) return 0;
    uint8_t *p = out;
    memcpy(p, MAGIC, 4);
    memcpy(p + 4, REPLY, 4);
    put_u32(p + 8, nonce);
    p += 12;
    *p++ = (uint8_t)info->protocol;
    *p++ = (uint8_t)(info->protocol >> 8);
    *p++ = info->players;
    *p++ = info->bots;
    *p++ = info->max_players;
    *p++ = info->mode;
    *p++ = info->password ? QUERY_FLAG_PASSWORD : 0;
    p += put_string(p, info->hostname, sizeof info->hostname);
    p += put_string(p, info->map, sizeof info->map);
    return (size_t)(p - out);
}

// A string as put_string lays it: false if it runs past the end or past `max`.
static bool get_string(const uint8_t **p, const uint8_t *end, char *s, size_t max)
{
    if (*p >= end) return false;
    size_t n = **p;
    if (n >= max || (size_t)(end - *p - 1) < n) return false;
    memcpy(s, *p + 1, n);
    s[n] = '\0';
    *p += 1 + n;
    return true;
}

bool query_read_reply(const uint8_t *data, size_t size, uint32_t nonce, ServerInfo *info)
{
    *info = (ServerInfo){0};
    if (size < 19 || !query_is_query(data, size) || memcmp(data + 4, REPLY, 4) != 0 || get_u32(data + 8) != nonce)
        return false;
    const uint8_t *p = data + 12, *end = data + size;
    info->protocol = (uint16_t)(p[0] | p[1] << 8);
    info->players = p[2];
    info->bots = p[3];
    info->max_players = p[4];
    info->mode = p[5];
    info->password = p[6] & QUERY_FLAG_PASSWORD;
    p += 7;
    return get_string(&p, end, info->hostname, sizeof info->hostname) && get_string(&p, end, info->map, sizeof info->map) &&
           p == end;
}
