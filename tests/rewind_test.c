// A shot heard late is judged against the soldiers as its shooter drew them: the server
// runs the bullet forward from the shooter's tick and, each step of the way, meets the
// frame the shooter's screen held at that step, out of the history ring, so what landed
// on the shooter's screen lands on the server however far the target has moved since
// (bullets_update, history_targets). And a client hearing the shot gives it the flash
// and the tracer its own weapon pass would have.

#include <math.h>
#include <stdlib.h>

#include "test.h"

#define GAP 60.0f // close enough that the bullet is on the target in a couple of ticks

// A tick with soldier 1 running right and soldier 0 standing, aiming at it.
static void tick_running(Game *g)
{
    World *w = &g->world;
    Command cmds[MAX_PLAYERS] = {0};
    cmds[0] = (Command){.seq = w->tick + 1, .aim = vec2_sub(w->soldiers[1].pos, vec2(0, 8))};
    cmds[1] = (Command){.seq = w->tick + 1, .buttons = BUTTON_RIGHT, .aim = vec2_add(w->soldiers[1].pos, vec2(100, 0))};
    game_tick(g, cmds);
}

static int count(const Events *events, EventType type, int target)
{
    int n = 0;
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        if (e->type != type) continue;
        if (type == EVENT_HIT && e->hit.target != target) continue;
        if (type == EVENT_DAMAGE && e->damage.target != target) continue;
        n++;
    }
    return n;
}

// The shot soldier 0 would make at soldier 1 as it stands, leading it by its run over
// the bullet's flight.
static EventShot shot_at(const Game *g, const Soldier *target)
{
    const World *w = &g->world;
    const Soldier *shooter = &w->soldiers[0];
    const WeaponStats *stats = &g->ctx.weapons.info[WEAPON_RUGER].stats;
    Vec2 origin = vec2(shooter->pos.x, shooter->pos.y - 4.0f);
    Vec2 chest = vec2_sub(target->pos, vec2(0, 8));
    float flight = vec2_length(vec2_sub(chest, origin)) / stats->speed;
    Vec2 lead = vec2_add(chest, vec2_scale(target->vel, flight));
    Vec2 vel = vec2_scale(vec2_normalize(vec2_sub(lead, origin)), stats->speed);
    return (EventShot){.player = 0, .weapon = WEAPON_RUGER, .pos = origin, .vel = vel, .damage = stats->damage, .shot = shooter->shot_count + 1};
}

void rewind_tests(void)
{
    Game *g = scene("Arena", GAP, WEAPON_RUGER, WEAPON_RUGER);
    g->world.history = calloc(1, sizeof(History));
    World *w = &g->world;
    settle(g);
    for (int i = 0; i < 40; i++) tick_running(g);
    CHECK(w->soldiers[1].vel.x > 1.0f, "soldier 1 is running right (%.2f a tick)", w->soldiers[1].vel.x);

    // The shooter's screen holds the frame of the tick just recorded; its shot of that
    // tick is stamped with the tick before, as the client's are, and the bullet's first
    // step meets this frame.
    uint32_t seen = w->tick;
    Soldier target_then = w->soldiers[1];
    EventShot shot = shot_at(g, &target_then);
    const Soldier *frame = history_at(w, seen);
    CHECK(frame && frame[1].pos.x == target_then.pos.x, "the history holds the frame the shooter saw (tick %u)", seen);

    // the shot arrives ten ticks later, the target having run on
    for (int i = 0; i < 10; i++) tick_running(g);
    float moved = w->soldiers[1].pos.x - target_then.pos.x;
    CHECK(moved > 15.0f, "by then soldier 1 has run %.1f units past where it was aimed at", moved);

    uint32_t stamp = seen - 1;
    shot.advance = (uint8_t)(w->tick - stamp);
    game_hear(g, (Event){.type = EVENT_SHOT, .tick = stamp, .shot = shot});
    tick_running(g);
    CHECK(count(&g->events, EVENT_HIT, 1) > 0 && count(&g->events, EVENT_DAMAGE, 1) > 0,
          "judged against the frames the shooter saw, the shot lands and wounds (%d hits, %d wounds)",
          count(&g->events, EVENT_HIT, 1), count(&g->events, EVENT_DAMAGE, 1));

    // the same aim as a shot of the present misses: the target is no longer there
    for (int i = 0; i < 10; i++) tick_running(g);
    EventShot now = shot;
    now.shot = w->soldiers[0].shot_count + 1;
    now.advance = 0;
    game_hear(g, (Event){.type = EVENT_SHOT, .tick = 0, .shot = now});
    int hits = 0;
    for (int i = 0; i < 6; i++) {
        tick_running(g);
        hits += count(&g->events, EVENT_HIT, 1);
    }
    CHECK(hits == 0, "the same shot judged at the present misses (%d hits)", hits);

    // a client hearing a shot run forward gives it the flash and a tracer to where it got
    w->authority = false;
    EventShot heard = shot_at(g, &w->soldiers[1]);
    heard.shot = w->soldiers[0].shot_count + 1;
    heard.advance = 3;
    bool fired_before = w->soldiers[0].fired;
    game_hear(g, (Event){.type = EVENT_SHOT, .tick = w->tick - 3, .shot = heard});
    tick_running(g);
    int fires = count(&g->events, EVENT_FIRE, -1), traces = count(&g->events, EVENT_BULLET_TRACE, -1);
    const Event *trace = NULL;
    for (int i = 0; i < g->events.count; i++)
        if (g->events.items[i].type == EVENT_BULLET_TRACE) trace = &g->events.items[i];
    CHECK(fires == 1 && w->soldiers[0].fired && !fired_before, "a shot heard flashes and sounds at its shooter once (%d fire events)", fires);
    CHECK(traces == 1 && trace && trace->bullet_trace.ticks == 3 && trace->bullet_trace.owner == 0 &&
              vec2_length(vec2_sub(trace->bullet_trace.to, trace->bullet_trace.from)) > 2.0f * g->ctx.weapons.info[WEAPON_RUGER].stats.speed,
          "and leaves a tracer along the flight it was run through (%d, %.0f units over %d ticks)", traces,
          trace ? vec2_length(vec2_sub(trace->bullet_trace.to, trace->bullet_trace.from)) : 0.0f, trace ? trace->bullet_trace.ticks : 0);

    free(w->history);
    scene_free(g);
}
