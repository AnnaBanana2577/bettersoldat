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
    *c = (Connections){.link = link, .console = console, .round = 1, .vote = {.starter = -1}};
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
static void route_vote(NetBuf *b, void *m) { msg_vote(b, m); }
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

// The vote as it stands, to one peer or (NULL) everyone: for the HUD.
static void tell_vote(Connections *c, ENetPeer *peer)
{
    MsgVote m = {.kind = c->vote.kind, .seconds = (uint16_t)((c->vote.ticks_left + TICK_RATE - 1) / TICK_RATE)};
    snprintf(m.target, sizeof m.target, "%s", c->vote.target);
    snprintf(m.starter, sizeof m.starter, "%s", c->vote.starter >= 0 ? c->items[c->vote.starter].name : "");
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_VOTE, route_vote, &m);
    if (!n) return;
    if (peer) net_send(peer, MSG_VOTE, buf, n);
    else connections_broadcast(c, MSG_VOTE, buf, n);
}

// A line from the server itself to everyone: who came, who went.
static void announce(Connections *c, const char *fmt, ...)
{
    MsgChat m = {.slot = MAX_PLAYERS, .team = true};
    va_list args;
    va_start(args, fmt);
    vsnprintf(m.text, sizeof m.text, fmt, args);
    va_end(args);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) connections_broadcast(c, MSG_CHAT, buf, n);
}

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

// The team a newcomer joins: the emptier of alpha and bravo in a team game, none
// otherwise, as the original's team 0: everyone on alpha would be friends, and
// friendly fire off, nobody's shots would count.
static Team team_for(const Game *g)
{
    if (!match_has_teams(&g->match)) return TEAM_NONE;
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
    Soldier *s = &g->world.soldiers[slot];
    s->look = m.look;
    s->primary_choice = m.primary;
    s->secondary_choice = m.secondary;
    place(c, g, slot);

    uint8_t buf[NET_MTU];
    MsgWelcome w = {.slot = (uint8_t)slot, .tick = g->world.tick};
    size_t n = build(buf, sizeof buf, MSG_WELCOME, route_welcome, &w);
    if (n) net_send(peer, MSG_WELCOME, buf, n);
    tell_map(c, peer); // joining is hearing of the round
    c->vote.answer[slot] = 0;
    if (c->vote.kind != VOTE_NONE) tell_vote(c, peer);
    say(c->console, "%s joined as %d\n", conn->name, slot);
    announce(c, "%s has joined the game", conn->name);
}

static void place(Connections *c, Game *g, int slot)
{
    (void)c;
    Team team = team_for(g);
    Soldier *s = &g->world.soldiers[slot];
    Vec2 at = spawn_point(g->ctx.map, team, &g->world.rng);
    // with the weapons it chose, or the original's first loadout for a choice that isn't one
    WeaponId primary = weapon_is_primary(s->primary_choice) ? s->primary_choice : WEAPON_EAGLE;
    WeaponId secondary = weapon_is_secondary(s->secondary_choice) ? s->secondary_choice : WEAPON_KNIFE;
    soldier_spawn(&g->ctx, s, at, team, primary, secondary);
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
    char name[NET_NAME_SIZE];
    snprintf(name, sizeof name, "%s", conn->name);
    bool joined = conn->joined;
    *conn = (Connection){0};
    peer->data = NULL;
    if (joined) announce(c, "%s has left the game", name);
}

static void vote_command(Connections *c, const Game *g, int slot, const char *text);

static void chat(Connections *c, const Game *g, ENetPeer *peer, const NetEvent *e)
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
    if (m.text[0] == '/') {
        vote_command(c, g, slot, m.text + 1);
        return;
    }
    say(c->console, "%s%s: %s\n", m.team ? "(team) " : "", c->items[slot].name, m.text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (!n) return;
    if (!m.team) {
        connections_broadcast(c, MSG_CHAT, buf, n);
        return;
    }
    Team team = g->world.soldiers[slot].team; // to the team, the sender among them
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->items[i].joined && g->world.soldiers[i].team == team) net_send(c->items[i].peer, MSG_CHAT, buf, n);
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
            else if (e.msg == MSG_CHAT) chat(c, g, e.peer, &e);
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

static void vote_tick(Connections *c);

void connections_snapshots(Connections *c, const Game *g)
{
    vote_tick(c);
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

// --- votes ---------------------------------------------------------------------------

// A line from the server to one player: an answer to its command.
static void tell(Connections *c, int slot, const char *fmt, ...)
{
    MsgChat m = {.slot = MAX_PLAYERS, .team = true};
    va_list args;
    va_start(args, fmt);
    vsnprintf(m.text, sizeof m.text, fmt, args);
    va_end(args);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n && c->items[slot].peer) net_send(c->items[slot].peer, MSG_CHAT, buf, n);
}

static bool map_exists(const Connections *c, const char *map)
{
    if (!c->maps_dir[0]) return true;
    if (!map[0] || strchr(map, '/') || strchr(map, '\\') || strstr(map, "..")) return false;
    char path[600];
    snprintf(path, sizeof path, "%s/%s.pms", c->maps_dir, map);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The player a kick names: a slot's number, or a name, whole or as much of it as typed.
static int player_named(const Connections *c, const char *name)
{
    char *end;
    long slot = strtol(name, &end, 10);
    if (*end == '\0' && end != name) return slot >= 0 && slot < MAX_PLAYERS && c->items[slot].joined ? (int)slot : -1;
    int found = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined) continue;
        if (strcmp(c->items[i].name, name) == 0) return i;
        if (strncmp(c->items[i].name, name, strlen(name)) == 0 && found < 0) found = i;
    }
    return found;
}

static void vote_end(Connections *c, const char *outcome)
{
    announce(c, "The vote %s", outcome);
    c->vote = (Vote){.kind = VOTE_NONE, .starter = -1};
    tell_vote(c, NULL);
}

// Counts the answers: passed on VOTE_PERCENT of the players, failed once it no longer can.
static void vote_check(Connections *c)
{
    if (c->vote.kind == VOTE_NONE) return;
    int players = connections_count(c), yes = 0, no = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined) continue;
        yes += c->vote.answer[i] == 1;
        no += c->vote.answer[i] == 2;
    }
    int needed = (players * VOTE_PERCENT + 99) / 100;
    if (needed < 1) needed = 1;
    if (yes >= needed) {
        Vote v = c->vote;
        if (v.kind == VOTE_MAP) {
            snprintf(c->vote_map, sizeof c->vote_map, "%s", v.target);
            vote_end(c, "passed: the next map");
        } else {
            vote_end(c, "passed: kicked");
            if (c->items[v.slot].joined) connections_kick(c, v.slot, "kicked by a vote");
        }
    } else if (players - no < needed) {
        vote_end(c, "did not pass");
    }
}

static void vote_tick(Connections *c)
{
    if (c->vote.kind == VOTE_NONE) return;
    if (--c->vote.ticks_left <= 0) vote_end(c, "ran out of time");
}

static void vote_start(Connections *c, int slot, VoteKind kind, const char *target, int target_slot)
{
    c->vote = (Vote){.kind = kind, .slot = target_slot, .starter = slot, .ticks_left = VOTE_TICKS};
    snprintf(c->vote.target, sizeof c->vote.target, "%s", target);
    c->vote.answer[slot] = 1;
    if (kind == VOTE_MAP) announce(c, "%s started a vote to change the map to %s: /yes or /no", c->items[slot].name, target);
    else announce(c, "%s started a vote to kick %s: /yes or /no", c->items[slot].name, target);
    tell_vote(c, NULL);
    vote_check(c);
}

// A command said in the chat: votemap <map>, votekick <player>, yes, no.
static void vote_command(Connections *c, const Game *g, int slot, const char *text)
{
    (void)g;
    char word[NET_TEXT_SIZE];
    const char *rest = text;
    int n = 0;
    while (*rest && *rest != ' ' && n < (int)sizeof word - 1) word[n++] = *rest++;
    word[n] = '\0';
    while (*rest == ' ') rest++;

    if (strcmp(word, "votemap") == 0 || strcmp(word, "votekick") == 0) {
        if (c->vote.kind != VOTE_NONE) {
            tell(c, slot, "A vote is already on");
            return;
        }
        if (strcmp(word, "votemap") == 0) {
            if (!map_exists(c, rest)) {
                tell(c, slot, "No such map: %s", rest);
                return;
            }
            vote_start(c, slot, VOTE_MAP, rest, -1);
        } else {
            int target = player_named(c, rest);
            if (target < 0) {
                tell(c, slot, "No such player: %s", rest);
                return;
            }
            vote_start(c, slot, VOTE_KICK, c->items[target].name, target);
        }
    } else if (strcmp(word, "yes") == 0 || strcmp(word, "no") == 0) {
        if (c->vote.kind == VOTE_NONE) {
            tell(c, slot, "No vote is on");
            return;
        }
        c->vote.answer[slot] = strcmp(word, "yes") == 0 ? 1 : 2;
        vote_check(c);
    } else {
        tell(c, slot, "Unknown command: /%s", word);
    }
}

bool connections_take_vote_map(Connections *c, char *map, size_t size)
{
    if (!c->vote_map[0]) return false;
    snprintf(map, size, "%s", c->vote_map);
    c->vote_map[0] = '\0';
    return true;
}

void connections_say(Connections *c, const char *text)
{
    MsgChat m = {.slot = MAX_PLAYERS};
    snprintf(m.text, sizeof m.text, "%s", text);
    say(c->console, "*SERVER*: %s\n", text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) connections_broadcast(c, MSG_CHAT, buf, n);
}

void connections_kick(Connections *c, int slot, const char *reason)
{
    Connection *conn = &c->items[slot];
    if (!conn->peer) return;
    say(c->console, "%s kicked: %s\n", conn->name, reason);
    deny(c, conn->peer, reason);
    enet_peer_disconnect_later(conn->peer, 0); // the Denied goes first; the leave frees the slot
}
