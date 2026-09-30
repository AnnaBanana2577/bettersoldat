// The passes' mail: a pass sees every event emitted since it last ran, once.

#include <string.h>

#include "test.h"

static Event echo(int n) { return (Event){.type = EVENT_ECHO_TEST, .echo = {.n = n}}; }

// The numbers a pass sees, in order, as a string.
static const char *seen(const Events *last, Events *now, Pass pass, char *out)
{
    int n = 0;
    EventCursor c = events_pending(last, now, pass);
    for (const Event *e = events_next(&c); e; e = events_next(&c)) out[n++] = (char)('0' + e->echo.n);
    out[n] = '\0';
    return out;
}

void events_tests(void)
{
    char buf[16];
    Events a = {0}, b = {0};

    // tick 1: bullets run after 1 and 2, then 3 and 4 are emitted behind it
    event_emit(&a, echo(1));
    event_emit(&a, echo(2));
    CHECK(strcmp(seen(NULL, &a, PASS_BULLETS, buf), "12") == 0, "a pass sees this tick's events before it (%s)", buf);
    event_emit(&a, echo(3));
    event_emit(&a, echo(4));

    // tick 2: 5 emitted before the bullets run
    events_clear(&b);
    event_emit(&b, echo(5));
    CHECK(strcmp(seen(&a, &b, PASS_BULLETS, buf), "345") == 0,
          "next tick it sees what came after it last tick, then this tick's, each once (%s)", buf);
    event_emit(&b, echo(6));
    CHECK(strcmp(seen(&a, &b, PASS_THINGS, buf), "123456") == 0,
          "a pass that never ran last tick sees all of it, and this tick's so far (%s)", buf);

    // tick 3: nothing new, and nothing seen twice
    Events c = {0};
    CHECK(strcmp(seen(&b, &c, PASS_BULLETS, buf), "6") == 0, "only what came after the pass last tick (%s)", buf);
    CHECK(strcmp(seen(&b, &c, PASS_THINGS, buf), "") == 0, "and nothing for a pass that had seen it all (%s)", buf);
}
