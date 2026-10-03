// The bots: the .bot files read as the original reads them, a bot that sees an enemy
// fights it, one with nobody in sight walks the map's waypoints; and the mode a host
// asks for, on a map that allows it.

#include <string.h>

#include "bots.h"
#include "test.h"

// Ticks with soldier 0 standing still and the bot in slot 1 on its own mind; how many
// shots the bot asked for.
static int bot_run(Game *g, Bots *bots, int ticks)
{
    int shots = 0;
    const char *names[MAX_PLAYERS] = {"Tester", "Admiral"};
    for (int t = 0; t < ticks; t++) {
        Command cmds[MAX_PLAYERS] = {0};
        cmds[0] = (Command){.seq = g->world.tick + 1, .aim = vec2(g->world.soldiers[1].pos.x, g->world.soldiers[1].pos.y)};
        bots_commands(bots, g, names, cmds);
        game_tick(g, cmds);
        for (int i = 0; i < g->events.count; i++) {
            const Event *e = &g->events.items[i];
            if (e->type == EVENT_SHOT && e->shot.player == 1) shots++;
        }
        bots_hear(bots, g);
    }
    return shots;
}

static int said; // lines the bots said
static void count_said(void *user, int slot, const char *text)
{
    (void)user, (void)slot, (void)text;
    said++;
}

void bot_tests(void)
{
    Game *g = scene("ctf_Ash", 100.0f, WEAPON_AK74, WEAPON_AK74);

    // the profiles
    BotProfile profiles[BOT_PROFILES];
    int count = bot_profiles_load(TEST_ASSETS, &g->ctx.weapons, profiles, BOT_PROFILES);
    CHECK(count >= 10, "the bot files under assets/bots are read (%d)", count);
    const BotProfile *admiral = NULL;
    for (int i = 0; i < count; i++)
        if (strcmp(profiles[i].name, "Admiral") == 0) admiral = &profiles[i];
    CHECK(admiral != NULL, "Admiral among them");
    if (!admiral) {
        scene_free(g);
        return;
    }
    CHECK(admiral->favourite == WEAPON_M249 && admiral->secondary == WEAPON_COLT, "with his FN Minimi and USSOCOM (%d, %d)",
          admiral->favourite, admiral->secondary);
    CHECK(admiral->accuracy == 70 && admiral->grenade_frequency == 160 && admiral->camping == 0 && admiral->chat_frequency == 7,
          "his numbers as the file has them");
    CHECK(admiral->look.shirt.r == 0xEE && admiral->look.shirt.g == 0x53 && admiral->look.shirt.b == 0xE2,
          "his shirt's colour turned round from the file's $00BBGGRR (%02X%02X%02X)", admiral->look.shirt.r, admiral->look.shirt.g,
          admiral->look.shirt.b);
    CHECK(admiral->look.skin.r == 0x6D && admiral->look.skin.g == 0x4A && admiral->look.skin.b == 0x1A,
          "and his skin's as written (%02X%02X%02X)", admiral->look.skin.r, admiral->look.skin.g, admiral->look.skin.b);
    CHECK(admiral->look.hair_style == 2 && admiral->look.head_style == 1 && admiral->look.chain_style == 2, "his punk hair, helmet and chain");
    CHECK(strcmp(admiral->chat_kill, "Ha ha") == 0, "and what he says (%s)", admiral->chat_kill);
    uint64_t rng = 3;
    const BotProfile *pick = bot_profile_random(profiles, count, &rng);
    CHECK(pick && strcmp(pick->name, "Boogie Man") != 0, "a random bot is never the Boogie Man (%s)", pick ? pick->name : "none");

    // a bot a hundred units from an enemy it can see fights it
    Bots bots;
    bots_init(&bots, (BotSettings){.difficulty = BOT_DIFFICULTY_NORMAL, .chat = true}, count_said, NULL);
    bots_attach(&bots, 1, admiral, 5);
    g->world.soldiers[1].bot = true;
    CHECK(bots_has(&bots, 1) && bots_count(&bots) == 1, "the bot has slot 1");
    settle(g);
    int shots = bot_run(g, &bots, 120);
    CHECK(shots > 0, "it fires at the enemy in front of it (%d shots in two seconds)", shots);
    const Brain *br = &bots.brains[1];
    CHECK(fabsf(br->aim.x - g->world.soldiers[0].pos.x) < 60.0f, "aiming its way (aim %.0f, target %.0f)", br->aim.x,
          g->world.soldiers[0].pos.x);

    // hit, it is pissed off at the shooter
    g->world.soldiers[0].pos = g->world.soldiers[1].pos; // a hit from nobody in particular: the event is what counts
    game_hear(g, (Event){.type = EVENT_HIT, .hit = {.shooter = 0, .target = 1, .weapon = WEAPON_AK74, .amount = 1.0f}});
    Command none[MAX_PLAYERS] = {0};
    game_tick(g, none);
    bots_hear(&bots, g);
    CHECK(br->pissed_off == 1, "a hit makes it pissed off at who fired (%d)", br->pissed_off);

    // alone, it takes to the waypoints and moves
    g->world.soldiers[0].active = false;
    Vec2 start = g->world.soldiers[1].pos;
    float farthest = 0.0f;
    for (int t = 0; t < 10; t++) {
        bot_run(g, &bots, 60);
        float d = vec2_length(vec2_sub(g->world.soldiers[1].pos, start));
        if (d > farthest) farthest = d;
    }
    CHECK(br->current_waypoint > 0 || br->next_waypoint > 0, "with nobody in sight it finds a waypoint (%d, next %d)",
          br->current_waypoint, br->next_waypoint);
    CHECK(farthest > 40.0f, "and walks the map (%.0f units from where it stood)", farthest);
    CHECK(g->world.soldiers[1].active, "still in the game");

    // a new round forgets the paths; a detached slot is nobody's
    bots_new_round(&bots);
    CHECK(br->current_waypoint == 0 && !br->go_thing, "a new round forgets the path");
    bots_detach(&bots, 1);
    CHECK(!bots_has(&bots, 1), "a detached slot is no bot");
    scene_free(g);

    // the mode asked for, as the map allows: ctf_Ash plays either, Arena only a deathmatch
    g = scene("ctf_Ash", 60.0f, WEAPON_AK74, WEAPON_AK74);
    CHECK(match_mode_choose(g->ctx.map, MATCH_MODE_COUNT) == MATCH_CTF, "ctf_Ash plays CTF on its own");
    CHECK(match_mode_choose(g->ctx.map, MATCH_DEATHMATCH) == MATCH_DEATHMATCH, "or a deathmatch when asked");
    MatchSettings dm = match_settings_for_map(g->ctx.map);
    dm.mode = MATCH_DEATHMATCH;
    game_init(g, 1, dm);
    CHECK(find_thing(g, THING_ALPHA_FLAG) < 0 && find_thing(g, THING_BRAVO_FLAG) < 0, "in which the flags are not placed");
    MatchSettings ctf = match_settings_for_map(g->ctx.map);
    game_init(g, 1, ctf);
    CHECK(find_thing(g, THING_ALPHA_FLAG) >= 0 && find_thing(g, THING_BRAVO_FLAG) >= 0, "and in CTF they are");
    scene_free(g);
    g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    CHECK(match_mode_choose(g->ctx.map, MATCH_CTF) == MATCH_DEATHMATCH, "Arena has no flags: a deathmatch however asked");
    // a deathmatch ends on one soldier's kills
    g->match.settings.score_limit = 3;
    g->world.soldiers[0].kills = 3;
    game_tick(g, none);
    CHECK(g->match.state == MATCH_ENDED, "and a deathmatch ends at the kill limit");
    scene_free(g);
}
