#include "game/game.h"

#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"

// ---------------------------------------------------------------------------------
// Context

bool context_load(Context *ctx, const char *base_dir, const char *map_name)
{
    memset(ctx, 0, sizeof(*ctx));
    weapons_default(&ctx->weapons);

    ctx->map = calloc(1, sizeof(Map));
    if (!ctx->map || !map_load_file(ctx->map, base_dir, map_name)) goto fail;

    ctx->anims = anims_load(base_dir);
    if (!ctx->anims) goto fail;

    ctx->skeletons = skeletons_load(base_dir);
    if (!ctx->skeletons) goto fail;

    return true;

fail:
    context_destroy(ctx);
    return false;
}

void context_destroy(Context *ctx)
{
    if (ctx->map) map_destroy(ctx->map);
    free(ctx->map);
    free(ctx->anims);
    skeletons_destroy(ctx->skeletons);
    memset(ctx, 0, sizeof(*ctx));
}

// ---------------------------------------------------------------------------------
// World

void world_init(World *w, uint64_t seed)
{
    memset(w, 0, sizeof(*w));
    w->gravity = DEFAULT_GRAVITY;
    w->rng = seed;
    w->rules = match_rules(&(Match){.settings = match_default_settings()});
}

void world_step(const Context *ctx, World *w, const Command cmds[MAX_PLAYERS], const Events *last, Events *events)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &w->soldiers[i];
        if (s->active) soldier_step(ctx, w, (uint8_t)i, cmds[i], events, !s->remote);
    }
    ragdolls_update(ctx, w, events);
    bullets_update(ctx, w, last, events);
    wounds_apply(ctx, w, last, events);
    things_update(ctx, w, last, events);
    soldiers_receive(ctx, w, last, events);
    w->tick++;
}

// ---------------------------------------------------------------------------------
// Match. Port of soldat-odin's round.odin.

MatchSettings match_default_settings(void)
{
    return (MatchSettings){
        .time_limit = DEFAULT_TIME_LIMIT,
        .score_limit = DEFAULT_SCORE_LIMIT,
        .respawn_time = DEFAULT_RESPAWN_TIME,
        .max_grenades = DEFAULT_MAX_GRENADES,
        .medikit_cooldown = DEFAULT_MEDIKIT_COOLDOWN,
    };
}

void match_init(Match *m, MatchSettings settings)
{
    *m = (Match){.settings = settings, .state = MATCH_PLAYING, .time_left = settings.time_limit};
}

WorldRules match_rules(const Match *m)
{
    return (WorldRules){
        .frozen = m->state == MATCH_ENDED,
        .friendly_fire = m->settings.friendly_fire,
        .kits_collide = m->settings.kits_collide,
        .guns_collide = m->settings.guns_collide,
        .medikit_cooldown = m->settings.medikit_cooldown * TICK_RATE,
        .stationary_guns = m->settings.stationary_guns,
        .respawn_time = m->settings.respawn_time,
        .max_grenades = m->settings.max_grenades,
    };
}

static void match_end(Match *m, Events *events)
{
    m->state = MATCH_ENDED;
    m->counter = ROUND_END_TICKS;

    Team winner = TEAM_NONE;
    if (m->scores[TEAM_ALPHA] > m->scores[TEAM_BRAVO]) winner = TEAM_ALPHA;
    else if (m->scores[TEAM_BRAVO] > m->scores[TEAM_ALPHA]) winner = TEAM_BRAVO;
    event_emit(events, (Event){.type = EVENT_MATCH_END, .match_end = {.winner = winner}});
}

void match_run(const Context *ctx, World *w, Match *m, Events *events)
{
    for (int i = 0; i < MAX_PLAYERS; i++) soldier_served_tick(ctx, w, (uint8_t)i, events);

    // a capture scores for the carrier's team
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        if (e->type == EVENT_FLAG_SCORE) m->scores[w->soldiers[e->flag_score.player].team]++;
    }
    bonuses_spawn(ctx, w, &m->settings, w->tick - 1); // on the tick just run

    if (m->state == MATCH_ENDED && m->counter > 0) m->counter--;
    if (m->state != MATCH_PLAYING) return;

    m->time_left--;
    int32_t limit = m->settings.score_limit;
    if (m->time_left <= 0 || m->scores[TEAM_ALPHA] >= limit || m->scores[TEAM_BRAVO] >= limit) match_end(m, events);
}

bool match_over(const Match *m)
{
    return m->state == MATCH_ENDED && m->counter <= 0;
}

// ---------------------------------------------------------------------------------
// Game

void game_init(Game *g, uint64_t seed, MatchSettings settings)
{
    world_init(&g->world, seed);
    match_init(&g->match, settings);
    g->world.rules = match_rules(&g->match);
    things_spawn(&g->ctx, &g->world);
    events_clear(&g->events);
}

void game_tick(Game *g, const Command cmds[MAX_PLAYERS])
{
    g->last = g->events;
    events_clear(&g->events);
    g->world.rules = match_rules(&g->match);
    world_step(&g->ctx, &g->world, cmds, &g->last, &g->events);
    match_run(&g->ctx, &g->world, &g->match, &g->events);
    if (g->world.history) history_record(g->world.history, &g->world); // the server's, for the snapshots' deltas
}
