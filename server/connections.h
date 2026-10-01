#pragma once

// The server's connections: who is on the line, the join, and the two streams. A peer
// that connects is nobody until its Hello; a Hello with the right version and a free
// slot makes it a player with a soldier, told by Welcome with its slot, the tick and
// the map; anything else is Denied and dropped. A peer that leaves frees its slot. Chat
// is relayed to everyone with the sender's slot. A player's client states are taken as
// its soldier's word (stream.h), and every tick each player gets a snapshot. A line of
// chat beginning with '/' is a command: a vote to change the map or kick a player,
// and the answers to it; one vote runs at a time, for a minute, and passes on 51% of
// the players. A map vote passed is the server's to act on (connections_take_vote_map);
// a kick is done here.
//
// A bot holds a slot too (connections_add_bot): a soldier the server plays itself, with a
// name on the roster and no peer. It is placed with the players each round, its name
// goes in the snapshots, and its chat is relayed as a player's (connections_say_as);
// what it does each tick is the bots' (bots.c), not the line's.
//
// Systems in the server's shape: this takes the link and the game and keeps only the
// table; the console, if given, hears who came and went.

#include "console/console.h"
#include "game/game.h"
#include "network/stream.h"
#include "network/transport.h"

// Why a player is being cut off, for the word of its leaving.
typedef enum KickWhy { KICK_NONE, KICK_VOTED, KICK_CONSOLE } KickWhy;

typedef struct Connection {
    KickWhy kick_why; // set before the kick; the leaving is announced by it
    ENetPeer *peer; // NULL: the slot is free, unless a bot's
    bool joined;    // Hello accepted: it has a soldier
    bool bot;       // the server's own player: a soldier and a name, no peer
    char name[NET_NAME_SIZE];
    bool chose_team; // said /team; in a game with teams a spectator until it does
    Team team;       // what it said
} Connection;

#define VOTE_TICKS (60 * 60) // a minute to decide
#define VOTE_PERCENT 51

typedef struct Vote {
    VoteKind kind;              // VOTE_NONE: none running
    char target[NET_MAP_SIZE];  // the map, or the player's name
    char reason[NET_REASON_SIZE]; // a kick's, as the starter typed it
    int slot;                   // the player, for a kick
    int starter;
    int32_t ticks_left;
    uint8_t answer[MAX_PLAYERS]; // 0 none, 1 yes, 2 no
} Vote;

typedef struct Connections {
    NetLink *link;
    Connection items[MAX_PLAYERS]; // by slot, the soldier's index
    ServerStream *streams;         // by slot, on the heap
    WireQueue events;              // the server's decisions and what it relays, for everyone
    Console *console;              // may be NULL
    uint16_t round;                // the round being played, from 1
    char map[NET_MAP_SIZE];        // on which map
    char hostname[NET_NAME_SIZE];  // the server's name, told with the map (sv_hostname)
    char maps_dir[512];            // where a voted map must be found, <assets>/maps; empty accepts any
    Vote vote;
    char vote_map[NET_MAP_SIZE];   // a map vote passed, until the server takes it
} Connections;

// `map` is the map being played, round 1. False if the streams couldn't be made.
bool connections_init(Connections *c, NetLink *link, Console *console, const char *map);
void connections_free(Connections *c);

// A new round on `map`, the world already made anew: everyone joined is placed on their
// team, their streams begin afresh, and everyone is told (MsgMap).
void connections_new_round(Connections *c, Game *g, const char *map);

// Everything the line has for the server right now: joins, leaves, states, chat.
void connections_poll(Connections *c, Game *g);

// The command each player's soldier steps on this tick: its last keys, or none once quiet.
void connections_commands(const Connections *c, const Game *g, Command cmds[MAX_PLAYERS]);

// After the tick: what it left that travels (the server's decisions, and the players'
// heard this tick, for the others) into the queue, then a snapshot to every player.
void connections_snapshots(Connections *c, const Game *g);

// A message to every joined player.
void connections_broadcast(Connections *c, MsgKind kind, const uint8_t *data, size_t size);

int connections_count(const Connections *c);

// A player's soldier on `team`: alive on its spawn, or a spectator, present on the roster
// and nowhere else. What it held is let go of.
void connections_place(Connections *c, Game *g, int slot, Team team);

// A map vote passed since last asked: true, with the map, once.
bool connections_take_vote_map(Connections *c, char *map, size_t size);

// The server's own chat to everyone, shown as "*SERVER*: text".
void connections_say(Connections *c, const char *text);

// A player put off the server: told why, and cut off. The slot frees as the line closes.
void connections_kick(Connections *c, int slot, const char *reason);

// A bot into a free slot, placed on `team` (the emptier side in a team game when it
// isn't alpha or bravo; none otherwise) and announced: its slot, or -1 when full. The
// soldier is the server's to play: not remote, marked a bot.
int connections_add_bot(Connections *c, Game *g, const char *name, PlayerLook look, WeaponId primary, WeaponId secondary,
                        Team team);
// The bot in `slot` leaves: its soldier gone, its slot free, its leaving announced.
void connections_remove_bot(Connections *c, Game *g, int slot);
// A line of chat from the player in `slot` (a bot's), to everyone.
void connections_say_as(Connections *c, int slot, const char *text);
