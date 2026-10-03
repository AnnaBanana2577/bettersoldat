#include "host_cvars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h" // the launcher's
#include "network/query.h"

const char *const HOST_CVAR_PREFIXES[] = {"sv_", "bots_", "net_", NULL};

void host_cvars_register(Console *con, HostCvars *c)
{
    const uint32_t SAVED = CVAR_ARCHIVE;
    // not saved: the map on now as well, which both set as the rounds go
    c->map = cvar_register(con, "map", "ctf_Ash", 0, "the map played first, if set; else the rotation's first");
    // not saved: config/maplist.txt is the rotation, this a run's own over it
    c->maps = cvar_register(con, "sv_maps", "", 0,
                            "the maps in rotation for this run, space-separated, over config/maplist.txt");
    c->port = cvar_register(con, "sv_port", "23073", SAVED, "the UDP port to listen on");
    c->ip = cvar_register(con, "sv_ip", "", SAVED, "the address to listen on; empty for every one");
    c->hostname = cvar_register(con, "sv_hostname", "Soldat Reloaded server", SAVED, "the game's name, on the scoreboard");
    c->password = cvar_register(con, "sv_password", "", SAVED, "the password to join; empty for none. Read live, so a script may set it");
    cvar_register(con, "sv_adminpassword", "", SAVED,
                  "the password a player says with /login to be an admin until they leave; empty for none");
    c->gamemode = cvar_register(con, "sv_gamemode", "0", SAVED, "0 the map's own, 1 deathmatch, 2 capture the flag");
    c->timelimit = cvar_register(con, "sv_timelimit", "15", SAVED, "minutes a round lasts");
    c->killlimit = cvar_register(con, "sv_killlimit", "10", SAVED, "the score that wins a round: kills, or captures in CTF");
    c->bots_noteam = cvar_register(con, "bots_random_noteam", "0", SAVED, "bots in a deathmatch");
    c->bots_alpha = cvar_register(con, "bots_random_alpha", "0", SAVED, "bots on alpha in capture the flag");
    c->bots_bravo = cvar_register(con, "bots_random_bravo", "0", SAVED, "bots on bravo in capture the flag");
    c->bots_difficulty = cvar_register(con, "bots_difficulty", "100", SAVED, "300 stupid, 200 poor, 100 normal, 50 hard, 10 impossible");
    c->bots_chat = cvar_register(con, "bots_chat", "1", SAVED, "whether the bots talk");
    c->script = cvar_register(con, "sv_script", "scripts/main.lua", SAVED,
                              "the Lua script a dedicated server runs, if the file is there (docs/scripting.md)");
    c->votepercent = cvar_register(con, "sv_votepercent", "60", SAVED, "the percentage of players whose yes passes a vote");
    c->floodingpackets = cvar_register(con, "net_floodingpackets", "120", SAVED,
                                       "messages in a second from one player that count as flooding (a client sends sixty)");
    c->warnings_flood = cvar_register(con, "sv_warnings_flood", "4", SAVED,
                                      "flood warnings before the player is kicked and barred for a quarter of an hour");
    c->rope = cvar_register(con, "sv_rope", "0", SAVED,
                            "1: the rope is allowed, an experimental gear in place of the jets; 0 gives everyone jets");
    c->public = cvar_register(con, "sv_public", "0", SAVED,
                              "1: a dedicated server is listed with the lobby, for the game's server browser. Read live");
    c->lobby_url = cvar_register(con, "sv_lobby", QUERY_LOBBY_URL, SAVED, "the lobby a dedicated server lists itself with");
    c->lobby_ip = cvar_register(con, "sv_lobby_ip", "", SAVED,
                                "the IPv4 address the lobby lists; empty for the one the server reaches it from");
}

const ConsoleFile HOST_CONFIG_FILE = {
    CONFIG_SERVER,
    "// How this install hosts a game: a dedicated server, and the game's Local Play alike.\n"
    "// Every setting with what it is, commented out while it holds its default: take a\n"
    "// line's // off to set it otherwise. Yours to edit: the game changes only the line of a\n"
    "// setting you change on its Local Play page. The command line (+sv_hostname \"...\")\n"
    "// goes over it, and is kept by the game, not by a server. The rotation is\n"
    "// config/maplist.txt, a map to a line; a weapons mod is config/weapons.ini.\n\n",
    HOST_CVAR_PREFIXES,
    false,
};

static const char MAPLIST_HEADER[] =
    "// The maps in rotation, one to a line, played in turn; with none, the map is played\n"
    "// again. The game's Local Play page writes it as you tick maps, and a server beside it\n"
    "// reads it as it starts (sv_maps on the command line goes over it, for that run). A map\n"
    "// that isn't in data/maps/ is passed over.\n"
    "\n";

void maplist_read(char *out, size_t size)
{
    out[0] = '\0';
    char *text = files_read(CONFIG_MAPLIST, NULL);
    if (!text) return;
    size_t n = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *comment = strstr(line, "//");
        if (comment) *comment = '\0';
        for (char *p = line; *p;) {
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
            if (p > start && n + (size_t)(p - start) + 2 < size)
                n += (size_t)snprintf(out + n, size - n, "%s%.*s", n ? " " : "", (int)(p - start), start);
        }
    }
    free(text);
}

bool maplist_write(const char *maps)
{
    size_t cap = sizeof MAPLIST_HEADER + strlen(maps) + 2, n = 0;
    char *text = malloc(cap);
    if (!text) return false;
    n += (size_t)snprintf(text, cap, "%s", MAPLIST_HEADER);
    for (const char *p = maps; *p;) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
        if (p > start) n += (size_t)snprintf(text + n, cap - n, "%.*s\n", (int)(p - start), start);
    }
    bool ok = files_make_parents(CONFIG_MAPLIST) && files_write(CONFIG_MAPLIST, text, n);
    free(text);
    return ok;
}

void maplist_make(void)
{
    if (!files_exists(CONFIG_MAPLIST) && files_make_parents(CONFIG_MAPLIST))
        files_write(CONFIG_MAPLIST, MAPLIST_HEADER, sizeof MAPLIST_HEADER - 1);
}

void maplist_take_cvar(HostCvars *c, Console *con)
{
    if (!c->maps->value[0]) return;
    if (maplist_write(c->maps->value)) console_print(con, "sv_maps moved into %s\n", CONFIG_MAPLIST);
    cvar_set(con, "sv_maps", "");
}
