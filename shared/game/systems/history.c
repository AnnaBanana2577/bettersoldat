// Where everyone was over the last second, kept on the server so a shot is judged
// against the soldiers as its shooter saw them. A client's tick is the server tick of
// the frame it shows, and a shot it fires is stamped with it; the server runs the
// bullet forward from that tick to its present, and each step of the way judges it
// against the frame the shooter's screen held at that step, out of this ring, until it
// has caught up and meets the present (bullets_update). A world without a history (a
// client's) judges against what it shows, which is the same thing.

#include <math.h>
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

static int ping_ticks(float ms)
{
    return (int)lroundf(ms * (float)TICK_RATE / 1000.0f);
}

int ping_lead(uint16_t mine, uint16_t theirs)
{
    int lead = ping_ticks(((float)mine + (float)theirs) / 2.0f);
    return lead > LEAD_MAX ? LEAD_MAX : lead;
}

int ping_shift(uint16_t mine, uint16_t theirs)
{
    int shift = ping_ticks(((float)mine - (float)theirs) / 2.0f);
    return shift > LEAD_MAX ? LEAD_MAX : shift < -LEAD_MAX ? -LEAD_MAX : shift;
}

const Soldier *history_future(const Context *ctx, World *w, int i, int ahead)
{
    History *h = w->history;
    if (ahead <= 0 || !h) return &w->soldiers[i];
    if (ahead > LEAD_MAX) ahead = LEAD_MAX;
    if (h->future_tick[i] != w->tick) {
        h->future_tick[i] = w->tick;
        h->future_count[i] = 0;
    }
    // stepped on in place, the soldier standing in for its own future for a moment
    Soldier present = w->soldiers[i];
    while (h->future_count[i] < ahead) {
        int n = h->future_count[i];
        if (n > 0) w->soldiers[i] = h->future[i][n - 1];
        Events scratch = {0}; // what the step would say is said by nobody
        soldier_step(ctx, w, (uint8_t)i, soldier_last_command(&w->soldiers[i], false), &scratch, false);
        h->future[i][n] = w->soldiers[i];
        h->future_count[i] = n + 1;
    }
    w->soldiers[i] = present;
    return &h->future[i][ahead - 1];
}

const Soldier *bullet_target(const Context *ctx, World *w, const Bullet *b, int i)
{
    if (i == b->owner || !w->history) return &w->soldiers[i]; // the shooter from the present; a client from what it shows
    // the frame the shooter had at this step is `lag` behind the present, and it drew the
    // target `lead` ticks on from it
    int shift = -(int)b->lag;
    if (b->now) shift += ping_lead(w->soldiers[b->owner].ping, w->soldiers[i].ping);
    if (shift == 0) return &w->soldiers[i];
    if (shift > 0) return history_future(ctx, w, i, shift);
    uint32_t frame = w->tick + 1 + (uint32_t)shift;
    return history_has(w->history, frame) ? &w->history->frames[frame % HISTORY_TICKS][i] : &w->soldiers[i];
}
