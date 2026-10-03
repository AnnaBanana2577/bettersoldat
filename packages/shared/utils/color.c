#include "utils/utils.h"

#include <ctype.h>
#include <string.h>

Rgba rgba_from_bgra(const uint8_t bgra[4])
{
    return (Rgba){bgra[2], bgra[1], bgra[0], bgra[3]};
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return -1;
}

bool rgba_parse_hex(const char *text, Rgba *out)
{
    if (!text) return false;
    if (*text == '#') text++;
    if (strlen(text) != 6) return false;
    uint8_t bytes[3];
    for (int i = 0; i < 3; i++) {
        int hi = hex_digit(text[2 * i]), lo = hex_digit(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes[i] = (uint8_t)(hi * 16 + lo);
    }
    *out = (Rgba){bytes[0], bytes[1], bytes[2], 255};
    return true;
}
