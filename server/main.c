// The server, headless: the console, the world, the line, and a loop that ticks it at
// TICK_RATE until it is told to stop. It reads config.cfg and the command line, loads
// the map, listens on sv_port, gives everyone who says Hello a soldier, ticks the world
// with authority, and stops on `quit` or Ctrl-C. What the players' clients send about
// their soldiers, and what they hear back, comes with the netcode's next steps.
//
//   console      the cvars and the commands (shared/console): config.cfg, then the
//                command line over it
//   connections  who is on the line, and the join (connections.c)
//   game         the world (shared/game), ticked here with authority
//
// It runs from the directory that holds config.cfg and assets/, as the client does.
//
//   bettersoldat-server [+map <name>] [+sv_port <port>] [+<cvar> <value>] [+<command> <args>...]

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "connections.h"
#include "console/console.h"
#include "game/game.h"
#include "rounds.h"
#include "stdin_reader.h"

// After the game's headers: GDI has a Polygon of its own.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif

#define CONFIG "config.cfg"
#define MAX_STALL 0.25 // a stall never turns into a burst of ticks
#define SLEEP_MS 1     // between passes of the loop, so it never spins flat out

typedef struct Server {
    Console *console; // large; on the heap
    Cvar *assets;
    Cvar *map;
    Cvar *maps; // the rotation
    Cvar *port;
    Game *game; // large; on the heap
    NetLink link;
    Connections connections;
    double accumulator;
    bool quit;
    bool next_round; // asked for (nextmap), or the match over: at the end of the tick
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
    connections_say(&sv->connections, text);
}

// nextmap: the round ends now and the next begins.
static void cmd_nextmap(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)argv;
    ((Server *)user)->next_round = true;
}

// The next round: on `chosen` if a vote chose one, else on the map after this one in
// sv_maps (or this one again).
static bool next_round(Server *sv, const char *chosen)
{
    char map[NET_MAP_SIZE];
    if (chosen) snprintf(map, sizeof map, "%s", chosen);
    else rounds_next_map(sv->maps->value, sv->map->value, map, sizeof map);
    if (!round_start(sv->game, &sv->connections, sv->assets->value, map)) {
        fprintf(stderr, "could not load map '%s' from '%s'\n", map, sv->assets->value);
        return false;
    }
    cvar_set(sv->console, "map", map);
    sv->next_round = false;
    return true;
}

// The console and what the server keeps in it, then config.cfg and the command line
// over it. Nothing is saved on the way out: nothing here changes a setting yet.
static bool console_open(Server *sv, int argc, char *argv[])
{
    Console *con = sv->console = console_create(print_stdout, NULL);
    if (!con) return false;

    sv->assets = cvar_register(con, "assets", "./assets", 0, "the base assets directory: maps/, anims/, objects/...");
    sv->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    sv->maps = cvar_register(con, "sv_maps", "", 0, "the maps in rotation, space-separated; empty plays the map again");
    sv->port = cvar_register(con, "sv_port", "23073", 0, "the UDP port to listen on");
    console_add_command(con, "quit", cmd_quit, sv, "stop the server");
    console_add_command(con, "nextmap", cmd_nextmap, sv, "end the round and begin the next");
    console_add_command(con, "say", cmd_say, sv, "say something to everyone, as the server");

    if (file_exists(CONFIG)) console_execute_file(con, CONFIG);
    console_execute_args(con, argc, argv);
    return true;
}

// The world, empty, with this server deciding what happens in it.
static bool game_open(Server *sv)
{
    sv->game = calloc(1, sizeof(Game));
    if (!sv->game || !context_load(&sv->game->ctx, sv->assets->value, sv->map->value)) return false;
    game_init(sv->game, (uint64_t)time(NULL), match_settings_for_map(sv->game->ctx.map));
    sv->game->world.authority = true;
    return true;
}

static void game_close(Server *sv)
{
    if (!sv->game) return;
    context_destroy(&sv->game->ctx);
    free(sv->game);
    sv->game = NULL;
}

int main(int argc, char *argv[])
{
    Server sv = {0};

    if (!console_open(&sv, argc, argv)) return 1;
    if (!game_open(&sv)) {
        fprintf(stderr, "could not load map '%s' from '%s'\n", sv.map->value, sv.assets->value);
        game_close(&sv);
        console_destroy(sv.console);
        return 1;
    }
    if (!net_init() || !net_listen(&sv.link, (uint16_t)sv.port->integer, MAX_PLAYERS)) {
        fprintf(stderr, "could not listen on port %d\n", sv.port->integer);
        game_close(&sv);
        console_destroy(sv.console);
        return 1;
    }
    sv.game->world.history = calloc(1, sizeof(History)); // the snapshots' deltas are against it
    if (!sv.game->world.history || !connections_init(&sv.connections, &sv.link, sv.console, sv.map->value)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    snprintf(sv.connections.maps_dir, sizeof sv.connections.maps_dir, "%s/maps", sv.assets->value);
    if (!stdin_reader_start()) fprintf(stderr, "the console won't read its input\n");
    signal(SIGINT, on_interrupt);
    signal(SIGTERM, on_interrupt);
    console_print(sv.console, "bettersoldat-server: %s on port %d, %d ticks a second\n", sv.map->value, sv.port->integer,
                  TICK_RATE);

    // Ticks come out of the time that has passed, one whole tick at a time, and the
    // rest waits for the next pass. The line is heard before the ticks; each tick the
    // players' soldiers step on their last keys, and each tick a snapshot goes to
    // everyone; the line is flushed after.
    double last = now();
    while (!sv.quit && !interrupted) {
        double t = now();
        sv.accumulator += t - last;
        last = t;
        if (sv.accumulator > MAX_STALL) sv.accumulator = MAX_STALL;
        char line[STDIN_LINE_SIZE];
        while (stdin_reader_take(line, sizeof line)) console_execute(sv.console, line);
        connections_poll(&sv.connections, sv.game);
        while (sv.accumulator >= TICK_SECONDS) {
            Command cmds[MAX_PLAYERS] = {0};
            connections_commands(&sv.connections, sv.game, cmds);
            game_tick(sv.game, cmds);
            connections_snapshots(&sv.connections, sv.game);
            sv.accumulator -= TICK_SECONDS;
            char chosen[NET_MAP_SIZE];
            bool voted = connections_take_vote_map(&sv.connections, chosen, sizeof chosen);
            if ((voted || sv.next_round || match_over(&sv.game->match)) && !next_round(&sv, voted ? chosen : NULL)) sv.quit = true;
        }
        net_flush(&sv.link);
        sleep_ms(SLEEP_MS);
    }

    console_print(sv.console, "stopping\n");
    net_close(&sv.link);
    net_shutdown();
    connections_free(&sv.connections);
    free(sv.game->world.history);
    game_close(&sv);
    console_destroy(sv.console);
    return 0;
}
