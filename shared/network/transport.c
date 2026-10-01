// ENet under the messages.

#include <string.h>

#include "network/transport.h"

// The links answering queries. ENet's intercept is told only the host, so the host is
// looked up here; a client hosting Local Play has two links, and nothing has more.
#define NET_ANSWERING 4

typedef struct Answering {
    ENetHost *host;
    NetQueryAnswer answer;
    void *user;
} Answering;

static Answering answering[NET_ANSWERING];

// Every datagram the host receives, before ENet reads it: a query is answered and kept
// from ENet, and so is anything else with a query's front; the rest goes on.
static int ENET_CALLBACK intercept(ENetHost *host, ENetEvent *event)
{
    (void)event;
    if (!query_is_query(host->receivedData, host->receivedDataLength)) return 0;
    uint32_t nonce;
    if (!query_read_request(host->receivedData, host->receivedDataLength, &nonce)) return 1;
    for (int i = 0; i < NET_ANSWERING; i++) {
        if (answering[i].host != host) continue;
        ServerInfo info = {0};
        answering[i].answer(answering[i].user, &info);
        uint8_t reply[QUERY_REPLY_MAX];
        ENetBuffer buffer = {.data = reply, .dataLength = query_write_reply(reply, sizeof reply, nonce, &info)};
        if (buffer.dataLength) enet_socket_send(host->socket, &host->receivedAddress, &buffer, 1);
        break;
    }
    return 1;
}

bool net_answer_queries(NetLink *l, NetQueryAnswer answer, void *user)
{
    if (!l->host) return false;
    for (int i = 0; i < NET_ANSWERING; i++)
        if (answering[i].host == l->host) answering[i] = (Answering){0};
    l->host->intercept = NULL;
    if (!answer) return true;
    for (int i = 0; i < NET_ANSWERING; i++) {
        if (answering[i].host) continue;
        answering[i] = (Answering){.host = l->host, .answer = answer, .user = user};
        l->host->intercept = intercept;
        return true;
    }
    return false;
}

bool net_init(void) { return enet_initialize() == 0; }

void net_shutdown(void) { enet_deinitialize(); }

bool net_listen(NetLink *l, const char *ip, uint16_t port, int max_peers)
{
    ENetAddress address = {.host = ENET_HOST_ANY, .port = port};
    if (ip && ip[0] && enet_address_set_host(&address, ip) != 0) {
        *l = (NetLink){0};
        return false;
    }
    *l = (NetLink){.host = enet_host_create(&address, (size_t)max_peers, NET_CHANNELS, 0, 0)};
    return l->host != NULL;
}

bool net_connect(NetLink *l, const char *address, uint16_t port)
{
    *l = (NetLink){.host = enet_host_create(NULL, 1, NET_CHANNELS, 0, 0)};
    if (!l->host) return false;
    ENetAddress to = {.port = port};
    if (enet_address_set_host(&to, address) != 0) {
        net_close(l);
        return false;
    }
    l->peer = enet_host_connect(l->host, &to, NET_CHANNELS, 0);
    if (!l->peer) {
        net_close(l);
        return false;
    }
    return true;
}

void net_close(NetLink *l)
{
    if (!l->host) return;
    for (size_t i = 0; i < l->host->peerCount; i++) {
        ENetPeer *p = &l->host->peers[i];
        if (p->state == ENET_PEER_STATE_CONNECTED) enet_peer_disconnect(p, 0);
    }
    // a few rounds for the goodbyes to go out and be answered
    ENetEvent e;
    for (int i = 0; i < 10 && enet_host_service(l->host, &e, 10) >= 0; i++) {
        if (e.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(e.packet);
    }
    net_answer_queries(l, NULL, NULL);
    enet_host_destroy(l->host);
    *l = (NetLink){0};
}

bool net_send(ENetPeer *peer, MsgKind kind, const uint8_t *data, size_t size)
{
    if (!peer || kind <= MSG_INVALID || kind >= MSG_COUNT || size > NET_MTU) return false;
    bool reliable = MSG_RELIABLE[kind];
    ENetPacket *packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet) return false;
    if (enet_peer_send(peer, reliable ? NET_CHANNEL_RELIABLE : NET_CHANNEL_UNRELIABLE, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    return true;
}

NetEventKind net_poll(NetLink *l, NetEvent *e, uint32_t timeout_ms)
{
    *e = (NetEvent){0};
    if (!l->host) return NET_EVENT_NONE;
    ENetEvent event;
    if (enet_host_service(l->host, &event, timeout_ms) <= 0) return NET_EVENT_NONE;
    e->peer = event.peer;
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT: e->kind = NET_EVENT_CONNECT; break;
    case ENET_EVENT_TYPE_DISCONNECT: e->kind = NET_EVENT_DISCONNECT; break;
    case ENET_EVENT_TYPE_RECEIVE:
        e->kind = NET_EVENT_MESSAGE;
        if (event.packet->dataLength <= NET_MTU) {
            e->size = event.packet->dataLength;
            memcpy(e->data, event.packet->data, e->size);
            NetBuf b = netbuf_reader(e->data, e->size);
            MsgKind kind = MSG_INVALID;
            msg_kind(&b, &kind);
            e->msg = netbuf_ok(&b) ? kind : MSG_INVALID;
        }
        enet_packet_destroy(event.packet);
        break;
    default: e->kind = NET_EVENT_NONE; break;
    }
    return e->kind;
}

void net_flush(NetLink *l)
{
    if (l->host) enet_host_flush(l->host);
}
