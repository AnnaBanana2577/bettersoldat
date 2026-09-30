#pragma once

// The client's end of the line: connecting, the join, and the two streams (stream.h).
// Everything it hears about the line it tells the console; what it hears about the
// world it applies to the game it is given.

#include "console/console.h"
#include "network/stream.h"
#include "network/transport.h"

typedef enum ClientNetState { CLIENT_NET_OFF, CLIENT_NET_CONNECTING, CLIENT_NET_JOINING, CLIENT_NET_JOINED } ClientNetState;

typedef struct ClientNet {
    NetLink link;
    ClientNetState state;
    ClientStream stream;
    char name[NET_NAME_SIZE];
    int slot;               // mine on the server, once welcomed; -1 before
    uint32_t tick;          // the server's, as of the welcome
    char map[NET_MAP_SIZE]; // the server's map, as of the welcome
    bool welcomed;          // a Welcome not yet taken (client_net_take_welcome)
} ClientNet;

// Once per program; false if ENet or the ring wouldn't start.
bool client_net_init(ClientNet *n);
void client_net_shutdown(ClientNet *n);

// Connects and, once the line is up, says Hello with `name`. Any line already open is
// closed first.
void client_net_connect(ClientNet *n, Console *con, const char *address, uint16_t port, const char *name);
void client_net_disconnect(ClientNet *n, Console *con);

// Everything the line has for the client right now. Snapshots go into `g`, as the
// soldier in `n->slot`.
void client_net_poll(ClientNet *n, Console *con, Game *g);
// A Welcome came: once, true with the slot and the map, for the game to be made theirs.
bool client_net_take_welcome(ClientNet *n);
// After the client's tick: its state to the server, and everything queued out.
void client_net_tick(ClientNet *n, const Soldier *me);
void client_net_flush(ClientNet *n);

bool client_net_joined(const ClientNet *n);

// A line of chat to the server, which relays it. False if not joined.
bool client_net_say(ClientNet *n, const char *text, bool team);
