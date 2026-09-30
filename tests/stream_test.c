// The two streams over the loopback: a client's soldier is where its client put it, a
// soldier the server runs is heard of and stepped on its keys, an old state is dropped,
// and the deltas stay small. Real sockets; a bad line is simulated outside these tests.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "connections.h"
#include "test.h"

#define PORT 40024
#define ROUNDS 400 // of 10 ms
#define BOT 1      // the soldier the server plays itself

typedef struct StreamClient {
    NetLink link;
    Game *game;
    ClientStream stream;
    int slot;
    bool welcomed;
    int snapshots;
    size_t state_bytes, snapshot_bytes; // the last of each
} StreamClient;

static void route_hello(NetBuf *b, void *m) { msg_hello(b, m); }

static void client_send(StreamClient *c, MsgKind kind, void (*routine)(NetBuf *, void *), void *m)
{
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    msg_kind(&b, &kind);
    routine(&b, m);
    if (netbuf_ok(&b)) net_send(c->link.peer, kind, buf, netbuf_bytes(&b));
}

static void client_pump(StreamClient *c)
{
    NetEvent e;
    while (c->link.host && net_poll(&c->link, &e, 0) != NET_EVENT_NONE) {
        if (e.kind == NET_EVENT_CONNECT) {
            MsgHello hello = {.version = NET_VERSION, .name = "Mover"};
            client_send(c, MSG_HELLO, route_hello, &hello);
        } else if (e.kind == NET_EVENT_MESSAGE && e.msg == MSG_WELCOME) {
            NetBuf b = netbuf_reader(e.data, e.size);
            MsgKind kind;
            MsgWelcome m = {0};
            msg_kind(&b, &kind);
            msg_welcome(&b, &m);
            if (!netbuf_done(&b)) continue;
            c->slot = m.slot;
            c->welcomed = true;
            client_stream_reset(&c->stream);
        } else if (e.kind == NET_EVENT_MESSAGE && e.msg == MSG_SNAPSHOT && c->welcomed) {
            if (client_stream_hear(&c->stream, c->game, c->slot, e.data, e.size)) {
                c->snapshots++;
                c->snapshot_bytes = e.size;
            }
        }
    }
}

// One tick of the client: its soldier on `buttons`, aiming right; the others on their
// word. Then its state to the server.
static void client_tick(StreamClient *c, Buttons buttons)
{
    World *w = &c->game->world;
    Command cmds[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        s->remote = i != c->slot;
        if (s->remote) cmds[i] = stream_command(s, client_stream_quiet(&c->stream, i));
    }
    Soldier *me = &w->soldiers[c->slot];
    cmds[c->slot] = (Command){.seq = w->tick + 1, .buttons = buttons, .aim = vec2(me->pos.x + 100.0f, me->pos.y)};
    game_tick(c->game, cmds);
    if (me->active) {
        uint8_t buf[NET_MTU];
        size_t n = client_stream_state(&c->stream, me, buf, sizeof buf);
        if (n && net_send(c->link.peer, MSG_CLIENT_STATE, buf, n)) c->state_bytes = n;
    }
    net_flush(&c->link);
}

// One tick of the server: the players on their word, the bot walking left.
static void server_tick(Connections *conns, Game *g)
{
    Command cmds[MAX_PLAYERS] = {0};
    connections_commands(conns, g, cmds);
    const Soldier *bot = &g->world.soldiers[BOT];
    cmds[BOT] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_LEFT, .aim = vec2(bot->pos.x - 100.0f, bot->pos.y)};
    game_tick(g, cmds);
    connections_snapshots(conns, g);
    net_flush(conns->link);
}

// Both ends for `rounds` ticks, the client pressing `buttons`.
static void play(Connections *conns, Game *g, StreamClient *c, int rounds, Buttons buttons)
{
    for (int round = 0; round < rounds; round++) {
        connections_poll(conns, g);
        server_tick(conns, g);
        client_pump(c);
        if (c->welcomed) client_tick(c, buttons);
        enet_host_service(conns->link->host, NULL, 10); // the wait; what arrives is dispatched next round
    }
}

void stream_tests(void)
{
    CHECK(net_init(), "ENet starts");

    // the server: slot 0 free for the client, the bot in slot 1, the history for the deltas
    Game *gs = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    gs->world.soldiers[0].active = false;
    gs->world.history = calloc(1, sizeof(History));
    NetLink server;
    CHECK(net_listen(&server, PORT, 8), "the server listens on %d", PORT);
    Connections conns;
    connections_init(&conns, &server, NULL, "Arena");

    // the client: the same map, nobody in it, no authority
    StreamClient c = {.slot = -1};
    c.game = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    for (int i = 0; i < MAX_PLAYERS; i++) c.game->world.soldiers[i].active = false;
    c.game->world.authority = false;
    CHECK(client_stream_init(&c.stream), "the client's ring is made");
    CHECK(net_connect(&c.link, "127.0.0.1", PORT), "the client connects");

    // the join, and the first snapshot
    for (int round = 0; round < ROUNDS && c.snapshots == 0; round++) play(&conns, gs, &c, 1, 0);
    CHECK(c.welcomed && c.slot == 0, "the client is welcomed into slot 0");
    Soldier *mine = &c.game->world.soldiers[0], *theirs = &gs->world.soldiers[0];
    CHECK(c.snapshots > 0 && mine->active && !mine->remote && mine->life == theirs->life && fabsf(mine->pos.x - theirs->pos.x) < 1.0f,
          "the first snapshot places my soldier where the server spawned it, on the life it gave (%d snapshots)", c.snapshots);
    CHECK(c.game->world.soldiers[BOT].active && c.game->world.soldiers[BOT].remote,
          "and brings the bot, heard of and not played here");

    // a second of running right
    float start = mine->pos.x;
    play(&conns, gs, &c, 60, BUTTON_RIGHT);
    CHECK(mine->pos.x > start + 20.0f, "my soldier ran right (%.1f to %.1f)", start, mine->pos.x);
    CHECK(theirs->pos.x > start + 10.0f && fabsf(theirs->pos.x - mine->pos.x) < 30.0f,
          "and the server has it where I put it, a packet behind (%.1f there, %.1f here)", theirs->pos.x, mine->pos.x);
    CHECK(theirs->controls & BUTTON_RIGHT, "with my keys, to step it on between my states");
    CHECK(conns.streams[0].dropped == 0 && c.stream.dropped == 0, "nothing was dropped either way (%u, %u)",
          conns.streams[0].dropped, c.stream.dropped);

    // the bot, heard of and stepped on its keys
    const Soldier *bot_here = &c.game->world.soldiers[BOT], *bot_there = &gs->world.soldiers[BOT];
    CHECK(bot_there->vel.x < 0.0f || bot_there->controls & BUTTON_LEFT, "the server's bot walks left");
    CHECK(fabsf(bot_here->pos.x - bot_there->pos.x) < 30.0f, "and here it is near where the server has it (%.1f vs %.1f)",
          bot_here->pos.x, bot_there->pos.x);

    // the deltas
    CHECK(c.state_bytes > 0 && c.state_bytes < 60, "a tick's client state is small (%zu bytes)", c.state_bytes);
    CHECK(c.snapshot_bytes > 0 && c.snapshot_bytes < 160, "and so is a snapshot of two soldiers (%zu bytes)", c.snapshot_bytes);

    // an old state is dropped
    uint32_t dropped = conns.streams[0].dropped;
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_CLIENT_STATE;
    MsgClientState old = {.seq = 1, .owned = *mine};
    msg_kind(&b, &kind);
    msg_client_state(&b, &old, NULL);
    net_send(c.link.peer, MSG_CLIENT_STATE, buf, netbuf_bytes(&b));
    net_flush(&c.link);
    play(&conns, gs, &c, 5, 0);
    CHECK(conns.streams[0].dropped == dropped + 1, "a state older than the newest is dropped (%u dropped)", conns.streams[0].dropped);

    net_close(&c.link);
    for (int round = 0; round < 50 && conns.items[0].peer; round++) {
        connections_poll(&conns, gs);
        enet_host_service(server.host, NULL, 10);
    }
    net_close(&server);
    connections_free(&conns);
    client_stream_free(&c.stream);
    free(gs->world.history);
    scene_free(gs);
    scene_free(c.game);
    net_shutdown();
}
