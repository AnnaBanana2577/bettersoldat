// Colours: the config's hex form.

#include "test.h"
#include "utils/utils.h"

void color_tests(void)
{
    Rgba c = {1, 2, 3, 4};
    CHECK(rgba_parse_hex("C73833", &c) && c.r == 199 && c.g == 56 && c.b == 51 && c.a == 255, "RRGGBB parses, opaque");
    CHECK(rgba_parse_hex("#c73833", &c) && c.r == 199, "a leading # and lower case are fine");
    c = (Rgba){1, 2, 3, 4};
    CHECK(!rgba_parse_hex("C7383", &c) && !rgba_parse_hex("C7383G", &c) && !rgba_parse_hex("", &c) && c.r == 1,
          "anything else is refused and leaves the colour alone");
}
