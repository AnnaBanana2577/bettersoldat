// The server, headless: the console around a hosted game (host.c), and a loop that
// pumps it until it is told to stop. It reads its config (config/) and the command line, loads
// the map, listens on sv_port, gives everyone who says Hello a soldier, plays the bots
// asked for, ticks the world with authority, and stops on `quit` or Ctrl-C.
//
//   console  the cvars and the commands (shared/console): the code's defaults,
//            config/server.cfg over them, then the command line
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
#include "host_cvars.h"
#include "rounds.h"
#include "weapons_ini.h"
#include "files.h" // the launcher's, to find the install from bin/
#include "main_lua.h" // runtime/scripts/main.lua, made into a string by xmake.lua
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
// The config (config/): the defaults are the code's; server.cfg over them (host_cvars.h),
// then the weapons mod (weapons.ini) and the rotation (maplist.txt). As it starts, before
// the command line, the server writes server.cfg whole, every setting with the game's
// value commented out where it holds it, and makes the files it reads where they are
// missing. The game's Local Play starts this same program, on these same files. A server
// set up before config/ had one config.cfg: it is read once, as config/ is made.
#define CONFIG_OLD "config.cfg"
#define CONFIG_LISTS "config"                            // banlist.txt, mutelist.txt, admins.txt (lists.h)
#define CONFIG_WEAPONS "config/weapons.ini"              // a weapons mod, as Soldat's (weapons_ini.h)
#define MAIN_SCRIPT "scripts/main.lua"                            // sv_script's, made from main_lua.h where missing
#define SLEEP_MS 1 // between passes of the loop, so it never spins flat out

typedef struct Server {
    Console *console; // large; on the heap
    Cvar *data;
    HostCvars cvars;  // what it hosts by, as Local Play does (host_cvars.h)
    Cvar *rope_debug; // sv_rope_debug: each soldier's rope each half second, and changes at once
    // the weapons mod: the game's own numbers, changed by config/weapons.ini and `weapon`
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

// The files of config/ a server reads but never writes once there, made where missing with
// what they are for, so they are found from the first start (lists.c makes the lists').
static void config_make_missing(void)
{
    maplist_make();

    // The script, unpacked from the server's own package, which has none (unpacking a
    // release over a server would put an owner's back as it came). A game's install has a
    // manifest.txt: its launcher makes it there, once, so one taken out stays out.
    if (!file_exists(MAIN_SCRIPT) && !file_exists("manifest.txt") && files_make_parents(MAIN_SCRIPT))
        files_write(MAIN_SCRIPT, MAIN_LUA, sizeof MAIN_LUA - 1);
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
    const char *path = sv->cvars.script->value;
    if (!path[0]) return;
    if (!file_exists(path)) {
        if (strcmp(path, sv->cvars.script->default_value) != 0) console_print(sv->console, "no script at %s\n", path);
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

static void weapons_passed(void *user, const char *what)
{
    console_print(((Server *)user)->console, "%s: %s, passed over\n", CONFIG_WEAPONS, what);
}

// config/weapons.ini over the game's own numbers, a mod if it changes any.
static void weapons_load(Server *sv)
{
    WeaponStats own[WEAPON_COUNT];
    memcpy(own, sv->weapons, sizeof own);
    char name[64];
    if (!weapons_ini_read(CONFIG_WEAPONS, sv->weapons, name, sizeof name, weapons_passed, sv)) return;
    sv->weapons_mod = memcmp(own, sv->weapons, sizeof own) != 0;
    if (sv->weapons_mod) console_print(sv->console, "weapons mod %s\n", name[0] ? name : CONFIG_WEAPONS);
}

// The console and what the server keeps in it, then its config and the command line
// over it. Its files are written as it starts (config/, above); nothing on the way out.
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
    host_cvars_register(con, &sv->cvars);
    sv->rope_debug = cvar_register(con, "sv_rope_debug", "0", 0,
                                   "log each soldier's rope each half second and changes at once, with the states dropped");
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
    console_add_command(con, "admins", cmd_admin, sv, "the admins (config/admins.txt)");
    console_add_command(con, "weapon", cmd_weapon, sv, "a weapons mod's line: weapon <name> <field> <value> [<field> <value>...]");
    console_add_command(con, "weaponlist", cmd_weaponlist, sv, "every weapon's numbers, as the lines that set them");
    console_add_command(con, "addbot", cmd_addbot, sv, "add a bot: addbot [name]");
    console_add_command(con, "addbot1", cmd_addbot, sv, "add a bot to alpha: addbot1 [name]");
    console_add_command(con, "addbot2", cmd_addbot, sv, "add a bot to bravo: addbot2 [name]");
    console_add_command(con, "pause", cmd_pause, sv, "stop the game where it stands");
    console_add_command(con, "unpause", cmd_pause, sv, "let it go on");
    console_add_command(con, "script_reload", cmd_script_reload, sv, "read the script again, from the start");
    console_add_command(con, "lua", cmd_lua, sv, "run a line of Lua in the script: lua <code>");

    if (file_exists(CONFIG_SERVER)) console_execute_file(con, CONFIG_SERVER);
    if (file_exists(CONFIG_OLD)) {
        // a server set up before config/: read over config/, which is written from it below.
        // A game's install shares it with the client, which moves it aside once it has read it
        // too; a server's own install has no client, so the server does.
        console_execute_file(con, CONFIG_OLD);
        maplist_take_cvar(&sv->cvars, con);
        if (!file_exists("bin/client.exe") && !file_exists("bin/client")) {
            remove(CONFIG_OLD ".old");
            rename(CONFIG_OLD, CONFIG_OLD ".old");
        }
    }
    weapons_load(sv);
    // its own files whole, as they stand before the command line, which isn't kept
    config_make_missing();
    if (!console_save_files(con, &HOST_CONFIG_FILE, 1)) console_print(con, "could not write %s\n", CONFIG_SERVER);
    if (!file_exists(CONFIG_WEAPONS) && !weapons_ini_template(CONFIG_WEAPONS))
        console_print(con, "could not write %s\n", CONFIG_WEAPONS);
    console_execute_args(con, argc, argv);
    return true;
}

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
    if (!weapons_moddable(id)) {
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

// A weapon's line, as `weapon` reads it: every field of `stats`, or with `base` only those
// that differ from it. How many fields it has.
static int weapon_line(char *line, size_t size, const char *name, const WeaponStats *stats, const WeaponStats *base)
{
    int n = snprintf(line, size, "weapon \"%s\"", name), fields = 0;
    for (int k = 0; k < WEAPON_FIELD_COUNT && n < (int)size; k++) {
        const NetField *f = &WEAPON_FIELDS[k];
        const uint8_t *at = (const uint8_t *)stats + f->offset;
        if (base && !memcmp(at, (const uint8_t *)base + f->offset, f->kind == NET_F32 ? sizeof(float) : sizeof(int32_t))) continue;
        if (f->kind == NET_F32) n += snprintf(line + n, size - (size_t)n, " %s %g", f->name, *(const float *)at);
        else n += snprintf(line + n, size - (size_t)n, " %s %d", f->name, *(const int32_t *)at);
        fields++;
    }
    return fields;
}

// weaponlist: every weapon a mod may change, as the lines that set it.
static void cmd_weaponlist(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv;
    Server *sv = user;
    Weapons names;
    weapons_default(&names);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        if (!weapons_moddable((WeaponId)id)) continue;
        char line[CONSOLE_TEXT_SIZE];
        weapon_line(line, sizeof line, names.info[id].name, &sv->weapons[id], NULL);
        console_print(con, "%s\n", line);
    }
}

// What the cvars say the game is.
static HostSettings settings_from_cvars(const Server *sv)
{
    HostSettings s = {
        .port = (uint16_t)sv->cvars.port->integer,
        .mode = sv->cvars.gamemode->integer == 1 ? MATCH_DEATHMATCH : sv->cvars.gamemode->integer == 2 ? MATCH_CTF : MATCH_MODE_COUNT,
        .time_limit = sv->cvars.timelimit->integer,
        .score_limit = sv->cvars.killlimit->integer,
        .bots_noteam = clampi(sv->cvars.bots_noteam->integer, 0, MAX_PLAYERS),
        .bots_alpha = clampi(sv->cvars.bots_alpha->integer, 0, MAX_PLAYERS),
        .bots_bravo = clampi(sv->cvars.bots_bravo->integer, 0, MAX_PLAYERS),
        .bots_difficulty = sv->cvars.bots_difficulty->integer,
        .bots_chat = sv->cvars.bots_chat->integer != 0,
        .vote_percent = sv->cvars.votepercent->integer,
        .flood_packets = sv->cvars.floodingpackets->integer,
        .flood_warnings = sv->cvars.warnings_flood->integer,
        .rope = sv->cvars.rope->integer != 0,
    };
    snprintf(s.data, sizeof s.data, "%s", sv->data->value);
    snprintf(s.ip, sizeof s.ip, "%s", sv->cvars.ip->value);
    snprintf(s.map, sizeof s.map, "%s", sv->cvars.map->value);
    // the rotation: sv_maps when it is set, else maplist.txt's
    if (sv->cvars.maps->value[0]) snprintf(s.maps, sizeof s.maps, "%s", sv->cvars.maps->value);
    else maplist_read(s.maps, sizeof s.maps);
    // the first map: `map` as given (the command line), else the rotation's first
    if (!strcmp(sv->cvars.map->value, sv->cvars.map->default_value) && s.maps[0])
        rounds_next_map(s.maps, "", s.map, sizeof s.map);
    snprintf(s.hostname, sizeof s.hostname, "%s", sv->cvars.hostname->value);
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
        LobbySettings lobby = {.public = sv.cvars.public->integer != 0, .url = sv.cvars.lobby_url->value, .address = sv.cvars.lobby_ip->value,
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
