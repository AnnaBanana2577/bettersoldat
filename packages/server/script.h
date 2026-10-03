#pragma once

// A Lua script on the server: an admin's ears and voice on a hosted game. It is read
// once (sv_script, scripts/server.lua by default), and from then on it is called when
// things happen and may act on the game through a small API. docs/scripting.md is the
// reference; scripts/example.lua shows it in use.
//
// What the script hears, when it defines these functions:
//
//   on_chat(slot, text, team) -> keep    a line said; true keeps it from everyone else
//   on_command(slot, text) -> handled    a /command the server doesn't know
//   on_join(slot, name)  on_leave(slot, name)
//   on_kill(killer, victim, weapon)      on_capture(slot, team)
//   on_spawn(slot)                       on_match_end(winner)
//   on_round_end(stats)                  on_round_start(map)
//   on_tick(tick)                        on_second()
//
// What it may do: the `server` table (say, say_to, pause, unpause, next_map, players,
// kick, add_bot, command...), `http` for requests on their own threads with the answer
// delivered later, and `json` for their bodies.
//
// The script runs on the server's own thread, between ticks; an http request runs on
// a thread of its own and its callback on the server's, from script_pump. Nothing here
// touches the world directly but through the host.

#include "host.h"

typedef struct lua_State lua_State;
typedef struct HttpJob HttpJob;

typedef struct Script {
    lua_State *L;   // NULL: no script
    Host *host;
    Console *console; // may be NULL
    char path[512];
    HttpJob *jobs;    // requests made and not yet answered to the script
    bool quiet;       // say nothing on the console (the tests)
} Script;

// Runs `path` with the API in place and the hooks on the host. False, with the
// reason on the console, if it can't be read or fails, and then there is no script.
bool script_open(Script *s, Host *h, Console *console, const char *path);
// Takes the hooks off the host and ends the script; requests still out are waited for.
void script_close(Script *s);

// Between pumps of the host: the answers to requests, to their callbacks.
void script_pump(Script *s);

// A chunk of Lua run now, named for its errors: the console's `lua` command.
bool script_run(Script *s, const char *code, const char *name);
