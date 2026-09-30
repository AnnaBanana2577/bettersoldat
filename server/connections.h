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
// Systems in the server's shape: this takes the link and the game and keeps only the
// table; the console, if given, hears who came and went.

#include "console/console.h"
#include "game/game.h"
#include "network/stream.h"
#include "network/transport.h"

typedef struct Connection {
    ENetPeer *peer; // NULL: the slot is free
    bool joined;    // Hello accepted: it has a soldier
    char name[NET_NAME_SIZE];
} Connection;

#define VOTE_TICKS (60 * 60) // a minute to decide
#define VOTE_PERCENT 51

typedef struct Vote {
    VoteKind kind;              // VOTE_NONE: none running
    char target[NET_MAP_SIZE];  // the map, or the player's name
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

// A map vote passed since last asked: true, with the map, once.
bool connections_take_vote_map(Connections *c, char *map, size_t size);

// A player put off the server: told why, and cut off. The slot frees as the line closes.
void connections_kick(Connections *c, int slot, const char *reason);
