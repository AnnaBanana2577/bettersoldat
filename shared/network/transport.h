#pragma once

// The transport: ENet under the messages. Two channels, one unreliable for state and
// one reliable for news, and which a message takes is its kind's to say (MSG_RELIABLE).
// A NetLink is one end: a server listening for many, or a client with its one peer.
// Nothing here knows what the bytes mean; the messages (network.h) do.
//
// ENet sends what it was given when it is next serviced or flushed; both ends flush at
// the end of their tick, so nothing waits a tick in the queue.

#include "network/network.h" // the game's headers first: Windows' GDI has a Polygon of its own
#include "network/query.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#endif
#include <enet/enet.h>

#define NET_CHANNEL_UNRELIABLE 0
#define NET_CHANNEL_RELIABLE 1
#define NET_CHANNELS 2

typedef struct NetLink {
    ENetHost *host;
    ENetPeer *peer; // a client's server; NULL on a server
} NetLink;

typedef enum NetEventKind { NET_EVENT_NONE, NET_EVENT_CONNECT, NET_EVENT_DISCONNECT, NET_EVENT_MESSAGE } NetEventKind;

// What net_poll found: who, and for a message its bytes and the kind read off its front
// (MSG_INVALID for a message too big or with nothing readable in front).
typedef struct NetEvent {
    NetEventKind kind;
    ENetPeer *peer;
    MsgKind msg;
    uint8_t data[NET_MTU];
    size_t size;
} NetEvent;

// Once per program.
bool net_init(void);
void net_shutdown(void);

// Listens on `port`, on every address when `ip` is NULL or empty, else on that one
// alone (an address, or a name to resolve: a host behind a UDP proxy that rewrites
// addresses answers from the one it was reached on).
bool net_listen(NetLink *l, const char *ip, uint16_t port, int max_peers);
bool net_connect(NetLink *l, const char *address, uint16_t port);
// Tells the peer(s) goodbye and gives them a moment to hear it, then closes.
void net_close(NetLink *l);

// A message to a peer, on the channel its kind calls for. False if it couldn't be queued.
bool net_send(ENetPeer *peer, MsgKind kind, const uint8_t *data, size_t size);
// The next event, waiting up to `timeout_ms` for one. NET_EVENT_NONE when there is none.
NetEventKind net_poll(NetLink *l, NetEvent *e, uint32_t timeout_ms);
// Sends what is queued now.
void net_flush(NetLink *l);

// A query (query.h) arriving on the link's port is answered with what `answer` fills
// in, from whichever call into ENet receives it, and never reaches the peers. NULL
// stops the answering, as net_close does. False if too many links answer already.
typedef void (*NetQueryAnswer)(void *user, ServerInfo *info);
bool net_answer_queries(NetLink *l, NetQueryAnswer answer, void *user);
