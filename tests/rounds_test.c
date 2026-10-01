// A round's end as the original has it (MapChangeCounter): the match stopped, nothing
// moves while the scores stand, nobody respawns, and after the count the next round is
// due; and the messages that carry it.

#include <string.h>

#include "network/network.h"
#include "test.h"

void round_tests(void)
{
    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    settle(g);

    // a shot in flight, and a soldier running, as the round is stopped
    Command cmds[MAX_PLAYERS] = {0};
    cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_FIRE, .aim = vec2(g->world.soldiers[0].pos.x + 100, g->world.soldiers[0].pos.y)};
    game_tick(g, cmds);
    int bullets = 0, first = -1;
    for (int i = 0; i < MAX_BULLETS; i++)
        if (g->world.bullets[i].active && first < 0) first = i, bullets++;
    CHECK(bullets > 0, "a bullet flies");
    g->world.soldiers[1].dead = true; // and a soldier lies dead, with its count to go
    g->world.soldiers[1].respawn_counter = 10;

    match_stop(&g->match, &g->incoming);
    CHECK(g->match.state == MATCH_ENDED && g->match.counter == ROUND_END_TICKS, "the match is stopped, its count set (%d)", g->match.counter);
    Vec2 bullet_at = g->world.bullets[first].pos, soldier_at = g->world.soldiers[0].pos;
    uint32_t tick = g->world.tick;
    int ends = 0;
    cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_RIGHT | BUTTON_FIRE, .aim = cmds[0].aim};
    for (int t = 0; t < 60; t++) {
        cmds[0].seq = g->world.tick + 1;
        game_tick(g, cmds);
        for (int i = 0; i < g->events.count; i++) ends += g->events.items[i].type == EVENT_MATCH_END;
    }
    CHECK(ends == 1, "and the end is among the next tick's events, once (%d)", ends);
    CHECK(g->world.tick == tick + 60, "the ticks still count (%u)", g->world.tick - tick);
    CHECK(g->world.bullets[first].active && vec2_length(vec2_sub(g->world.bullets[first].pos, bullet_at)) < 0.01f,
          "the bullet hangs where it was");
    CHECK(vec2_length(vec2_sub(g->world.soldiers[0].pos, soldier_at)) < 0.01f, "the soldier stands, its keys held or not");
    CHECK(g->world.soldiers[1].dead && g->world.soldiers[1].respawn_counter == 10, "the dead stay dead, their count stopped");
    CHECK(g->match.counter == ROUND_END_TICKS - 60 && !match_over(&g->match), "the count runs (%d)", g->match.counter);
    for (int t = 0; t < ROUND_END_TICKS; t++) game_tick(g, cmds);
    CHECK(match_over(&g->match), "run out, the next round is due");
    match_stop(&g->match, &g->incoming);
    CHECK(g->match.counter == 0, "stopping a stopped match changes nothing");
    scene_free(g);

    // the words of it on the wire
    uint8_t data[256];
    NetBuf w = netbuf_writer(data, sizeof data);
    MsgKind kind = MSG_MAP_CHANGE;
    MsgMapChange change = {.counter = 320, .map = "ctf_Ash"};
    msg_kind(&w, &kind);
    msg_map_change(&w, &change);
    NetBuf r = netbuf_reader(data, netbuf_bytes(&w));
    MsgKind got_kind = MSG_INVALID;
    MsgMapChange got = {0};
    msg_kind(&r, &got_kind);
    msg_map_change(&r, &got);
    CHECK(got_kind == MSG_MAP_CHANGE && got.counter == 320 && strcmp(got.map, "ctf_Ash") == 0 && netbuf_done(&r), "a MapChange round trips");
    w = netbuf_writer(data, sizeof data);
    kind = MSG_MAP_REPLY;
    MsgMapReply reply = {.index = 3, .count = 7, .map = "Arena"};
    msg_kind(&w, &kind);
    msg_map_reply(&w, &reply);
    r = netbuf_reader(data, netbuf_bytes(&w));
    MsgMapReply got_reply = {0};
    msg_kind(&r, &got_kind);
    msg_map_reply(&r, &got_reply);
    CHECK(got_kind == MSG_MAP_REPLY && got_reply.index == 3 && got_reply.count == 7 && strcmp(got_reply.map, "Arena") == 0 && netbuf_done(&r),
          "a MapReply round trips");
    CHECK(MSG_RELIABLE[MSG_MAP_CHANGE] && MSG_RELIABLE[MSG_MAP_QUERY] && MSG_RELIABLE[MSG_MAP_REPLY], "all three are news, sent reliably");
}
