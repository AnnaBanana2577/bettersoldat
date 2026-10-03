// Where a team is placed: the one pick over the map that rolls the world's rng.

#include "game/systems/systems.h"

Vec2 spawn_point(const Map *m, Team team, uint64_t *rng)
{
    int32_t want = (int32_t)team;
    for (int pass = 0; pass < 2; pass++) {
        int count = 0;
        for (int i = 0; i < m->spawnpoint_count; i++) {
            if (m->spawnpoints[i].active && m->spawnpoints[i].team == want) count++;
        }
        if (count > 0) {
            int pick = rand_int(rng, count);
            for (int i = 0; i < m->spawnpoint_count; i++) {
                const Spawnpoint *s = &m->spawnpoints[i];
                if (!(s->active && s->team == want)) continue;
                if (pick == 0) return s->pos;
                pick--;
            }
        }
        want = 0; // then the general ones
    }
    return (Vec2){0};
}
