// The tick's events: what happened, for whoever is listening, and how the passes talk.

#include <string.h>

#include "game/systems/systems.h"

void event_emit(Events *events, Event e)
{
    if (events->count < MAX_EVENTS) events->items[events->count++] = e;
}

void events_clear(Events *events)
{
    events->count = 0;
    memset(events->passed, 0, sizeof events->passed);
}

EventCursor events_pending(const Events *last, Events *now, Pass pass)
{
    EventCursor c = {.last = last, .now = now, .i = last ? last->passed[pass] : 0, .end = now->count};
    now->passed[pass] = now->count;
    return c;
}

const Event *events_next(EventCursor *c)
{
    if (!c->in_now) {
        if (c->last && c->i < c->last->count) return &c->last->items[c->i++];
        c->in_now = true;
        c->i = 0;
    }
    if (c->i < c->end) return &c->now->items[c->i++];
    return NULL;
}
