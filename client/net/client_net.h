#pragma once

// The client's end of the line: connecting, the join, and what comes down it. Today
// that is Welcome, Denied and Chat; the state streams come with the netcode's next
// steps. Everything it hears it tells the console.

#include "console/console.h"
#include "network/transport.h"

typedef enum ClientNetState { CLIENT_NET_OFF, CLIENT_NET_CONNECTING, CLIENT_NET_JOINING, CLIENT_NET_JOINED } ClientNetState;

typedef struct ClientNet {
    NetLink link;
    ClientNetState state;
    char name[NET_NAME_SIZE];
    int slot;      // mine on the server, once welcomed; -1 before
    uint32_t tick; // the server's, as of the welcome
} ClientNet;

// Once per program; false if ENet wouldn't start.
bool client_net_init(ClientNet *n);
void client_net_shutdown(ClientNet *n);

// Connects and, once the line is up, says Hello with `name`. Any line already open is
// closed first.
void client_net_connect(ClientNet *n, Console *con, const char *address, uint16_t port, const char *name);
void client_net_disconnect(ClientNet *n, Console *con);

// Everything the line has for the client right now.
void client_net_poll(ClientNet *n, Console *con);
// Sends what is queued.
void client_net_flush(ClientNet *n);

// A line of chat to the server, which relays it. False if not joined.
bool client_net_say(ClientNet *n, const char *text, bool team);
