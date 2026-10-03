#include "script.h"

#include <curl/curl.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/systems/systems.h"

#define REGISTRY_KEY "soldatreloaded.script"
#define HTTP_URL_SIZE 2048
#define HTTP_RESPONSE_MAX (4 * 1024 * 1024) // past this an answer is cut
#define HTTP_DEFAULT_TIMEOUT 15L            // seconds

// --- threads, as stdin_reader.c has them ---------------------------------------------

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Lock;
typedef HANDLE Thread;
static void lock_init(Lock *l) { InitializeCriticalSection(l); }
static void lock(Lock *l) { EnterCriticalSection(l); }
static void unlock(Lock *l) { LeaveCriticalSection(l); }
#else
#include <pthread.h>
typedef pthread_mutex_t Lock;
typedef pthread_t Thread;
static void lock_init(Lock *l) { pthread_mutex_init(l, NULL); }
static void lock(Lock *l) { pthread_mutex_lock(l); }
static void unlock(Lock *l) { pthread_mutex_unlock(l); }
#endif

// --- http: a request on a thread of its own ------------------------------------------

struct HttpJob {
    HttpJob *next;
    Script *script;
    char url[HTTP_URL_SIZE];
    char method[16];
    char *body;
    size_t body_size;
    struct curl_slist *headers;
    long timeout;
    int callback; // a reference in the registry, or LUA_NOREF
    Thread thread;
    bool started;
    // written by the thread, read once `done`, under the lock
    long status;
    char *response;
    size_t response_size;
    char error[CURL_ERROR_SIZE];
    bool done;
};

static Lock http_lock;
static bool http_ready; // curl_global_init done, the lock made

static size_t http_write(char *data, size_t size, size_t count, void *user)
{
    HttpJob *job = user;
    size_t n = size * count;
    if (job->response_size + n > HTTP_RESPONSE_MAX) n = HTTP_RESPONSE_MAX - job->response_size;
    if (n == 0) return size * count; // cut, but not an error
    char *grown = realloc(job->response, job->response_size + n + 1);
    if (!grown) return 0;
    job->response = grown;
    memcpy(job->response + job->response_size, data, n);
    job->response_size += n;
    job->response[job->response_size] = '\0';
    return size * count;
}

static void http_perform(HttpJob *job)
{
    long status = 0;
    char error[CURL_ERROR_SIZE] = "";
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(error, sizeof error, "curl could not start");
    } else {
        curl_easy_setopt(curl, CURLOPT_URL, job->url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, job->method);
        if (job->body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, job->body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)job->body_size);
        }
        if (job->headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, job->headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, job);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, job->timeout);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "soldatreloaded-server");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
        CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK && !error[0]) snprintf(error, sizeof error, "%s", curl_easy_strerror(code));
        if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
    }
    lock(&http_lock);
    job->status = status;
    snprintf(job->error, sizeof job->error, "%s", error);
    job->done = true;
    unlock(&http_lock);
}

#ifdef _WIN32
static DWORD WINAPI http_thread(LPVOID arg)
{
    http_perform(arg);
    return 0;
}
#else
static void *http_thread(void *arg)
{
    http_perform(arg);
    return NULL;
}
#endif

static bool http_start(HttpJob *job)
{
#ifdef _WIN32
    job->thread = CreateThread(NULL, 0, http_thread, job, 0, NULL);
    job->started = job->thread != NULL;
#else
    job->started = pthread_create(&job->thread, NULL, http_thread, job) == 0;
#endif
    return job->started;
}

static void http_join(HttpJob *job)
{
    if (!job->started) return;
#ifdef _WIN32
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
#else
    pthread_join(job->thread, NULL);
#endif
    job->started = false;
}

static void http_free(lua_State *L, HttpJob *job)
{
    if (L && job->callback != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, job->callback);
    curl_slist_free_all(job->headers);
    free(job->body);
    free(job->response);
    free(job);
}

static bool http_is_done(HttpJob *job)
{
    lock(&http_lock);
    bool done = job->done;
    unlock(&http_lock);
    return done;
}

// --- the script's side of the API ----------------------------------------------------

static Script *script_of(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    Script *s = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return s;
}

static void report(Script *s, const char *fmt, ...)
{
    if (!s->console || s->quiet) return;
    char text[CONSOLE_TEXT_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    console_print(s->console, "script: %s\n", text);
}

static int traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(no message)", 1);
    return 1;
}

// --- the handlers ----------------------------------------------------------------------

// What a script may hear, each hook below's event. A script hands a function to as many
// as it likes with server.on(event, fn), and so does every file it requires, so several
// scripts run side by side; each event's handlers are called in the order they were
// handed in. A global on_<event>, as a lone script may still write it, is heard last.
static const char *const EVENTS[] = {"chat",      "command",   "join",        "leave", "kill",   "capture", "spawn",
                                     "match_end", "round_end", "round_start", "tick",  "second", NULL};

// The registry's table of them: event -> {fn, fn, ...}.
#define HANDLERS_KEY "soldatreloaded.handlers"

// The event's list of handlers on the stack, made if it was none.
static void push_handlers(lua_State *L, const char *event)
{
    lua_getfield(L, LUA_REGISTRYINDEX, HANDLERS_KEY);
    if (lua_getfield(L, -1, event) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, event);
    }
    lua_remove(L, -2);
}

// server.on(event, fn): fn heard on `event`, after the handlers already there. It comes
// back, for server.off.
static int l_on(lua_State *L)
{
    const char *event = EVENTS[luaL_checkoption(L, 1, NULL, EVENTS)];
    luaL_checktype(L, 2, LUA_TFUNCTION);
    push_handlers(L, event);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    lua_pushvalue(L, 2);
    return 1;
}

// server.off(event, fn): fn heard no more on `event`; whether it was.
static int l_off(lua_State *L)
{
    const char *event = EVENTS[luaL_checkoption(L, 1, NULL, EVENTS)];
    luaL_checktype(L, 2, LUA_TFUNCTION);
    push_handlers(L, event);
    lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        bool same = lua_rawequal(L, -1, 2);
        lua_pop(L, 1);
        if (!same) continue;
        for (; i < n; i++) { // the ones after it close up, in their order
            lua_rawgeti(L, -1, i + 1);
            lua_rawseti(L, -2, i);
        }
        lua_pushnil(L);
        lua_rawseti(L, -2, n);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// Whether anything hears `event`: what a hook asks before it builds its arguments.
static bool listened(Script *s, const char *event)
{
    lua_State *L = s->L;
    if (!L) return false;
    push_handlers(L, event);
    bool heard = lua_rawlen(L, -1) > 0;
    lua_pop(L, 1);
    char name[32];
    snprintf(name, sizeof name, "on_%s", event);
    if (!heard) {
        heard = lua_getglobal(L, name) == LUA_TFUNCTION;
        lua_pop(L, 1);
    }
    return heard;
}

// Every handler of `event`, then the global on_<event>, each on the `nargs` values on top
// of the stack, which go. A handler's error is reported and the next is heard. With
// `until_true` the first to return true ends it, and true comes back: the line kept, the
// command answered.
static bool dispatch(Script *s, const char *event, int nargs, bool until_true)
{
    lua_State *L = s->L;
    int args = lua_gettop(L) - nargs + 1;
    char name[32];
    snprintf(name, sizeof name, "on_%s", event);
    push_handlers(L, event);
    int list = lua_gettop(L);
    lua_Integer n = (lua_Integer)lua_rawlen(L, list); // those handed in while it runs are heard next time
    bool taken = false;
    for (lua_Integer i = 1; i <= n + 1 && !taken; i++) {
        int top = lua_gettop(L);
        lua_pushcfunction(L, traceback);
        if (i <= n) lua_rawgeti(L, list, i);
        else lua_getglobal(L, name);
        if (lua_isfunction(L, -1)) {
            for (int a = 0; a < nargs; a++) lua_pushvalue(L, args + a);
            if (lua_pcall(L, nargs, 1, top + 1) != LUA_OK) report(s, "%s: %s", name, lua_tostring(L, -1));
            else if (until_true && lua_toboolean(L, -1)) taken = true;
        }
        lua_settop(L, top);
    }
    lua_settop(L, args - 1);
    return taken;
}

static const char *team_name(Team team)
{
    switch (team) {
    case TEAM_ALPHA: return "alpha";
    case TEAM_BRAVO: return "bravo";
    case TEAM_CHARLIE: return "charlie";
    case TEAM_DELTA: return "delta";
    case TEAM_SPECTATOR: return "spectator";
    default: return "none";
    }
}

static Team team_of(const char *name)
{
    if (!name) return TEAM_NONE;
    if (strcmp(name, "alpha") == 0) return TEAM_ALPHA;
    if (strcmp(name, "bravo") == 0) return TEAM_BRAVO;
    if (strcmp(name, "charlie") == 0) return TEAM_CHARLIE;
    if (strcmp(name, "delta") == 0) return TEAM_DELTA;
    if (strcmp(name, "spectator") == 0) return TEAM_SPECTATOR;
    return TEAM_NONE;
}

// A player's table pushed: its slot, name, team, tally, ping, and whether it is a bot
// and alive.
static void push_player(lua_State *L, const Host *h, int slot)
{
    const Soldier *s = &h->game->world.soldiers[slot];
    lua_createtable(L, 0, 11);
    lua_pushinteger(L, slot);
    lua_setfield(L, -2, "slot");
    lua_pushstring(L, h->connections.items[slot].name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, team_name(s->team));
    lua_setfield(L, -2, "team");
    lua_pushinteger(L, s->kills);
    lua_setfield(L, -2, "kills");
    lua_pushinteger(L, s->deaths);
    lua_setfield(L, -2, "deaths");
    lua_pushinteger(L, s->flags);
    lua_setfield(L, -2, "flags");
    lua_pushinteger(L, s->ping);
    lua_setfield(L, -2, "ping");
    lua_pushboolean(L, s->bot);
    lua_setfield(L, -2, "bot");
    lua_pushboolean(L, !s->dead && s->team != TEAM_SPECTATOR);
    lua_setfield(L, -2, "alive");
    lua_pushboolean(L, s->team == TEAM_SPECTATOR);
    lua_setfield(L, -2, "spectator");
    lua_pushnumber(L, s->health);
    lua_setfield(L, -2, "health");
}

static bool slot_joined(const Host *h, lua_Integer slot)
{
    return slot >= 0 && slot < MAX_PLAYERS && h->game->world.soldiers[slot].active;
}

// A colour argument: "RRGGBB", or {r, g, b}; none for the script colour.
static Rgba color_arg(lua_State *L, int index)
{
    Rgba color = {0};
    if (lua_isnoneornil(L, index)) return color;
    if (lua_type(L, index) == LUA_TSTRING) {
        if (!rgba_parse_hex(lua_tostring(L, index), &color)) luaL_argerror(L, index, "a colour is \"RRGGBB\"");
        color.a = 255;
        return color;
    }
    luaL_checktype(L, index, LUA_TTABLE);
    uint8_t rgb[3];
    for (int i = 0; i < 3; i++) {
        lua_rawgeti(L, index, i + 1);
        rgb[i] = (uint8_t)clampi((int)luaL_checkinteger(L, -1), 0, 255);
        lua_pop(L, 1);
    }
    return (Rgba){rgb[0], rgb[1], rgb[2], 255};
}

static int l_say(lua_State *L)
{
    Script *s = script_of(L);
    const char *text = luaL_checkstring(L, 1);
    connections_say_kind(&s->host->connections, CHAT_SCRIPT, color_arg(L, 2), text);
    return 0;
}

static int l_say_to(lua_State *L)
{
    Script *s = script_of(L);
    lua_Integer slot = luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);
    if (slot_joined(s->host, slot)) connections_say_to(&s->host->connections, (int)slot, CHAT_SCRIPT, color_arg(L, 3), text);
    return 0;
}

static int l_print(lua_State *L)
{
    Script *s = script_of(L);
    const char *text = luaL_checkstring(L, 1);
    if (s->console && !s->quiet) console_print(s->console, "%s\n", text);
    return 0;
}

static int l_command(lua_State *L)
{
    Script *s = script_of(L);
    const char *text = luaL_checkstring(L, 1);
    if (s->console) console_execute(s->console, text);
    return 0;
}

static int l_pause(lua_State *L)
{
    Script *s = script_of(L);
    lua_pushboolean(L, host_pause(s->host, true));
    return 1;
}

static int l_unpause(lua_State *L)
{
    Script *s = script_of(L);
    lua_pushboolean(L, host_pause(s->host, false));
    return 1;
}

static int l_paused(lua_State *L)
{
    lua_pushboolean(L, host_paused(script_of(L)->host));
    return 1;
}

// Whether the server has `map` to load, as a file among its maps: one it hasn't would stop
// it at the round's end, unable to go on.
static bool map_there(const Host *h, const char *map)
{
    if (!h->connections.maps_dir[0]) return true;
    if (!map[0] || strchr(map, '/') || strchr(map, '\\') || strstr(map, "..")) return false;
    char path[600];
    snprintf(path, sizeof path, "%s/%s.pms", h->connections.maps_dir, map);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// server.next_map([map]): the round ends now, on `map` if given, else the rotation's next.
// True, or false (and nothing changes) for a map the server hasn't got.
static int l_next_map(lua_State *L)
{
    Script *s = script_of(L);
    const char *map = luaL_optstring(L, 1, NULL);
    if (map && map[0]) {
        if (!map_there(s->host, map)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        host_change_map(s->host, map);
    } else {
        host_end_round(s->host);
    }
    lua_pushboolean(L, 1);
    return 1;
}

// server.maps(): the server's list of maps, the one its votes and its map window pick from:
// the rotation (sv_maps), or every map it has when there is none.
static int l_maps(lua_State *L)
{
    const Host *h = script_of(L)->host;
    lua_createtable(L, h->map_count, 0);
    for (int i = 0; i < h->map_count; i++) {
        lua_pushstring(L, h->maps[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_map(lua_State *L)
{
    lua_pushstring(L, host_map(script_of(L)->host));
    return 1;
}

static int l_round(lua_State *L)
{
    lua_pushinteger(L, script_of(L)->host->connections.round);
    return 1;
}

static int l_mode(lua_State *L)
{
    const Host *h = script_of(L)->host;
    lua_pushstring(L, h->game->match.settings.mode == MATCH_CTF ? "ctf" : "dm");
    return 1;
}

static int l_tick(lua_State *L)
{
    lua_pushinteger(L, script_of(L)->host->game->world.tick);
    return 1;
}

static int l_time_left(lua_State *L)
{
    lua_pushnumber(L, (double)script_of(L)->host->game->match.time_left / TICK_RATE);
    return 1;
}

static void push_scores(lua_State *L, const Host *h)
{
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, h->game->match.scores[TEAM_ALPHA]);
    lua_setfield(L, -2, "alpha");
    lua_pushinteger(L, h->game->match.scores[TEAM_BRAVO]);
    lua_setfield(L, -2, "bravo");
}

static int l_scores(lua_State *L)
{
    push_scores(L, script_of(L)->host);
    return 1;
}

static void push_players(lua_State *L, const Host *h)
{
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!h->game->world.soldiers[i].active) continue;
        push_player(L, h, i);
        lua_rawseti(L, -2, ++n);
    }
}

static int l_players(lua_State *L)
{
    push_players(L, script_of(L)->host);
    return 1;
}

static int l_player(lua_State *L)
{
    Script *s = script_of(L);
    lua_Integer slot = luaL_checkinteger(L, 1);
    if (!slot_joined(s->host, slot)) return 0;
    push_player(L, s->host, (int)slot);
    return 1;
}

static int l_kick(lua_State *L)
{
    Script *s = script_of(L);
    lua_Integer slot = luaL_checkinteger(L, 1);
    const char *reason = luaL_optstring(L, 2, "kicked by the server");
    if (!slot_joined(s->host, slot)) return 0;
    if (s->host->connections.items[slot].bot) {
        bots_detach(&s->host->bots, (int)slot);
        connections_remove_bot(&s->host->connections, s->host->game, (int)slot);
    } else {
        s->host->connections.items[slot].kick_why = KICK_CONSOLE;
        connections_kick(&s->host->connections, (int)slot, reason);
    }
    return 0;
}

static int l_add_bot(lua_State *L)
{
    Script *s = script_of(L);
    Team team = team_of(luaL_optstring(L, 1, NULL));
    const char *name = luaL_optstring(L, 2, NULL);
    int slot = host_add_bot(s->host, team, name);
    if (slot < 0) return 0;
    lua_pushinteger(L, slot);
    return 1;
}

static const luaL_Reg SERVER_API[] = {
    {"say", l_say},             {"say_to", l_say_to},   {"print", l_print},     {"command", l_command},
    {"pause", l_pause},         {"unpause", l_unpause}, {"paused", l_paused},   {"next_map", l_next_map},
    {"map", l_map},             {"maps", l_maps},       {"round", l_round},     {"mode", l_mode},
    {"tick", l_tick},           {"time_left", l_time_left}, {"scores", l_scores}, {"players", l_players},
    {"player", l_player},       {"kick", l_kick},       {"add_bot", l_add_bot}, {"on", l_on},
    {"off", l_off},             {NULL, NULL},
};

// http.request{url=, method=, body=, headers={}, timeout=}, callback(response)
// The response: {status=, body=, error=}; status 0 with an error when nothing came back.
static int l_http_request(lua_State *L)
{
    Script *s = script_of(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    HttpJob *job = calloc(1, sizeof *job);
    if (!job) return luaL_error(L, "out of memory");
    job->script = s;
    job->callback = LUA_NOREF;
    job->timeout = HTTP_DEFAULT_TIMEOUT;

    lua_getfield(L, 1, "url");
    const char *url = lua_tostring(L, -1);
    if (!url || !url[0]) {
        free(job);
        return luaL_error(L, "http.request needs a url");
    }
    snprintf(job->url, sizeof job->url, "%s", url);
    lua_pop(L, 1);

    lua_getfield(L, 1, "method");
    snprintf(job->method, sizeof job->method, "%s", luaL_optstring(L, -1, "GET"));
    lua_pop(L, 1);

    lua_getfield(L, 1, "body");
    if (!lua_isnoneornil(L, -1)) {
        size_t n;
        const char *body = luaL_checklstring(L, -1, &n);
        job->body = malloc(n + 1);
        if (job->body) {
            memcpy(job->body, body, n + 1);
            job->body_size = n;
        }
        if (strcmp(job->method, "GET") == 0) snprintf(job->method, sizeof job->method, "POST");
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "timeout");
    if (!lua_isnoneornil(L, -1)) job->timeout = (long)clampi((int)luaL_checknumber(L, -1), 1, 300);
    lua_pop(L, 1);

    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            char line[1024];
            snprintf(line, sizeof line, "%s: %s", lua_tostring(L, -2), lua_tostring(L, -1));
            job->headers = curl_slist_append(job->headers, line);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    if (lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        job->callback = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    if (!http_start(job)) {
        http_free(L, job);
        return luaL_error(L, "the request's thread would not start");
    }
    job->next = s->jobs;
    s->jobs = job;
    return 0;
}

static const luaL_Reg HTTP_API[] = {{"request", l_http_request}, {NULL, NULL}};

// The rest of http, and json, in Lua: get and post over request; json.encode and
// json.decode, with json.null for a null. A table is an array when its keys are 1..n.
static const char *const LUA_PRELUDE =
    "function http.get(url, callback, headers)\n"
    "  return http.request({url = url, method = 'GET', headers = headers}, callback)\n"
    "end\n"
    "function http.post(url, body, content_type, callback)\n"
    "  if type(body) == 'table' then body = json.encode(body); content_type = content_type or 'application/json' end\n"
    "  return http.request({url = url, method = 'POST', body = body,\n"
    "    headers = {['Content-Type'] = content_type or 'text/plain'}}, callback)\n"
    "end\n"
    "json = {}\n"
    "json.null = setmetatable({}, {__tostring = function() return 'null' end})\n"
    "local escapes = {['\"'] = '\\\\\"', ['\\\\'] = '\\\\\\\\', ['\\b'] = '\\\\b', ['\\f'] = '\\\\f',\n"
    "  ['\\n'] = '\\\\n', ['\\r'] = '\\\\r', ['\\t'] = '\\\\t'}\n"
    "local function is_array(t)\n"
    "  local n = 0\n"
    "  for k in pairs(t) do\n"
    "    if type(k) ~= 'number' or k ~= math.floor(k) or k < 1 then return false end\n"
    "    n = n + 1\n"
    "  end\n"
    "  return n == #t\n"
    "end\n"
    "local function encode(v, out)\n"
    "  local t = type(v)\n"
    "  if v == json.null or v == nil then out[#out + 1] = 'null'\n"
    "  elseif t == 'boolean' then out[#out + 1] = tostring(v)\n"
    "  elseif t == 'number' then\n"
    "    if v ~= v or v == math.huge or v == -math.huge then error('json: a number must be finite') end\n"
    "    if v == math.floor(v) and math.abs(v) < 2^53 then out[#out + 1] = string.format('%d', v)\n"
    "    else out[#out + 1] = string.format('%.14g', v) end\n"
    "  elseif t == 'string' then\n"
    "    out[#out + 1] = '\"' .. v:gsub('[%c\"\\\\]', function(c)\n"
    "      return escapes[c] or string.format('\\\\u%04x', c:byte()) end) .. '\"'\n"
    "  elseif t == 'table' then\n"
    "    if #v > 0 or (next(v) == nil and getmetatable(v) == json.array_mt) then\n"
    "      if not is_array(v) then error('json: a table with both numbered and named keys') end\n"
    "      out[#out + 1] = '['\n"
    "      for i = 1, #v do\n"
    "        if i > 1 then out[#out + 1] = ',' end\n"
    "        encode(v[i], out)\n"
    "      end\n"
    "      out[#out + 1] = ']'\n"
    "    else\n"
    "      out[#out + 1] = '{'\n"
    "      local first = true\n"
    "      for k, val in pairs(v) do\n"
    "        if type(k) ~= 'string' then error('json: an object key must be a string') end\n"
    "        if not first then out[#out + 1] = ',' end\n"
    "        first = false\n"
    "        encode(k, out)\n"
    "        out[#out + 1] = ':'\n"
    "        encode(val, out)\n"
    "      end\n"
    "      out[#out + 1] = '}'\n"
    "    end\n"
    "  else error('json: cannot encode a ' .. t) end\n"
    "end\n"
    "json.array_mt = {}\n"
    "function json.array(t) return setmetatable(t or {}, json.array_mt) end\n"
    "function json.encode(v) local out = {}; encode(v, out); return table.concat(out) end\n"
    "local decode_value\n"
    "local function skip(s, i) return s:find('%S', i) or #s + 1 end\n"
    "local function decode_string(s, i)\n"
    "  local out, j = {}, i + 1\n"
    "  while true do\n"
    "    local c = s:sub(j, j)\n"
    "    if c == '' then error('json: an unterminated string') end\n"
    "    if c == '\"' then return table.concat(out), j + 1 end\n"
    "    if c == '\\\\' then\n"
    "      local e = s:sub(j + 1, j + 1)\n"
    "      local map = {b = '\\b', f = '\\f', n = '\\n', r = '\\r', t = '\\t', ['\"'] = '\"', ['\\\\'] = '\\\\', ['/'] = '/'}\n"
    "      if e == 'u' then\n"
    "        local code = tonumber(s:sub(j + 2, j + 5), 16)\n"
    "        if not code then error('json: a bad \\\\u escape') end\n"
    "        out[#out + 1] = utf8.char(code)\n"
    "        j = j + 6\n"
    "      elseif map[e] then out[#out + 1] = map[e]; j = j + 2\n"
    "      else error('json: a bad escape \\\\' .. e) end\n"
    "    else out[#out + 1] = c; j = j + 1 end\n"
    "  end\n"
    "end\n"
    "function decode_value(s, i)\n"
    "  i = skip(s, i)\n"
    "  local c = s:sub(i, i)\n"
    "  if c == '{' then\n"
    "    local obj = {}\n"
    "    i = skip(s, i + 1)\n"
    "    if s:sub(i, i) == '}' then return obj, i + 1 end\n"
    "    while true do\n"
    "      i = skip(s, i)\n"
    "      if s:sub(i, i) ~= '\"' then error('json: an object key must be a string, at ' .. i) end\n"
    "      local key; key, i = decode_string(s, i)\n"
    "      i = skip(s, i)\n"
    "      if s:sub(i, i) ~= ':' then error('json: expected a colon at ' .. i) end\n"
    "      obj[key], i = decode_value(s, i + 1)\n"
    "      i = skip(s, i)\n"
    "      local d = s:sub(i, i)\n"
    "      if d == '}' then return obj, i + 1 end\n"
    "      if d ~= ',' then error('json: expected a comma at ' .. i) end\n"
    "      i = i + 1\n"
    "    end\n"
    "  elseif c == '[' then\n"
    "    local arr = json.array()\n"
    "    i = skip(s, i + 1)\n"
    "    if s:sub(i, i) == ']' then return arr, i + 1 end\n"
    "    while true do\n"
    "      arr[#arr + 1], i = decode_value(s, i)\n"
    "      i = skip(s, i)\n"
    "      local d = s:sub(i, i)\n"
    "      if d == ']' then return arr, i + 1 end\n"
    "      if d ~= ',' then error('json: expected a comma at ' .. i) end\n"
    "      i = i + 1\n"
    "    end\n"
    "  elseif c == '\"' then return decode_string(s, i)\n"
    "  elseif s:sub(i, i + 3) == 'true' then return true, i + 4\n"
    "  elseif s:sub(i, i + 4) == 'false' then return false, i + 5\n"
    "  elseif s:sub(i, i + 3) == 'null' then return json.null, i + 4\n"
    "  else\n"
    "    local num = s:match('^-?%d+%.?%d*[eE]?[-+]?%d*', i)\n"
    "    if not num or num == '' then error('json: unexpected text at ' .. i) end\n"
    "    return tonumber(num), i + #num\n"
    "  end\n"
    "end\n"
    "function json.decode(s)\n"
    "  local v, i = decode_value(s, 1)\n"
    "  if skip(s, i) <= #s then error('json: text after the value at ' .. i) end\n"
    "  return v\n"
    "end\n";

// --- the host's side: the hooks --------------------------------------------------------

static bool hook_chat(void *user, int slot, const char *text, bool team)
{
    Script *s = user;
    if (!listened(s, "chat")) return false;
    lua_pushinteger(s->L, slot);
    lua_pushstring(s->L, text);
    lua_pushboolean(s->L, team);
    return dispatch(s, "chat", 3, true);
}

static bool hook_command(void *user, int slot, const char *text)
{
    Script *s = user;
    if (!listened(s, "command")) return false;
    lua_pushinteger(s->L, slot);
    lua_pushstring(s->L, text);
    return dispatch(s, "command", 2, true);
}

static void hook_joined(void *user, int slot)
{
    Script *s = user;
    if (!listened(s, "join")) return;
    lua_pushinteger(s->L, slot);
    lua_pushstring(s->L, s->host->connections.items[slot].name);
    dispatch(s, "join", 2, false);
}

static void hook_left(void *user, int slot, const char *name)
{
    Script *s = user;
    if (!listened(s, "leave")) return;
    lua_pushinteger(s->L, slot);
    lua_pushstring(s->L, name);
    dispatch(s, "leave", 2, false);
}

static void hook_ticked(void *user)
{
    Script *s = user;
    const Game *g = s->host->game;
    for (int i = 0; i < g->events.count; i++) {
        const Event *e = &g->events.items[i];
        switch (e->type) {
        case EVENT_KILL:
            if (!listened(s, "kill")) break;
            lua_pushinteger(s->L, e->kill.killer);
            lua_pushinteger(s->L, e->kill.target);
            lua_pushstring(s->L, g->ctx.weapons.info[e->kill.weapon].name);
            dispatch(s, "kill", 3, false);
            break;
        case EVENT_FLAG_SCORE:
            if (!listened(s, "capture")) break;
            lua_pushinteger(s->L, e->flag_score.player);
            lua_pushstring(s->L, team_name(g->world.soldiers[e->flag_score.player].team));
            dispatch(s, "capture", 2, false);
            break;
        case EVENT_RESPAWN:
            if (!listened(s, "spawn")) break;
            lua_pushinteger(s->L, e->respawn.target);
            dispatch(s, "spawn", 1, false);
            break;
        case EVENT_MATCH_END:
            if (!listened(s, "match_end")) break;
            if (e->match_end.winner == TEAM_NONE) lua_pushnil(s->L);
            else lua_pushstring(s->L, team_name(e->match_end.winner));
            dispatch(s, "match_end", 1, false);
            break;
        default: break;
        }
    }
    if (listened(s, "tick")) {
        lua_pushinteger(s->L, g->world.tick);
        dispatch(s, "tick", 1, false);
    }
    if (g->world.tick % TICK_RATE == 0 && listened(s, "second")) dispatch(s, "second", 0, false);
}

// The round's figures, as it ends: why, the map, the scores, the winner (a team's name,
// or the top scorer's slot in a deathmatch), the seconds left, and everyone's tally.
static void hook_round_ending(void *user, const char *why)
{
    Script *s = user;
    if (!listened(s, "round_end")) return;
    const Host *h = s->host;
    const Match *m = &h->game->match;
    lua_State *L = s->L;
    lua_createtable(L, 0, 7);
    lua_pushstring(L, why);
    lua_setfield(L, -2, "why");
    lua_pushstring(L, host_map(h));
    lua_setfield(L, -2, "map");
    lua_pushinteger(L, h->connections.round);
    lua_setfield(L, -2, "round");
    lua_pushnumber(L, (double)m->time_left / TICK_RATE);
    lua_setfield(L, -2, "time_left");
    push_scores(L, h);
    lua_setfield(L, -2, "scores");
    push_players(L, h);
    lua_setfield(L, -2, "players");
    if (match_has_teams(m)) {
        if (m->scores[TEAM_ALPHA] > m->scores[TEAM_BRAVO]) lua_pushstring(L, "alpha");
        else if (m->scores[TEAM_BRAVO] > m->scores[TEAM_ALPHA]) lua_pushstring(L, "bravo");
        else lua_pushnil(L);
    } else {
        int top = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Soldier *p = &h->game->world.soldiers[i];
            if (p->active && p->team != TEAM_SPECTATOR && (top < 0 || p->kills > h->game->world.soldiers[top].kills)) top = i;
        }
        if (top >= 0) lua_pushinteger(L, top);
        else lua_pushnil(L);
    }
    lua_setfield(L, -2, "winner");
    dispatch(s, "round_end", 1, false);
}

static void hook_round_started(void *user)
{
    Script *s = user;
    if (!listened(s, "round_start")) return;
    lua_pushstring(s->L, host_map(s->host));
    dispatch(s, "round_start", 1, false);
}

// --- the script's life ---------------------------------------------------------------

static void http_global_init(void)
{
    if (http_ready) return;
    lock_init(&http_lock);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    http_ready = true;
}

// The script's directory on the module path, so it may require its neighbours.
static void set_module_path(lua_State *L, const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back > slash) slash = back;
    char dir[512];
    if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
    else snprintf(dir, sizeof dir, ".");
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    lua_pushfstring(L, "%s/?.lua;%s", dir, lua_tostring(L, -1));
    lua_setfield(L, -3, "path");
    lua_pop(L, 2);
}

bool script_open(Script *s, Host *h, Console *console, const char *path)
{
    http_global_init();
    *s = (Script){.host = h, .console = console};
    snprintf(s->path, sizeof s->path, "%s", path);
    lua_State *L = s->L = luaL_newstate();
    if (!L) {
        report(s, "no memory for Lua");
        return false;
    }
    luaL_openlibs(L);
    lua_pushlightuserdata(L, s);
    lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, HANDLERS_KEY);
    luaL_newlib(L, SERVER_API);
    lua_setglobal(L, "server");
    luaL_newlib(L, HTTP_API);
    lua_setglobal(L, "http");
    if (luaL_dostring(L, LUA_PRELUDE) != LUA_OK) {
        report(s, "the prelude: %s", lua_tostring(L, -1));
        lua_close(L);
        s->L = NULL;
        return false;
    }
    set_module_path(L, path);

    lua_pushcfunction(L, traceback);
    int base = lua_gettop(L);
    if (luaL_loadfile(L, path) != LUA_OK || lua_pcall(L, 0, 0, base) != LUA_OK) {
        report(s, "%s", lua_tostring(L, -1));
        lua_close(L);
        s->L = NULL;
        return false;
    }
    lua_settop(L, 0);

    HostHooks hooks = {.user = s, .ticked = hook_ticked, .round_ending = hook_round_ending, .round_started = hook_round_started};
    LineHooks line = {.user = s, .chat = hook_chat, .command = hook_command, .joined = hook_joined, .left = hook_left};
    host_set_hooks(h, &hooks, &line);
    report(s, "running %s", path);
    return true;
}

void script_close(Script *s)
{
    if (!s->L) return;
    host_set_hooks(s->host, NULL, NULL);
    while (s->jobs) { // a request still out is waited for, and its answer dropped
        HttpJob *job = s->jobs;
        s->jobs = job->next;
        http_join(job);
        http_free(s->L, job);
    }
    lua_close(s->L);
    s->L = NULL;
}

void script_pump(Script *s)
{
    if (!s->L) return;
    HttpJob **link = &s->jobs;
    while (*link) {
        HttpJob *job = *link;
        if (!http_is_done(job)) {
            link = &job->next;
            continue;
        }
        *link = job->next;
        http_join(job);
        if (job->callback != LUA_NOREF) {
            lua_State *L = s->L;
            lua_pushcfunction(L, traceback);
            lua_rawgeti(L, LUA_REGISTRYINDEX, job->callback);
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, job->status);
            lua_setfield(L, -2, "status");
            lua_pushlstring(L, job->response ? job->response : "", job->response_size);
            lua_setfield(L, -2, "body");
            if (job->error[0]) {
                lua_pushstring(L, job->error);
                lua_setfield(L, -2, "error");
            }
            if (lua_pcall(L, 1, 0, -3) != LUA_OK) {
                report(s, "http callback: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            lua_pop(L, 1); // the traceback
        }
        http_free(s->L, job);
    }
}

bool script_run(Script *s, const char *code, const char *name)
{
    if (!s->L) return false;
    lua_State *L = s->L;
    lua_pushcfunction(L, traceback);
    int base = lua_gettop(L);
    bool ok = luaL_loadbuffer(L, code, strlen(code), name) == LUA_OK && lua_pcall(L, 0, 0, base) == LUA_OK;
    if (!ok) report(s, "%s", lua_tostring(L, -1));
    lua_settop(L, 0);
    return ok;
}
