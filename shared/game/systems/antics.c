// The idle antics: a soldier standing still long enough smokes, wipes its brow or
// scratches; /mercy, /pwn, /smoke and the rest. Port of idleControl by way of
// soldat-odin, which keeps the clock but does not play the antic yet.

#include "game/systems/systems.h"

#define DEFAULT_IDLE_TIME 1800 // ticks standing still before an antic

void antics_apply(const Context *ctx, World *w, Soldier *s)
{
    (void)ctx;
    (void)w;
    if (s->stat != 0) return;

    if (s->legs.id == ANIM_STAND && s->body.id == ANIM_STAND && !s->dead) {
        s->idle.time++;
        if (s->idle.time > DEFAULT_IDLE_TIME) {
            // Picking and playing the antic on the body animation is not ported yet.
            s->idle.time = 0;
        }
    } else {
        s->idle.time = 0;
    }
}
