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
    *c = (Connections){.link = link, .console = console, .round = 1, .vote = {.starter = -1}, .vote_percent = VOTE_PERCENT_DEFAULT, .flood_packets = FLOOD_PACKETS_DEFAULT, .flood_warnings_max = FLOOD_WARNINGS_DEFAULT};
    for (int i = 0; i < MAX_PLAYERS; i++) c->vote_cooldown[i] = -1;
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
static void route_map_change(NetBuf *b, void *m) { msg_map_change(b, m); }
static void route_map_reply(NetBuf *b, void *m) { msg_map_reply(b, m); }

// The round's end to one peer, or (NULL) everyone: the map coming and the ticks until it.
static void tell_map_change(Connections *c, ENetPeer *peer, const Game *g)
{
    uint8_t buf[NET_MTU];
    MsgMapChange m = {.counter = (uint16_t)(g->match.counter > 0 ? g->match.counter : 0)};
    snprintf(m.map, sizeof m.map, "%s", c->next_map);
    size_t n = build(buf, sizeof buf, MSG_MAP_CHANGE, route_map_change, &m);
    if (!n) return;
    if (peer) net_send(peer, MSG_MAP_CHANGE, buf, n);
    else connections_broadcast(c, MSG_MAP_CHANGE, buf, n);
}

void connections_map_change(Connections *c, const Game *g, const char *map)
{
    snprintf(c->next_map, sizeof c->next_map, "%s", map);
    tell_map_change(c, NULL, g);
}

// The ban on this address, or NULL.
static const Ban *banned(const Connections *c, uint32_t host)
{
    for (int i = 0; i < MAX_BANS; i++)
        if (c->bans[i].host == host && c->bans[i].host != 0 && c->bans[i].until > c->ticks) return &c->bans[i];
    return NULL;
}

void connections_ban(Connections *c, int slot, uint32_t ticks, const char *reason)
{
    const Connection *conn = &c->items[slot];
    if (!conn->peer) return;
    Ban *b = NULL;
    for (int i = 0; i < MAX_BANS && !b; i++) // an empty place, or one whose ban has lifted
        if (c->bans[i].host == 0 || c->bans[i].until <= c->ticks) b = &c->bans[i];
    if (!b) b = &c->bans[0];
    *b = (Ban){.host = conn->peer->address.host, .until = c->ticks + ticks};
    snprintf(b->reason, sizeof b->reason, "%s", reason);
}

// The round's map to one peer.
static void tell_map(Connections *c, ENetPeer *peer)
{
    uint8_t buf[NET_MTU];
    MsgMap m = {.round = c->round, .hook = c->hook, .hook_tuning = c->hook_tuning};
    snprintf(m.map, sizeof m.map, "%s", c->map);
    snprintf(m.hostname, sizeof m.hostname, "%s", c->hostname);
    size_t n = build(buf, sizeof buf, MSG_MAP, route_map, &m);
    if (n) net_send(peer, MSG_MAP, buf, n);
}

// A player's soldier placed anew on its team: a join, a new round.
static void place(Connections *c, Game *g, int slot);
static void announce(Connections *c, ChatKind kind, const char *fmt, ...);

// Who came, as the original's client says it: by the team it came to.
static void announce_join(Connections *c, const Game *g, int slot)
{
    const char *name = c->items[slot].name;
    switch (g->world.soldiers[slot].team) {
    case TEAM_ALPHA: announce(c, CHAT_ALPHA, "%s has joined alpha team", name); break;
    case TEAM_BRAVO: announce(c, CHAT_BRAVO, "%s has joined bravo team", name); break;
    case TEAM_SPECTATOR: announce(c, CHAT_SPECTATOR, "%s has joined as spectator", name); break;
    default: announce(c, CHAT_ENTER, "%s has joined the game", name); break;
    }
}

// The vote as it stands, to one peer or (NULL) everyone: for the HUD.
static void tell_vote(Connections *c, ENetPeer *peer)
{
    MsgVote m = {.kind = c->vote.kind, .seconds = (uint16_t)((c->vote.ticks_left + TICK_RATE - 1) / TICK_RATE)};
    snprintf(m.target, sizeof m.target, "%s", c->vote.target);
    snprintf(m.starter, sizeof m.starter, "%s", c->vote.starter >= 0 ? c->items[c->vote.starter].name : "");
    snprintf(m.reason, sizeof m.reason, "%s", c->vote.reason);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_VOTE, route_vote, &m);
    if (!n) return;
    if (peer) net_send(peer, MSG_VOTE, buf, n);
    else connections_broadcast(c, MSG_VOTE, buf, n);
}

// A line from the server itself to everyone, of a kind the client colours: who came,
// who went, a vote.
static void announce(Connections *c, ChatKind kind, const char *fmt, ...)
{
    MsgChat m = {.slot = MAX_PLAYERS, .kind = (uint8_t)kind};
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
    if (c->password[0] && strcmp(c->password, m.password) != 0) {
        deny(c, peer, "wrong password");
        return;
    }
    if (slot_of(c, peer) >= 0) return; // said hello twice
    const Ban *ban = banned(c, peer->address.host);
    if (ban) {
        char reason[NET_TEXT_SIZE];
        snprintf(reason, sizeof reason, "You have been banned on this server. Reason: %s", ban->reason);
        deny(c, peer, reason);
        return;
    }
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
    s->gear = m.gear;
    s->primary_choice = m.primary;
    s->secondary_choice = m.secondary;
    s->kills = s->deaths = s->flags = 0; // the slot's last occupant's tally is not its
    s->bot = false;
    place(c, g, slot);

    uint8_t buf[NET_MTU];
    MsgWelcome w = {.slot = (uint8_t)slot, .tick = g->world.tick};
    size_t n = build(buf, sizeof buf, MSG_WELCOME, route_welcome, &w);
    if (n) net_send(peer, MSG_WELCOME, buf, n);
    tell_map(c, peer); // joining is hearing of the round
    if (g->match.state == MATCH_ENDED) tell_map_change(c, peer, g); // and of its end, if it is ending
    c->vote.answer[slot] = 0;
    c->vote_cooldown[slot] = VOTE_COOLDOWN_TICKS; // no votes for two minutes after joining
    if (c->vote.kind != VOTE_NONE) tell_vote(c, peer);
    say(c->console, "%s joined as %d\n", conn->name, slot);
    if (!match_has_teams(&g->match)) announce_join(c, g, slot); // with teams, once it has chosen one
    if (c->hooks && c->hooks->joined) c->hooks->joined(c->hooks->user, slot);
}

void connections_place(Connections *c, Game *g, int slot, Team team)
{
    Soldier *s = &g->world.soldiers[slot];
    if (s->active && s->held) things_let_go(&g->world, slot, NULL);
    // with the weapons it chose, or the original's first loadout for a choice that isn't one
    WeaponId primary = weapon_is_primary(s->primary_choice) ? s->primary_choice : WEAPON_EAGLE;
    WeaponId secondary = weapon_is_secondary(s->secondary_choice) ? s->secondary_choice : WEAPON_KNIFE;
    Gear gear = !g->world.rules.hook && s->gear == GEAR_HOOK ? GEAR_JETS : s->gear; // no hook in this game: the boots are jets
    if (team == TEAM_SPECTATOR) {
        // present and dead, never respawned: the simulation passes a spectator by
        soldier_spawn(&g->ctx, s, vec2(0, 0), TEAM_SPECTATOR, gear, primary, secondary);
        s->dead = true;
    } else {
        Vec2 at = spawn_point(g->ctx.map, team, &g->world.rng);
        soldier_spawn(&g->ctx, s, at, team, gear, primary, secondary);
    }
    s->life++;
    s->remote = !c->items[slot].bot; // a player's keys move it and it tells what it fires; a bot is played here
}

// The team a player is placed on: with teams, what it chose, and a spectator until it
// has; without, none, unless it chose to watch.
static void place(Connections *c, Game *g, int slot)
{
    const Connection *conn = &c->items[slot];
    Team team;
    if (match_has_teams(&g->match)) team = conn->chose_team ? conn->team : TEAM_SPECTATOR;
    else team = conn->chose_team && conn->team == TEAM_SPECTATOR ? TEAM_SPECTATOR : TEAM_NONE;
    connections_place(c, g, slot, team);
}

void connections_new_round(Connections *c, Game *g, const char *map)
{
    c->round++;
    snprintf(c->map, sizeof c->map, "%s", map);
    c->hook = g->world.rules.hook; // what the next map says of the hook, to every client
    c->hook_tuning = g->world.rules.hook_tuning;
    wire_queue_init(&c->events); // the old round's news is nobody's now
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (c->items[i].bot) {
            Soldier *s = &g->world.soldiers[i];
            s->kills = s->deaths = s->flags = 0; // as the players' are, made anew with the world
            place(c, g, i);
            continue;
        }
        if (!c->items[i].joined) continue;
        server_stream_init(&c->streams[i], c->round);
        place(c, g, i);
        tell_map(c, c->items[i].peer);
    }
    say(c->console, "round %u on %s\n", c->round, c->map);
}

// A soldier leaving the world, as the original's TSprite.Kill has it: the flag it
// carried falls where it is, and the stationary gun it manned is free for the next.
// Without this a leaver's flag stayed with nobody, held by a soldier no longer there,
// and its gun stayed manned by nobody.
static void soldier_leave(Game *g, int slot)
{
    Soldier *s = &g->world.soldiers[slot];
    if (!s->active) return;
    if (s->held) things_let_go(&g->world, (uint8_t)slot, NULL);
    if (s->stat) {
        g->world.things[s->stat - 1].is_static = false;
        s->stat = 0;
    }
    s->active = false;
}

static void vote_end(Connections *c, bool passed);

static void leave(Connections *c, Game *g, ENetPeer *peer)
{
    int slot = slot_of(c, peer);
    if (slot < 0) return;
    Connection *conn = &c->items[slot];
    Team team = g->world.soldiers[slot].team;
    if (conn->joined) {
        say(c->console, "%s left\n", conn->name);
        soldier_leave(g, slot);
    }
    // the target of a kick vote leaving before it is decided (NetworkServerConnection.pas):
    // barred five minutes, and the vote is over; else it ran on against the slot, and
    // whoever came into it next could be the one kicked
    if (c->vote.kind == VOTE_KICK && c->vote.slot == slot) {
        connections_ban(c, slot, VOTE_LEFT_BAN_TICKS, "Vote Kicked (Left game)");
        vote_end(c, false);
    }
    char name[NET_NAME_SIZE];
    snprintf(name, sizeof name, "%s", conn->name);
    bool joined = conn->joined;
    KickWhy why = conn->kick_why;
    *conn = (Connection){0};
    peer->data = NULL;
    if (!joined) return;
    // said as the original's client says a leaving (NetworkClientGame.pas): a kick by its
    // reason, else by the team left
    if (why == KICK_VOTED) announce(c, CHAT_CLIENT, "%s has been voted to leave the game", name);
    else if (why == KICK_CONSOLE) announce(c, CHAT_CLIENT, "%s has been kicked from console", name);
    else if (why == KICK_FLOODING) announce(c, CHAT_CLIENT, "%s has been kicked for flooding", name);
    else if (team == TEAM_ALPHA) announce(c, CHAT_ALPHA, "%s has left alpha team", name);
    else if (team == TEAM_BRAVO) announce(c, CHAT_BRAVO, "%s has left bravo team", name);
    else if (team == TEAM_SPECTATOR) announce(c, CHAT_SPECTATOR, "%s has left spectators", name);
    else announce(c, CHAT_ENTER, "%s has left the game", name);
    if (c->hooks && c->hooks->left) c->hooks->left(c->hooks->user, slot, name);
}

static void vote_command(Connections *c, Game *g, int slot, const char *text);

static void chat(Connections *c, Game *g, ENetPeer *peer, const NetEvent *e)
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
    c->items[slot].chat_warnings++;
    // a script hears it first, and may keep it
    if (c->hooks && c->hooks->chat && c->hooks->chat(c->hooks->user, slot, m.text, m.team)) return;
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

// The map window asks for the n-th of the server's maps: its name, and how many there
// are (the original's VoteMapReply). Nothing is answered past the end.
static void map_query(Connections *c, ENetPeer *peer, const NetEvent *e)
{
    NetBuf b = netbuf_reader(e->data, e->size);
    MsgKind kind;
    MsgMapQuery q = {0};
    msg_kind(&b, &kind);
    msg_map_query(&b, &q);
    if (!netbuf_done(&b)) return;
    MsgMapReply m = {.index = q.index};
    if (c->maps) {
        if (q.index >= c->map_count) return;
        m.count = (uint16_t)c->map_count;
        snprintf(m.map, sizeof m.map, "%s", c->maps[q.index]);
    } else { // no list: the map being played is the whole of it
        if (q.index > 0) return;
        m.count = 1;
        snprintf(m.map, sizeof m.map, "%s", c->map);
    }
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_MAP_REPLY, route_map_reply, &m);
    if (n) net_send(peer, MSG_MAP_REPLY, buf, n);
}

void connections_set_password(Connections *c, const char *password)
{
    snprintf(c->password, sizeof c->password, "%s", password ? password : "");
}

void connections_broadcast(Connections *c, MsgKind kind, const uint8_t *data, size_t size)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->items[i].joined) net_send(c->items[i].peer, kind, data, size);
}

void connections_poll(Connections *c, Game *g)
{
    // everyone's round trip, for the HUDs, in the served half
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->items[i].joined && c->items[i].peer) g->world.soldiers[i].ping = (uint16_t)(c->items[i].peer->roundTripTime > 65535 ? 65535 : c->items[i].peer->roundTripTime);
    NetEvent e;
    while (net_poll(c->link, &e, 0) != NET_EVENT_NONE) {
        switch (e.kind) {
        case NET_EVENT_CONNECT: break; // nobody until its Hello
        case NET_EVENT_DISCONNECT: leave(c, g, e.peer); break;
        case NET_EVENT_MESSAGE: {
            int slot = slot_of(c, e.peer);
            if (slot >= 0) c->items[slot].messages++;
            if (e.msg == MSG_HELLO) hello(c, g, e.peer, &e);
            else if (slot < 0) deny(c, e.peer, "no Hello first"); // the rest is for players
            else if (e.msg == MSG_CHAT) chat(c, g, e.peer, &e);
            else if (e.msg == MSG_CLIENT_STATE) server_stream_receive(&c->streams[slot], g, slot, e.data, e.size);
            else if (e.msg == MSG_MAP_QUERY) map_query(c, e.peer, &e);
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
static void flood_tick(Connections *c);

void connections_snapshots(Connections *c, const Game *g)
{
    c->ticks++;
    vote_tick(c);
    flood_tick(c);
    wire_collect(&c->events, &g->events, g->world.tick - 1, -1); // the tick just run
    char names[MAX_PLAYERS][NET_NAME_SIZE];
    for (int i = 0; i < MAX_PLAYERS; i++)
        snprintf(names[i], NET_NAME_SIZE, "%s", c->items[i].joined || c->items[i].bot ? c->items[i].name : "");
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
    MsgChat m = {.slot = MAX_PLAYERS, .kind = CHAT_SERVER}; // "*SERVER*: ", as ServerSendStringMessage from 255
    va_list args;
    va_start(args, fmt);
    vsnprintf(m.text, sizeof m.text, fmt, args);
    va_end(args);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) net_send(c->items[slot].peer, MSG_CHAT, buf, n);
}

// Whether the server knows `map`: in its list (the original's MapsList), or, with no
// list, as a file under maps_dir.
static bool map_exists(const Connections *c, const char *map)
{
    if (c->maps) {
        for (int i = 0; i < c->map_count; i++)
            if (strcmp(c->maps[i], map) == 0) return true;
        return false;
    }
    if (!c->maps_dir[0]) return true;
    if (!map[0] || strchr(map, '/') || strchr(map, '\\') || strstr(map, "..")) return false;
    char path[600];
    snprintf(path, sizeof path, "%s/%s.pms", c->maps_dir, map);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The player a kick names, a bot among them as in the original: a slot's number, or a
// name, whole or as much of it as typed; nobody for nothing.
static int player_named(const Connections *c, const char *name)
{
    if (!name[0]) return -1;
    char *end;
    long slot = strtol(name, &end, 10);
    if (*end == '\0' && end != name)
        return slot >= 0 && slot < MAX_PLAYERS && (c->items[slot].joined || c->items[slot].bot) ? (int)slot : -1;
    int found = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!c->items[i].joined && !c->items[i].bot) continue;
        if (strcmp(c->items[i].name, name) == 0) return i;
        if (strncmp(c->items[i].name, name, strlen(name)) == 0 && found < 0) found = i;
    }
    return found;
}

// Over (StopVote): the original says nothing of a kick that ran out, and of a map vote
// that did, that no map was voted; a pass shows as the kick or the next map.
static void vote_end(Connections *c, bool passed)
{
    if (c->vote.kind == VOTE_MAP && !passed) announce(c, CHAT_VOTE, "No map has been voted");
    c->vote = (Vote){.kind = VOTE_NONE, .starter = -1};
    tell_vote(c, NULL);
}

// A yes from `slot` (CountVote): once each; passed when the yeses reach sv_votepercent
// of the players there were when the vote began. A kick passed puts the player off for
// an hour; a map passed is the server's to play next.
static void vote_count(Connections *c, Game *g, int slot)
{
    if (c->vote.kind == VOTE_NONE || c->vote.answer[slot]) return;
    c->vote.answer[slot] = 1;
    int yes = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) yes += c->vote.answer[i] == 1;
    int max = c->vote.max_votes > 0 ? c->vote.max_votes : 1;
    if ((float)yes / (float)max < (float)c->vote_percent / 100.0f) return;
    Vote v = c->vote;
    if (v.kind == VOTE_MAP) {
        snprintf(c->vote_map, sizeof c->vote_map, "%s", v.target);
    } else if (c->items[v.slot].joined) {
        c->items[v.slot].kick_why = KICK_VOTED;
        connections_ban(c, v.slot, VOTE_KICK_BAN_TICKS, "Vote Kicked");
        connections_kick(c, v.slot, "Vote Kicked");
    }
    vote_end(c, true);
    if (v.kind == VOTE_KICK && c->items[v.slot].bot) connections_remove_bot(c, g, v.slot); // a bot is taken off
}

// Each tick (TimerVote): the vote runs out, and the cooldowns run down.
static void vote_tick(Connections *c)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (c->vote_cooldown[i] > -1) c->vote_cooldown[i]--;
    if (c->vote.kind == VOTE_NONE) return;
    if (--c->vote.ticks_left <= 0) vote_end(c, false);
}

// StartVote: the players on now, people alone, are the votes there are to gather; the
// starter may not start another for two minutes. Nobody has voted by starting it: the
// starter presses F12 as everyone does, kick or map, as the original plays (its client
// means to send a kick starter's yes, but reads the vote's kind before it is set).
static void vote_start(Connections *c, int slot, VoteKind kind, const char *target, int target_slot, const char *reason)
{
    c->vote = (Vote){.kind = kind, .slot = target_slot, .starter = slot, .ticks_left = VOTE_TICKS, .max_votes = connections_count(c)};
    snprintf(c->vote.target, sizeof c->vote.target, "%s", target);
    snprintf(c->vote.reason, sizeof c->vote.reason, "%s", reason ? reason : "");
    c->vote_cooldown[slot] = VOTE_COOLDOWN_TICKS;
    tell_vote(c, NULL); // the vote's box says who wants what; the original's console says nothing
    if (kind == VOTE_KICK) say(c->console, "%s started votekick against %s - Reason:%s\n", c->items[slot].name, target, c->vote.reason);
}

// /team <n>: the team menu's choice, the original's numbering: 0 to play with no teams,
// 1 alpha, 2 bravo, 5 to watch. The soldier is placed anew on it.
static void team_command(Connections *c, Game *g, int slot, const char *rest)
{
    char *end;
    long n = strtol(rest, &end, 10);
    bool teams = match_has_teams(&g->match);
    bool ok = end != rest && (n == TEAM_SPECTATOR || (teams ? n == TEAM_ALPHA || n == TEAM_BRAVO : n == TEAM_NONE));
    if (!ok) {
        tell(c, slot, teams ? "Teams: 1 alpha, 2 bravo, 5 spectator" : "Teams: 0 play, 5 spectator");
        return;
    }
    Connection *conn = &c->items[slot];
    Team team = (Team)n;
    if (conn->chose_team && conn->team == team) return; // already there
    conn->chose_team = true;
    conn->team = team;
    connections_place(c, g, slot, team);
    announce_join(c, g, slot);
}

// A command said in the chat: team <n>, votemap <map>, votekick <player> [reason], yes,
// no. The votes as the original's ServerHandleVoteKick and CommandVotemap take them.
static void vote_command(Connections *c, Game *g, int slot, const char *text)
{
    char word[NET_TEXT_SIZE];
    const char *rest = text;
    int n = 0;
    while (*rest && *rest != ' ' && n < (int)sizeof word - 1) word[n++] = *rest++;
    word[n] = '\0';
    while (*rest == ' ') rest++;

    if (strcmp(word, "team") == 0) {
        team_command(c, g, slot, rest);
    } else if (strcmp(word, "votemap") == 0) {
        // CommandVotemap: with a vote on, a yes to it if it is for this map; else a new one,
        // for a map the server has, by a player who may
        if (!rest[0]) return; // with nothing named, nothing
        if (c->vote.kind != VOTE_NONE) {
            if (c->vote.kind == VOTE_MAP && strcmp(c->vote.target, rest) == 0) vote_count(c, g, slot);
            return;
        }
        if (!map_exists(c, rest)) {
            tell(c, slot, "Map not found (%s)", rest);
            return;
        }
        if (c->vote_cooldown[slot] >= 0) {
            tell(c, slot, "Can't vote for 2:00 minutes after joining game or last vote");
            return;
        }
        vote_start(c, slot, VOTE_MAP, rest, -1, "---");
    } else if (strcmp(word, "votekick") == 0) {
        // ServerHandleVoteKick: a yes to the kick on, unless I am its target; else a new one,
        // quietly refused within the cooldown, and never against myself (the kick window's
        // button refuses it). The reason is the rest as said, past one space: the kick
        // window's keeps its leading space, as the original's box shows it ("Reason: afk").
        char who[NET_TEXT_SIZE];
        int k = 0;
        while (*rest && *rest != ' ' && k < (int)sizeof who - 1) who[k++] = *rest++;
        who[k] = '\0';
        if (*rest == ' ') rest++;
        int target = player_named(c, who);
        if (c->vote.kind != VOTE_NONE) {
            if (c->vote.kind != VOTE_KICK) return;
            if (c->vote.slot == slot) {
                tell(c, slot, "A vote has been cast against you. You can not vote.");
                return;
            }
            if (target == c->vote.slot) vote_count(c, g, slot);
            return;
        }
        if (c->vote_cooldown[slot] >= 0 || target == slot) return;
        if (target < 0) {
            if (who[0]) tell(c, slot, "No such player: %s", who);
            return;
        }
        vote_start(c, slot, VOTE_KICK, c->items[target].name, target, rest);
    } else if (strcmp(word, "yes") == 0) {
        // F12: a yes to the vote on, whatever it is for; its target may not
        if (c->vote.kind == VOTE_NONE) return;
        if (c->vote.kind == VOTE_KICK && c->vote.slot == slot) {
            tell(c, slot, "A vote has been cast against you. You can not vote.");
            return;
        }
        vote_count(c, g, slot);
    } else if (strcmp(word, "no") == 0) {
        // F11 is the voter's own business: the original's client only drops the box
    } else if (strcmp(word, "tabac") == 0 || strcmp(word, "smoke") == 0 || strcmp(word, "takeoff") == 0 ||
               strcmp(word, "victory") == 0 || strcmp(word, "piss") == 0 || strcmp(word, "mercy") == 0 || strcmp(word, "pwn") == 0) {
        // CommandPlayerCommand: the taunts, asked of the soldier's idle machine by their
        // number (Idle.random's); a mercy costs a kill
        static const char *const TAUNTS[] = {"tabac", "smoke", NULL, NULL, "takeoff", "victory", "piss", "mercy", "pwn"};
        Soldier *s = &g->world.soldiers[slot];
        if (!s->active || s->dead) return;
        for (int i = 0; i < (int)(sizeof TAUNTS / sizeof TAUNTS[0]); i++) {
            if (!TAUNTS[i] || strcmp(word, TAUNTS[i]) != 0) continue;
            s->antic = (int8_t)i;
            s->antic_seq++;
            if (i == 7 && s->kills > 0) s->kills--;
        }
    } else if (strcmp(word, "kill") == 0 || strcmp(word, "brutalkill") == 0) {
        // the original's: the vest off, a wound that kills (that tears apart, brutal), a kill fewer
        Soldier *s = &g->world.soldiers[slot];
        if (!s->active || s->dead) return;
        s->vest = 0.0f;
        float amount = word[0] == 'b' ? 3423.0f : 150.0f;
        game_hear(g, (Event){.type = EVENT_HIT, .hit = {.shooter = (uint8_t)slot, .target = (uint8_t)slot, .weapon = s->weapon.id, .amount = amount, .pos = s->pos}});
        if (s->kills > 0) s->kills--;
    } else {
        // a script's command, if it has one by that name
        if (c->hooks && c->hooks->command && c->hooks->command(c->hooks->user, slot, text)) return;
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

void connections_say_kind(Connections *c, ChatKind kind, Rgba color, const char *text)
{
    MsgChat m = {.slot = MAX_PLAYERS, .kind = (uint8_t)kind, .color = color};
    snprintf(m.text, sizeof m.text, "%s", text);
    say(c->console, "%s\n", m.text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) connections_broadcast(c, MSG_CHAT, buf, n);
}

void connections_say_to(Connections *c, int slot, ChatKind kind, Rgba color, const char *text)
{
    if (slot < 0 || slot >= MAX_PLAYERS || !c->items[slot].joined || !c->items[slot].peer) return;
    MsgChat m = {.slot = MAX_PLAYERS, .kind = (uint8_t)kind, .color = color};
    snprintf(m.text, sizeof m.text, "%s", text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) net_send(c->items[slot].peer, MSG_CHAT, buf, n);
}

void connections_kick(Connections *c, int slot, const char *reason)
{
    Connection *conn = &c->items[slot];
    if (!conn->peer) return;
    say(c->console, "%s kicked: %s\n", conn->name, reason);
    deny(c, conn->peer, reason);
    enet_peer_disconnect_later(conn->peer, 0); // the Denied goes first; the leave frees the slot
}

// --- bots ----------------------------------------------------------------------------

int connections_add_bot(Connections *c, Game *g, const char *name, PlayerLook look, WeaponId primary, WeaponId secondary,
                        Team team)
{
    int slot = free_slot(c, g);
    if (slot < 0) return -1;
    bool teams = match_has_teams(&g->match);
    if (teams && team != TEAM_ALPHA && team != TEAM_BRAVO) team = team_for(g);
    if (!teams) team = TEAM_NONE;

    Connection *conn = &c->items[slot];
    *conn = (Connection){.bot = true, .chose_team = true, .team = team};
    snprintf(conn->name, sizeof conn->name, "%s", name && name[0] ? name : "Bot");
    Soldier *s = &g->world.soldiers[slot];
    s->look = look;
    s->primary_choice = primary;
    s->secondary_choice = secondary;
    s->kills = s->deaths = s->flags = 0;
    s->bot = true;
    place(c, g, slot);
    say(c->console, "%s joined as %d (bot)\n", conn->name, slot);
    announce_join(c, g, slot);
    if (c->hooks && c->hooks->joined) c->hooks->joined(c->hooks->user, slot);
    return slot;
}

void connections_remove_bot(Connections *c, Game *g, int slot)
{
    Connection *conn = &c->items[slot];
    if (!conn->bot) return;
    Soldier *s = &g->world.soldiers[slot];
    Team team = s->team;
    soldier_leave(g, slot);
    s->bot = false;
    if (c->vote.kind == VOTE_KICK && c->vote.slot == slot) vote_end(c, false); // a kick vote against it is over
    char name[NET_NAME_SIZE];
    snprintf(name, sizeof name, "%s", conn->name);
    *conn = (Connection){0};
    say(c->console, "%s left (bot)\n", name);
    if (team == TEAM_ALPHA) announce(c, CHAT_ALPHA, "%s has left alpha team", name);
    else if (team == TEAM_BRAVO) announce(c, CHAT_BRAVO, "%s has left bravo team", name);
    else announce(c, CHAT_ENTER, "%s has left the game", name);
    if (c->hooks && c->hooks->left) c->hooks->left(c->hooks->user, slot, name);
}

void connections_say_as(Connections *c, int slot, const char *text)
{
    if (slot < 0 || slot >= MAX_PLAYERS || !text || !text[0]) return;
    MsgChat m = {.slot = (uint8_t)slot};
    snprintf(m.text, sizeof m.text, "%s", text);
    say(c->console, "%s: %s\n", c->items[slot].name, m.text);
    uint8_t buf[NET_MTU];
    size_t n = build(buf, sizeof buf, MSG_CHAT, route_chat, &m);
    if (n) connections_broadcast(c, MSG_CHAT, buf, n);
}

// --- flooding --------------------------------------------------------------------------

// Each tick (ServerLoop.pas): once a second, whoever was heard from more than the limit
// gets a warning and the count starts over, and the chat warnings drain one, kicking
// past five; every five minutes a flood warning is forgiven. A kick bars the address.
static void flood_tick(Connections *c)
{
    bool second = c->ticks % TICK_RATE == 0, five_minutes = c->ticks % FLOOD_FORGIVE_TICKS == 0;
    if (!second && !five_minutes) return;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Connection *conn = &c->items[i];
        if (!conn->peer || !conn->joined) continue;
        if (five_minutes && conn->flood_warnings > 0) conn->flood_warnings--;
        if (!second) continue;
        if (conn->messages > c->flood_packets) {
            say(c->console, "%s is flooding the server\n", conn->name);
            if (++conn->flood_warnings > c->flood_warnings_max) {
                conn->kick_why = KICK_FLOODING;
                connections_ban(c, i, FLOOD_BAN_TICKS, "Flood Kicked");
                connections_kick(c, i, "Flood Kicked");
                continue; // gone; the leave frees the slot
            }
        }
        conn->messages = 0;
        if (conn->chat_warnings > CHAT_FLOOD_WARNINGS) {
            conn->kick_why = KICK_FLOODING;
            connections_ban(c, i, CHAT_FLOOD_BAN_TICKS, "Chat Flood"); // twenty minutes is too harsh, says the original
            connections_kick(c, i, "Chat Flood");
            continue;
        }
        if (conn->chat_warnings > 0) conn->chat_warnings--;
    }
}
