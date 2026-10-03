#pragma once

// SHA-256 (FIPS 180-4), what the release's manifest names every file by. Fed in pieces
// so a file is hashed as it is read or downloaded.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Sha256 {
    uint32_t state[8];
    uint64_t length; // bytes fed so far
    uint8_t block[64];
    size_t used;     // bytes of `block` filled
} Sha256;

void sha256_init(Sha256 *s);
void sha256_feed(Sha256 *s, const void *data, size_t size);
void sha256_finish(Sha256 *s, uint8_t digest[32]);

// The digest as 64 lower-case hex digits and a terminator, and back; false if `hex` is
// anything else.
void sha256_to_hex(const uint8_t digest[32], char hex[65]);
bool sha256_from_hex(const char *hex, uint8_t digest[32]);
