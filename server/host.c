#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rounds.h"

#define MAX_STALL 0.25 // a stall never turns into a burst of ticks

static void bot_say(void *user, int slot, const char *text) { connections_say_as(&((Host *)user)->connections, slot, text); }

// The match as the settings ask it, on this map.
static MatchSettings match_settings(const Host *h, const Map *map)
{
    MatchSettings s = match_settings_for_map(map);
    s.mode = match_mode_choose(map, h->settings.mode);
    if (h->settings.time_limit > 0) s.time_limit = h->settings.time_limit * 60 * TICK_RATE;
    if (h->settings.score_limit > 0) s.score_limit = h->settings.score_limit;
    return s;
}

const char *host_map(const Host *h) { return h->connections.map; }

int host_add_bot(Host *h, Team team, const char *name)
{
    const BotProfile *profile = NULL;
    if (name && name[0]) {
        for (int i = 0; i < h->profile_count; i++)
            if (strcmp(h->profiles[i].name, name) == 0) profile = &h->profiles[i];
        if (!profile && h->console) console_print(h->console, "no bot named %s in %s/bots\n", name, h->settings.assets);
    } else {
        profile = bot_profile_random(h->profiles, h->profile_count, &h->rng);
        if (!profile && h->console) console_print(h->console, "no bots in %s/bots\n", h->settings.assets);
    }
    if (!profile) return -1;
    int slot = connections_add_bot(&h->connections, h->game, profile->name, profile->look, profile->favourite, profile->secondary, team);
    if (slot < 0) {
        if (h->console) console_print(h->console, "no room for a bot\n");
        return -1;
    }
    bots_attach(&h->bots, slot, profile, rand_next(&h->rng));
    return slot;
}

// The bots the settings ask for: by team in a game with teams, together without.
static void add_bots(Host *h)
{
    if (match_has_teams(&h->game->match)) {
        for (int i = 0; i < h->settings.bots_alpha; i++) host_add_bot(h, TEAM_ALPHA, NULL);
        for (int i = 0; i < h->settings.bots_bravo; i++) host_add_bot(h, TEAM_BRAVO, NULL);
    } else {
        for (int i = 0; i < h->settings.bots_noteam; i++) host_add_bot(h, TEAM_NONE, NULL);
    }
}

bool host_open(Host *h, Console *console, const HostSettings *settings)
{
    *h = (Host){.settings = *settings, .console = console, .rng = (uint64_t)time(NULL) ^ 0x5DEECE66Dull};
    h->game = calloc(1, sizeof(Game));
    if (!h->game) return false;
    if (!context_load(&h->game->ctx, settings->assets, settings->map)) {
        fprintf(stderr, "could not load map '%s' from '%s'\n", settings->map, settings->assets);
        free(h->game);
        h->game = NULL;
        return false;
    }
    game_init(h->game, (uint64_t)time(NULL), match_settings(h, h->game->ctx.map));
    h->game->world.authority = true;
    h->game->world.history = calloc(1, sizeof(History)); // the snapshots' deltas are against it
    if (!h->game->world.history || !net_listen(&h->link, settings->port, MAX_PLAYERS)) {
        fprintf(stderr, "could not listen on port %d\n", settings->port);
        free(h->game->world.history);
        context_destroy(&h->game->ctx);
        free(h->game);
        h->game = NULL;
        return false;
    }
    if (!connections_init(&h->connections, &h->link, console, settings->map)) {
        fprintf(stderr, "out of memory\n");
        host_close(h);
        return false;
    }
    snprintf(h->connections.maps_dir, sizeof h->connections.maps_dir, "%s/maps", settings->assets);
    snprintf(h->connections.hostname, sizeof h->connections.hostname, "%s", settings->hostname);

    bots_init(&h->bots, (BotSettings){.difficulty = settings->bots_difficulty, .chat = settings->bots_chat}, bot_say, h);
    h->profiles = calloc(BOT_PROFILES, sizeof *h->profiles);
    if (h->profiles) h->profile_count = bot_profiles_load(settings->assets, &h->game->ctx.weapons, h->profiles, BOT_PROFILES);
    add_bots(h);

    if (console) {
        console_print(console, "hosting %s on port %d, %s, %d ticks a second\n", settings->map, settings->port,
                      h->game->match.settings.mode == MATCH_CTF ? "capture the flag" : "deathmatch", TICK_RATE);
    }
    return true;
}

void host_close(Host *h)
{
    if (!h->game) return;
    if (h->link.host) net_close(&h->link);
    connections_free(&h->connections);
    free(h->profiles);
    free(h->game->world.history);
    context_destroy(&h->game->ctx);
    free(h->game);
    *h = (Host){0};
}

void host_end_round(Host *h) { h->next_round = true; }

void host_say(Host *h, const char *text) { connections_say(&h->connections, text); }

// The next round: on `chosen` if a vote chose one, else on the map after this one in
// the rotation (or this one again).
static bool next_round(Host *h, const char *chosen)
{
    char map[NET_MAP_SIZE];
    if (chosen) snprintf(map, sizeof map, "%s", chosen);
    else rounds_next_map(h->settings.maps, h->connections.map, map, sizeof map);
    if (!round_start(h->game, &h->connections, h->settings.assets, map, h->settings.mode)) {
        fprintf(stderr, "could not load map '%s' from '%s'\n", map, h->settings.assets);
        return false;
    }
    bots_new_round(&h->bots);
    h->next_round = false;
    return true;
}

bool host_pump(Host *h, double dt)
{
    h->accumulator += dt;
    if (h->accumulator > MAX_STALL) h->accumulator = MAX_STALL;
    connections_poll(&h->connections, h->game);
    while (h->accumulator >= TICK_SECONDS) {
        Command cmds[MAX_PLAYERS] = {0};
        const char *names[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; i++) names[i] = h->connections.items[i].name;
        connections_commands(&h->connections, h->game, cmds);
        bots_commands(&h->bots, h->game, names, cmds);
        game_tick(h->game, cmds);
        bots_hear(&h->bots, h->game);
        connections_snapshots(&h->connections, h->game);
        h->accumulator -= TICK_SECONDS;
        char chosen[NET_MAP_SIZE];
        bool voted = connections_take_vote_map(&h->connections, chosen, sizeof chosen);
        if ((voted || h->next_round || match_over(&h->game->match)) && !next_round(h, voted ? chosen : NULL)) return false;
    }
    net_flush(&h->link);
    return true;
}
