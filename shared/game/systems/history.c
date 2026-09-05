// Where everyone was over the last second, kept on the server so a shot is judged
// against the soldiers as its shooter saw them. A client shows the others some ticks
// behind the server's present and says which tick with every packet; a bullet it fires
// carries that lag and meets the soldiers from that many ticks ago, out of this ring,
// for as long as it flies. A world without a history (a client's) judges against what
// it shows, which is the same thing.

#include <string.h>

#include "game/systems/systems.h"

void history_record(History *h, const World *w)
{
    memcpy(h->frames[w->tick % HISTORY_TICKS], w->soldiers, sizeof(w->soldiers));
    h->tick = w->tick;
    h->count = h->count + 1 < HISTORY_TICKS ? h->count + 1 : HISTORY_TICKS;
}

Soldier *history_targets(World *w, uint8_t lag)
{
    History *h = w->history;
    if (!h || lag == 0 || lag >= h->count || lag > w->tick) return w->soldiers;
    return h->frames[(w->tick - lag) % HISTORY_TICKS];
}

// A client sees the others its lag ago but itself where it is, so the shooter is taken
// from the present. Rewound with the rest, a thrower who backed off from its grenade
// would stand in the blast on the server alone.
Soldier *target_soldier(World *w, Soldier *frame, uint8_t owner, int i)
{
    return i == owner ? &w->soldiers[i] : &frame[i];
}
