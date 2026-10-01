// Where everyone was over the last second, kept on the server so a shot is judged
// against the soldiers as its shooter saw them. A client's tick is the server tick of
// the frame it shows, and a shot it fires is stamped with it; the server runs the
// bullet forward from that tick to its present, and each step of the way judges it
// against the frame the shooter's screen held at that step, out of this ring, until it
// has caught up and meets the present (bullets_update). A world without a history (a
// client's) judges against what it shows, which is the same thing.

#include <string.h>

#include "game/systems/systems.h"

void history_record(History *h, const World *w)
{
    memcpy(h->frames[w->tick % HISTORY_TICKS], w->soldiers, sizeof(w->soldiers));
    memcpy(h->things[w->tick % HISTORY_TICKS], w->things, sizeof(w->things));
    h->tick = w->tick;
    h->count = h->count + 1 < HISTORY_TICKS ? h->count + 1 : HISTORY_TICKS;
}

static bool history_has(const History *h, uint32_t tick) { return h && tick <= h->tick && h->tick - tick < h->count; }

const Soldier *history_at(const World *w, uint32_t tick)
{
    return history_has(w->history, tick) ? w->history->frames[tick % HISTORY_TICKS] : NULL;
}

const Thing *history_things_at(const World *w, uint32_t tick)
{
    return history_has(w->history, tick) ? w->history->things[tick % HISTORY_TICKS] : NULL;
}

// The present is the soldiers as they stand after this tick's step, which the frame
// recorded at the tick's end will hold: a lag of 1 is the frame before it, which is the
// last one recorded.
Soldier *history_targets(World *w, uint8_t lag)
{
    History *h = w->history;
    if (!h || lag == 0 || lag > w->tick + 1) return w->soldiers;
    uint32_t frame = w->tick + 1 - lag;
    return history_has(h, frame) ? h->frames[frame % HISTORY_TICKS] : w->soldiers;
}

// A client sees the others its lag ago but itself where it is, so the shooter is taken
// from the present. Rewound with the rest, a thrower who backed off from its grenade
// would stand in the blast on the server alone.
Soldier *target_soldier(World *w, Soldier *frame, uint8_t owner, int i)
{
    return i == owner ? &w->soldiers[i] : &frame[i];
}

const Soldier *bullet_target(World *w, const Bullet *b, int i)
{
    if (i == b->owner) return &w->soldiers[i]; // the shooter from the present, as above
    return &history_targets(w, b->lag)[i];
}
