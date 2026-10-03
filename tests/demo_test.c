// Demos (client/net/demo.h): a client joins a game hosted here with bots, fights a while,
// and records a demo begun mid-round; the demo played back makes the same world, tick
// for tick: every soldier where it stood, the bullets as they flew, mine among them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "net/client_net.h"
#include "net/demo.h"
#include "test.h"

#define PORT 40041
#define DEMO_PATH "build/demo_test" DEMO_EXT
#define WARMUP 180   // ticks joined before the recording begins: mid-round
#define RECORDED 600 // ticks recorded
#define SETTLE 120   // ticks of the playback before it must match: the bullets in flight as it began, never heard fired, gone

// A client's end, as the client's main loop has it: the line, the world it makes.
typedef struct Side {
    ClientNet net;
    Game *g;
    int me;
} Side;

// What a tick left, to hold the playback's to.
typedef struct Seen {
    bool active[MAX_PLAYERS], dead[MAX_PLAYERS];
    Vec2 pos[MAX_PLAYERS];
    float health[MAX_PLAYERS];
    int bullets, mine; // in flight, and of them mine
} Seen;

static void side_free(Side *s)
{
    if (s->g) context_destroy(&s->g->ctx);
    free(s->g);
    s->g = NULL;
}

// What the frame's messages began: a Map makes the world anew.
static void side_take(Side *s)
{
    if (client_net_take_map(&s->net)) {
        side_free(s);
        s->g = calloc(1, sizeof(Game));
        if (!s->g || !context_load(&s->g->ctx, "assets", s->net.map)) {
            printf("could not load map '%s' from assets/\n", s->net.map);
            exit(2);
        }
        MatchSettings settings = match_settings_for_map(s->g->ctx.map);
        settings.rope = s->net.rope;
        game_init(s->g, 1, settings);
        s->g->world.authority = false;
        s->me = s->net.slot;
    }
    client_net_take_map_change(&s->net);
    MsgChat chat;
    while (client_net_take_chat(&s->net, &chat)) {
    }
}

// One tick, as the client's: the snapshot of the tick on show, the others on their last
// keys, me on `cmd`; with `t`, a demo's, the tick it showed, and me put back as its
// tick left me. The tick shown.
static uint32_t side_tick(Side *s, Command cmd, const DemoTick *t)
{
    World *w = &s->g->world;
    Command cmds[MAX_PLAYERS] = {0};
    if (t) s->net.stream.view_at = t->view;
    client_stream_begin_tick(&s->net.stream, s->g, s->me, 0);
    uint32_t view = w->tick;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *x = &w->soldiers[i];
        x->remote = i != s->me;
        if (x->remote) cmds[i] = stream_command(x, client_stream_quiet(&s->net.stream, i));
    }
    cmds[s->me] = cmd;
    game_tick(s->g, cmds);
    Soldier *me = &w->soldiers[s->me];
    if (t && t->soldier && me->active) {
        me->look = t->self.look;
        me->gear = t->self.gear;
        me->primary_choice = t->self.primary_choice;
        me->secondary_choice = t->self.secondary_choice;
        me->hit_spray = t->self.hit_spray;
        if (t->self.life == me->life) soldier_copy_owned(s->g->ctx.anims, me, &t->self);
    }
    client_net_tick(&s->net, s->g);
    return view;
}

static Seen seen(const Side *s)
{
    Seen v = {0};
    const World *w = &s->g->world;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        v.active[i] = w->soldiers[i].active;
        v.dead[i] = w->soldiers[i].dead;
        v.pos[i] = w->soldiers[i].pos;
        v.health[i] = w->soldiers[i].health;
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!w->bullets[i].active) continue;
        v.bullets++;
        if (w->bullets[i].owner == s->me) v.mine++;
    }
    return v;
}

static bool same_seen(const Seen *a, const Seen *b)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (a->active[i] != b->active[i]) return false;
        if (!a->active[i]) continue;
        if (a->dead[i] != b->dead[i] || a->pos[i].x != b->pos[i].x || a->pos[i].y != b->pos[i].y || a->health[i] != b->health[i])
            return false;
    }
    return a->bullets == b->bullets && a->mine == b->mine;
}

// What the line brings, into the recording, as the client's main loop has it.
static void tap(void *user, const uint8_t *data, size_t size, MsgKind kind)
{
    (void)kind;
    demo_record_packet(user, data, size);
}

// A playback to the demo's end, held to what was recorded once SETTLE ticks are in.
typedef struct Played {
    int at, wrong, first_wrong, mine; // ticks played, those that differ and the first, those with my bullets
} Played;

static Played play(DemoPlayer *p, Side *s, Console *con, const Seen *recorded, int ticks)
{
    Played r = {.first_wrong = -1};
    const uint8_t *data;
    size_t size;
    DemoTick t;
    for (DemoNext next; (next = demo_play_next(p, &data, &size, &t)) != DEMO_NEXT_END;) {
        if (next == DEMO_NEXT_PACKET) client_net_feed(&s->net, con, s->g, data, size);
        else if (next == DEMO_NEXT_FRAME) side_take(s);
        else if (s->g && r.at < ticks) {
            side_tick(s, t.cmd, &t);
            Seen v = seen(s);
            r.mine += v.mine > 0;
            if (r.at >= SETTLE && !same_seen(&v, &recorded[r.at])) {
                r.wrong++;
                if (r.first_wrong < 0) r.first_wrong = r.at;
            }
            r.at++;
        }
    }
    return r;
}

// My command on tick `n`: running to and fro, jumping now and then, firing in bursts
// at a point ahead.
static Command command(const Side *s, uint32_t n)
{
    const Soldier *me = &s->g->world.soldiers[s->me];
    bool right = (n / 90) % 2 == 0;
    Command c = {.seq = n, .buttons = right ? BUTTON_RIGHT : BUTTON_LEFT};
    if (n % 70 < 8) c.buttons |= BUTTON_JUMP;
    if (n % 40 < 15) c.buttons |= BUTTON_FIRE;
    c.aim = vec2_add(me->pos, vec2(right ? 200.0f : -200.0f, -20.0f));
    return c;
}

void demo_tests(void)
{
    CHECK(net_init(), "ENet starts");
    Host host;
    HostSettings settings = {.port = PORT, .mode = MATCH_DEATHMATCH, .hostname = "demo test", .bots_noteam = 3, .quiet = true};
    snprintf(settings.assets, sizeof settings.assets, "assets");
    snprintf(settings.map, sizeof settings.map, "Arena");
    if (!host_open(&host, NULL, &settings)) {
        CHECK(false, "a host on port %d for the demo", PORT);
        net_shutdown();
        return;
    }
    Console *con = console_create(NULL, NULL);

    // the live client: joined, a while in, then recorded
    static Side live, played;
    static Seen recorded[RECORDED];
    memset(&live, 0, sizeof live);
    client_net_init(&live.net);
    client_net_connect(&live.net, con, "127.0.0.1", PORT, "Recorder", "");
    static DemoRecorder rec;
    memset(&rec, 0, sizeof rec);
    live.net.tap = tap;
    live.net.tap_user = &rec;
    int ticks = 0, joined = 0, n = 0;
    for (int frame = 0; frame < 2000 && ticks < RECORDED; frame++) {
        host_pump(&host, TICK_SECONDS);
        client_net_poll(&live.net, con, live.g);
        side_take(&live);
        if (!live.g || !client_net_joined(&live.net) || live.net.stream.newest == 0) continue;
        if (++joined == WARMUP) {
            DemoHeader h = {.slot = (uint8_t)live.net.slot, .name = "Recorder"};
            snprintf(h.map, sizeof h.map, "%s", live.net.map);
            CHECK(demo_record_open(&rec, DEMO_PATH, &h), "the demo is written to %s", DEMO_PATH);
            demo_record_join(&rec, &live.net);
        }
        demo_record_frame(&rec);
        Command cmd = command(&live, (uint32_t)++n);
        uint32_t view = side_tick(&live, cmd, NULL);
        if (demo_recording(&rec)) {
            const Soldier *me = &live.g->world.soldiers[live.me];
            demo_record_tick(&rec, view, &cmd, vec2(320, 240), me->active ? me : NULL);
            recorded[ticks++] = seen(&live);
        }
    }
    CHECK(ticks == RECORDED, "the client joined and %d ticks were recorded (%d)", RECORDED, ticks);
    int fired = 0;
    for (int i = 0; i < ticks; i++) fired += recorded[i].mine > 0;
    CHECK(fired > 0, "my bullets flew while it was recorded");
    demo_record_close(&rec);
    client_net_disconnect(&live.net, con);
    host_close(&host);

    // played back: the same world, tick for tick
    DemoPlayer player = {0};
    char error[256] = "";
    bool opened = demo_play_open(&player, DEMO_PATH, error, sizeof error);
    CHECK(opened, "the demo opens (%s)", error);
    if (opened) {
        CHECK(player.header.ticks == (uint32_t)ticks && player.header.slot == live.me && strcmp(player.header.map, "Arena") == 0,
              "its header says its ticks (%u), my slot and the map", player.header.ticks);
        memset(&played, 0, sizeof played);
        client_net_init(&played.net);
        client_net_play(&played.net, player.header.slot);
        Played run = play(&player, &played, con, recorded, ticks);
        CHECK(run.at == ticks, "every tick played (%d of %d)", run.at, ticks);
        CHECK(run.wrong == 0, "the world played back is the world recorded (%d ticks differ, the first %d)", run.wrong, run.first_wrong);
        CHECK(run.mine > 0, "my bullets fly in it");

        // a seek back: from the start again, on the world and line already there, as
        // demo_tick does
        demo_play_rewind(&player);
        client_net_play(&played.net, player.header.slot);
        run = play(&player, &played, con, recorded, ticks);
        CHECK(run.at == ticks && run.wrong == 0, "played again from the start, it is the same (%d ticks of %d, %d differ)", run.at, ticks,
              run.wrong);
        DemoHeader h;
        CHECK(demo_read_header(DEMO_PATH, &h) && h.ticks == (uint32_t)ticks && strcmp(h.name, "Recorder") == 0,
              "its header reads alone, for the menu's list");
        side_free(&played);
        client_stream_free(&played.net.stream);
    }
    demo_play_close(&player);

    // a file that isn't a demo, and one of another version, are refused
    FILE *f = fopen(DEMO_PATH, "r+b");
    if (f) {
        fwrite("SRDM\x01\x00\xff\xff", 1, 8, f); // the net version, wrong
        fclose(f);
    }
    CHECK(!demo_play_open(&player, DEMO_PATH, error, sizeof error), "a demo of another net version is refused");
    CHECK(!demo_play_open(&player, "build/no_such_demo" DEMO_EXT, error, sizeof error), "and none at all");

    side_free(&live);
    client_stream_free(&live.net.stream);
    console_destroy(con);
    net_shutdown();
    remove(DEMO_PATH);
}
