// The server, headless: the console, the world, and a loop that ticks it at TICK_RATE
// until it is told to stop. Nobody can connect yet; this is the skeleton the netcode
// fills in. Today it reads config.cfg and the command line, loads the map, ticks an
// empty world with authority, and stops on `quit` or Ctrl-C.
//
//   console  the cvars and the commands (shared/console): config.cfg, then the
//            command line over it
//   game     the world (shared/game), ticked here with authority
//
// It runs from the directory that holds config.cfg and assets/, as the client does.
//
//   bettersoldat-server [+map <name>] [+<cvar> <value>] [+<command> <args>...]

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "console/console.h"
#include "game/game.h"

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
    Game *game; // large; on the heap
    double accumulator;
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

// The console and what the server keeps in it, then config.cfg and the command line
// over it. Nothing is saved on the way out: nothing here changes a setting yet.
static bool console_open(Server *sv, int argc, char *argv[])
{
    Console *con = sv->console = console_create(print_stdout, NULL);
    if (!con) return false;

    sv->assets = cvar_register(con, "assets", "./assets", 0, "the base assets directory: maps/, anims/, objects/...");
    sv->map = cvar_register(con, "map", "Arena", 0, "the map to load");
    console_add_command(con, "quit", cmd_quit, sv, "stop the server");

    if (file_exists(CONFIG)) console_execute_file(con, CONFIG);
    console_execute_args(con, argc, argv);
    return true;
}

// The world, empty, with this server deciding what happens in it.
static bool game_open(Server *sv)
{
    sv->game = calloc(1, sizeof(Game));
    if (!sv->game || !context_load(&sv->game->ctx, sv->assets->value, sv->map->value)) return false;
    game_init(sv->game, (uint64_t)time(NULL), match_default_settings());
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
    signal(SIGINT, on_interrupt);
    signal(SIGTERM, on_interrupt);
    console_print(sv.console, "bettersoldat-server: %s, %d ticks a second\n", sv.map->value, TICK_RATE);

    // Ticks come out of the time that has passed, one whole tick at a time, and the
    // rest waits for the next pass. Nobody is connected, so every command is empty.
    Command cmds[MAX_PLAYERS] = {0};
    double last = now();
    while (!sv.quit && !interrupted) {
        double t = now();
        sv.accumulator += t - last;
        last = t;
        if (sv.accumulator > MAX_STALL) sv.accumulator = MAX_STALL;
        while (sv.accumulator >= TICK_SECONDS) {
            game_tick(sv.game, cmds);
            sv.accumulator -= TICK_SECONDS;
        }
        sleep_ms(SLEEP_MS);
    }

    console_print(sv.console, "stopping\n");
    game_close(&sv);
    console_destroy(sv.console);
    return 0;
}
