#include "connections.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void say(Console *con, const char *fmt, ...)
{
    if (!con) return;
    char text[CONSOLE_TEXT_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    console_print(con, "%s", text);
}

bool connections_init(Connections *c, NetLink *link, Console *console, const char *map)
{
    *c = (Connections){.link = link, .console = console, .round = 1};
    snprintf(c->map, sizeof c->map, "%s", map ? map : "");
    wire_queue_init(&c->events);
    c->streams = calloc(MAX_PLAYERS, sizeof *c->streams);
    return c->streams != NULL;
}

void connections_free(Connections *c)
{
    free(c->streams);
    c->streams = NULL;
}

int connections_count(const Connections *c)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) n += c->items[i].joined;
    return n;
}

// The slot a peer was given, or -1.
static int slot_of(const Connections *c, const ENetPeer *peer)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->items[i].peer == peer) return i;
    return -1;
}

// A slot with no peer and no soldier (a local or scripted soldier keeps its slot).
static int free_slot(const Connections *c, const Game *g)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!c->items[i].peer && !g->world.soldiers[i].active) return i;
    return -1;
}

// A message built into `buf`; the bytes to send, or 0 if it didn't fit.
static size_t build(uint8_t *buf, size_t size, MsgKind kind, void (*routine)(NetBuf *, void *), void *m)
{
    NetBuf b = netbuf_writer(buf, size);
    msg_kind(&b, &kind);
    routine(&b, m);
    return netbuf_ok(&b) ? netbuf_bytes(&b) : 0;
}

static void route_welcome(NetBuf *b, void *m) { msg_welcome(b, m); }
static void route_denied(NetBuf *b, void *m) { msg_denied(b, m); }
static void route_chat(NetBuf *b, void *m) { msg_chat(b, m); }
static void route_map(NetBuf *b, void *m) { msg_map(b, m); }

// The round's map to one peer.
static void tell_map(Connections *c, ENetPeer *peer)
{
    uint8_t buf[NET_MTU];
    MsgMap m = {.round = c->round};
    snprintf(m.map, sizeof m.map, "%s", c->map);
    size_t n = build(buf, sizeof buf, MSG_MAP, route_map, &m);
    if (n) net_send(peer, MSG_MAP, buf, n);
}

// A player's soldier placed anew on its team: a join, a new round.
static void place(Connections *c, Game *g, int slot);

static void deny(Connections *c, ENetPeer *peer, const char *reason)
{
    uint8_t buf[NET_MTU];
    MsgDenied m;
    snprintf(m.reason, sizeof m.reason, "%s", reason);
    size_t n = build(buf, sizeof buf, MSG_DENIED, route_denied, &m);
    if (n) net_send(peer, MSG_DENIED, buf, n);
    net_flush(c->link);
    enet_peer_disconnect_later(peer, 0);
    say(c->console, "denied a join: %s\n", reason);
}

// The team a newcomer joins: the emptier of alpha and bravo in a team game, alpha
// otherwise. A map with a flag's spawn point is a team game, until the server's
// settings say.
static Team team_for(const Game *g)
{
    uint64_t rng = 1;
    Vec2 at;
    if (!thing_spawn_point(g->ctx.map, SPAWN_ALPHA_FLAG, &rng, &at)) return TEAM_ALPHA;
    int alpha = 0, bravo = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &g->world.soldiers[i];
        if (!s->active) continue;
        alpha += s->team == TEAM_ALPHA;
        bravo += s->team == TEAM_BRAVO;
    }
    return bravo < alpha ? TEAM_BRAVO : TEAM_ALPHA;
}

static void hello(Connections *c, Game *g, ENetPeer *peer, const NetEvent *e)
{
    NetBuf b = netbuf_reader(e->data, e->size);
    MsgKind kind;
    MsgHello m = {0};
    msg_kind(&b, &kind);
    msg_hello(&b, &m);
    if (!netbuf_done(&b)) {
        deny(c, peer, "a Hello that couldn't be read");
        return;
    }
    if (m.version != NET_VERSION) {
        char reason[NET_TEXT_SIZE];
        snprintf(reason, sizeof reason, "version %u, but this server is version %u", m.version, NET_VERSION);
        deny(c, peer, reason);
        return;
    }
    if (slot_of(c, peer) >= 0) return; // said hello twice
    int slot = free_slot(c, g);
    if (slot < 0) {
        deny(c, peer, "the server is full");
        return;
    }

    Connection *conn = &c->items[slot];
    *conn = (Connection){.peer = peer, .joined = true};
    snprintf(conn->name, sizeof conn->name, "%s", m.name[0] ? m.name : "Player");
    peer->data = conn;
    server_stream_init(&c->streams[slot], c->round);
    c->streams[slot].event_ack = wire_queue_present(&c->events); // what happened before it came is nobody's news
    place(c, g, slot);

    uint8_t buf[NET_MTU];
    MsgWelcome w = {.slot = (uint8_t)slot, .tick = g->world.tick};
    size_t n = build(buf, sizeof buf, MSG_WELCOME, route_welcome, &w);
    if (n) net_send(peer, MSG_WELCOME, buf, n);
    tell_map(c, peer); // joining is hearing of the round
    say(c->console, "%s joined as %d\n", conn->name, slot);
}

static void place(Connections *c, Game *g, int slot)
{
    (void)c;
    Team team = team_for(g);
    Soldier *s = &g->world.soldiers[slot];
    Vec2 at = spawn_point(g->ctx.map, team, &g->world.rng);
    soldier_spawn(&g->ctx, s, at, team, WEAPON_EAGLE, WEAPON_KNIFE);
    s->life++;
    s->remote = true; // its keys move it; what it fires it tells
}

void connections_new_round(Connections *c, Game *g, const char *map)
{
    c->round++;
    snprintf(c->map, sizeof c->map, "%s", map);
    wire_queue_init(&c->events); // the old round's news is nobody's now
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined) continue;
        server_stream_init(&c->streams[i], c->round);
        place(c, g, i);
        tell_map(c, c->items[i].peer);
    }
    say(c->console, "round %u on %s\n", c->round, c->map);
}

static void leave(Connections *c, Game *g, ENetPeer *peer)
{
    int slot = slot_of(c, peer);
    if (slot < 0) return;
    Connection *conn = &c->items[slot];
    if (conn->joined) {
        say(c->console, "%s left\n", conn->name);
        g->world.soldiers[slot].active = false;
    }
    *conn = (Connection){0};
    peer->data = NULL;
}

static void chat(Connections *c, ENetPeer *peer, const NetEvent *e)
{
    int slot = slot_of(c, peer);
    if (slot < 0 || !c->items[slot].joined) return;
    NetBuf b = netbuf_reader(e->data, e->size);
    MsgKind kind;
    MsgChat m = {0};
    msg_kind(&b, &kind);
    msg_chat(&b, &m);
    if (!netbuf_done(&b)) return;

    m.slot = (uint8_t)slot; // whatever it claimed, it is who it is
    say(c->console, "%s%s: %s\n", m.team ? "(team) " : "", c->items[slot].name, m.text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) connections_broadcast(c, MSG_CHAT, buf, n);
}

void connections_broadcast(Connections *c, MsgKind kind, const uint8_t *data, size_t size)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->items[i].joined) net_send(c->items[i].peer, kind, data, size);
}

void connections_poll(Connections *c, Game *g)
{
    NetEvent e;
    while (net_poll(c->link, &e, 0) != NET_EVENT_NONE) {
        switch (e.kind) {
        case NET_EVENT_CONNECT: break; // nobody until its Hello
        case NET_EVENT_DISCONNECT: leave(c, g, e.peer); break;
        case NET_EVENT_MESSAGE: {
            int slot = slot_of(c, e.peer);
            if (e.msg == MSG_HELLO) hello(c, g, e.peer, &e);
            else if (slot < 0) deny(c, e.peer, "no Hello first"); // the rest is for players
            else if (e.msg == MSG_CHAT) chat(c, e.peer, &e);
            else if (e.msg == MSG_CLIENT_STATE) server_stream_receive(&c->streams[slot], g, slot, e.data, e.size);
            break;
        }
        default: break;
        }
    }
}

void connections_commands(const Connections *c, const Game *g, Command cmds[MAX_PLAYERS])
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined) continue;
        cmds[i] = stream_command(&g->world.soldiers[i], server_stream_quiet(&c->streams[i], g->world.tick));
    }
}

void connections_snapshots(Connections *c, const Game *g)
{
    wire_collect(&c->events, &g->events, g->world.tick - 1, -1); // the tick just run
    char names[MAX_PLAYERS][NET_NAME_SIZE];
    for (int i = 0; i < MAX_PLAYERS; i++) snprintf(names[i], NET_NAME_SIZE, "%s", c->items[i].joined ? c->items[i].name : "");
    uint8_t buf[NET_MTU];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined) continue;
        size_t n = server_stream_snapshot(&c->streams[i], g, i, &c->events, names, buf, sizeof buf);
        if (n) net_send(c->items[i].peer, MSG_SNAPSHOT, buf, n);
    }
}
