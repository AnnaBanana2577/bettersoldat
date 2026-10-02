#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rounds.h"

#define MAX_STALL 0.25 // a stall never turns into a burst of ticks
#define HOST_MAX_MAPS 128

static void bot_say(void *user, int slot, const char *text) { connections_say_as(&((Host *)user)->connections, slot, text); }

// The match as the settings ask it, on this map.
static MatchSettings match_settings(const Host *h, const Map *map)
{
    MatchSettings s = match_settings_for_map(map);
    s.mode = match_mode_choose(map, h->settings.mode);
    s.hook = h->settings.hook; // sv_hook
    const HookTuning *t = &h->settings.hook_tuning; // sv_hook_*, each above 0 in place of the default
    if (t->length > 0) s.hook_tuning.length = t->length;
    if (t->fire_speed > 0) s.hook_tuning.fire_speed = t->fire_speed;
    if (t->drag_accel > 0) s.hook_tuning.drag_accel = t->drag_accel;
    if (t->drag_speed > 0) s.hook_tuning.drag_speed = t->drag_speed;
    if (h->settings.time_limit > 0) s.time_limit = h->settings.time_limit * 60 * TICK_RATE;
    if (h->settings.score_limit > 0) s.score_limit = h->settings.score_limit;
    return s;
}

const char *host_map(const Host *h) { return h->connections.map; }

// What a query is told (query.h): the game being played and who is in it.
static void answer_query(void *user, ServerInfo *info)
{
    const Host *h = user;
    const Connections *c = &h->connections;
    *info = (ServerInfo){.protocol = NET_VERSION, .max_players = MAX_PLAYERS, .mode = (uint8_t)h->game->match.settings.mode,
                         .password = c->password[0] != '\0'};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (c->items[i].bot) info->bots++;
        else if (c->items[i].joined) info->players++;
    }
    snprintf(info->hostname, sizeof info->hostname, "%s", c->hostname);
    snprintf(info->map, sizeof info->map, "%s", c->map);
}

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
    h->password = console ? cvar_find(console, "sv_password") : NULL;
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
    if (!h->game->world.history || !net_listen(&h->link, settings->ip, settings->port, MAX_PLAYERS)) {
        fprintf(stderr, "could not listen on %s%sport %d\n", settings->ip, settings->ip[0] ? " " : "", settings->port);
        free(h->game->world.history);
        context_destroy(&h->game->ctx);
        free(h->game);
        h->game = NULL;
        return false;
    }
    if (!connections_init(&h->connections, &h->link, settings->quiet ? NULL : console, settings->map)) {
        fprintf(stderr, "out of memory\n");
        host_close(h);
        return false;
    }
    h->connections.hooks = &h->line_hooks;
    snprintf(h->connections.maps_dir, sizeof h->connections.maps_dir, "%s/maps", settings->assets);
    snprintf(h->connections.hostname, sizeof h->connections.hostname, "%s", settings->hostname);
    if (settings->vote_percent > 0) h->connections.vote_percent = settings->vote_percent;
    if (settings->flood_packets > 0) h->connections.flood_packets = settings->flood_packets;
    if (settings->flood_warnings > 0) h->connections.flood_warnings_max = settings->flood_warnings;
    net_answer_queries(&h->link, answer_query, h);

    // the server's list of maps (the original's MapsList): the rotation as given, or
    // every map under assets when there is none; the map window pages it, a vote picks from it
    h->maps = calloc(HOST_MAX_MAPS, sizeof *h->maps);
    if (h->maps) {
        const char *p = settings->maps;
        while (*p && h->map_count < HOST_MAX_MAPS) {
            while (*p == ' ' || *p == ',' || *p == '\t') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != ',' && *p != '\t') p++;
            snprintf(h->maps[h->map_count++], sizeof h->maps[0], "%.*s", (int)(p - start), start);
        }
        if (h->map_count == 0) h->map_count = list_files(h->connections.maps_dir, ".pms", h->maps, HOST_MAX_MAPS);
        h->connections.maps = (const char (*)[64])h->maps;
        h->connections.map_count = h->map_count;
    }
    h->connections.hook = h->game->world.rules.hook; // what the map tells every client
    h->connections.hook_tuning = h->game->world.rules.hook_tuning;

    bots_init(&h->bots, (BotSettings){.difficulty = settings->bots_difficulty, .chat = settings->bots_chat}, bot_say, h);
    h->profiles = calloc(BOT_PROFILES, sizeof *h->profiles);
    if (h->profiles) h->profile_count = bot_profiles_load(settings->assets, &h->game->ctx.weapons, h->profiles, BOT_PROFILES);
    add_bots(h);

    if (console) {
        console_print(console, "hosting %s on %s%sport %d, %s, %d ticks a second\n", settings->map, settings->ip,
                      settings->ip[0] ? " " : "", settings->port,
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
    free(h->maps);
    free(h->game->world.history);
    context_destroy(&h->game->ctx);
    free(h->game);
    *h = (Host){0};
}

void host_end_round(Host *h) { h->next_round = true; }

void host_change_map(Host *h, const char *map)
{
    snprintf(h->chosen_map, sizeof h->chosen_map, "%s", map ? map : "");
    h->next_round = true;
}

bool host_pause(Host *h, bool paused) { return match_pause(&h->game->match, paused); }

bool host_paused(const Host *h) { return h->game->match.state == MATCH_PAUSED; }

void host_set_hooks(Host *h, const HostHooks *hooks, const LineHooks *line_hooks)
{
    h->hooks = hooks ? *hooks : (HostHooks){0};
    h->line_hooks = line_hooks ? *line_hooks : (LineHooks){0};
}

void host_say(Host *h, const char *text) { connections_say(&h->connections, text); }

// The next round, on the map the countdown led to.
static bool next_round(Host *h)
{
    if (!round_start(h->game, &h->connections, h->settings.assets, h->pending_map, h->settings.mode)) {
        fprintf(stderr, "could not load map '%s' from '%s'\n", h->pending_map, h->settings.assets);
        return false;
    }
    bots_new_round(&h->bots);
    h->next_round = false;
    h->ending_told = false;
    h->chosen_map[0] = h->pending_map[0] = '\0';
    return true;
}

// The round's end, after the tick: asked for (nextmap, a vote, a script) the match is
// stopped now, as the original's PrepareMapChange does; as the match ends, by whatever,
// the map coming is settled (the one asked for, else the rotation's next) and told to
// everyone, and the scores stand while the counter runs; run out, the next round begins.
static bool round_change(Host *h)
{
    Game *g = h->game;
    char chosen[NET_MAP_SIZE];
    if (connections_take_vote_map(&h->connections, chosen, sizeof chosen)) {
        snprintf(h->chosen_map, sizeof h->chosen_map, "%s", chosen);
        snprintf(h->end_why, sizeof h->end_why, "vote");
        h->next_round = true;
    } else if (h->next_round && !h->end_why[0]) {
        snprintf(h->end_why, sizeof h->end_why, "nextmap");
    }
    if (h->next_round && g->match.state != MATCH_ENDED) {
        match_stop(&g->match, &g->incoming); // the end goes out with the next tick's events
        h->next_round = false;
    }
    if (g->match.state == MATCH_ENDED && !h->ending_told) {
        if (!h->end_why[0]) snprintf(h->end_why, sizeof h->end_why, "limit");
        if (h->chosen_map[0]) snprintf(h->pending_map, sizeof h->pending_map, "%s", h->chosen_map);
        else rounds_next_map(h->settings.maps, h->connections.map, h->pending_map, sizeof h->pending_map);
        connections_map_change(&h->connections, g, h->pending_map);
        if (h->console && !h->settings.quiet) console_print(h->console, "Next map: %s\n", h->pending_map);
        h->ending_told = true;
        h->next_round = false;
    }
    if (match_over(&g->match)) {
        if (h->hooks.round_ending) h->hooks.round_ending(h->hooks.user, h->end_why[0] ? h->end_why : "limit");
        h->end_why[0] = '\0';
        if (!next_round(h)) return false;
        if (h->hooks.round_started) h->hooks.round_started(h->hooks.user);
    }
    return true;
}

bool host_pump(Host *h, double dt)
{
    if (h->password) connections_set_password(&h->connections, h->password->value);
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
        if (h->hooks.ticked) h->hooks.ticked(h->hooks.user);
        bots_hear(&h->bots, h->game);
        connections_snapshots(&h->connections, h->game);
        h->accumulator -= TICK_SECONDS;
        if (!round_change(h)) return false;
    }
    net_flush(&h->link);
    return true;
}
