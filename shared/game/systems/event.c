// The tick's events: what happened, for whoever is listening.

#include "game/systems/systems.h"

void event_emit(Events *events, Event e)
{
    if (events->count < MAX_EVENTS) events->items[events->count++] = e;
}

void events_clear(Events *events)
{
    events->count = 0;
}
