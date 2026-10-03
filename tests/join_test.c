// The join, over ENet on the loopback: a server listening, a client connecting, Hello
// answered with Welcome and a soldier, the wrong version Denied, a leaver's slot freed,
// chat relayed. Real sockets; a bad line is simulated outside these tests.

#include <string.h>
#include <time.h>

#include "connections.h"

#include <stdio.h>
#include "test.h"

#define PORT 40023
#define ROUNDS 300 // of 10 ms: three seconds at most for anything to arrive

// A client's end for the test: connects, says hello, remembers what it heard.
typedef struct TestClient {
    NetLink link;
    uint16_t version;
    char password[NET_PASSWORD_SIZE]; // said in the Hello
    bool connected, welcomed, denied, closed, mapped;
    MsgWelcome welcome;
    MsgMap map;
    MsgDenied denial;
    MsgChat chat;
    int chats;         // lines from players
    int announcements; // and from the server itself
    MsgVote vote;      // the last word of a vote
    int votes;
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
            snprintf(hello.password, sizeof hello.password, "%s", c->password);
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
            } else if (kind == MSG_VOTE) {
                msg_vote(&b, &c->vote);
                if (netbuf_done(&b)) c->votes++;
            } else if (kind == MSG_CHAT) {
                msg_chat(&b, &c->chat);
                if (netbuf_done(&b) && c->chat.slot == MAX_PLAYERS) c->announcements++;
                else if (netbuf_done(&b)) c->chats++;
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
static int want_votes, want_announcements; // what the first client waits to have heard
static bool vote_heard(TestClient **c) { return c[0]->votes >= want_votes && c[2]->votes >= want_votes; }
static bool answered(TestClient **c) { return c[0]->announcements >= want_announcements; }
static bool third_cut_off(TestClient **c) { return c[2]->closed; }
static bool third_answered(TestClient **c) { return c[2]->denied || c[2]->welcomed; }
static int never_rounds; // a few rounds for word to travel, waiting for nothing in particular
static bool never(TestClient **c)
{
    (void)c;
    return ++never_rounds % 20 == 0;
}

void join_tests(void)
{
    CHECK(net_init(), "ENet starts");
    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    g->world.soldiers[0].active = g->world.soldiers[1].active = false; // an empty server

    NetLink server;
    CHECK(net_listen(&server, NULL, PORT, 8), "the server listens on %d", PORT);
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
    CHECK(a.announcements == 1, "and the server announced the join as a line of its own (%d)", a.announcements);

    // team chat goes to the team alone: a third client joins, is put on another team
    // by the server, and hears the public line but not the team's
    TestClient d;
    TestClient *three[3] = {&a, &b, &d};
    CHECK(client_open(&d, NET_VERSION), "a third client connects");
    pump(&conns, g, three, 3, third_welcomed);
    CHECK(d.welcomed && d.welcome.slot == 1, "and is welcomed into slot 1");
    CHECK(a.announcements == 2, "which the first hears of (%d announcements)", a.announcements);
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

    // Votes ride the chat, and run as the original's: a newcomer may not start one for
    // two minutes, so the cooldowns are waived here; only yeses count, against the two
    // players on, and 60% of them is both. A map vote's starter has not voted by
    // starting it: one yes of two is half, short; the starter's own passes it, and the
    // server is handed the map.
    snprintf(conns.maps_dir, sizeof conns.maps_dir, "assets/maps");
    MsgChat cmd = {.slot = 0, .text = "/votemap ctf_Ash"};
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    want_votes = 1;
    pump(&conns, g, three, 3, vote_heard);
    CHECK(conns.vote.kind == VOTE_NONE, "a player just joined may not start a vote: it is refused");
    CHECK(a.announcements == 3 && strstr(a.chat.text, "2:00 minutes") != NULL, "and told why, alone (%s)", a.chat.text);
    conns.vote_cooldown[0] = conns.vote_cooldown[1] = -1;
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    pump(&conns, g, three, 3, vote_heard);
    char voted[NET_MAP_SIZE];
    CHECK(a.vote.kind == VOTE_MAP && strcmp(a.vote.target, "ctf_Ash") == 0 && strcmp(a.vote.starter, "Tester") == 0 &&
              d.vote.kind == VOTE_MAP && !connections_take_vote_map(&conns, voted, sizeof voted) && conns.vote.max_votes == 2,
          "a map vote begins, everyone told what and by whom, against the two players on");
    snprintf(cmd.text, sizeof cmd.text, "/yes");
    client_send(&d, MSG_CHAT, route_chat, &cmd);
    pump(&conns, g, three, 3, never);
    CHECK(conns.vote.kind == VOTE_MAP && conns.vote.answer[1] == 1 && !connections_take_vote_map(&conns, voted, sizeof voted),
          "one yes of two is half, short of the 60%%: the vote is still on");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    want_votes = 2;
    pump(&conns, g, three, 3, vote_heard);
    CHECK(a.vote.kind == VOTE_NONE && connections_take_vote_map(&conns, voted, sizeof voted) && strcmp(voted, "ctf_Ash") == 0,
          "the starter's yes passes it: everyone hears it is over and the server is handed the map (%s)", voted);
    conns.vote_cooldown[0] = -1;
    snprintf(cmd.text, sizeof cmd.text, "/votemap NoSuchMap");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    want_announcements = a.announcements + 1; // the server's answer, to the asker alone
    pump(&conns, g, three, 3, answered);
    CHECK(conns.vote.kind == VOTE_NONE && strstr(a.chat.text, "Map not found") != NULL,
          "a vote for a map the server doesn't have never begins (%s)", a.chat.text);
    // A kick vote against the third. Myself, or nobody, is no vote. Starting one is no
    // yes, as the original plays: its starter presses F12 as everyone does. The third, its
    // target, may not vote. Unanswered it runs out; at a server's 50% the starter's yes
    // passes it, and the third is cut off and barred for an hour. The kick window's
    // reason keeps its leading space.
    conns.vote_cooldown[0] = -1;
    snprintf(cmd.text, sizeof cmd.text, "/votekick 0 me");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    snprintf(cmd.text, sizeof cmd.text, "/votekick");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    pump(&conns, g, three, 3, never);
    CHECK(conns.vote.kind == VOTE_NONE && conns.vote_cooldown[0] < 0, "a kick of myself, or of nobody, never begins");
    snprintf(cmd.text, sizeof cmd.text, "/votekick 1  afk");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    want_votes = 3;
    pump(&conns, g, three, 3, vote_heard);
    CHECK(conns.vote.kind == VOTE_KICK && conns.vote.slot == 1 && conns.vote.answer[0] == 0 && strcmp(a.vote.reason, " afk") == 0,
          "a kick vote names the player by slot, nobody's yes yet, the reason as typed (kind %d, slot %d, reason '%s')",
          conns.vote.kind, conns.vote.slot, a.vote.reason);
    snprintf(cmd.text, sizeof cmd.text, "/yes");
    client_send(&d, MSG_CHAT, route_chat, &cmd);
    pump(&conns, g, three, 3, never);
    CHECK(conns.vote.kind == VOTE_KICK && conns.vote.answer[1] == 0, "its target's yes is not taken");
    conns.vote.ticks_left = 1; // it runs out
    pump(&conns, g, three, 3, never);
    for (int t = 0; t < 2 && conns.vote.kind != VOTE_NONE; t++) connections_snapshots(&conns, g);
    CHECK(conns.vote.kind == VOTE_NONE && conns.items[1].joined, "run out, nothing happens to it");
    conns.vote_percent = 50;
    conns.vote_cooldown[0] = -1;
    snprintf(cmd.text, sizeof cmd.text, "/votekick 1 afk");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    want_votes = 5;
    pump(&conns, g, three, 3, vote_heard);
    CHECK(conns.vote.kind == VOTE_KICK && conns.items[1].joined, "begun again, it waits on a yes");
    snprintf(cmd.text, sizeof cmd.text, "/yes");
    client_send(&a, MSG_CHAT, route_chat, &cmd);
    pump(&conns, g, three, 3, third_cut_off);
    for (int round = 0; round < 50 && conns.items[1].joined; round++) { // the server hears the line close
        connections_poll(&conns, g);
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(d.closed && d.denied && !conns.items[1].joined && conns.vote.kind == VOTE_NONE,
          "passed, it is told why and cut off, and its slot frees (denied: %s; closed %d, joined %d, vote %d)", d.denial.reason, d.closed, conns.items[1].joined, conns.vote.kind);
    int64_t lifts = conns.lists.bans[0].expires - (int64_t)time(NULL);
    CHECK(conns.lists.ban_count == 1 && lifts > VOTE_KICK_BAN_SECONDS - 5 && lifts <= VOTE_KICK_BAN_SECONDS &&
              strcmp(conns.lists.bans[0].reason, "Vote Kicked") == 0,
          "and its address is barred for an hour (%lld seconds)", (long long)lifts);
    // which keeps it out: the same address comes back and is denied
    TestClient e;
    TestClient *four[3] = {&a, &b, &e};
    CHECK(client_open(&e, NET_VERSION), "the kicked player's address connects again");
    pump(&conns, g, four, 3, third_answered);
    CHECK(e.denied && !e.welcomed && strstr(e.denial.reason, "banned") != NULL, "and is denied: %s", e.denial.reason);
    net_close(&e.link);

    // A password asked for (sv_password) must be said in the Hello. The ban on this
    // address is lifted first, so the password alone decides.
    lists_unban(&conns.lists, conns.lists.bans[0].host);
    connections_set_password(&conns, "s3cret");
    TestClient p;
    TestClient *five[3] = {&a, &b, &p};
    CHECK(client_open(&p, NET_VERSION), "a client connects to a server with a password, saying none");
    pump(&conns, g, five, 3, third_answered);
    CHECK(p.denied && !p.welcomed && strstr(p.denial.reason, "password") != NULL, "and is denied, told why (%s)", p.denial.reason);
    net_close(&p.link);
    CHECK(client_open(&p, NET_VERSION), "it connects again");
    snprintf(p.password, sizeof p.password, "%s", "s3cret");
    pump(&conns, g, five, 3, third_answered);
    CHECK(p.welcomed && p.welcome.slot == 1, "and saying the password is welcomed into slot 1 (welcomed %d, denied %d: %s)",
          p.welcomed, p.denied, p.denial.reason);
    net_close(&p.link);
    connections_set_password(&conns, "");

    // the first leaves carrying bravo's flag and manning a stationary gun
    Thing *flag = &g->world.things[MAX_THINGS - 1], *gun = &g->world.things[MAX_THINGS - 2];
    *flag = (Thing){.style = THING_BRAVO_FLAG, .holder = 1, .points = 4};
    *gun = (Thing){.style = THING_STAT_GUN, .is_static = true, .points = 4};
    g->world.soldiers[0].held = MAX_THINGS;
    g->world.soldiers[0].stat = MAX_THINGS - 1;
    // and with a kick vote against it on
    conns.vote = (Vote){.kind = VOTE_KICK, .slot = 0, .starter = 1, .ticks_left = VOTE_TICKS, .max_votes = 2};
    net_close(&d.link);
    net_close(&a.link);
    for (int round = 0; round < ROUNDS && conns.items[0].peer; round++) {
        connections_poll(&conns, g);
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(!conns.items[0].peer && !g->world.soldiers[0].active, "a client that leaves frees its slot and its soldier");
    CHECK(flag->style == THING_BRAVO_FLAG && flag->holder == 0 && g->world.soldiers[0].held == 0,
          "and the flag it carried falls, held by nobody (holder %d)", flag->holder);
    CHECK(!gun->is_static && g->world.soldiers[0].stat == 0, "and the stationary gun it manned is free");
    bool left_ban = false;
    for (int i = 0; i < conns.lists.ban_count; i++)
        left_ban |= strcmp(conns.lists.bans[i].reason, "Vote Kicked (Left game)") == 0 &&
                    conns.lists.bans[i].expires - (int64_t)time(NULL) > VOTE_LEFT_BAN_SECONDS - 5;
    CHECK(conns.vote.kind == VOTE_NONE && left_ban,
          "and leaving before the kick vote against it is decided ends the vote, and bars it five minutes");
    *flag = *gun = (Thing){0};

    net_close(&b.link);
    net_close(&server);
    connections_free(&conns);
    scene_free(g);
    net_shutdown();
}
