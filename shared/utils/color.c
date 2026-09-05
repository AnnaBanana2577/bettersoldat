#include "utils/utils.h"

Rgba rgba_from_bgra(const uint8_t bgra[4])
{
    return (Rgba){bgra[2], bgra[1], bgra[0], bgra[3]};
}
