#include "net/client_net.h"

#include <stdio.h>
#include <string.h>

bool client_net_init(ClientNet *n)
{
    *n = (ClientNet){.slot = -1};
    return net_init();
}

void client_net_shutdown(ClientNet *n)
{
    net_close(&n->link);
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
    console_print(con, "connecting to %s:%u...\n", address, port);
}

void client_net_disconnect(ClientNet *n, Console *con)
{
    if (!n->link.host) return;
    net_close(&n->link);
    n->state = CLIENT_NET_OFF;
    n->slot = -1;
    console_print(con, "disconnected\n");
}

static void send_hello(ClientNet *n)
{
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_HELLO;
    MsgHello m = {.version = NET_VERSION};
    snprintf(m.name, sizeof m.name, "%s", n->name);
    msg_kind(&b, &kind);
    msg_hello(&b, &m);
    if (netbuf_ok(&b)) net_send(n->link.peer, MSG_HELLO, buf, netbuf_bytes(&b));
    n->state = CLIENT_NET_JOINING;
}

static void heard(ClientNet *n, Console *con, const NetEvent *e)
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
        console_print(con, "joined as %d, at the server's tick %u\n", m.slot, m.tick);
        break;
    }
    case MSG_DENIED: {
        MsgDenied m = {0};
        msg_denied(&b, &m);
        if (netbuf_done(&b)) console_print(con, "denied: %s\n", m.reason);
        break;
    }
    case MSG_CHAT: {
        MsgChat m = {0};
        msg_chat(&b, &m);
        if (!netbuf_done(&b)) return;
        if (m.slot == MAX_PLAYERS) console_print(con, "server: %s\n", m.text);
        else console_print(con, "%s%d: %s\n", m.team ? "(team) " : "", m.slot, m.text);
        break;
    }
    default: break; // the streams, once there are streams
    }
}

void client_net_poll(ClientNet *n, Console *con)
{
    if (!n->link.host) return;
    NetEvent e;
    while (net_poll(&n->link, &e, 0) != NET_EVENT_NONE) {
        switch (e.kind) {
        case NET_EVENT_CONNECT:
            console_print(con, "connected; saying hello\n");
            send_hello(n);
            break;
        case NET_EVENT_DISCONNECT:
            console_print(con, n->state == CLIENT_NET_CONNECTING ? "no answer\n" : "the server closed the line\n");
            net_close(&n->link);
            n->state = CLIENT_NET_OFF;
            n->slot = -1;
            return;
        case NET_EVENT_MESSAGE: heard(n, con, &e); break;
        default: break;
        }
    }
}

void client_net_flush(ClientNet *n) { net_flush(&n->link); }

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
