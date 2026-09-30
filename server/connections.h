#pragma once

// The server's connections: who is on the line, the join, and the two streams. A peer
// that connects is nobody until its Hello; a Hello with the right version and a free
// slot makes it a player with a soldier, told by Welcome with its slot, the tick and
// the map; anything else is Denied and dropped. A peer that leaves frees its slot. Chat
// is relayed to everyone with the sender's slot. A player's client states are taken as
// its soldier's word (stream.h), and every tick each player gets a snapshot.
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

typedef struct Connections {
    NetLink *link;
    Connection items[MAX_PLAYERS]; // by slot, the soldier's index
    ServerStream *streams;         // by slot, on the heap
    WireQueue events;              // the server's decisions and what it relays, for everyone
    Console *console;              // may be NULL
    uint16_t round;                // the round being played, from 1
    char map[NET_MAP_SIZE];        // on which map
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
