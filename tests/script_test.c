// The server's script: a Lua file read and run with the API in place. It hears who
// comes, the chat first of all and may keep a line, the /commands the server doesn't
// know, the kills and the ticks; it pauses the game and sees a round out; its json
// goes both ways, and a request it makes is answered on the server's thread.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "host.h"
#include "script.h"
#include "test.h"

#define PORT 40031
#define SCRIPT_PATH "build/script_test.lua"

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

// Ticks until the script's `flag` is set, or `max` ticks have run.
static bool pump_until(Host *h, Script *s, const char *flag, int max)
{
    char code[128];
    snprintf(code, sizeof code, "assert(%s ~= nil)", flag);
    for (int i = 0; i < max; i++) {
        host_pump(h, TICK_SECONDS);
        script_pump(s);
        s->quiet = true;
        bool set = script_run(s, code, "flag");
        s->quiet = false;
        if (set) return true;
    }
    return false;
}

void script_tests(void)
{
    CHECK(net_init(), "ENet starts");
    Host host;
    HostSettings settings = {.port = PORT, .mode = MATCH_CTF, .hostname = "script test"};
    snprintf(settings.assets, sizeof settings.assets, "%s", TEST_ASSETS);
    snprintf(settings.map, sizeof settings.map, "ctf_Ash");
    if (!host_open(&host, NULL, &settings)) {
        CHECK(false, "a host on port %d for the script", PORT);
        net_shutdown();
        return;
    }

    write_file(SCRIPT_PATH,
               "kills = 0; joins = {}; ticks = 0; ended = nil; started = nil; answered = nil\n"
               "function on_join(slot, name) joins[#joins + 1] = name end\n"
               "function on_chat(slot, text, team) return text == 'secret' end\n"
               "function on_command(slot, text) return text == 'hello' end\n"
               "function on_kill(killer, victim, weapon) kills = kills + 1; last_weapon = weapon end\n"
               "function on_tick(tick) ticks = ticks + 1 end\n"
               "function on_round_end(stats) ended = stats end\n"
               "function on_round_start(map) started = map end\n"
               "encoded = json.encode({list = {true, 'x\\n', 2.5, json.null}})\n"
               "decoded = json.decode('{\"n\": [1, 2.5, \"s\\\\u0041\"], \"t\": true, \"o\": {}}')\n"
               "http.request({url = 'http://127.0.0.1:1/', timeout = 2}, function(r) answered = r end)\n");
    Script script = {0};
    bool opened = script_open(&script, &host, NULL, SCRIPT_PATH);
    CHECK(opened, "the script is read and run");
    if (!opened) {
        host_close(&host);
        return;
    }
    script.quiet = true; // its errors are the checks' to report

    // the API's answers
    CHECK(script_run(&script, "assert(server.map() == 'ctf_Ash' and server.mode() == 'ctf' and server.round() == 1)", "map"),
          "it knows the map, the mode and the round");
    CHECK(script_run(&script, "assert(#server.players() == 0 and server.player(0) == nil)", "empty"), "and that nobody is on");
    CHECK(script_run(&script, "assert(encoded == '{\"list\":[true,\"x\\\\n\",2.5,null]}', encoded)", "json"), "json encodes a table");
    CHECK(script_run(&script, "assert(decoded.n[2] == 2.5 and decoded.n[3] == 'sA' and decoded.t == true and next(decoded.o) == nil)", "json"),
          "and decodes one");

    // a bot comes: the script hears the join, and sees it among the players
    int bot = host_add_bot(&host, TEAM_ALPHA, NULL);
    CHECK(bot >= 0, "a bot joins (%d)", bot);
    CHECK(script_run(&script, "assert(#joins == 1 and joins[1] == server.player(0).name)", "join"), "on_join heard it, by name");
    CHECK(script_run(&script, "local p = server.players()[1]; assert(p.team == 'alpha' and p.bot and p.alive and p.kills == 0)", "player"),
          "the player's table says its team, that it is a bot, alive, with no kills");

    // the chat and the commands go through the script first
    const LineHooks *line = &host.line_hooks;
    CHECK(line->chat && line->chat(line->user, 0, "secret", false), "on_chat keeps the line it wants");
    CHECK(line->chat && !line->chat(line->user, 0, "hello all", false), "and lets the rest through");
    CHECK(line->command && line->command(line->user, 0, "hello"), "on_command answers a /command it knows");
    CHECK(line->command && !line->command(line->user, 0, "unknown"), "and not one it doesn't");

    // the ticks, and a kill heard among the tick's events
    host_pump(&host, TICK_SECONDS * 5);
    CHECK(script_run(&script, "assert(ticks >= 5, ticks)", "ticks"), "on_tick runs with every tick");
    game_hear(host.game, (Event){.type = EVENT_KILL, .kill = {.killer = 0, .target = 0, .weapon = WEAPON_AK74}});
    host_pump(&host, TICK_SECONDS);
    char code[256];
    snprintf(code, sizeof code, "assert(kills == 1 and last_weapon == '%s', last_weapon)", host.game->ctx.weapons.info[WEAPON_AK74].name);
    CHECK(script_run(&script, code, "kill"), "on_kill heard the kill, with the weapon's name");

    // a pause stops the clock
    int32_t before = host.game->match.time_left;
    CHECK(script_run(&script, "assert(server.pause() and server.paused())", "pause"), "the script pauses the game");
    host_pump(&host, TICK_SECONDS * 3);
    CHECK(host.game->match.time_left == before, "and the clock stands (%d -> %d)", before, host.game->match.time_left);
    CHECK(script_run(&script, "assert(server.unpause() and not server.paused())", "unpause"), "then resumes it");
    host_pump(&host, TICK_SECONDS * 3);
    CHECK(host.game->match.time_left < before, "and the clock runs again");
    CHECK(script_run(&script, "server.say('hello', 'FF8800'); server.say_to(0, 'you'); server.print('log')", "say"),
          "it says lines in a colour of its own, to all and to one");

    // the round is seen out: the script asks for the next map, and hears the end and the start
    CHECK(script_run(&script, "server.next_map()", "next"), "the script ends the round");
    CHECK(pump_until(&host, &script, "started", ROUND_END_TICKS + 120), "and the next begins (round %u)", host.connections.round);
    CHECK(script_run(&script, "assert(ended and ended.why == 'nextmap' and ended.map == 'ctf_Ash' and #ended.players == 1)", "ended"),
          "on_round_end had why, the map and the players");
    CHECK(script_run(&script, "assert(ended.scores.alpha == 0 and ended.winner == nil and started == 'ctf_Ash')", "round"),
          "the scores, no winner, and on_round_start the map");

    // the request to nowhere comes back with an error, on this thread
    clock_t start = clock();
    bool answered = false;
    while (!answered && clock() - start < 5 * CLOCKS_PER_SEC) {
        script_pump(&script);
        answered = script_run(&script, "assert(answered ~= nil)", "answered");
    }
    CHECK(answered, "the request is answered");
    CHECK(script_run(&script, "assert(answered.status == 0 and answered.error and #answered.error > 0, answered.error)", "error"),
          "with no status and the error");

    script_close(&script);
    CHECK(host.line_hooks.chat == NULL && host.hooks.ticked == NULL, "closed, the script's ears are off the host");
    host_close(&host);
    net_shutdown();
    remove(SCRIPT_PATH);
}
