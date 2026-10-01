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

#include "bots.h"
#include "connections.h"
#include "console/console.h"
#include "game/game.h"
#include "network/transport.h"

typedef struct HostSettings {
    uint16_t port;
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
} HostSettings;

typedef struct Host {
    HostSettings settings;
    Console *console; // hears who came and went; may be NULL
    Game *game;       // large; on the heap
    NetLink link;
    Connections connections;
    Bots bots;
    BotProfile *profiles; // what assets/bots holds, for the random bots
    int profile_count;
    uint64_t rng;
    double accumulator;
    bool next_round; // asked for (nextmap): at the end of the tick
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
// A bot on `team` (TEAM_NONE for the emptier side), named, or one at random: its slot,
// or -1 when the server is full, no profile is known by that name, or there is none.
int host_add_bot(Host *h, Team team, const char *name);
// The server's chat to everyone.
void host_say(Host *h, const char *text);
// The map being played.
const char *host_map(const Host *h);
