// The two streams over the loopback: a client's soldier is where its client put it, a
// soldier the server runs is heard of and stepped on its keys, an old state is dropped,
// and the deltas stay small. Real sockets; a bad line is simulated outside these tests.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connections.h"
#include "rounds.h"
#include "test.h"

#define PORT 40024
#define ROUNDS 400 // of 10 ms
#define BOT 1      // the soldier the server plays itself
#define THING_TOLERANCE_TEST 10.0f // the client keeps its own things within this of the server's

typedef struct StreamClient {
    NetLink link;
    Game *game;
    ClientStream stream;
    int slot;
    bool welcomed;
    uint16_t round;
    char map[NET_MAP_SIZE];
    int snapshots;
    size_t state_bytes, snapshot_bytes; // the last of each
    int bot_shots, damages;
    int frags_of[MAX_PLAYERS]; // grenades seen thrown here, by thrower             // events heard: the bot's shots made here, wounds to me
} StreamClient;

// The client's world for a map: the same map as the server's, nobody in it, no authority.
static Game *client_world(const char *map)
{
    Game *g = scene(map, 60.0f, WEAPON_AK74, WEAPON_AK74);
    for (int i = 0; i < MAX_PLAYERS; i++) g->world.soldiers[i].active = false;
    g->world.authority = false;
    return g;
}

static void route_hello(NetBuf *b, void *m) { msg_hello(b, m); }
static void route_chat(NetBuf *b, void *m) { msg_chat(b, m); }

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
            MsgHello hello = {.version = NET_VERSION, .name = "Mover", .primary = WEAPON_EAGLE, .secondary = WEAPON_KNIFE};
            hello.look = (PlayerLook){.shirt = {9, 8, 7, 255}, .hair_style = 3, .head_style = 2, .chain_style = 1};
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
        } else if (e.kind == NET_EVENT_MESSAGE && e.msg == MSG_MAP) {
            // a round: the world made anew for its map, the streams from its start
            NetBuf b = netbuf_reader(e.data, e.size);
            MsgKind kind;
            MsgMap m = {0};
            msg_kind(&b, &kind);
            msg_map(&b, &m);
            if (!netbuf_done(&b)) continue;
            c->round = m.round;
            snprintf(c->map, sizeof c->map, "%s", m.map);
            if (c->game) scene_free(c->game);
            c->game = client_world(m.map);
            client_stream_reset(&c->stream, m.round);
        } else if (e.kind == NET_EVENT_MESSAGE && e.msg == MSG_SNAPSHOT && c->welcomed && c->round) {
            if (client_stream_hear(&c->stream, c->game, c->slot, e.data, e.size)) {
                c->snapshots++;
                c->snapshot_bytes = e.size;
            }
        }
    }
}

static bool aim_at_bot; // the client aims at the bot's chest instead of right

// One tick of the client: its soldier on `buttons`, aiming right; the others on their
// word. Then its state to the server.
static int client_interp = 2; // ticks the test's client keeps behind the newest snapshot

static void client_tick(StreamClient *c, Buttons buttons)
{
    World *w = &c->game->world;
    Command cmds[MAX_PLAYERS] = {0};
    client_stream_begin_tick(&c->stream, c->game, c->slot, client_interp);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        s->remote = i != c->slot;
        if (s->remote) cmds[i] = stream_command(s, client_stream_quiet(&c->stream, i));
    }
    Soldier *me = &w->soldiers[c->slot];
    const Soldier *bot = &w->soldiers[BOT];
    Vec2 aim = aim_at_bot ? vec2(bot->pos.x, bot->pos.y - 8.0f) : vec2(me->pos.x + 100.0f, me->pos.y);
    cmds[c->slot] = (Command){.seq = w->tick + 1, .buttons = buttons, .aim = aim};
    game_tick(c->game, cmds);
    for (int i = 0; i < c->game->events.count; i++) {
        const Event *e = &c->game->events.items[i];
        if (e->type == EVENT_BULLET_SPAWN && e->bullet_spawn.player == BOT) c->bot_shots++;
        if (e->type == EVENT_BULLET_SPAWN && e->bullet_spawn.weapon == WEAPON_FRAG) c->frags_of[e->bullet_spawn.player]++;
        if (e->type == EVENT_DAMAGE && e->damage.target == c->slot) c->damages++;
    }
    client_stream_collect(&c->stream, c->game, c->slot);
    if (me->active) {
        uint8_t buf[NET_MTU];
        size_t n = client_stream_state(&c->stream, me, buf, sizeof buf);
        if (n && net_send(c->link.peer, MSG_CLIENT_STATE, buf, n)) c->state_bytes = n;
    }
    net_flush(&c->link);
}

static int server_bullets_of_client; // bullets the server made for slot 0, all told
static int server_frags_of[MAX_PLAYERS]; // grenades the server made, by thrower
static int server_hits_by_client;    // hits the server ruled for slot 0's bullets

// One tick of the server: the players on their word, the bot on `bot_buttons`, aiming
// at the client's soldier's chest wherever it is.
static void server_tick(Connections *conns, Game *g, Buttons bot_buttons)
{
    Command cmds[MAX_PLAYERS] = {0};
    connections_commands(conns, g, cmds);
    const Soldier *target = &g->world.soldiers[0];
    cmds[BOT] = (Command){.seq = g->world.tick + 1, .buttons = bot_buttons, .aim = vec2(target->pos.x, target->pos.y - 8.0f)};
    game_tick(g, cmds);
    for (int i = 0; i < g->events.count; i++) {
        const Event *e = &g->events.items[i];
        if (e->type == EVENT_BULLET_SPAWN && e->bullet_spawn.player == 0) server_bullets_of_client++;
        if (e->type == EVENT_BULLET_SPAWN && e->bullet_spawn.weapon == WEAPON_FRAG) server_frags_of[e->bullet_spawn.player]++;
        if (e->type == EVENT_HIT && e->hit.shooter == 0) server_hits_by_client++;
    }
    connections_snapshots(conns, g);
    net_flush(conns->link);
}

// Both ends for `rounds` ticks, the client pressing `buttons`, the bot `bot_buttons`.
// A match over on the server begins the next round, on ctf_Ash.
static void play(Connections *conns, Game *g, StreamClient *c, int rounds, Buttons buttons, Buttons bot_buttons)
{
    for (int round = 0; round < rounds; round++) {
        connections_poll(conns, g);
        server_tick(conns, g, bot_buttons);
        if (match_over(&g->match)) round_start(g, conns, "assets", "ctf_Ash", MATCH_MODE_COUNT);
        client_pump(c);
        if (c->welcomed && c->round) client_tick(c, buttons);
        enet_host_service(conns->link->host, NULL, 10); // the wait; what arrives is dispatched next round
    }
}

// Bullets alive in a world, by owner.
static int bullets_of(const World *w, int owner)
{
    int n = 0;
    for (int i = 0; i < MAX_BULLETS; i++) n += w->bullets[i].active && w->bullets[i].owner == owner;
    return n;
}

void stream_tests(void)
{
    CHECK(net_init(), "ENet starts");

    // the server: slot 0 free for the client, the bot in slot 1, the history for the deltas
    Game *gs = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    gs->world.soldiers[0].active = false;
    gs->world.history = calloc(1, sizeof(History));
    NetLink server;
    CHECK(net_listen(&server, NULL, PORT, 8), "the server listens on %d", PORT);
    Connections conns;
    connections_init(&conns, &server, NULL, "Arena");

    // the client: no world until the Map says which
    StreamClient c = {.slot = -1};
    CHECK(client_stream_init(&c.stream), "the client's ring is made");
    CHECK(net_connect(&c.link, "127.0.0.1", PORT), "the client connects");

    // the join, and the first snapshot
    for (int round = 0; round < ROUNDS && c.stream.applied == 0; round++) play(&conns, gs, &c, 1, 0, 0);
    CHECK(c.welcomed && c.slot == 0, "the client is welcomed into slot 0");
    CHECK(c.round == 1 && strcmp(c.map, "Arena") == 0 && c.game, "and told the round: %u on %s", c.round, c.map);
    Soldier *mine = &c.game->world.soldiers[0], *theirs = &gs->world.soldiers[0];
    CHECK(c.snapshots > 0 && mine->active && !mine->remote && mine->life == theirs->life && fabsf(mine->pos.x - theirs->pos.x) < 1.0f,
          "the first snapshot places my soldier where the server spawned it, on the life it gave (%d snapshots)", c.snapshots);
    CHECK(c.game->world.soldiers[BOT].active && c.game->world.soldiers[BOT].remote,
          "and brings the bot, heard of and not played here");

    // a second of running right, the bot walking left
    float start = mine->pos.x;
    play(&conns, gs, &c, 60, BUTTON_RIGHT, BUTTON_LEFT);
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

    // The kits that came up meanwhile may still be falling, and a thing in motion goes
    // whole, so the size depends on the tick the clock has reached: three seconds standing
    // still first, for everything to come to rest.
    play(&conns, gs, &c, 180, 0, 0);

    // the deltas
    CHECK(c.state_bytes > 0 && c.state_bytes < 60, "a tick's client state is small (%zu bytes)", c.state_bytes);
    CHECK(c.snapshot_bytes > 0 && c.snapshot_bytes < 200, "and so is a snapshot of two soldiers and the map's things (%zu bytes)",
          c.snapshot_bytes);

    // the things, the match and the roster came with the snapshots
    int things_there = 0, things_here = 0, things_agree = 0;
    float thing_off = 0.0f;
    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing *there = &gs->world.things[i], *here = &c.game->world.things[i];
        things_there += there->style != THING_NONE;
        things_here += here->style != THING_NONE;
        if (there->style != THING_NONE && there->style == here->style) {
            things_agree++;
            float d = vec2_length(vec2_sub(there->pos[0], here->pos[0]));
            if (d > thing_off) thing_off = d;
        }
    }
    CHECK(things_there > 0 && things_here == things_there && things_agree == things_there,
          "the map's things are here as they are there (%d there, %d here, %d agree)", things_there, things_here, things_agree);
    CHECK(thing_off <= THING_TOLERANCE_TEST, "and lie within the tolerance of where the server has them (%.1f at most)", thing_off);
    CHECK(c.game->match.time_left > 0 && gs->match.time_left - c.game->match.time_left <= 2 && c.game->match.state == gs->match.state,
          "the match's clock is the server's, a tick behind at most (%d there, %d here)", gs->match.time_left, c.game->match.time_left);
    CHECK(strcmp(c.stream.names[0], "Mover") == 0, "my name came with my soldier (%s)", c.stream.names[0]);

    // Everyone to the open ground the scenes shoot on (an alpha spawn), the bot a hundred
    // units left of me, whatever spawn the server gave a player with no team; my soldier
    // on both ends, as my word about where I am is the one that holds.
    uint64_t spot_rng = 7;
    Vec2 spot = spawn_point(gs->ctx.map, TEAM_ALPHA, &spot_rng);
    place(mine, spot);
    place(theirs, spot);
    place(&gs->world.soldiers[BOT], vec2(spot.x - 100.0f, spot.y));
    // the spawn protection wears off, then shots: mine to the server, the bot's to me, as
    // events; a few quiet ticks after each so the last packet lands before the count
    play(&conns, gs, &c, 45, 0, 0);
    uint32_t shots_before = theirs->shot_count;
    play(&conns, gs, &c, 12, BUTTON_FIRE, 0);
    play(&conns, gs, &c, 5, 0, 0);
    CHECK(theirs->shot_count > shots_before && theirs->shot_count == mine->shot_count,
          "my shots reach the server as events, numbered as mine (%u there, %u here)", theirs->shot_count, mine->shot_count);
    CHECK(server_bullets_of_client > 0, "and the server makes the bullets (%d)", server_bullets_of_client);
    (void)bullets_of;
    int heard_before = c.bot_shots;
    play(&conns, gs, &c, 12, 0, BUTTON_FIRE);
    play(&conns, gs, &c, 5, 0, 0);
    {
        // every part of a snapshot of a played world writes: which would not, if one
        // wouldn't. A width too small stalls the stream, which once looked like culling.
        uint8_t big[8192];
        NetBuf b = netbuf_writer(big, sizeof big);
        for (int i = 0; i < MAX_PLAYERS && netbuf_ok(&b); i++) {
            if (!gs->world.soldiers[i].active) continue;
            netfields_serialize(&b, SOLDIER_SERVED_FIELDS, SOLDIER_SERVED_COUNT, &gs->world.soldiers[i], NULL);
            CHECK(netbuf_ok(&b), "soldier %d's served half fits its widths", i);
            netfields_serialize(&b, SOLDIER_OWNED_FIELDS, SOLDIER_OWNED_COUNT, &gs->world.soldiers[i], NULL);
            CHECK(netbuf_ok(&b), "soldier %d's owned half fits its widths (jets %d, frames %d/%d, ammo %d/%d, counts %d/%d)", i,
                  gs->world.soldiers[i].jets, gs->world.soldiers[i].legs.frame, gs->world.soldiers[i].body.frame,
                  gs->world.soldiers[i].weapon.ammo, gs->world.soldiers[i].secondary.ammo, gs->world.soldiers[i].weapon.fire_count,
                  gs->world.soldiers[i].weapon.reload_count);
        }
        for (int i = 0; i < MAX_THINGS && netbuf_ok(&b); i++) {
            if (gs->world.things[i].style == THING_NONE) continue;
            netfields_serialize(&b, THING_FIELDS, THING_COUNT, &gs->world.things[i], NULL);
            CHECK(netbuf_ok(&b), "thing %d (style %d) fits its widths (timeout %d, interest %d, ammo %d)", i, gs->world.things[i].style,
                  gs->world.things[i].timeout, gs->world.things[i].interest, gs->world.things[i].ammo);
        }
        netfields_serialize(&b, MATCH_FIELDS, MATCH_COUNT, &gs->match, NULL);
        CHECK(netbuf_ok(&b), "the match fits its widths");
        wire_write(&b, &conns.events, 0, 0, WIRE_PER_PACKET);
        CHECK(netbuf_ok(&b), "the pending events write (%u..%u)", conns.events.first, conns.events.next);
        CHECK(conns.streams[0].unwritable == 0, "and no snapshot went unwritten (%u did)", conns.streams[0].unwritable);
    }
    CHECK(c.bot_shots > heard_before && c.game->world.soldiers[BOT].shot_count == gs->world.soldiers[BOT].shot_count,
          "the bot's shots are heard here and made, its count in step (%d made, %u/%u; queue %u..%u, server thinks acked %u, client "
          "applied %u)",
          c.bot_shots, c.game->world.soldiers[BOT].shot_count, gs->world.soldiers[BOT].shot_count, conns.events.first,
          conns.events.next, conns.streams[0].event_ack, c.stream.event_last);

    // wounds: the bot fires at me for a while; the server rules and I hear
    float health_before = mine->health;
    play(&conns, gs, &c, 90, 0, BUTTON_FIRE);
    CHECK(mine->dead && mine->respawn_counter > 0 && abs(mine->respawn_counter - theirs->respawn_counter) <= 2,
          "the volley killed me, and the respawn count reaches me with the served half (%d here, %d there)", mine->respawn_counter,
          theirs->respawn_counter);
    CHECK(c.damages > 0 && mine->health < health_before,
          "the server's wounds reach me as events, and my health with the served half (%d wounds, %.0f -> %.0f)", c.damages,
          health_before, mine->health);
    CHECK(c.stream.dropped == 0 && conns.streams[0].dropped == 0, "still nothing dropped either way");

    // and mine wound the bot: my shots, heard as events, are the server's bullets, which
    // meet its soldiers as any others do
    play(&conns, gs, &c, DEFAULT_RESPAWN_TIME + 60, 0, 0); // the volley killed me: my new life, and its protection over
    CHECK(!gs->world.soldiers[0].dead && !c.game->world.soldiers[0].dead && c.game->world.soldiers[0].life == gs->world.soldiers[0].life,
          "I respawn on the server and take the new life here (life %u/%u)", c.game->world.soldiers[0].life, gs->world.soldiers[0].life);
    aim_at_bot = true;
    float bot_health_before = gs->world.soldiers[BOT].health;
    int bot_deaths_before = gs->world.soldiers[BOT].deaths;
    play(&conns, gs, &c, 120, BUTTON_FIRE, 0);
    aim_at_bot = false;
    CHECK(server_hits_by_client > 0 && (gs->world.soldiers[BOT].health < bot_health_before || gs->world.soldiers[BOT].deaths > bot_deaths_before),
          "my shots wound the bot on the server (%d hits, health %.0f -> %.0f, deaths %d -> %d)", server_hits_by_client, bot_health_before,
          gs->world.soldiers[BOT].health, bot_deaths_before, gs->world.soldiers[BOT].deaths);

    // a grenade: the key held, then let go; one goes, here and on the server
    mine->grenades = 3;
    play(&conns, gs, &c, 20, BUTTON_THROW, 0);
    CHECK(theirs->body.id == ANIM_THROW && mine->body.id == ANIM_THROW && abs(theirs->body.frame - mine->body.frame) <= 3,
          "the wind-up shows on the server as here, its frame in step (%d/%d here, %d/%d there)", mine->body.id, mine->body.frame,
          theirs->body.id, theirs->body.frame);
    play(&conns, gs, &c, 60, 0, 0);
    CHECK(c.frags_of[0] == 1 && server_frags_of[0] == 1 && mine->grenades == 2 && gs->world.soldiers[0].grenades == 2,
          "one release throws one grenade here and one on the server (%d here, %d there; %d left, %d there)", c.frags_of[0],
          server_frags_of[0], mine->grenades, gs->world.soldiers[0].grenades);

    // an old state is dropped
    uint32_t dropped = conns.streams[0].dropped;
    uint8_t buf[NET_MTU];
    NetBuf b = netbuf_writer(buf, sizeof buf);
    MsgKind kind = MSG_CLIENT_STATE;
    MsgClientState old = {.seq = 1, .owned = *mine};
    msg_kind(&b, &kind);
    msg_client_state(&b, &old, NULL);
    wire_write(&b, &c.stream.out, c.stream.event_ack, -1, WIRE_PER_PACKET);
    net_send(c.link.peer, MSG_CLIENT_STATE, buf, netbuf_bytes(&b));
    net_flush(&c.link);
    play(&conns, gs, &c, 5, 0, 0);
    CHECK(conns.streams[0].dropped == dropped + 1, "a state older than the newest is dropped (%u dropped)", conns.streams[0].dropped);

    // a second client joins while the first plays: it is placed, and the first sees it
    StreamClient d = {.slot = -1};
    CHECK(client_stream_init(&d.stream) && net_connect(&d.link, "127.0.0.1", PORT), "a second client connects");
    for (int round = 0; round < ROUNDS && d.snapshots < 3; round++) {
        connections_poll(&conns, gs);
        server_tick(&conns, gs, 0);
        client_pump(&c);
        if (c.welcomed && c.round) client_tick(&c, 0);
        client_pump(&d);
        if (d.welcomed && d.round) client_tick(&d, BUTTON_LEFT);
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(d.welcomed && d.slot == 2 && d.round == 1 && d.game, "it is welcomed into slot 2 and told the round (slot %d, %d snapshots)",
          d.slot, d.snapshots);
    const Soldier *second_there = &gs->world.soldiers[2];
    const Soldier *second_here = d.game ? &d.game->world.soldiers[2] : NULL;
    CHECK(second_there->active && second_here && second_here->active && second_here->life == second_there->life &&
              fabsf(second_here->pos.x - second_there->pos.x) < 30.0f,
          "its soldier is placed on both ends (%d/%d active, life %u/%u)", second_there->active, second_here ? second_here->active : 0,
          second_there->life, second_here ? second_here->life : 0);
    // five seconds of both running about and shooting: nobody is ever without word, and
    // nobody's picture of the other ever jumps farther in a tick than a soldier can move
    // (a new life is a placing, which is a jump by design, and is left out)
    float jump_on_c = 0.0f, jump_on_d = 0.0f;
    size_t steady = 0; // the largest snapshot once the join's whole ones are past
    const Soldier *c_sees = &c.game->world.soldiers[2], *d_sees = &d.game->world.soldiers[0];
    Vec2 was_on_c = c_sees->pos, was_on_d = d_sees->pos;
    uint8_t life_on_c = c_sees->life, life_on_d = d_sees->life;
    uint32_t held_before = c.stream.held_back + d.stream.held_back;
    for (int round = 0; round < 300; round++) {
        Buttons c_keys = (round / 40) % 2 ? BUTTON_RIGHT | BUTTON_FIRE : BUTTON_LEFT | BUTTON_JUMP;
        Buttons d_keys = (round / 30) % 2 ? BUTTON_LEFT | BUTTON_FIRE : BUTTON_RIGHT | BUTTON_JET;
        connections_poll(&conns, gs);
        server_tick(&conns, gs, 0);
        client_pump(&c);
        client_tick(&c, c_keys);
        client_pump(&d);
        client_tick(&d, d_keys);
        if (round > 5 && c_sees->life == life_on_c) jump_on_c = fmaxf(jump_on_c, vec2_length(vec2_sub(c_sees->pos, was_on_c)));
        if (round > 5 && d_sees->life == life_on_d) jump_on_d = fmaxf(jump_on_d, vec2_length(vec2_sub(d_sees->pos, was_on_d)));
        if (round > 60 && c.snapshot_bytes > steady) steady = c.snapshot_bytes;
        if (round > 60 && d.snapshot_bytes > steady) steady = d.snapshot_bytes;
        was_on_c = c_sees->pos;
        was_on_d = d_sees->pos;
        life_on_c = c_sees->life;
        life_on_d = d_sees->life;
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(c.stream.held_back + d.stream.held_back == held_before, "no soldier was held back from any snapshot (%u times)",
          c.stream.held_back + d.stream.held_back - held_before);
    CHECK(jump_on_c < 2.0f * MAX_VELOCITY && jump_on_d < 2.0f * MAX_VELOCITY,
          "neither client's picture of the other jumps more than a soldier can move in a tick (%.1f, %.1f at most)", jump_on_c,
          jump_on_d);
    CHECK(steady < NET_MTU / 3, "and the steady snapshots stay far from the datagram's size (%zu bytes at largest, %zu the join's)",
          steady, c.stream.largest);

    // the second client's look, said in its Hello, reached the first through the server, and
    // its weapon choices, made after, reach the server through its state
    d.game->world.soldiers[2].look.shirt.r = 1; // changed after the join: the others don't hear
    d.game->world.soldiers[2].primary_choice = WEAPON_BARRETT;
    d.game->world.soldiers[2].secondary_choice = WEAPON_LAW;
    for (int round = 0; round < 6; round++) {
        connections_poll(&conns, gs);
        server_tick(&conns, gs, 0);
        client_pump(&c);
        client_tick(&c, 0);
        client_pump(&d);
        client_tick(&d, 0);
        enet_host_service(server.host, NULL, 10);
    }
    const PlayerLook *seen_look = &c.game->world.soldiers[2].look;
    CHECK(seen_look->shirt.r == 9 && seen_look->hair_style == 3 && seen_look->head_style == 2 && seen_look->chain_style == 1,
          "a player's look, from its Hello, reaches the others (shirt %u, hair %u, head %u, chain %u)", seen_look->shirt.r,
          seen_look->hair_style, seen_look->head_style, seen_look->chain_style);
    CHECK(gs->world.soldiers[2].primary_choice == WEAPON_BARRETT && gs->world.soldiers[2].secondary_choice == WEAPON_LAW,
          "and its weapon choices reach the server for its next spawn");
    CHECK(d.game->world.soldiers[2].look.shirt.r == 1 && d.game->world.soldiers[2].primary_choice == WEAPON_BARRETT,
          "while the client keeps its own over the server's word of them");
    d.game->world.soldiers[2].typing = true; // at the prompt: the dots over its head reach the others
    for (int round = 0; round < 6; round++) {
        connections_poll(&conns, gs);
        server_tick(&conns, gs, 0);
        client_pump(&c);
        client_tick(&c, 0);
        client_pump(&d);
        client_tick(&d, 0);
        enet_host_service(server.host, NULL, 10);
    }
    CHECK(c.game->world.soldiers[2].typing, "a player typing is seen typing by the others");

    // a correction of another player goes to the picture and is smoothed away
    c.stream.blend[2] = vec2(20.0f, 0.0f);
    client_stream_smooth(&c.stream, 0.1f, 0.1f);
    CHECK(c.stream.blend[2].x > 1.5f && c.stream.blend[2].x < 2.5f, "nine tenths of a correction is gone after cl_smooth (%.2f left of 20)",
          c.stream.blend[2].x);
    client_stream_smooth(&c.stream, 1.0f, 0.1f);
    CHECK(c.stream.blend[2].x == 0.0f, "and all of it a while after");
    c.stream.blend[2] = vec2(20.0f, 0.0f);
    client_stream_smooth(&c.stream, 0.016f, 0.0f);
    CHECK(c.stream.blend[2].x == 0.0f, "with cl_smooth 0 it snaps");
    const Soldier *second_seen = &c.game->world.soldiers[2];
    CHECK(second_seen->active && second_seen->remote && fabsf(second_seen->pos.x - second_there->pos.x) < 30.0f &&
              strcmp(c.stream.names[2], "Mover") == 0,
          "and the first client sees it, named, near where the server has it (%d active, %.1f vs %.1f, '%s')", second_seen->active,
          second_seen->pos.x, second_there->pos.x, c.stream.names[2]);
    net_close(&d.link);
    for (int round = 0; round < 50 && conns.items[2].peer; round++) {
        connections_poll(&conns, gs);
        enet_host_service(server.host, NULL, 10);
    }
    client_stream_free(&d.stream);
    if (d.game) scene_free(d.game);

    // the round ends, and the next begins on another map
    gs->match.time_left = 1;
    play(&conns, gs, &c, 2, 0, 0);
    CHECK(gs->match.state == MATCH_ENDED, "the clock runs out and the round ends");
    gs->match.counter = 1; // the scores stand a tick rather than five seconds
    play(&conns, gs, &c, 30, 0, 0);
    CHECK(conns.round == 2 && strcmp(conns.map, "ctf_Ash") == 0, "the server begins round %u on %s", conns.round, conns.map);
    CHECK(c.round == 2 && strcmp(c.map, "ctf_Ash") == 0, "and the client is told, and makes its world anew (%u on %s)", c.round, c.map);
    mine = &c.game->world.soldiers[0];
    CHECK(match_has_teams(&gs->match) && match_has_teams(&c.game->match), "a map with flags plays CTF, on the server and by its snapshots here");
    theirs = &gs->world.soldiers[0];
    // a game with teams: watching until a team is chosen, then placed on it
    CHECK(theirs->active && theirs->team == TEAM_SPECTATOR && theirs->dead, "a player who has not chosen a team watches (team %d, dead %d)",
          theirs->team, theirs->dead);
    MsgChat pick = {.slot = 0, .text = "/team 2"};
    client_send(&c, MSG_CHAT, route_chat, &pick);
    play(&conns, gs, &c, 6, 0, 0);
    CHECK(theirs->team == TEAM_BRAVO && !theirs->dead, "/team places it on the team it chose (team %d)", theirs->team);
    snprintf(pick.text, sizeof pick.text, "/team 5");
    client_send(&c, MSG_CHAT, route_chat, &pick);
    play(&conns, gs, &c, 12, 0, 0);
    CHECK(theirs->team == TEAM_SPECTATOR && theirs->dead && mine->team == TEAM_SPECTATOR,
          "/team 5 makes it a spectator, which the client hears (there team %d dead %d life %u; here team %d dead %d life %u; dropped %u stale %u)",
          theirs->team, theirs->dead, theirs->life, mine->team, mine->dead, mine->life, c.stream.dropped, c.stream.stale);
    snprintf(pick.text, sizeof pick.text, "/team 1");
    client_send(&c, MSG_CHAT, route_chat, &pick);
    play(&conns, gs, &c, 6, 0, 0);
    CHECK(theirs->active && !theirs->dead && theirs->remote && conns.items[0].joined, "everyone joined is placed in the new round");
    {
        // on one of the new map's alpha spawns, in the open: not the old map's, not the middle
        const Map *map = gs->ctx.map;
        bool on_spawn = false;
        for (int i = 0; i < map->spawnpoint_count; i++) {
            const Spawnpoint *sp = &map->spawnpoints[i];
            if (sp->active && sp->team == TEAM_ALPHA && vec2_length(vec2_sub(sp->pos, theirs->pos)) < 40.0f) on_spawn = true;
        }
        CHECK(on_spawn && !map_collision_test(map, theirs->pos, false, NULL),
              "on one of ctf_Ash's alpha spawns, in the open (%.0f,%.0f; on a spawn %d)", theirs->pos.x, theirs->pos.y, on_spawn);
        CHECK(!map_collision_test(c.game->ctx.map, mine->pos, false, NULL),
              "and so here, on the client's copy of it (%.0f,%.0f)", mine->pos.x, mine->pos.y);
    }
    CHECK(mine->active && mine->life == theirs->life && fabsf(mine->pos.x - theirs->pos.x) < 1.0f,
          "and hears where, from the snapshots of the new round (life %u, %.1f vs %.1f)", mine->life, mine->pos.x, theirs->pos.x);
    int flags_here = 0;
    for (int i = 0; i < MAX_THINGS; i++) flags_here += thing_is_flag(c.game->world.things[i].style);
    CHECK(flags_here == 2 && gs->match.state == MATCH_PLAYING, "with the new map's flags, and the match playing again (%d flags)",
          flags_here);
    CHECK(c.stream.dropped == 0, "and nothing of the new round was dropped (%u; %u stale ones of the old were)", c.stream.dropped,
          c.stream.stale);

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
