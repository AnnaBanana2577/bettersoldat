#include "rounds.h"

#include <stdio.h>
#include <string.h>

#include "game/systems/systems.h"

static bool separator(char c) { return c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r'; }

const char *rounds_next_map(const char *list, const char *current, char *out, size_t size)
{
    char first[64] = "", next[64] = "";
    bool found = false, take_next = false;
    const char *p = list ? list : "";
    while (*p) {
        while (*p && separator(*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !separator(*p)) p++;
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(p - start), start);
        if (!first[0]) snprintf(first, sizeof first, "%s", name);
        if (take_next) {
            snprintf(next, sizeof next, "%s", name);
            take_next = false;
        }
        if (strcmp(name, current) == 0) {
            found = true;
            take_next = true;
        }
    }
    const char *chosen = found && next[0] ? next : first[0] ? first : current;
    snprintf(out, size, "%s", chosen);
    return out;
}

bool round_start(Game *g, Connections *c, const char *assets, const char *map)
{
    History *history = g->world.history;
    MatchSettings settings = g->match.settings;
    context_destroy(&g->ctx);
    if (!context_load(&g->ctx, assets, map)) {
        g->world.history = history;
        return false;
    }
    settings.mode = match_settings_for_map(g->ctx.map).mode; // the limits stay, the mode is the map's
    game_init(g, (uint64_t)g->world.tick + 1, settings);
    g->world.authority = true;
    g->world.history = history;
    if (history) memset(history, 0, sizeof *history);
    connections_new_round(c, g, map);
    return true;
}
