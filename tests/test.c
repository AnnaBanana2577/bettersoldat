#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int checks, failures;

void check_that(bool ok, const char *file, int line, const char *fmt, ...)
{
    checks++;
    if (ok) return;
    failures++;
    va_list args;
    va_start(args, fmt);
    printf("FAIL %s:%d: ", file, line);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

// --- scenes ------------------------------------------------------------------------

Game *scene(const char *map, float gap, WeaponId a_weapon, WeaponId b_weapon)
{
    Game *g = calloc(1, sizeof(Game));
    if (!g || !context_load(&g->ctx, "assets", map)) {
        printf("could not load map '%s' from assets/: the tests run from the project directory\n", map);
        exit(2);
    }
    game_init(g, 1, match_settings_for_map(g->ctx.map));
    g->world.authority = true;

    uint64_t rng = 7;
    Vec2 at = spawn_point(g->ctx.map, TEAM_ALPHA, &rng);
    soldier_spawn(&g->ctx, &g->world.soldiers[0], at, TEAM_ALPHA, GEAR_JETS, a_weapon, WEAPON_COLT);
    soldier_spawn(&g->ctx, &g->world.soldiers[1], vec2(at.x + gap, at.y), TEAM_BRAVO, GEAR_JETS, b_weapon, WEAPON_COLT);
    return g;
}

void scene_free(Game *g)
{
    context_destroy(&g->ctx);
    free(g);
}

// Aimed at the other's chest.
static Command command(const Game *g, int from, Buttons buttons)
{
    Vec2 aim = g->world.soldiers[1 - from].pos;
    aim.y -= 8.0f;
    return (Command){.seq = g->world.tick + 1, .buttons = buttons, .aim = aim};
}

static void tally(Tally *t, const Events *events)
{
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        switch (e->type) {
        case EVENT_BULLET_SPAWN: t->spawned[e->bullet_spawn.weapon]++; break;
        case EVENT_FIRE: t->fired++; break;
        case EVENT_HIT: t->hits++; break;
        case EVENT_KILL: t->kills++; break;
        case EVENT_EXPLOSION: t->explosions++; break;
        case EVENT_DAMAGE: t->damage += e->damage.amount; break;
        default: break;
        }
    }
}

Tally run(Game *g, int ticks, Presses buttons)
{
    Tally t = {0};
    Command cmds[MAX_PLAYERS] = {0};
    for (int i = 0; i < ticks; i++) {
        cmds[0] = command(g, 0, buttons(i));
        cmds[1] = command(g, 1, 0);
        game_tick(g, cmds);
        tally(&t, &g->events);
    }
    return t;
}

void settle(Game *g) { run(g, 120, press_nothing); }

Buttons press_nothing(int tick)
{
    (void)tick;
    return 0;
}

Buttons press_fire(int tick)
{
    (void)tick;
    return BUTTON_FIRE;
}

void place(Soldier *s, Vec2 at)
{
    s->pos = s->old_pos = at;
    s->vel = (Vec2){0};
}

int find_thing(const Game *g, ThingStyle style)
{
    for (int i = 0; i < MAX_THINGS; i++)
        if (g->world.things[i].style == style) return i;
    return -1;
}

Vec2 open_sky(const Game *g, int soldier)
{
    const Soldier *s = &g->world.soldiers[soldier];
    for (int up = 600; up <= 2000; up += 100) {
        Vec2 at = vec2(s->pos.x, s->pos.y - (float)up);
        RayFilter filter = {.player = true, .team = s->team};
        if (!map_ray_cast(g->ctx.map, at, vec2(at.x, at.y + 500.0f), 550.0f, filter, NULL)) return at;
    }
    return s->pos;
}

static bool same_vec(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

bool same_world(const World *a, const World *b)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *x = &a->soldiers[i], *y = &b->soldiers[i];
        if (x->active != y->active || !same_vec(x->pos, y->pos) || !same_vec(x->vel, y->vel) || x->health != y->health ||
            x->dead != y->dead || x->weapon.id != y->weapon.id || x->weapon.ammo != y->weapon.ammo ||
            x->shot_count != y->shot_count || x->hit_spray != y->hit_spray || x->legs.id != y->legs.id ||
            x->body.id != y->body.id || x->body.frame != y->body.frame) {
            return false;
        }
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        const Bullet *x = &a->bullets[i], *y = &b->bullets[i];
        if (x->active != y->active) return false;
        if (x->active && (!same_vec(x->pos, y->pos) || !same_vec(x->vel, y->vel) || x->timeout != y->timeout ||
                          x->hit_multiply != y->hit_multiply)) {
            return false;
        }
    }
    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing *x = &a->things[i], *y = &b->things[i];
        if (x->style != y->style) return false;
        for (int k = 0; k < x->points; k++)
            if (!same_vec(x->pos[k], y->pos[k])) return false;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Ragdoll *x = &a->ragdolls[i], *y = &b->ragdolls[i];
        if (x->active != y->active) return false;
        for (int k = 0; x->active && k < RAGDOLL_POINTS; k++)
            if (!same_vec(x->pos[k], y->pos[k])) return false;
    }
    return true;
}

int main(void)
{
    combat_tests();
    thing_tests();
    corpse_tests();
    events_tests();
    rope_tests();
    network_tests();
    join_tests();
    wire_tests();
    stream_tests();
    rewind_tests();
    console_tests();
    taunt_tests();
    color_tests();
    bot_tests();
    round_tests();
    script_tests();
    query_tests();
    lobby_tests();
    launcher_tests();
    demo_tests();
    shot_end_tests();
    bink_tests();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
