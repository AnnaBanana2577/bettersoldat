// The join, over ENet on the loopback: a server listening, a client connecting, Hello
// answered with Welcome and a soldier, the wrong version Denied, a leaver's slot freed,
// chat relayed. Real sockets; a bad line is simulated outside these tests.

#include <string.h>

#include "connections.h"
#include "test.h"

#define PORT 40023
#define ROUNDS 300 // of 10 ms: three seconds at most for anything to arrive

// A client's end for the test: connects, says hello, remembers what it heard.
typedef struct TestClient {
    NetLink link;
    uint16_t version;
    bool connected, welcomed, denied, closed, mapped;
    MsgWelcome welcome;
    MsgMap map;
    MsgDenied denial;
    MsgChat chat;
    int chats;
} TestClient;

static bool client_open(TestClient *c, uint16_t version)
{
    *c = (TestClient){.version = version};
    return net_connect(&c->link, "127.0.0.1", PORT);
}

static void client_send(TestClient *c, MsgKind kind, void (*routine)(NetBuf *, void *), void *m)
{
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    msg_kind(&b, &kind);
    routine(&b, m);
    if (netbuf_ok(&b)) net_send(c->link.peer, kind, buf, netbuf_bytes(&b));
}

static void route_hello(NetBuf *b, void *m) { msg_hello(b, m); }
static void route_chat(NetBuf *b, void *m) { msg_chat(b, m); }

static void client_pump(TestClient *c)
{
    NetEvent e;
    while (c->link.host && net_poll(&c->link, &e, 0) != NET_EVENT_NONE) {
        if (e.kind == NET_EVENT_CONNECT) {
            c->connected = true;
            MsgHello hello = {.version = c->version, .name = "Tester"};
            client_send(c, MSG_HELLO, route_hello, &hello);
        } else if (e.kind == NET_EVENT_DISCONNECT) {
            c->closed = true;
        } else if (e.kind == NET_EVENT_MESSAGE) {
            NetBuf b = netbuf_reader(e.data, e.size);
            MsgKind kind;
            msg_kind(&b, &kind);
            if (kind == MSG_WELCOME) {
                msg_welcome(&b, &c->welcome);
                c->welcomed = netbuf_done(&b);
            } else if (kind == MSG_MAP) {
                msg_map(&b, &c->map);
                c->mapped = netbuf_done(&b);
            } else if (kind == MSG_DENIED) {
                msg_denied(&b, &c->denial);
                c->denied = netbuf_done(&b);
            } else if (kind == MSG_CHAT) {
                msg_chat(&b, &c->chat);
                if (netbuf_done(&b)) c->chats++;
            }
        }
    }
    net_flush(&c->link);
}

// Both ends, until `done` or the rounds run out.
static void pump(Connections *server, Game *g, TestClient **clients, int count, bool (*done)(TestClient **))
{
    for (int round = 0; round < ROUNDS && !done(clients); round++) {
        connections_poll(server, g);
        net_flush(server->link);
        for (int i = 0; i < count; i++) client_pump(clients[i]);
        enet_host_service(clients[0]->link.host ? clients[0]->link.host : server->link->host, NULL, 10); // the wait
    }
}

static bool first_welcomed(TestClient **c) { return c[0]->welcomed; }
static bool second_answered(TestClient **c) { return c[1]->denied || c[1]->welcomed; }
static bool first_heard_chat(TestClient **c) { return c[0]->chats > 0; }
static bool third_welcomed(TestClient **c) { return c[2]->welcomed; }
static bool third_heard_chat(TestClient **c) { return c[2]->chats > 0 && c[0]->chats > 2; }

void join_tests(void)
{
    CHECK(net_init(), "ENet starts");
    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    g->world.soldiers[0].active = g->world.soldiers[1].active = false; // an empty server

    NetLink server;
    CHECK(net_listen(&server, PORT, 8), "the server listens on %d", PORT);
    Connections conns;
    CHECK(connections_init(&conns, &server, NULL, "Arena"), "the connections are made");

    TestClient a, b;
    TestClient *clients[2] = {&a, &b};
    CHECK(client_open(&a, NET_VERSION), "a client connects to the loopback");
    pump(&conns, g, clients, 1, first_welcomed);
    CHECK(a.connected, "the line comes up");
    CHECK(a.welcomed && a.welcome.slot == 0, "and Hello is answered with Welcome, slot 0 (welcomed %d, slot %d)", a.welcomed,
          a.welcome.slot);
    CHECK(g->world.soldiers[0].active && !g->world.soldiers[0].dead && strcmp(conns.items[0].name, "Tester") == 0,
          "with a soldier alive in that slot, named");
    CHECK(connections_count(&conns) == 1, "one player on the server");

    CHECK(client_open(&b, NET_VERSION + 1), "a second client connects, with the wrong version");
    pump(&conns, g, clients, 2, second_answered);
    CHECK(b.denied && !b.welcomed && strstr(b.denial.reason, "version") != NULL, "and is denied, told why (%s)", b.denial.reason);
    CHECK(connections_count(&conns) == 1 && !g->world.soldiers[1].active, "without a slot");

    MsgChat line = {.slot = 7, .team = false, .text = "hello there"};
    client_send(&a, MSG_CHAT, route_chat, &line);
    pump(&conns, g, clients, 2, first_heard_chat);
    CHECK(a.chats == 1 && a.chat.slot == 0 && strcmp(a.chat.text, "hello there") == 0,
          "a line of chat comes back from the server, stamped with the sender's real slot (%d)", a.chat.slot);
    CHECK(a.mapped && a.map.round == 1 && strcmp(a.map.map, "Arena") == 0, "and the Map came with the join: round %u on %s",
          a.map.round, a.map.map);

    // team chat goes to the team alone: a third client joins, is put on another team
    // by the server, and hears the public line but not the team's
    TestClient d;
    TestClient *three[3] = {&a, &b, &d};
    CHECK(client_open(&d, NET_VERSION), "a third client connects");
    pump(&conns, g, three, 3, third_welcomed);
    CHECK(d.welcomed && d.welcome.slot == 1, "and is welcomed into slot 1");
    g->world.soldiers[1].team = TEAM_BRAVO;
    int a_before = a.chats, d_before = d.chats;
    MsgChat team_line = {.slot = 0, .team = true, .text = "to the team"};
    client_send(&a, MSG_CHAT, route_chat, &team_line);
    MsgChat public_line = {.slot = 0, .team = false, .text = "to all"};
    client_send(&a, MSG_CHAT, route_chat, &public_line);
    pump(&conns, g, three, 3, third_heard_chat);
    CHECK(a.chats == a_before + 2 && d.chats == d_before + 1 && strcmp(d.chat.text, "to all") == 0,
          "a team line reaches the team alone, a public one everyone (a heard %d, d heard %d: %s)", a.chats - a_before,
          d.chats - d_before, d.chat.text);

    net_close(&d.link);
    net_close(&a.link);
    for (int round = 0; round < ROUNDS && conns.items[0].peer; round++) {
        connections_poll(&conns, g);
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(!conns.items[0].peer && !g->world.soldiers[0].active, "a client that leaves frees its slot and its soldier");

    net_close(&b.link);
    net_close(&server);
    connections_free(&conns);
    scene_free(g);
    net_shutdown();
}
