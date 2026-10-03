// The server, headless: the console around a hosted game (host.c), and a loop that
// pumps it until it is told to stop. It reads its config (config/) and the command line, loads
// the map, listens on sv_port, gives everyone who says Hello a soldier, plays the bots
// asked for, ticks the world with authority, and stops on `quit` or Ctrl-C.
//
//   console  the cvars and the commands (shared/console): config/defaults/settings.server.cfg,
//            the admin's config/server/settings.cfg over it, then the command line
//   host     the world, the line, the players, the bots and the rounds (host.c)
//   lobby    the heartbeat that lists it with the lobby, while sv_public is on (lobby.c)
//
// It runs from the directory that holds config/ and data/, as the client does: the
// install, above bin/ where it sits, found from wherever it is started (files_enter_install).
//
//   server [+map <name>] [+sv_port <port>] [+<cvar> <value>] [+<command> <args>...]

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console/console.h"
#include "game/game.h"
#include "game/systems/systems.h"
#include "host.h"
#include "files.h" // the launcher's, to find the install from bin/
#include "http.h"
#include "lobby.h"
#include "script.h"
#include "stdin_reader.h"

// After the game's headers: GDI has a Polygon of its own.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev" // xmake.lua sets it from set_version
#endif
// The config: the defaults the game ships (replaced by each update), then the owner's own
// in config/server/, which the server makes on its first start with what each is for, and
// never writes again but the lists of bans and mutes. A server set up before config/ had
// one config.cfg: it becomes settings.cfg, once.
#define CONFIG_DEFAULT_SETTINGS "config/defaults/settings.server.cfg"
#define CONFIG_SETTINGS "config/server/settings.cfg"
#define CONFIG_OLD "config.cfg"
#define CONFIG_LISTS "config/server" // banlist.txt, mutelist.txt, admins.txt (lists.h)
#define CONFIG_DEFAULT_WEAPONS "config/defaults/weapons.server.cfg" // the game's own numbers, as `weapon` lines
#define CONFIG_WEAPONS "config/server/weapons.cfg"                // a weapons mod, over them
#define CONFIG_MAPLIST "config/server/maplist.txt"                // the rotation, a map to a line
#define SLEEP_MS 1 // between passes of the loop, so it never spins flat out

typedef struct Server {
    Console *console; // large; on the heap
    Cvar *data;
    Cvar *map;
    Cvar *maps; // the rotation
    Cvar *port;
    Cvar *ip; // sv_ip: the address to listen on
    Cvar *password; // sv_password
    Cvar *hostname;
    Cvar *gamemode, *timelimit, *killlimit;
    Cvar *bots_noteam, *bots_alpha, *bots_bravo, *bots_difficulty, *bots_chat;
    Cvar *votepercent;
    Cvar *floodingpackets, *warnings_flood;
    Cvar *script_path; // sv_script
    Cvar *rope; // sv_rope: the rope allowed; off, everyone's boots are jets
    Cvar *rope_debug; // sv_rope_debug: each soldier's rope each half second, and changes at once
    Cvar *public, *lobby_url, *lobby_ip; // sv_public, sv_lobby, sv_lobby_ip
    // the weapons mod: the game's own numbers, changed by `weapon` (config/server/weapons.cfg)
    WeaponStats weapons[WEAPON_COUNT];
    bool weapons_mod;
    Host host;
    Script script;
    Lobby lobby;
    bool quit;
} Server;

static volatile sig_atomic_t interrupted; // Ctrl-C, or a kill

static void on_interrupt(int sig)
{
    (void)sig;
    interrupted = 1;
}

// Seconds from some fixed point: the platform's monotonic clock.
static double now(void)
{
#ifdef _WIN32
    LARGE_INTEGER frequency, count;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

// Flushed as it goes: a server's output is usually a pipe or a log file, which would
// otherwise hold it back until the buffer fills.
static void print_stdout(const char *text, void *user)
{
    (void)user;
    fputs(text, stdout);
    fflush(stdout);
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static const char SETTINGS_HEADER[] =
    "// This server's settings, over config/defaults/settings.server.cfg, which every update\n"
    "// replaces: a `set` line for each you want otherwise, as that file writes them. Read as\n"
    "// the server starts; the command line (+sv_hostname \"...\") goes over both.\n";

// The files of config/server/ the owner writes, made where missing with what each is for,
// so they are found from the first start (lists.c makes the lists'). A config.cfg from
// before config/ is the settings, copied once; it stays, as a game's own install shares it
// with the client, which moves it aside itself.
static void config_make_missing(void)
{
    static const struct {
        const char *path, *text;
    } FILES[] = {
        {CONFIG_SETTINGS, "//\n"
                          "// set sv_hostname \"My server\"\n"
                          "// set sv_password \"secret\"\n"
                          "// set sv_public \"1\"\n"},
        {CONFIG_MAPLIST, "// The maps in rotation, one to a line, played in turn after the first (the `map`\n"
                         "// setting). sv_maps, when it is set (in settings.cfg or on the command line), is the\n"
                         "// rotation instead; with neither, the map is played again. A map that isn't in\n"
                         "// data/maps/ is passed over.\n"
                         "//\n"
                         "// ctf_Ash\n"
                         "// ctf_Kampf\n"},
        {CONFIG_WEAPONS, "// A weapons mod: `weapon` lines in the form config/defaults/weapons.server.cfg shows, with\n"
                         "// only the weapons and the fields you change. Every player who joins is sent them, so the\n"
                         "// mod plays the same for all.\n"
                         "//\n"
                         "// weapon \"Desert Eagles\" damage 2.2 ammo 9\n"},
    };
    for (size_t i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
        if (file_exists(FILES[i].path) || !files_make_parents(FILES[i].path)) continue;
        FILE *f = fopen(FILES[i].path, "wb");
        if (!f) continue;
        if (!strcmp(FILES[i].path, CONFIG_SETTINGS)) {
            fputs(SETTINGS_HEADER, f);
            uint64_t size = 0;
            char *old = files_read(CONFIG_OLD, &size);
            if (old) {
                fputs("\n// From config.cfg, where they were before config/:\n\n", f);
                fwrite(old, 1, (size_t)size, f);
                free(old);
                fclose(f);
                continue;
            }
        }
        fputs(FILES[i].text, f);
        fclose(f);
    }
}

// The rotation maplist.txt holds: its maps, a word each, space-separated into `out`.
static void maplist_read(char *out, size_t size)
{
    out[0] = '\0';
    char *text = files_read(CONFIG_MAPLIST, NULL);
    if (!text) return;
    size_t n = 0;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *comment = strstr(line, "//");
        if (comment) *comment = '\0';
        for (char *p = line; *p;) {
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',') p++;
            if (p > start && n + (size_t)(p - start) + 2 < size)
                n += (size_t)snprintf(out + n, size - n, "%s%.*s", n ? " " : "", (int)(p - start), start);
        }
    }
    free(text);
}

static void cmd_quit(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    ((Server *)user)->quit = true;
}

// kick, ban, banip, unban, mute, unmute, bans, mutes, admins: the admin commands, as an
// admin says them in the chat (connections_admin), answered here.
static void cmd_admin(Console *con, int argc, char **argv, void *user)
{
    Server *sv = user;
    char text[NET_TEXT_SIZE];
    size_t n = 0;
    text[0] = '\0';
    for (int i = 0; i < argc && n < sizeof text - 1; i++) {
        int w = snprintf(text + n, sizeof text - n, i > 0 ? " %s" : "%s", argv[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    if (!sv->host.game) {
        console_print(con, "no game is being hosted\n");
        return;
    }
    connections_admin(&sv->host.connections, sv->host.game, -1, text);
}

// say <text...>: the server's chat to everyone.
static void cmd_say(Console *con, int argc, char **argv, void *user)
{
    Server *sv = user;
    if (argc < 2) {
        console_print(con, "usage: say <text>\n");
        return;
    }
    char text[NET_TEXT_SIZE];
    size_t n = 0;
    text[0] = '\0';
    for (int i = 1; i < argc && n < sizeof text - 1; i++) {
        int w = snprintf(text + n, sizeof text - n, i > 1 ? " %s" : "%s", argv[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    host_say(&sv->host, text);
}

// nextmap: the round ends now and the next begins.
static void cmd_nextmap(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    host_end_round(&((Server *)user)->host);
}

// addbot [name] / addbot1 [name] / addbot2 [name]: a bot, on the emptier side, on
// alpha, or on bravo; one of data/bots at random unless named (the original's commands).
static void cmd_addbot(Console *con, int argc, char **argv, void *user)
{
    (void)con;
    Server *sv = user;
    Team team = argv[0][6] == '1' ? TEAM_ALPHA : argv[0][6] == '2' ? TEAM_BRAVO : TEAM_NONE;
    host_add_bot(&sv->host, team, argc > 1 ? argv[1] : NULL);
}

// pause / unpause: the game stands still, nobody moving and the clock stopped, or goes
// on; everyone is told.
static void cmd_pause(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc;
    Server *sv = user;
    bool pause = strcmp(argv[0], "pause") == 0;
    if (host_pause(&sv->host, pause)) connections_say_kind(&sv->host.connections, CHAT_GAME, (Rgba){0}, pause ? "Game paused" : "Game unpaused");
}

// The script sv_script names, if there is one. A path set by hand that isn't there is
// said; the default's absence is nothing.
static void script_start(Server *sv)
{
    const char *path = sv->script_path->value;
    if (!path[0]) return;
    if (!file_exists(path)) {
        if (strcmp(path, sv->script_path->default_value) != 0) console_print(sv->console, "no script at %s\n", path);
        return;
    }
    script_open(&sv->script, &sv->host, sv->console, path);
}

// script_reload: the script read again, from the start; its state is lost.
static void cmd_script_reload(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    Server *sv = user;
    script_close(&sv->script);
    script_start(sv);
}

// lua <code...>: a line of Lua run in the script's state.
static void cmd_lua(Console *con, int argc, char **argv, void *user)
{
    Server *sv = user;
    if (!sv->script.L) {
        console_print(con, "no script is running\n");
        return;
    }
    char code[CONSOLE_TEXT_SIZE];
    size_t n = 0;
    code[0] = '\0';
    for (int i = 1; i < argc && n < sizeof code - 1; i++) {
        int w = snprintf(code + n, sizeof code - n, i > 1 ? " %s" : "%s", argv[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    script_run(&sv->script, code, "console");
}

// The console and what the server keeps in it, then its config and the command line
// over it. Nothing is saved on the way out: nothing here changes a setting.
static void cmd_weapon(Console *con, int argc, char **argv, void *user);
static void cmd_weaponlist(Console *con, int argc, char **argv, void *user);

static bool console_open(Server *sv, int argc, char *argv[])
{
    Console *con = sv->console = console_create(print_stdout, NULL);
    if (!con) return false;
    Weapons own; // the mod starts from the game's own numbers
    weapons_default(&own);
    weapons_stats(&own, sv->weapons);

    sv->data = cvar_register(con, "data", "./data", 0, "what the game plays by: maps/, anims/, objects/, bots/");
    sv->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    sv->maps = cvar_register(con, "sv_maps", "", 0, "the maps in rotation, space-separated, over config/server/maplist.txt; with neither the map plays again");
    sv->port = cvar_register(con, "sv_port", "23073", 0, "the UDP port to listen on");
    sv->ip = cvar_register(con, "sv_ip", "", 0, "the address to listen on; empty for every one");
    sv->hostname = cvar_register(con, "sv_hostname", "Soldat Reloaded server", 0, "the server's name, on the scoreboard");
    sv->password = cvar_register(con, "sv_password", "", 0, "the password to join; empty for none. Read live, so a script may set it");
    cvar_register(con, "sv_adminpassword", "", 0, "the password a player says with /login to become an admin until they leave; empty for none");
    sv->gamemode = cvar_register(con, "sv_gamemode", "0", 0, "0 the map's own, 1 deathmatch, 2 capture the flag");
    sv->timelimit = cvar_register(con, "sv_timelimit", "15", 0, "minutes a round lasts");
    sv->killlimit = cvar_register(con, "sv_killlimit", "10", 0, "the score that wins a round: kills, or captures in CTF");
    sv->bots_noteam = cvar_register(con, "bots_random_noteam", "0", 0, "bots in a deathmatch");
    sv->bots_alpha = cvar_register(con, "bots_random_alpha", "0", 0, "bots on alpha in capture the flag");
    sv->bots_bravo = cvar_register(con, "bots_random_bravo", "0", 0, "bots on bravo in capture the flag");
    sv->bots_difficulty = cvar_register(con, "bots_difficulty", "100", 0, "300 stupid, 200 poor, 100 normal, 50 hard, 10 impossible");
    sv->bots_chat = cvar_register(con, "bots_chat", "1", 0, "whether the bots talk");
    sv->script_path = cvar_register(con, "sv_script", "scripts/main.lua", 0, "the Lua script to run, if the file is there (docs/scripting.md)");
    sv->votepercent = cvar_register(con, "sv_votepercent", "60", 0, "the percentage of players whose yes passes a vote");
    sv->floodingpackets = cvar_register(con, "net_floodingpackets", "120", 0, "messages in a second from one player that count as flooding (a client sends sixty)");
    sv->warnings_flood = cvar_register(con, "sv_warnings_flood", "4", 0, "flood warnings before the player is kicked and barred for a quarter of an hour");
    sv->rope = cvar_register(con, "sv_rope", "0", 0, "1: the rope is allowed, an experimental gear in place of the jets; 0 gives everyone jets");
    sv->rope_debug = cvar_register(con, "sv_rope_debug", "0", 0,
                                   "log each soldier's rope each half second and changes at once, with the states dropped");
    sv->public = cvar_register(con, "sv_public", "0", 0, "1: listed with the lobby, for the game's server browser. Read live");
    sv->lobby_url = cvar_register(con, "sv_lobby", QUERY_LOBBY_URL, 0, "the lobby the server lists itself with");
    sv->lobby_ip = cvar_register(con, "sv_lobby_ip", "", 0, "the IPv4 address the lobby lists; empty for the one the server reaches it from");
    console_add_command(con, "quit", cmd_quit, sv, "stop the server");
    console_add_command(con, "nextmap", cmd_nextmap, sv, "end the round and begin the next");
    console_add_command(con, "say", cmd_say, sv, "say something to everyone, as the server");
    console_add_command(con, "kick", cmd_admin, sv, "put a player off: kick <player> [reason]");
    console_add_command(con, "ban", cmd_admin, sv, "put a player off and bar their address: ban <player> [minutes] [reason]; no minutes for ever");
    console_add_command(con, "banip", cmd_admin, sv, "bar an address: banip <address> [minutes] [reason]");
    console_add_command(con, "unban", cmd_admin, sv, "lift a ban: unban <address or name>");
    console_add_command(con, "mute", cmd_admin, sv, "their chat reaches nobody, until unmuted: mute <player>");
    console_add_command(con, "unmute", cmd_admin, sv, "unmute <player, address or name>");
    console_add_command(con, "bans", cmd_admin, sv, "the ban list");
    console_add_command(con, "mutes", cmd_admin, sv, "the mute list");
    console_add_command(con, "admins", cmd_admin, sv, "the admins (config/server/admins.txt)");
    console_add_command(con, "weapon", cmd_weapon, sv, "a weapons mod's line: weapon <name> <field> <value> [<field> <value>...]");
    console_add_command(con, "weaponlist", cmd_weaponlist, sv, "every weapon's numbers, as the lines that set them");
    console_add_command(con, "addbot", cmd_addbot, sv, "add a bot: addbot [name]");
    console_add_command(con, "addbot1", cmd_addbot, sv, "add a bot to alpha: addbot1 [name]");
    console_add_command(con, "addbot2", cmd_addbot, sv, "add a bot to bravo: addbot2 [name]");
    console_add_command(con, "pause", cmd_pause, sv, "stop the game where it stands");
    console_add_command(con, "unpause", cmd_pause, sv, "let it go on");
    console_add_command(con, "script_reload", cmd_script_reload, sv, "read the script again, from the start");
    console_add_command(con, "lua", cmd_lua, sv, "run a line of Lua in the script: lua <code>");

    config_make_missing();
    if (file_exists(CONFIG_DEFAULT_SETTINGS)) console_execute_file(con, CONFIG_DEFAULT_SETTINGS);
    if (file_exists(CONFIG_SETTINGS)) console_execute_file(con, CONFIG_SETTINGS);
    if (file_exists(CONFIG_DEFAULT_WEAPONS)) console_execute_file(con, CONFIG_DEFAULT_WEAPONS);
    if (file_exists(CONFIG_WEAPONS)) console_execute_file(con, CONFIG_WEAPONS);
    console_execute_args(con, argc, argv);
    return true;
}

// The weapons a mod may change: not those that follow another (the cluster grenade and
// its bomblets the frag grenade, the thrown knife the knife).
static bool moddable(WeaponId id) { return id != WEAPON_CLUSTER_NADE && id != WEAPON_CLUSTER && id != WEAPON_THROWN_KNIFE; }

// weapon <name> <field> <value> [<field> <value>...]: a weapons mod's line. The fields are
// WEAPON_FIELDS'; while a game is on it takes the new numbers at once, and everyone on
// is told.
static void cmd_weapon(Console *con, int argc, char **argv, void *user)
{
    Server *sv = user;
    if (argc < 4 || argc % 2 != 0) {
        console_print(con, "usage: weapon <name> <field> <value> [<field> <value>...]; weaponlist shows them all\n");
        return;
    }
    WeaponId id = weapon_named(argv[1]);
    if (id == WEAPON_NONE && strcmp(argv[1], "Hands") != 0) {
        console_print(con, "weapon: no weapon \"%s\"\n", argv[1]);
        return;
    }
    if (!moddable(id)) {
        console_print(con, "weapon: %s follows another weapon's numbers\n", argv[1]);
        return;
    }
    WeaponStats *stats = &sv->weapons[id];
    for (int a = 2; a + 1 < argc; a += 2) {
        const NetField *f = NULL;
        for (int k = 0; k < WEAPON_FIELD_COUNT && !f; k++)
            if (strcmp(WEAPON_FIELDS[k].name, argv[a]) == 0) f = &WEAPON_FIELDS[k];
        if (!f) {
            console_print(con, "weapon: no field \"%s\"\n", argv[a]);
            continue;
        }
        uint8_t *at = (uint8_t *)stats + f->offset;
        if (f->kind == NET_F32) *(float *)at = (float)atof(argv[a + 1]);
        else *(int32_t *)at = (int32_t)atoi(argv[a + 1]);
    }
    sv->weapons_mod = true;
    if (sv->host.game) { // on the game as it plays, and told
        weapons_apply(&sv->host.game->ctx.weapons, sv->weapons);
        sv->host.settings.weapons_mod = true;
        memcpy(sv->host.settings.weapons, sv->weapons, sizeof sv->weapons);
        connections_send_weapons(&sv->host.connections, sv->host.game);
    }
}

// weaponlist: every weapon a mod may change, as the lines that set it.
static void cmd_weaponlist(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    Server *sv = user;
    Weapons names;
    weapons_default(&names);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        if (!moddable((WeaponId)id)) continue;
        char line[CONSOLE_TEXT_SIZE];
        int n = snprintf(line, sizeof line, "weapon \"%s\"", names.info[id].name);
        for (int k = 0; k < WEAPON_FIELD_COUNT && n < (int)sizeof line; k++) {
            const NetField *f = &WEAPON_FIELDS[k];
            const uint8_t *at = (const uint8_t *)&sv->weapons[id] + f->offset;
            if (f->kind == NET_F32) n += snprintf(line + n, sizeof line - (size_t)n, " %s %g", f->name, *(const float *)at);
            else n += snprintf(line + n, sizeof line - (size_t)n, " %s %d", f->name, *(const int32_t *)at);
        }
        console_print(con, "%s\n", line);
    }
}

// What the cvars say the game is.
static HostSettings settings_from_cvars(const Server *sv)
{
    HostSettings s = {
        .port = (uint16_t)sv->port->integer,
        .mode = sv->gamemode->integer == 1 ? MATCH_DEATHMATCH : sv->gamemode->integer == 2 ? MATCH_CTF : MATCH_MODE_COUNT,
        .time_limit = sv->timelimit->integer,
        .score_limit = sv->killlimit->integer,
        .bots_noteam = clampi(sv->bots_noteam->integer, 0, MAX_PLAYERS),
        .bots_alpha = clampi(sv->bots_alpha->integer, 0, MAX_PLAYERS),
        .bots_bravo = clampi(sv->bots_bravo->integer, 0, MAX_PLAYERS),
        .bots_difficulty = sv->bots_difficulty->integer,
        .bots_chat = sv->bots_chat->integer != 0,
        .vote_percent = sv->votepercent->integer,
        .flood_packets = sv->floodingpackets->integer,
        .flood_warnings = sv->warnings_flood->integer,
        .rope = sv->rope->integer != 0,
    };
    snprintf(s.data, sizeof s.data, "%s", sv->data->value);
    snprintf(s.ip, sizeof s.ip, "%s", sv->ip->value);
    snprintf(s.map, sizeof s.map, "%s", sv->map->value);
    // the rotation: sv_maps when it is set, else maplist.txt's
    if (sv->maps->value[0]) snprintf(s.maps, sizeof s.maps, "%s", sv->maps->value);
    else maplist_read(s.maps, sizeof s.maps);
    snprintf(s.hostname, sizeof s.hostname, "%s", sv->hostname->value);
    snprintf(s.lists_dir, sizeof s.lists_dir, "%s", CONFIG_LISTS); // the bans, mutes and admins, kept
    s.weapons_mod = sv->weapons_mod;
    memcpy(s.weapons, sv->weapons, sizeof s.weapons);
    return s;
}

int main(int argc, char *argv[])
{
    Server sv = {0};
    // its files beside it, from the install: started from bin/ itself, the folder above
    if (!files_enter_install("data")) fprintf(stderr, "no data/ here, beside the executable or above it\n");

    if (!console_open(&sv, argc, argv)) return 1;
    if (!net_init()) {
        fprintf(stderr, "ENet wouldn't start\n");
        console_destroy(sv.console);
        return 1;
    }
    HostSettings settings = settings_from_cvars(&sv);
    if (!host_open(&sv.host, sv.console, &settings)) {
        net_shutdown();
        console_destroy(sv.console);
        return 1;
    }
    script_start(&sv);
    sv.host.rope_debug = sv.rope_debug; // the host logs it, sv_rope_debug
    http_init();
    http_set_agent("soldatreloaded-server/" SOLDATRELOADED_VERSION);
    lobby_init(&sv.lobby, sv.console);
    if (!stdin_reader_start()) fprintf(stderr, "the console won't read its input\n");
    signal(SIGINT, on_interrupt);
    signal(SIGTERM, on_interrupt);

    // The line is heard before the ticks; each tick the players' soldiers step on their
    // last keys and the bots on their own minds, and a snapshot goes to everyone; the
    // line is flushed after (host_pump).
    double last = now();
    while (!sv.quit && !interrupted) {
        double t = now();
        double dt = t - last;
        last = t;
        char line[STDIN_LINE_SIZE];
        while (stdin_reader_take(line, sizeof line)) console_execute(sv.console, line);
        if (!host_pump(&sv.host, dt)) sv.quit = true;
        script_pump(&sv.script); // the answers to its requests
        cvar_set(sv.console, "map", host_map(&sv.host));
        LobbySettings lobby = {.public = sv.public->integer != 0, .url = sv.lobby_url->value, .address = sv.lobby_ip->value,
                               .port = sv.host.settings.port};
        lobby_pump(&sv.lobby, &lobby, t);
        sleep_ms(SLEEP_MS);
    }

    console_print(sv.console, "stopping\n");
    lobby_close(&sv.lobby);
    http_cleanup();
    script_close(&sv.script); // before the host it listens to
    host_close(&sv.host);
    net_shutdown();
    console_destroy(sv.console);
    return 0;
}
