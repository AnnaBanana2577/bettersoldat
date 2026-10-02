// The events on the wire: every type classified, every travelling type round trips, the
// queue gives each receiver what it lacks and never an owner its own.

#include <string.h>

#include "network/wire.h"
#include "test.h"

// Whether an event read back writes the same bits as the original: every field on the
// wire survived (the structs themselves have padding no one promises anything about).
static bool round_trips(const Event *original)
{
    uint8_t first[128], second[128];
    NetBuf w = netbuf_writer(first, sizeof first);
    Event e = *original;
    wire_event(&w, &e);
    Event got = {0};
    NetBuf r = netbuf_reader(first, netbuf_bytes(&w));
    wire_event(&r, &got);
    NetBuf again = netbuf_writer(second, sizeof second);
    wire_event(&again, &got);
    return netbuf_ok(&w) && netbuf_done(&r) && netbuf_bytes(&again) == netbuf_bytes(&w) && got.type == original->type &&
           memcmp(first, second, netbuf_bytes(&w)) == 0;
}

void wire_tests(void)
{
    int owner = 0, server = 0, local = 0;
    for (int t = 0; t <= EVENT_ECHO_TEST; t++) {
        switch (wire_side((EventType)t)) {
        case WIRE_OWNER: owner++; break;
        case WIRE_SERVER: server++; break;
        case WIRE_LOCAL: local++; break;
        }
    }
    CHECK(owner == 3 && server == 10 && local == EVENT_ECHO_TEST + 1 - 13, "every event type is classified (%d owner, %d server, %d local)",
          owner, server, local);

    // every travelling type round trips whole
    Event samples[] = {
        {.type = EVENT_SHOT, .shot = {.player = 3, .weapon = WEAPON_SPAS, .pos = {1, 2}, .vel = {3, -4}, .damage = 0.5f, .shot = 77, .self = true}},
        {.type = EVENT_WEAPON_DROP, .weapon_drop = {.player = 1, .weapon = WEAPON_AK74, .ammo = 12, .thrown = false, .pos = {5, 6}, .impact = {7, 8}}},
        {.type = EVENT_FLAG_THROW, .flag_throw = {.player = 9}},
        {.type = EVENT_DAMAGE, .damage = {.attacker = 1, .target = 2, .weapon = WEAPON_M79, .amount = 42.5f, .vest = true}},
        {.type = EVENT_KILL, .kill = {.killer = 4, .target = 5, .weapon = WEAPON_KNIFE, .pos = {9, 10}, .health = -30.0f, .part = 12, .kills = -1}},
        {.type = EVENT_RESPAWN, .respawn = {.target = 6, .life = 200, .team = TEAM_BRAVO, .gear = GEAR_ROPE, .primary = WEAPON_MINIGUN, .secondary = WEAPON_LAW, .pos = {11, 12}}},
        {.type = EVENT_FLAG_GRAB, .flag_grab = {.player = 7, .thing = 3, .flag = THING_BRAVO_FLAG, .pos = {13, 14}}},
        {.type = EVENT_FLAG_RETURN, .flag_return = {.player = 255, .flag = THING_ALPHA_FLAG, .pos = {15, 16}}},
        {.type = EVENT_FLAG_SCORE, .flag_score = {.player = 8, .flag = THING_ALPHA_FLAG, .pos = {17, 18}}},
        {.type = EVENT_KIT_PICKUP, .kit_pickup = {.player = 2, .thing = 60, .kit = THING_VEST_KIT, .pos = {19, 20}}},
        {.type = EVENT_WEAPON_PICKUP, .weapon_pickup = {.player = 11, .thing = 1, .weapon = WEAPON_BOW, .ammo = 1, .pos = {21, 22}}},
        {.type = EVENT_MATCH_END, .match_end = {.winner = TEAM_ALPHA}},
        {.type = EVENT_FLAG_DROP, .flag_drop = {.player = 2, .flag = THING_BRAVO_FLAG, .pos = {30, -40}}},
    };
    int n = (int)(sizeof samples / sizeof samples[0]);
    bool all = true;
    for (int i = 0; i < n; i++) {
        bool ok = round_trips(&samples[i]);
        if (!ok) CHECK(false, "event type %d doesn't round trip", samples[i].type);
        all = all && ok;
    }
    CHECK(all, "all %d travelling types round trip", n);
    uint8_t data[16];
    NetBuf w = netbuf_writer(data, sizeof data);
    Event blood = {.type = EVENT_BLOOD};
    wire_event(&w, &blood);
    CHECK(!netbuf_ok(&w), "a local event is bad on the wire");

    // the queue: the server's, with a shot heard from slot 3 and a kill of its own
    WireQueue q;
    wire_queue_init(&q);
    Events tick = {0};
    Event heard = samples[0];
    heard.tick = 40; // heard: it carries its tick
    event_emit(&tick, heard);
    event_emit(&tick, samples[3]);
    event_emit(&tick, (Event){.type = EVENT_BLOOD});
    wire_collect(&q, &tick, 50, -1);
    CHECK(q.next - q.first == 2, "the wire events of a tick go into the queue, the local ones not (%u)", q.next - q.first);

    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    g->world.tick = 55;
    uint8_t packet[NET_MTU];
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 0, 3, WIRE_PER_PACKET); // to slot 3, the shooter
    uint32_t last = 0;
    NetBuf r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, -1);
    CHECK(netbuf_done(&r) && last == 2 && g->incoming.count == 1 && g->incoming.items[0].type == EVENT_DAMAGE,
          "the shooter gets the kill but not its own shot back (%d heard, last %u)", g->incoming.count, last);

    events_clear(&g->incoming);
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 0, 7, WIRE_PER_PACKET); // to someone else
    last = 0;
    r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, -1);
    CHECK(g->incoming.count == 2 && g->incoming.items[0].type == EVENT_SHOT && g->incoming.items[0].shot.advance == 15 &&
              g->incoming.items[0].tick == 40 && g->incoming.items[1].tick == 50,
          "another gets both, the shot to be run forward from its tick to now (%d heard, advance %u)", g->incoming.count,
          g->incoming.items[0].shot.advance);

    events_clear(&g->incoming);
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 2, 7, WIRE_PER_PACKET); // everything acknowledged
    r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, -1);
    CHECK(netbuf_done(&r) && g->incoming.count == 0 && netbuf_bytes(&w) == 1, "nothing pending costs a byte");

    events_clear(&g->incoming);
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 0, 7, WIRE_PER_PACKET);
    r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, -1);
    CHECK(g->incoming.count == 0, "what was applied once is not applied again (last %u)", last);

    // fewer at a time, when a packet is short of room
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 0, 7, 1);
    last = 0;
    events_clear(&g->incoming);
    r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, -1);
    CHECK(g->incoming.count == 1 && last == 1, "asked for one, one goes, and the rest next time (last %u)", last);
    CHECK(wire_queue_present(&q) == 2, "a newcomer starts acknowledged up to the present (%u)", wire_queue_present(&q));

    // a server reading a client: that client's own decisions alone
    events_clear(&g->incoming);
    w = netbuf_writer(packet, sizeof packet);
    wire_write(&w, &q, 0, 7, WIRE_PER_PACKET);
    last = 0;
    r = netbuf_reader(packet, netbuf_bytes(&w));
    wire_read(&r, g, &last, 5); // as if slot 5 had sent these
    CHECK(g->incoming.count == 0 && last == 2, "a client's word about anyone but itself, or a server's kind, is dropped");

    // the client's collection: its own decisions alone
    WireQueue mine;
    wire_queue_init(&mine);
    Events my_tick = {0};
    event_emit(&my_tick, samples[0]); // slot 3's shot, local
    event_emit(&my_tick, samples[1]); // slot 1's drop
    event_emit(&my_tick, heard);       // slot 3's, but heard
    event_emit(&my_tick, samples[3]);  // the server's kind
    wire_collect(&mine, &my_tick, 60, 3);
    CHECK(mine.next - mine.first == 1 && mine.items[1].type == EVENT_SHOT && mine.items[1].tick == 60,
          "a client keeps its own decisions alone, stamped with the tick (%u kept)", mine.next - mine.first);
    scene_free(g);
}
