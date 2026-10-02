#pragma once

// A hosted game: the world with authority, the line everyone joins by, the players on
// it, the bots, and the rounds, ticked at TICK_RATE from whatever loop owns it. The
// dedicated server is one of these with a console around it (server/main.c); the
// client's Local Play is one of these inside the client, which then joins it over the
// loopback as any other client would, so a game against bots and a game hosted for
// friends are the same code and the same wire.
//
// The owner calls host_pump as often as it likes with the seconds since the last call:
// the ticks owed come out one whole tick at a time, the line heard before them and
// flushed after, and a round that ends (its limit, nextmap, a vote) begins the next on
// the rotation. net_init must have been called once already.
//
// A query on the host's port (network/query.h) is answered with the game being
// played: its name, map, mode, who is in it and whether it asks a password. A server
// browser asks it of every server it lists, and the lobby asks it to see that a
// server registering can be reached.

#include "bots.h"
#include "connections.h"
#include "console/console.h"
#include "game/game.h"
#include "network/transport.h"

typedef struct HostSettings {
    uint16_t port;
    char ip[128];                    // the address to listen on; empty for every one (sv_ip)
    char assets[512];
    char map[NET_MAP_SIZE];          // the first round's
    char maps[CONSOLE_VALUE_SIZE];   // the rotation, space-separated; empty plays `map` again
    char hostname[NET_NAME_SIZE];
    MatchMode mode;                  // MATCH_MODE_COUNT for the map's own
    int time_limit;                  // minutes; 0 for the default
    int score_limit;                 // kills or captures; 0 for the default
    int bots_noteam;                 // bots in a deathmatch (bots_random_noteam)
    int bots_alpha, bots_bravo;      // bots on each team in CTF (bots_random_alpha, bots_random_bravo)
    int bots_difficulty;             // 100 as the bot files say; less is harder
    bool bots_chat;
    int vote_percent;                // sv_votepercent; 0 for the default
    int flood_packets;               // net_floodingpackets; 0 for the default
    int flood_warnings;              // sv_warnings_flood; 0 for the default
    bool quiet;                      // no lines of its own to the console but the first: a client beside it says what matters
    bool hook;                       // sv_hook: the grappling hook is allowed; off, the boots are jets
    HookTuning hook_tuning;          // sv_hook_*: each above 0 replaces Teeworlds' value as brought here
} HostSettings;

// What a server script hangs on the host (host_set_hooks): it hears every tick once it
// has run, with the game's events to read; the round's ending, before the next map
// loads, with why ("limit", "nextmap" or "vote"); and the next round's start. Any may be
// NULL. The line's own hooks (LineHooks) are set the same way.
typedef struct HostHooks {
    void *user;
    void (*ticked)(void *user);
    void (*round_ending)(void *user, const char *why);
    void (*round_started)(void *user);
} HostHooks;

typedef struct Host {
    HostSettings settings;
    Console *console; // hears who came and went; may be NULL
    const Cvar *password; // sv_password, if the console has it: read every pump, as a script may set it
    Game *game;       // large; on the heap
    NetLink link;
    Connections connections;
    Bots bots;
    BotProfile *profiles; // what assets/bots holds, for the random bots
    int profile_count;
    uint64_t rng;
    double accumulator;
    bool next_round;             // asked for (nextmap): the round ends at the end of the tick
    char chosen_map[NET_MAP_SIZE]; // the map asked for with it (host_change_map), else the rotation's
    // The round's end, as the original's MapChangeCounter has it: the match ended (a
    // limit, nextmap, a vote), everyone told the map coming, the world frozen with the
    // scoreboard up while the match's counter runs, then that map.
    char pending_map[NET_MAP_SIZE]; // the map the countdown leads to
    bool ending_told;               // the countdown has begun and been announced
    char end_why[8];                // "limit", "nextmap" or "vote", for the hooks
    char (*maps)[64];               // the server's list of maps: the rotation, or every map under assets
    int map_count;
    HostHooks hooks;
    LineHooks line_hooks;
} Host;

// Everything up on `settings`: the map loaded, the port listening, the bots in. False,
// with the reason on stderr, and nothing to close.
bool host_open(Host *h, Console *console, const HostSettings *settings);
void host_close(Host *h);

// `dt` seconds have passed: the line, the ticks owed, the round change if one is due,
// the flush. False if the next round's map couldn't be loaded, which ends the game.
bool host_pump(Host *h, double dt);

// The round ends at the end of this tick.
void host_end_round(Host *h);
// The round ends at the end of this tick, and `map` is played next.
void host_change_map(Host *h, const char *map);
// Paused, nothing moves and the clock stands; true if the state changed.
bool host_pause(Host *h, bool paused);
bool host_paused(const Host *h);
// A script's ears on the host and the line; either may be NULL to take them off.
void host_set_hooks(Host *h, const HostHooks *hooks, const LineHooks *line_hooks);
// A bot on `team` (TEAM_NONE for the emptier side), named, or one at random: its slot,
// or -1 when the server is full, no profile is known by that name, or there is none.
int host_add_bot(Host *h, Team team, const char *name);
// The server's chat to everyone.
void host_say(Host *h, const char *text);
// The map being played.
const char *host_map(const Host *h);
