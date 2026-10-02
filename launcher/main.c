// The launcher, what a player starts: Soldat Reloaded.exe on Windows,
// soldatreloaded-launcher on Linux. It brings the install up to the latest release
// (update.h), says how that is going in a small window, and starts the game.
//
//   launcher [--no-update] [--update-only] [--verify] [--releases <url>] [anything for the game...]
//
//   --no-update       start the game as it is
//   --update-only     bring the install up to date and stop there, without the game; the
//                     exit status says whether it worked
//   --verify          hash every file, not only those manifest.txt doesn't vouch for
//   --releases <url>  where the releases are, rather than this repository's on GitHub
//                     (a local copy of the same layout, to try an update on)
//
// Everything else is passed to the game: launcher +map ctf_Ash plays as client +map
// ctf_Ash does. When the releases can't be reached the game starts as it is; when an
// update fails the window says why and waits, to play the version installed or quit.
//
// The work runs on a thread of its own and the window draws what it last said. The
// window opens only if the work takes longer than a glance, so a start with nothing to
// do goes straight to the game. Text is stb_easy_font's: no font file to find.

#include <SDL.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" // what of stb_easy_font goes unused
#pragma GCC diagnostic ignored "-Wmissing-braces"  // and how it initializes its colour
#endif
#include <stb_easy_font.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop.h"
#include "files.h"
#include "http.h"
#include "update.h"

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CLIENT_FILE "client.exe" // xmake.lua's client target
#else
#include <unistd.h>
#define CLIENT_FILE "client"
// the window's icon, which on Windows is the executable's own (assets/icon.ico)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#endif

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev"
#endif
#ifndef SOLDATRELOADED_PLATFORM
#define SOLDATRELOADED_PLATFORM "unknown"
#endif
#ifndef SOLDATRELOADED_RELEASES
#define SOLDATRELOADED_RELEASES ""
#endif

#define WINDOW_WIDTH 520
#define WINDOW_HEIGHT 180
#define WINDOW_DELAY_MS 300 // the window opens if the work is still going after this

// What the worker last said, for the window to draw.
typedef struct Shared {
    SDL_mutex *lock;
    UpdateOptions options;
    char phase[256];
    uint64_t done, total;
    bool finished;
    UpdateOutcome outcome;
    char version[MANIFEST_VERSION_SIZE];
    char error[512];
} Shared;

static void on_phase(void *user, const char *text)
{
    Shared *s = user;
    SDL_LockMutex(s->lock);
    snprintf(s->phase, sizeof s->phase, "%s", text);
    s->done = s->total = 0;
    SDL_UnlockMutex(s->lock);
}

static void on_progress(void *user, uint64_t done, uint64_t total)
{
    Shared *s = user;
    SDL_LockMutex(s->lock);
    s->done = done;
    s->total = total;
    SDL_UnlockMutex(s->lock);
}

static int work(void *user)
{
    Shared *s = user;
    UpdateReport report = {.user = s, .phase = on_phase, .progress = on_progress};
    char version[MANIFEST_VERSION_SIZE], error[512];
    UpdateOutcome outcome = update_run(&s->options, &report, version, sizeof version, error, sizeof error);
    SDL_LockMutex(s->lock);
    s->outcome = outcome;
    snprintf(s->version, sizeof s->version, "%s", version);
    snprintf(s->error, sizeof s->error, "%s", error);
    s->finished = true;
    SDL_UnlockMutex(s->lock);
    return 0;
}

// --- drawing ---------------------------------------------------------------------------

static void text(SDL_Renderer *r, float x, float y, float scale, SDL_Color color, const char *s)
{
    static char quads[64 * 1024];
    static SDL_Vertex vertices[(sizeof quads / 64) * 6];
    unsigned char rgba[4] = {color.r, color.g, color.b, color.a};
    int count = stb_easy_font_print(0, 0, (char *)s, rgba, quads, sizeof quads);
    int n = 0;
    for (int q = 0; q < count; q++) {
        const float *v = (const float *)(quads + q * 64); // 4 vertices of x, y, z, colour
        static const int corners[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; i++) {
            const float *c = v + corners[i] * 4;
            vertices[n++] = (SDL_Vertex){{x + c[0] * scale, y + c[1] * scale}, color, {0, 0}};
        }
    }
    SDL_RenderGeometry(r, NULL, vertices, n, NULL, 0);
}

static float text_width(const char *s, float scale) { return (float)stb_easy_font_width((char *)s) * scale; }

// `s` in lines no wider than `width`, broken at spaces; the y below the last. With no
// renderer it only measures.
static float wrapped(SDL_Renderer *r, float x, float y, float width, float scale, SDL_Color color, const char *s)
{
    char line[512];
    while (*s) {
        size_t fit = 0, n = 0;
        while (s[n] && n < sizeof line - 1) {
            memcpy(line, s, n + 1);
            line[n + 1] = '\0';
            if (text_width(line, scale) > width) break;
            n++;
            if (s[n] == ' ' || !s[n]) fit = n;
        }
        if (fit == 0) fit = n ? n : 1;
        memcpy(line, s, fit);
        line[fit] = '\0';
        if (r) text(r, x, y, scale, color, line);
        y += 12 * scale;
        s += fit;
        while (*s == ' ') s++;
    }
    return y;
}

static const SDL_Color BACKGROUND = {21, 23, 26, 255}, TITLE = {235, 235, 230, 255}, DIM = {140, 145, 150, 255},
                       ACCENT = {232, 163, 61, 255}, TROUBLE = {235, 110, 95, 255}, TRACK = {44, 48, 54, 255};

static void draw(SDL_Renderer *r, const Shared *s, bool waiting)
{
    SDL_SetRenderDrawColor(r, BACKGROUND.r, BACKGROUND.g, BACKGROUND.b, 255);
    SDL_RenderClear(r);
    text(r, 24, 20, 3, TITLE, "SOLDAT RELOADED");
    if (s->version[0]) {
        char version[48];
        snprintf(version, sizeof version, "v%s", s->version);
        text(r, WINDOW_WIDTH - 24 - text_width(version, 2), 26, 2, DIM, version);
    }

    if (waiting) {
        // the window grows to hold all of what went wrong
        int height = (int)wrapped(NULL, 24, 70, WINDOW_WIDTH - 48, 2, TROUBLE, s->error) + 46;
        if (height < WINDOW_HEIGHT) height = WINDOW_HEIGHT;
        int w, h;
        SDL_Window *window = SDL_RenderGetWindow(r);
        SDL_GetWindowSize(window, &w, &h);
        if (h != height) SDL_SetWindowSize(window, WINDOW_WIDTH, height);
        wrapped(r, 24, 70, WINDOW_WIDTH - 48, 2, TROUBLE, s->error);
        text(r, 24, (float)height - 34, 2, DIM,
             files_exists(CLIENT_FILE) ? "Enter: play anyway      Esc: quit" : "Esc: quit");
    } else {
        text(r, 24, 78, 2, TITLE, s->phase);
        SDL_Rect track = {24, 110, WINDOW_WIDTH - 48, 12};
        SDL_SetRenderDrawColor(r, TRACK.r, TRACK.g, TRACK.b, 255);
        SDL_RenderFillRect(r, &track);
        if (s->total > 0) {
            double part = (double)s->done / (double)s->total;
            SDL_Rect fill = track;
            fill.w = (int)(track.w * (part > 1 ? 1 : part));
            SDL_SetRenderDrawColor(r, ACCENT.r, ACCENT.g, ACCENT.b, 255);
            SDL_RenderFillRect(r, &fill);
            char percent[16];
            snprintf(percent, sizeof percent, "%d%%", (int)(part * 100 + 0.5));
            text(r, 24, 134, 2, DIM, percent);
        }
    }
    SDL_RenderPresent(r);
}

// --- the game --------------------------------------------------------------------------

// The game, with the arguments the launcher didn't take; it takes over this process
// (Linux) or is left running as this one ends (Windows). False if it couldn't start.
static bool start_game(int argc, char **argv)
{
#ifdef _WIN32
    // One command line, each argument quoted as CommandLineToArgvW reads it back.
    static wchar_t line[32768];
    size_t at = 0;
    const char *first = "\"" CLIENT_FILE "\"";
    for (const char *c = first; *c; c++) line[at++] = (wchar_t)*c;
    for (int i = 0; i < argc; i++) {
        wchar_t arg[4096];
        int n = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg, (int)(sizeof arg / sizeof arg[0]));
        if (n <= 0 || at + (size_t)n * 2 + 4 >= sizeof line / sizeof line[0]) return false;
        line[at++] = L' ';
        line[at++] = L'"';
        int slashes = 0;
        for (const wchar_t *c = arg; *c; c++) {
            if (*c == L'\\') {
                slashes++;
            } else {
                // backslashes before a quote are doubled, and the quote escaped
                for (; *c == L'"' && slashes > 0; slashes--) line[at++] = L'\\';
                if (*c == L'"') line[at++] = L'\\';
                slashes = 0;
            }
            line[at++] = *c;
        }
        for (; slashes > 0; slashes--) line[at++] = L'\\';
        line[at++] = L'"';
    }
    line[at] = L'\0';
    STARTUPINFOW startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    char **args = calloc((size_t)argc + 2, sizeof *args);
    if (!args) return false;
    args[0] = "./" CLIENT_FILE;
    for (int i = 0; i < argc; i++) args[i + 1] = argv[i];
    execv(args[0], args);
    free(args);
    return false;
#endif
}

#ifndef _WIN32
// assets/icon.png, the badge, as the window's icon.
static void set_icon(SDL_Window *window)
{
    int width, height, channels;
    unsigned char *rgba = stbi_load("assets/icon.png", &width, &height, &channels, 4);
    if (!rgba) return;
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom(rgba, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
    stbi_image_free(rgba);
}
#endif

int main(int argc, char **argv)
{
    Shared s = {.options = {.releases = SOLDATRELOADED_RELEASES, .platform = SOLDATRELOADED_PLATFORM}};
    bool check = true, update_only = false;
    // the arguments not the launcher's, for the game
    char **rest = calloc((size_t)argc + 1, sizeof *rest);
    int rest_count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-update")) check = false;
        else if (!strcmp(argv[i], "--update-only")) update_only = true;
        else if (!strcmp(argv[i], "--verify")) s.options.thorough = true;
        else if (!strcmp(argv[i], "--releases") && i + 1 < argc) s.options.releases = argv[++i];
        else if (rest) rest[rest_count++] = argv[i];
    }
    // the game's files are beside the launcher, wherever it was started from
    if (!files_enter_own_directory()) fprintf(stderr, "launcher: its own directory can't be found\n");

    if (!check || !s.options.releases[0]) {
        if (start_game(rest_count, rest)) return 0;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soldat Reloaded", "The game (" CLIENT_FILE ") can't be started.", NULL);
        return 1;
    }

#ifndef _WIN32
    // the window's class, the menu entry's, unless the player has given one
    setenv("SDL_VIDEO_X11_WMCLASS", DESKTOP_APP_ID, 0);
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", DESKTOP_APP_ID, 0);
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) fprintf(stderr, "launcher: no window: %s\n", SDL_GetError());
    if (!http_init()) fprintf(stderr, "launcher: curl couldn't start\n");
    s.lock = SDL_CreateMutex();
    snprintf(s.phase, sizeof s.phase, "Starting");
    char *installed = files_read(UPDATE_VERSION, NULL); // the version until the work says otherwise
    if (installed) {
        installed[strcspn(installed, "\r\n")] = '\0';
        snprintf(s.version, sizeof s.version, "%s", installed);
        free(installed);
    }
    SDL_Thread *worker = s.lock ? SDL_CreateThread(work, "update", &s) : NULL;
    if (!worker) work(&s); // no thread: the work, then the game, without a window

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    Uint32 started = SDL_GetTicks();
    bool quit = false, play = false, waiting = false;
    int status = 0;
    while (!quit && !play) {
        SDL_LockMutex(s.lock);
        Shared now = s;
        SDL_UnlockMutex(s.lock);
        if (now.finished && update_only) {
            if (now.error[0]) fprintf(stderr, "launcher: %s\n", now.error);
            status = now.outcome == UPDATE_FAILED;
            break;
        }
        if (now.finished && !waiting) {
            if (now.outcome != UPDATE_FAILED) {
                if (now.error[0]) fprintf(stderr, "launcher: %s\n", now.error); // couldn't check: play on
                play = true;
                break;
            }
            waiting = true; // the window says what went wrong and waits for an answer
        }
        if (!window && (waiting || SDL_GetTicks() - started > WINDOW_DELAY_MS)) {
            window = SDL_CreateWindow("Soldat Reloaded", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH,
                                      WINDOW_HEIGHT, 0);
            if (window) {
#ifndef _WIN32
                set_icon(window); // on Windows SDL gives it the executable's own
#endif
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
                if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            } else if (waiting) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soldat Reloaded", now.error, NULL);
                play = files_exists(CLIENT_FILE);
                break;
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (waiting && e.type == SDL_KEYDOWN) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) quit = true;
                if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && files_exists(CLIENT_FILE)) play = true;
            }
        }
        if (renderer) draw(renderer, &now, waiting);
        else SDL_Delay(16);
    }

    // Closed part way through an update: the thread is cut off with the process, and
    // the next start finishes what it began (update.h).
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (play) {
        if (worker) SDL_WaitThread(worker, NULL);
        http_cleanup();
        SDL_Quit();
        if (start_game(rest_count, rest)) return 0;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Soldat Reloaded", "The game (" CLIENT_FILE ") can't be started.", NULL);
        return 1;
    }
    SDL_Quit();
    return status;
}
