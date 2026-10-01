// The server, headless: the console around a hosted game (host.c), and a loop that
// pumps it until it is told to stop. It reads config.cfg and the command line, loads
// the map, listens on sv_port, gives everyone who says Hello a soldier, plays the bots
// asked for, ticks the world with authority, and stops on `quit` or Ctrl-C.
//
//   console  the cvars and the commands (shared/console): config.cfg, then the
//            command line over it
//   host     the world, the line, the players, the bots and the rounds (host.c)
//
// It runs from the directory that holds config.cfg and assets/, as the client does.
//
//   bettersoldat-server [+map <name>] [+sv_port <port>] [+<cvar> <value>] [+<command> <args>...]

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "console/console.h"
#include "game/game.h"
#include "host.h"
#include "stdin_reader.h"

// After the game's headers: GDI has a Polygon of its own.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif

#define CONFIG "config.cfg"
#define SLEEP_MS 1 // between passes of the loop, so it never spins flat out

typedef struct Server {
    Console *console; // large; on the heap
    Cvar *assets;
    Cvar *map;
    Cvar *maps; // the rotation
    Cvar *port;
    Cvar *hostname;
    Cvar *gamemode, *timelimit, *killlimit;
    Cvar *bots_noteam, *bots_alpha, *bots_bravo, *bots_difficulty, *bots_chat;
    Cvar *votepercent;
    Host host;
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

static void cmd_quit(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    ((Server *)user)->quit = true;
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
// alpha, or on bravo; one of assets/bots at random unless named (the original's commands).
static void cmd_addbot(Console *con, int argc, char **argv, void *user)
{
    (void)con;
    Server *sv = user;
    Team team = argv[0][6] == '1' ? TEAM_ALPHA : argv[0][6] == '2' ? TEAM_BRAVO : TEAM_NONE;
    host_add_bot(&sv->host, team, argc > 1 ? argv[1] : NULL);
}

// The console and what the server keeps in it, then config.cfg and the command line
// over it. Nothing is saved on the way out: nothing here changes a setting.
static bool console_open(Server *sv, int argc, char *argv[])
{
    Console *con = sv->console = console_create(print_stdout, NULL);
    if (!con) return false;

    sv->assets = cvar_register(con, "assets", "./assets", 0, "the base assets directory: maps/, anims/, objects/...");
    sv->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    sv->maps = cvar_register(con, "sv_maps", "", 0, "the maps in rotation, space-separated; empty plays the map again");
    sv->port = cvar_register(con, "sv_port", "23073", 0, "the UDP port to listen on");
    sv->hostname = cvar_register(con, "sv_hostname", "bettersoldat server", 0, "the server's name, on the scoreboard");
    sv->gamemode = cvar_register(con, "sv_gamemode", "0", 0, "0 the map's own, 1 deathmatch, 2 capture the flag");
    sv->timelimit = cvar_register(con, "sv_timelimit", "15", 0, "minutes a round lasts");
    sv->killlimit = cvar_register(con, "sv_killlimit", "10", 0, "the score that wins a round: kills, or captures in CTF");
    sv->bots_noteam = cvar_register(con, "bots_random_noteam", "0", 0, "bots in a deathmatch");
    sv->bots_alpha = cvar_register(con, "bots_random_alpha", "0", 0, "bots on alpha in capture the flag");
    sv->bots_bravo = cvar_register(con, "bots_random_bravo", "0", 0, "bots on bravo in capture the flag");
    sv->bots_difficulty = cvar_register(con, "bots_difficulty", "100", 0, "300 stupid, 200 poor, 100 normal, 50 hard, 10 impossible");
    sv->bots_chat = cvar_register(con, "bots_chat", "1", 0, "whether the bots talk");
    sv->votepercent = cvar_register(con, "sv_votepercent", "60", 0, "the percentage of players whose yes passes a vote");
    console_add_command(con, "quit", cmd_quit, sv, "stop the server");
    console_add_command(con, "nextmap", cmd_nextmap, sv, "end the round and begin the next");
    console_add_command(con, "say", cmd_say, sv, "say something to everyone, as the server");
    console_add_command(con, "addbot", cmd_addbot, sv, "add a bot: addbot [name]");
    console_add_command(con, "addbot1", cmd_addbot, sv, "add a bot to alpha: addbot1 [name]");
    console_add_command(con, "addbot2", cmd_addbot, sv, "add a bot to bravo: addbot2 [name]");

    if (file_exists(CONFIG)) console_execute_file(con, CONFIG);
    console_execute_args(con, argc, argv);
    return true;
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
    };
    snprintf(s.assets, sizeof s.assets, "%s", sv->assets->value);
    snprintf(s.map, sizeof s.map, "%s", sv->map->value);
    snprintf(s.maps, sizeof s.maps, "%s", sv->maps->value);
    snprintf(s.hostname, sizeof s.hostname, "%s", sv->hostname->value);
    return s;
}

int main(int argc, char *argv[])
{
    Server sv = {0};

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
        cvar_set(sv.console, "map", host_map(&sv.host));
        sleep_ms(SLEEP_MS);
    }

    console_print(sv.console, "stopping\n");
    host_close(&sv.host);
    net_shutdown();
    console_destroy(sv.console);
    return 0;
}
