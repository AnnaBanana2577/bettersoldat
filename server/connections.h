#pragma once

// The server's connections: who is on the line, and the join. A peer that connects is
// nobody until its Hello; a Hello with the right version and a free slot makes it a
// player with a soldier, told by Welcome; anything else is Denied and dropped. A peer
// that leaves frees its slot. Chat is relayed to everyone with the sender's slot.
//
// Systems in the server's shape: this takes the link and the game and keeps only the
// table; the console, if given, hears who came and went.

#include "console/console.h"
#include "game/game.h"
#include "network/transport.h"

typedef struct Connection {
    ENetPeer *peer; // NULL: the slot is free
    bool joined;    // Hello accepted: it has a soldier
    char name[NET_NAME_SIZE];
} Connection;

typedef struct Connections {
    NetLink *link;
    Connection items[MAX_PLAYERS]; // by slot, the soldier's index
    Console *console;              // may be NULL
} Connections;

void connections_init(Connections *c, NetLink *link, Console *console);

// Everything the line has for the server right now: joins, leaves, messages.
void connections_poll(Connections *c, Game *g);

// A message to every joined player.
void connections_broadcast(Connections *c, MsgKind kind, const uint8_t *data, size_t size);

int connections_count(const Connections *c);
