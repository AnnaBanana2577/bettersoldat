#include "net/client_net.h"
#include "ui/hud_data.h" // the colours of the line's word

#include <stdio.h>
#include <string.h>

bool client_net_init(ClientNet *n)
{
    *n = (ClientNet){.slot = -1};
    return net_init() && client_stream_init(&n->stream);
}

void client_net_shutdown(ClientNet *n)
{
    net_close(&n->link);
    client_stream_free(&n->stream);
    net_shutdown();
}

void client_net_connect(ClientNet *n, Console *con, const char *address, uint16_t port, const char *name)
{
    if (n->link.host) client_net_disconnect(n, con);
    snprintf(n->name, sizeof n->name, "%s", name);
    if (!net_connect(&n->link, address, port)) {
        console_print(con, "couldn't connect to %s:%u\n", address, port);
        return;
    }
    n->state = CLIENT_NET_CONNECTING;
    n->slot = -1;
    n->had_map = false;
    n->map_changing = n->map_replied = false;
    n->vote = (MsgVote){.kind = VOTE_NONE};
    snprintf(n->address, sizeof n->address, "%s", address);
    n->port = port;
    console_print(con, "connecting to %s:%u...\n", address, port);
}

void client_net_disconnect(ClientNet *n, Console *con)
{
    if (!n->link.host) return;
    net_close(&n->link);
    n->state = CLIENT_NET_OFF;
    n->slot = -1;
    n->vote.kind = VOTE_NONE;
    console_print(con, "disconnected\n");
}

static void send_hello(ClientNet *n)
{
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_HELLO;
    MsgHello m = {.version = NET_VERSION};
    snprintf(m.name, sizeof m.name, "%s", n->name);
    m.look = n->look;
    m.primary = n->primary;
    m.secondary = n->secondary;
    msg_kind(&b, &kind);
    msg_hello(&b, &m);
    if (netbuf_ok(&b)) net_send(n->link.peer, MSG_HELLO, buf, netbuf_bytes(&b));
    n->state = CLIENT_NET_JOINING;
}

static void heard(ClientNet *n, Console *con, Game *g, const NetEvent *e)
{
    NetBuf b = netbuf_reader(e->data, e->size);
    MsgKind kind;
    msg_kind(&b, &kind);
    switch (e->msg) {
    case MSG_WELCOME: {
        MsgWelcome m = {0};
        msg_welcome(&b, &m);
        if (!netbuf_done(&b)) return;
        n->slot = m.slot;
        n->tick = m.tick;
        n->state = CLIENT_NET_JOINED;
        console_print_color(con, HUD_COLOR_CLIENT, "Connection accepted to %s:%u\n", n->address, n->port);
        break;
    }
    case MSG_MAP: {
        MsgMap m = {0};
        msg_map(&b, &m);
        if (!netbuf_done(&b)) return;
        n->round = m.round;
        snprintf(n->map, sizeof n->map, "%s", m.map);
        snprintf(n->hostname, sizeof n->hostname, "%s", m.hostname);
        n->mapped = true;
        client_stream_reset(&n->stream, m.round);
        n->had_map = true;
        break;
    }
    case MSG_MAP_CHANGE: { // the round is over: said as the original's ClientHandleMapChange says it
        MsgMapChange m = {0};
        msg_map_change(&b, &m);
        if (!netbuf_done(&b)) return;
        n->map_change = m;
        n->map_changing = true;
        console_print_color(con, HUD_COLOR_GAME, "Next map: %s\n", m.map);
        break;
    }
    case MSG_MAP_REPLY: {
        MsgMapReply m = {0};
        msg_map_reply(&b, &m);
        if (!netbuf_done(&b)) return;
        n->map_reply = m;
        n->map_replied = true;
        break;
    }
    case MSG_DENIED: {
        MsgDenied m = {0};
        msg_denied(&b, &m);
        if (netbuf_done(&b)) console_print(con, "denied: %s\n", m.reason);
        break;
    }
    case MSG_VOTE: {
        MsgVote m = {0};
        msg_vote(&b, &m);
        if (!netbuf_done(&b)) return;
        if (m.kind != VOTE_NONE && (n->vote.kind == VOTE_NONE || strcmp(n->vote.target, m.target) != 0)) n->vote_seq++;
        n->vote = m;
        break;
    }
    case MSG_CHAT: {
        MsgChat m = {0};
        msg_chat(&b, &m);
        if (!netbuf_done(&b)) return;
        if (n->inbox_count == CLIENT_NET_INBOX) { // full: the oldest is lost
            memmove(n->inbox, n->inbox + 1, sizeof n->inbox - sizeof n->inbox[0]);
            n->inbox_count--;
        }
        n->inbox[n->inbox_count++] = m;
        break;
    }
    case MSG_SNAPSHOT:
        if (n->state == CLIENT_NET_JOINED && n->round && !n->mapped && g) client_stream_hear(&n->stream, g, n->slot, e->data, e->size);
        break;
    default: break;
    }
}

void client_net_poll(ClientNet *n, Console *con, Game *g)
{
    if (!n->link.host) return;
    NetEvent e;
    while (net_poll(&n->link, &e, 0) != NET_EVENT_NONE) {
        switch (e.kind) {
        case NET_EVENT_CONNECT:
            send_hello(n);
            break;
        case NET_EVENT_DISCONNECT:
            console_print_color(con, HUD_COLOR_WARNING, n->state == CLIENT_NET_CONNECTING ? "Connection timeout\n" : "Connection problem\n");
            net_close(&n->link);
            n->state = CLIENT_NET_OFF;
            n->slot = -1;
            return;
        case NET_EVENT_MESSAGE: heard(n, con, g, &e); break;
        default: break;
        }
    }
}

bool client_net_take_map(ClientNet *n)
{
    if (!n->mapped) return false;
    n->mapped = false;
    return true;
}

bool client_net_take_chat(ClientNet *n, MsgChat *out)
{
    if (n->inbox_count == 0) return false;
    *out = n->inbox[0];
    memmove(n->inbox, n->inbox + 1, sizeof n->inbox - sizeof n->inbox[0]);
    n->inbox_count--;
    return true;
}

void client_net_tick(ClientNet *n, const Game *g)
{
    if (n->state != CLIENT_NET_JOINED) return;
    client_stream_collect(&n->stream, g, n->slot);
    const Soldier *me = &g->world.soldiers[n->slot];
    if (!me->active) return; // nothing to say of a soldier the server hasn't placed yet
    uint8_t buf[NET_MTU];
    size_t size = client_stream_state(&n->stream, me, buf, sizeof buf);
    if (size) net_send(n->link.peer, MSG_CLIENT_STATE, buf, size);
}

void client_net_flush(ClientNet *n) { net_flush(&n->link); }

bool client_net_joined(const ClientNet *n) { return n->state == CLIENT_NET_JOINED; }

bool client_net_say(ClientNet *n, const char *text, bool team)
{
    if (n->state != CLIENT_NET_JOINED) return false;
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_CHAT;
    MsgChat m = {.slot = (uint8_t)n->slot, .team = team};
    snprintf(m.text, sizeof m.text, "%s", text);
    msg_kind(&b, &kind);
    msg_chat(&b, &m);
    return netbuf_ok(&b) && net_send(n->link.peer, MSG_CHAT, buf, netbuf_bytes(&b));
}

bool client_net_take_map_change(ClientNet *n)
{
    if (!n->map_changing) return false;
    n->map_changing = false;
    return true;
}

void client_net_map_query(ClientNet *n, int index)
{
    if (n->state != CLIENT_NET_JOINED || index < 0) return;
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_MAP_QUERY;
    MsgMapQuery m = {.index = (uint16_t)index};
    msg_kind(&b, &kind);
    msg_map_query(&b, &m);
    if (netbuf_ok(&b)) net_send(n->link.peer, MSG_MAP_QUERY, buf, netbuf_bytes(&b));
}

bool client_net_map_replied(ClientNet *n)
{
    if (!n->map_replied) return false;
    n->map_replied = false;
    return true;
}
